#include "sql/region_store.hpp"

#include <algorithm>
#include <format>

#include <core/log.hpp>
#include <core/morton.hpp>
#include <dataio/binary.hpp>

namespace sql {
namespace {
constexpr uint32_t kFileMagic = 0x47455256;	  // "VREG"
constexpr uint32_t kFileVersion = 1;
constexpr uint32_t kRecordMagic = 0x44434552; // "RECD"
constexpr uint32_t kTombstone = 0xFFFFFFFFu;
constexpr uint64_t kHeaderSize = 8;
constexpr uint64_t kRecordHeader = 4 + 8 + 4 + 4;
} // namespace

RegionStore::RegionStore(std::filesystem::path directory, int regionBits, jobs::ThreadPool *pool)
	: m_dir(std::move(directory)), m_regionBits(std::clamp(regionBits, 0, 8)), m_pool(pool ? pool : &jobs::ThreadPool::Global()) {
	std::error_code ec;
	std::filesystem::create_directories(m_dir, ec);
}

RegionStore::~RegionStore() {
	Flush();
	CloseAll();
}

RegionStore::Region &RegionStore::GetRegion(uint64_t regionKey) {
	std::scoped_lock lock(m_mapMutex);
	auto [slot, inserted] = m_regions.TryEmplace(regionKey);
	if (inserted) {
		*slot = std::make_unique<Region>();
		(*slot)->path = m_dir / std::format("r.{:x}.vreg", regionKey);
	}
	return **slot;
}

bool RegionStore::OpenLocked(Region &r) {
	if (r.opened)
		return true;
	const bool exists = std::filesystem::exists(r.path);
	r.file.open(r.path, std::ios::binary | std::ios::in | std::ios::out | (exists ? std::ios::openmode{} : std::ios::trunc));
	if (!r.file) {
		core::log::Error("region: impossible d'ouvrir {}", r.path.string());
		return false;
	}
	r.index.Clear();
	r.deadBytes = 0;
	if (!exists) {
		dataio::ByteWriter w;
		w.Put<uint32_t>(kFileMagic);
		w.Put<uint32_t>(kFileVersion);
		r.file.write(reinterpret_cast<const char *>(w.Bytes().data()), static_cast<std::streamsize>(w.Size()));
		r.file.flush();
		r.fileSize = kHeaderSize;
		r.opened = true;
		return true;
	}
	// Balayage du journal pour reconstruire l'index.
	r.file.seekg(0, std::ios::end);
	const uint64_t size = static_cast<uint64_t>(r.file.tellg());
	r.file.seekg(0);
	uint32_t hdr[2] = {};
	r.file.read(reinterpret_cast<char *>(hdr), sizeof hdr);
	if (!r.file || hdr[0] != kFileMagic || hdr[1] != kFileVersion) {
		core::log::Error("region: en-tête invalide dans {}", r.path.string());
		r.file.close();
		return false;
	}
	uint64_t pos = kHeaderSize;
	while (pos + kRecordHeader <= size) {
		uint8_t rh[kRecordHeader];
		r.file.seekg(static_cast<std::streamoff>(pos));
		r.file.read(reinterpret_cast<char *>(rh), sizeof rh);
		dataio::ByteReader rd(rh);
		const uint32_t magic = rd.Get<uint32_t>();
		const uint64_t key = rd.Get<uint64_t>();
		const uint32_t len = rd.Get<uint32_t>();
		if (!r.file || magic != kRecordMagic)
			break;
		const uint64_t payload = len == kTombstone ? 0 : len;
		if (pos + kRecordHeader + payload > size)
			break; // enregistrement tronqué : fin de journal valide
		if (const Entry *old = r.index.Find(key))
			r.deadBytes += kRecordHeader + old->size;
		if (len == kTombstone) {
			r.index.Erase(key);
			r.deadBytes += kRecordHeader;
		} else {
			r.index.InsertOrAssign(key, Entry{pos + kRecordHeader, len});
		}
		pos += kRecordHeader + payload;
	}
	r.file.clear();
	r.fileSize = pos;
	r.opened = true;
	return true;
}

bool RegionStore::AppendLocked(Region &r, uint64_t key, std::span<const uint8_t> data, bool tombstone) {
	dataio::ByteWriter w(kRecordHeader + data.size());
	w.Put<uint32_t>(kRecordMagic);
	w.Put<uint64_t>(key);
	w.Put<uint32_t>(tombstone ? kTombstone : static_cast<uint32_t>(data.size()));
	w.Put<uint32_t>(dataio::Crc32(data));
	w.PutBytes(data.data(), data.size());
	r.file.clear();
	r.file.seekp(static_cast<std::streamoff>(r.fileSize));
	r.file.write(reinterpret_cast<const char *>(w.Bytes().data()), static_cast<std::streamsize>(w.Size()));
	r.file.flush();
	if (!r.file)
		return false;
	if (const Entry *old = r.index.Find(key))
		r.deadBytes += kRecordHeader + old->size;
	if (tombstone) {
		r.index.Erase(key);
		r.deadBytes += kRecordHeader;
	} else {
		r.index.InsertOrAssign(key, Entry{r.fileSize + kRecordHeader, static_cast<uint32_t>(data.size())});
	}
	r.fileSize += w.Size();
	++m_writes;
	if (r.deadBytes > (1u << 20) && r.deadBytes * 2 > r.fileSize)
		CompactLocked(r);
	return true;
}

void RegionStore::CompactLocked(Region &r) {
	const auto tmpPath = r.path.string() + ".tmp";
	std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
	if (!out)
		return;
	dataio::ByteWriter w;
	w.Put<uint32_t>(kFileMagic);
	w.Put<uint32_t>(kFileVersion);
	core::RobinMap<uint64_t, Entry> fresh;
	fresh.Reserve(r.index.Size());
	uint64_t pos = kHeaderSize;
	std::vector<uint8_t> buf;
	// Ordre des clés croissant : les chunks voisins (ordre Z) sont voisins sur le disque.
	std::vector<std::pair<uint64_t, Entry>> entries;
	entries.reserve(r.index.Size());
	for (const auto &e : r.index)
		entries.emplace_back(e.key, e.value);
	std::ranges::sort(entries, {}, &std::pair<uint64_t, Entry>::first);
	out.write(reinterpret_cast<const char *>(w.Bytes().data()), static_cast<std::streamsize>(w.Size()));
	for (const auto &[key, e] : entries) {
		buf.resize(e.size);
		r.file.clear();
		r.file.seekg(static_cast<std::streamoff>(e.offset));
		r.file.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(e.size));
		dataio::ByteWriter rec(kRecordHeader + e.size);
		rec.Put<uint32_t>(kRecordMagic);
		rec.Put<uint64_t>(key);
		rec.Put<uint32_t>(e.size);
		rec.Put<uint32_t>(dataio::Crc32(buf));
		rec.PutBytes(buf.data(), buf.size());
		out.write(reinterpret_cast<const char *>(rec.Bytes().data()), static_cast<std::streamsize>(rec.Size()));
		fresh.InsertOrAssign(key, Entry{pos + kRecordHeader, e.size});
		pos += rec.Size();
	}
	out.close();
	if (!out)
		return;
	r.file.close();
	std::error_code ec;
	std::filesystem::rename(tmpPath, r.path, ec);
	r.file.open(r.path, std::ios::binary | std::ios::in | std::ios::out);
	r.index = std::move(fresh);
	r.fileSize = pos;
	r.deadBytes = 0;
	++m_compactions;
}

