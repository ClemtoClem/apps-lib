#include "voxel/storage.hpp"
#include "voxel/orientation.hpp"

#include <algorithm>
#include <format>

#include <core/log.hpp>
#include <dataio/binary.hpp>
#include <diagnostics/trace.hpp>

namespace voxel {
namespace {
constexpr uint32_t kChunkBlobVersion = 3; // 3 : orientations des blocs (1 et 2 toujours lus)

sql::PreparedStatement MustPrepare(sql::Database &db, std::string_view q) {
	auto r = db.Prepare(q);
	if (!r) {
		core::log::Error("storage: {}", r.Error());
		return {};
	}
	return std::move(r.Value());
}
} // namespace

WorldStorage::WorldStorage(std::filesystem::path directory, jobs::ThreadPool &pool) : m_dir(std::move(directory)), m_pool(pool) {}

WorldStorage::~WorldStorage() { Flush(); }

bool WorldStorage::Open() {
	std::error_code ec;
	std::filesystem::create_directories(m_dir / "regions", ec);
	m_regions = std::make_unique<sql::RegionStore>(m_dir / "regions", 4, &m_pool);
	const auto snapshot = (m_dir / "world.vqdb").string();
	if (std::filesystem::exists(snapshot)) {
		if (auto r = m_db.LoadSnapshot(snapshot); !r) {
			core::log::Error("monde : {}", r.Error());
			return false;
		}
	}
	auto created = m_db.ExecuteScript(R"(
		CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT);
		CREATE TABLE IF NOT EXISTS chunks (key INT PRIMARY KEY, zkey INT INDEX, cx INT, cy INT, cz INT, entities INT, tick INT);
		CREATE TABLE IF NOT EXISTS entities (id INT PRIMARY KEY, chunk INT INDEX, kind TEXT, x REAL, y REAL, z REAL);
		CREATE TABLE IF NOT EXISTS config (key TEXT PRIMARY KEY, value TEXT, mod TEXT);
		CREATE TABLE IF NOT EXISTS events (id INT PRIMARY KEY, tick INT INDEX, chunk INT INDEX, type TEXT, mod TEXT, data TEXT);
		CREATE TABLE IF NOT EXISTS mods (id TEXT PRIMARY KEY, name TEXT, version TEXT, enabled INT, priority INT);
	)");
	if (!created) {
		core::log::Error("monde : {}", created.Error());
		return false;
	}
	m_upsertChunk = MustPrepare(m_db, "INSERT OR REPLACE INTO chunks VALUES (?, ?, ?, ?, ?, ?, ?)");
	m_deleteEntities = MustPrepare(m_db, "DELETE FROM entities WHERE chunk = ?");
	m_insertEntity = MustPrepare(m_db, "INSERT INTO entities VALUES (?, ?, ?, ?, ?, ?)");
	m_insertEvent = MustPrepare(m_db, "INSERT INTO events VALUES (?, ?, ?, ?, ?, ?)");
	m_setMeta = MustPrepare(m_db, "INSERT OR REPLACE INTO meta VALUES (?, ?)");
	m_getMeta = MustPrepare(m_db, "SELECT value FROM meta WHERE key = ?");
	m_setConfig = MustPrepare(m_db, "INSERT OR REPLACE INTO config VALUES (?, ?, ?)");
	m_getConfig = MustPrepare(m_db, "SELECT value FROM config WHERE key = ?");
	m_chunksInRange = MustPrepare(m_db, "SELECT key, cx, cy, cz FROM chunks WHERE zkey BETWEEN ? AND ?");
	m_entitiesInChunk = MustPrepare(m_db, "SELECT id, kind, x, y, z FROM entities WHERE chunk = ?");
	auto maxId = [&](std::string_view table) {
		auto r = m_db.Execute(std::format("SELECT id FROM {} ORDER BY id DESC LIMIT 1", table));
		return (r && !r.Value().rows.empty()) ? r.Value().At(0, 0).AsInt() + 1 : 1;
	};
	m_nextEntityId = maxId("entities");
	m_nextEventId = maxId("events");
	return true;
}

bool WorldStorage::Flush() {
	if (!m_regions)
		return false;
	DIAG_ZONE("WorldStorage::Flush");
	m_regions->Flush();
	return m_db.SaveSnapshot((m_dir / "world.vqdb").string());
}

std::vector<uint8_t> WorldStorage::EncodeChunk(const BlockId *voxels, BlockId fill, const uint8_t *orients,
											   std::span<const EntityRecord> entities) {
	dataio::ByteWriter w(4096);
	w.PutVarUint(kChunkBlobVersion);
	if (voxels) {
		w.PutU8(1);
		dataio::RleEncode16(std::span<const uint16_t>(voxels, kChunkVolume), w);
	} else {
		w.PutU8(0);
		w.Put<uint16_t>(fill);
	}
	// Orientations (version 3+) : absentes si toutes nulles.
	if (orients) {
		w.PutU8(1);
		std::vector<uint16_t> wide(kChunkVolume);
		for (size_t i = 0; i < kChunkVolume; ++i)
			wide[i] = orients[i];
		dataio::RleEncode16(wide, w);
	} else {
		w.PutU8(0);
	}
	w.PutVarUint(entities.size());
	for (const EntityRecord &e : entities)
		WriteEntityRecord(w, e);
	return w.Take();
}