std::optional<std::vector<uint8_t>> RegionStore::Load(uint64_t key) {
	Region &r = GetRegion(RegionOf(key));
	std::scoped_lock lock(r.mutex);
	if (!OpenLocked(r))
		return std::nullopt;
	const Entry *e = r.index.Find(key);
	if (!e)
		return std::nullopt;
	std::vector<uint8_t> data(e->size);
	uint32_t storedCrc = 0;
	r.file.clear();
	r.file.seekg(static_cast<std::streamoff>(e->offset - 4));
	r.file.read(reinterpret_cast<char *>(&storedCrc), 4);
	r.file.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(e->size));
	++m_reads;
	if (!r.file || storedCrc != dataio::Crc32(data)) {
		core::log::Error("region: enregistrement corrompu (clé {:#x}) dans {}", key, r.path.string());
		return std::nullopt;
	}
	return data;
}

bool RegionStore::Save(uint64_t key, std::span<const uint8_t> data) {
	Region &r = GetRegion(RegionOf(key));
	std::scoped_lock lock(r.mutex);
	return OpenLocked(r) && AppendLocked(r, key, data, false);
}

bool RegionStore::Erase(uint64_t key) {
	Region &r = GetRegion(RegionOf(key));
	std::scoped_lock lock(r.mutex);
	if (!OpenLocked(r) || !r.index.Contains(key))
		return false;
	return AppendLocked(r, key, {}, true);
}

bool RegionStore::Contains(uint64_t key) {
	Region &r = GetRegion(RegionOf(key));
	std::scoped_lock lock(r.mutex);
	return OpenLocked(r) && r.index.Contains(key);
}

std::vector<uint64_t> RegionStore::KeysInRegion(uint64_t key) {
	Region &r = GetRegion(RegionOf(key));
	std::scoped_lock lock(r.mutex);
	std::vector<uint64_t> out;
	if (!OpenLocked(r))
		return out;
	out.reserve(r.index.Size());
	for (const auto &e : r.index)
		out.push_back(e.key);
	std::ranges::sort(out);
	return out;
}

void RegionStore::LoadAsync(uint64_t key, LoadCallback cb, jobs::Priority priority) {
	m_pending.Add();
	m_pool->Submit(
		[this, key, cb = std::move(cb)]() mutable {
			cb(key, Load(key));
			m_pending.Done();
		},
		priority);
}