bool WorldStorage::DecodeChunk(std::span<const uint8_t> blob, DecodedChunk &out) {
	dataio::ByteReader r(blob);
	const uint64_t version = r.GetVarUint();
	if (version < 1 || version > kChunkBlobVersion)
		return false;
	if (r.GetU8() == 1) {
		out.voxels = std::make_unique_for_overwrite<BlockId[]>(kChunkVolume);
		if (!dataio::RleDecode16(r, std::span<uint16_t>(out.voxels.get(), kChunkVolume)))
			return false;
	} else {
		out.fill = r.Get<uint16_t>();
	}
	if (version >= 3) {
		if (r.GetU8() == 1) {
			std::vector<uint16_t> wide(kChunkVolume);
			if (!dataio::RleDecode16(r, wide))
				return false;
			out.orients = std::make_unique_for_overwrite<uint8_t[]>(kChunkVolume);
			bool any = false;
			for (size_t i = 0; i < kChunkVolume; ++i) {
				if (wide[i] >= kOrientCount) // fichier corrompu : orientation hors des 24 rotations
					return false;
				out.orients[i] = uint8_t(wide[i]);
				any |= wide[i] != 0;
			}
			if (!any)
				out.orients.reset();
		}
	}
	const uint64_t n = r.GetVarUint();
	if (!r.Ok() || n > 1'000'000)
		return false;
	out.entities.resize(n);
	for (EntityRecord &e : out.entities)
		if (!ReadEntityRecord(r, e, uint32_t(version)))
			return false;
	return r.Ok();
}

void WorldStorage::SaveChunk(const IVec3 &coord, std::vector<uint8_t> blob, std::span<const EntityRecord> entities, int64_t tick) {
	DIAG_ZONE("WorldStorage::SaveChunk");
	const ChunkKey key = MakeChunkKey(coord);
	const auto k = static_cast<int64_t>(key);
	m_regions->SaveAsync(key, std::move(blob));
	const sql::Value row[] = {k, k, coord.x, coord.y, coord.z, static_cast<int64_t>(entities.size()), tick};
	(void)m_db.Execute(m_upsertChunk, row);
	const sql::Value del[] = {k};
	(void)m_db.Execute(m_deleteEntities, del);
	for (const EntityRecord &e : entities) {
		const sql::Value ent[] = {m_nextEntityId++, k, e.kind, e.position.x, e.position.y, e.position.z};
		(void)m_db.Execute(m_insertEntity, ent);
	}
}

std::vector<ChunkKey> WorldStorage::SavedChunksInBox(const IVec3 &min, const IVec3 &max) {
	std::vector<ChunkKey> out;
	std::vector<std::pair<uint64_t, uint64_t>> ranges;
	sql::MortonBoxRanges(min.x, min.y, min.z, max.x, max.y, max.z, ranges);
	for (auto [lo, hi] : ranges) {
		const sql::Value p[] = {static_cast<int64_t>(lo), static_cast<int64_t>(hi)};
		auto r = m_db.Execute(m_chunksInRange, p);
		if (!r)
			continue;
		for (const sql::Row &row : r.Value().rows) {
			const IVec3 c{static_cast<int>(row[1].AsInt()), static_cast<int>(row[2].AsInt()), static_cast<int>(row[3].AsInt())};
			// Les intervalles fusionnés peuvent déborder : filtre exact par coordonnées.
			if (c.x >= min.x && c.x <= max.x && c.y >= min.y && c.y <= max.y && c.z >= min.z && c.z <= max.z)
				out.push_back(static_cast<ChunkKey>(row[0].AsInt()));
		}
	}
	return out;
}

std::vector<EntityHit> WorldStorage::FindStoredEntities(const IVec3 &minChunk, const IVec3 &maxChunk, std::string_view kind) {
	std::vector<EntityHit> out;
	for (ChunkKey key : SavedChunksInBox(minChunk, maxChunk)) {
		const sql::Value p[] = {static_cast<int64_t>(key)};
		auto r = m_db.Execute(m_entitiesInChunk, p);
		if (!r)
			continue;
		for (const sql::Row &row : r.Value().rows) {
			if (!kind.empty() && row[1].AsText() != kind)
				continue;
			out.push_back({row[0].AsInt(), key, std::string(row[1].AsText()),
						   {static_cast<float>(row[2].AsReal()), static_cast<float>(row[3].AsReal()), static_cast<float>(row[4].AsReal())}});
		}
	}
	return out;
}

void WorldStorage::SetMeta(std::string_view key, std::string_view value) {
	const sql::Value p[] = {key, value};
	(void)m_db.Execute(m_setMeta, p);
}

std::string WorldStorage::GetMeta(std::string_view key, std::string_view fallback) {
	const sql::Value p[] = {key};
	auto r = m_db.Execute(m_getMeta, p);
	if (!r || r.Value().rows.empty())
		return std::string(fallback);
	return std::string(r.Value().At(0, 0).AsText());
}

void WorldStorage::SetConfig(std::string_view key, std::string_view value, std::string_view mod) {
	const sql::Value p[] = {key, value, mod};
	(void)m_db.Execute(m_setConfig, p);
}

std::optional<std::string> WorldStorage::GetConfig(std::string_view key) {
	const sql::Value p[] = {key};
	auto r = m_db.Execute(m_getConfig, p);
	if (!r || r.Value().rows.empty())
		return std::nullopt;
	return std::string(r.Value().At(0, 0).AsText());
}

void WorldStorage::LogEvent(int64_t tick, ChunkKey chunk, std::string_view type, std::string_view mod, std::string_view data) {
	const sql::Value p[] = {m_nextEventId++, tick, static_cast<int64_t>(chunk), type, mod, data};
	(void)m_db.Execute(m_insertEvent, p);
}

void WorldStorage::TrimEvents(size_t keep) {
	const int64_t cutoff = m_nextEventId - static_cast<int64_t>(keep);
	if (cutoff > 1)
		(void)m_db.Execute("DELETE FROM events WHERE id < ?", std::vector<sql::Value>{sql::Value(cutoff)});
}

} // namespace voxel