void RegionStore::SaveAsync(uint64_t key, std::vector<uint8_t> data, std::move_only_function<void(uint64_t, bool)> done) {
	m_pending.Add();
	m_pool->Submit(
		[this, key, data = std::move(data), done = std::move(done)]() mutable {
			const bool ok = Save(key, data);
			if (done)
				done(key, ok);
			m_pending.Done();
		},
		jobs::Priority::Low);
}

void RegionStore::EraseAsync(uint64_t key) {
	m_pending.Add();
	m_pool->Submit(
		[this, key] {
			Erase(key);
			m_pending.Done();
		},
		jobs::Priority::Low);
}

void RegionStore::Flush() {
	while (m_pending.Pending() > 0) {
		if (!m_pool->RunOne())
			m_pending.Wait();
	}
}

void RegionStore::CloseAll() {
	std::scoped_lock lock(m_mapMutex);
	for (auto &e : m_regions) {
		std::scoped_lock rl(e.value->mutex);
		if (e.value->opened)
			e.value->file.close();
		e.value->opened = false;
	}
	m_regions.Clear();
}

void RegionStore::CompactAll() {
	std::scoped_lock lock(m_mapMutex);
	for (auto &e : m_regions) {
		std::scoped_lock rl(e.value->mutex);
		if (e.value->opened && e.value->deadBytes)
			CompactLocked(*e.value);
	}
}

RegionStore::Stats RegionStore::GetStats() {
	Stats s;
	std::scoped_lock lock(m_mapMutex);
	for (auto &e : m_regions) {
		std::scoped_lock rl(e.value->mutex);
		if (!e.value->opened)
			continue;
		++s.regionsOpen;
		s.records += e.value->index.Size();
		s.deadBytes += e.value->deadBytes;
		s.liveBytes += e.value->fileSize - e.value->deadBytes;
	}
	s.reads = m_reads;
	s.writes = m_writes;
	s.compactions = m_compactions;
	return s;
}

// ── Découpage d'une boîte en intervalles de Morton ────────────────────────
namespace {
void Subdivide(uint32_t x0, uint32_t y0, uint32_t z0, int level, const uint32_t lo[3], const uint32_t hi[3],
			   std::vector<std::pair<uint64_t, uint64_t>> &out, size_t maxRanges) {
	const uint32_t size = 1u << level;
	const uint32_t x1 = x0 + size - 1, y1 = y0 + size - 1, z1 = z0 + size - 1;
	if (x1 < lo[0] || x0 > hi[0] || y1 < lo[1] || y0 > hi[1] || z1 < lo[2] || z0 > hi[2])
		return; // cellule disjointe
	const bool inside = x0 >= lo[0] && x1 <= hi[0] && y0 >= lo[1] && y1 <= hi[1] && z0 >= lo[2] && z1 <= hi[2];
	if (inside || level == 0 || out.size() >= maxRanges) {
		const uint64_t a = core::morton::Spread3(x0) | (core::morton::Spread3(y0) << 1) | (core::morton::Spread3(z0) << 2);
		const uint64_t b = a + (uint64_t(1) << (3 * level)) - 1;
		if (!out.empty() && out.back().second + 1 == a)
			out.back().second = b; // fusion avec l'intervalle précédent
		else
			out.emplace_back(a, b);
		return;
	}
	const uint32_t h = size >> 1;
	// Ordre Z des 8 enfants : bit0 = x, bit1 = y, bit2 = z.
	for (uint32_t child = 0; child < 8; ++child)
		Subdivide(x0 + (child & 1 ? h : 0), y0 + (child & 2 ? h : 0), z0 + (child & 4 ? h : 0), level - 1, lo, hi, out, maxRanges);
}
} // namespace

void MortonBoxRanges(int32_t minX, int32_t minY, int32_t minZ, int32_t maxX, int32_t maxY, int32_t maxZ,
					 std::vector<std::pair<uint64_t, uint64_t>> &out, size_t maxRanges) {
	out.clear();
	using core::morton::kBias3;
	const uint32_t lo[3] = {uint32_t(minX + kBias3), uint32_t(minY + kBias3), uint32_t(minZ + kBias3)};
	const uint32_t hi[3] = {uint32_t(maxX + kBias3), uint32_t(maxY + kBias3), uint32_t(maxZ + kBias3)};
	// Plus petite cellule alignée contenant la boîte.
	int level = 0;
	while (level < 21 && ((lo[0] >> level) != (hi[0] >> level) || (lo[1] >> level) != (hi[1] >> level) || (lo[2] >> level) != (hi[2] >> level)))
		++level;
	const uint32_t mask = level >= 32 ? 0 : ~((1u << level) - 1);
	Subdivide(lo[0] & mask, lo[1] & mask, lo[2] & mask, level, lo, hi, out, maxRanges);
}

} // namespace sql
