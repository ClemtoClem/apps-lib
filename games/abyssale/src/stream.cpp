#include "abyssale/stream.hpp"

#include <algorithm>

#include <core/hash.hpp>
#include <core/morton.hpp>
#include <core/robin_map.hpp>

namespace abyssale {

namespace {
/// Graine déterministe d'un chunk (dérivée de la graine du monde et de sa position).
uint64_t ChunkSeed(uint64_t seed, ChunkKey key) { return core::Mix64(seed ^ key); }
} // namespace

Ocean::Ocean(const Quality &quality, const Shapes &shapes, uint64_t seed, std::filesystem::path dir, jobs::ThreadPool &pool,
			 const math::FVector3 &diverStart, float startTime)
	: m_quality(ScaleQuality(quality, kQualityScale)), m_shapes(shapes), m_seed(seed), m_dir(std::move(dir)), m_pool(pool), m_terrain(seed),
	  m_store(m_dir / "chunks", 4, &pool) {
	m_eco = std::make_unique<Ecosystem>(m_terrain, m_shapes, diverStart, startTime, seed);
}

Ocean::~Ocean() = default;

bool Ocean::Open() {
	std::error_code ec;
	std::filesystem::create_directories(m_dir, ec);
	return true;
}

void Ocean::RequestLoads(int32_t ccx, int32_t ccz) {
	for (int dz = -kChunkRadius; dz <= kChunkRadius; ++dz) {
		for (int dx = -kChunkRadius; dx <= kChunkRadius; ++dx) {
			const int32_t cx = ccx + dx, cz = ccz + dz;
			const ChunkKey key = MakeChunkKey(cx, cz);
			if (m_loaded.Contains(key) || m_pending.load(std::memory_order_relaxed) >= 12)
				continue;
			const ChunkInfo info = m_terrain.GetLayout().At(cx, cz);
			const uint64_t chunkSeed = ChunkSeed(m_seed, key);
			m_pending.fetch_add(1, std::memory_order_relaxed);
			m_pool.Submit(
				[this, key, cx, cz, kind = info.kind, chunkSeed]() {
					ChunkGen r;
					r.key = key;
					r.cx = cx;
					r.cz = cz;
					r.kind = kind;
					r.terrain = m_terrain.BuildChunkMesh(cx, cz);
					r.decor = std::make_unique<World>(m_terrain, m_quality, chunkSeed, cx, cz, kind);
					m_generated.Push(std::move(r));
					m_pending.fetch_sub(1, std::memory_order_relaxed);
				},
				jobs::Priority::High);
		}
	}
}

void Ocean::IntegrateGenerated() {
	m_generated.Drain(m_genScratch);
	for (ChunkGen &r : m_genScratch)
		m_deferred.push_back(std::move(r));
	m_genScratch.clear();
	// Intègre un budget limité de chunks par image : la génération initiale (et les
	// retours dans une zone) est ainsi amortie au lieu de bloquer une image entière.
	for (int n = 0; n < kMaxIntegratePerFrame && !m_deferred.empty(); ++n) {
		ChunkGen r = std::move(m_deferred.front());
		m_deferred.pop_front();
		std::vector<EntityRecord> saved;
		if (auto blob = m_store.Load(r.key))
			saved = DeserializeRecords(*blob);
		const uint64_t chunkSeed = ChunkSeed(m_seed, r.key);
		m_eco->SpawnChunk(r.key, *r.decor, m_quality, chunkSeed, saved);
		m_loaded.InsertOrAssign(r.key, Loaded{r.cx, r.cz, r.kind});
		m_readyMeshes.emplace_back(r.key, std::move(r.terrain));
	}
}

void Ocean::UnloadFar(int32_t ccx, int32_t ccz) {
	const int limit = kChunkRadius + 2;
	std::vector<ChunkKey> drop;
	for (const auto &[key, l] : m_loaded) {
		const int dx = std::abs(l.cx - ccx), dz = std::abs(l.cz - ccz);
		if (dx > limit || dz > limit)
			drop.push_back(key);
	}
	for (const ChunkKey key : drop) {
		const Loaded *l = m_loaded.Find(key);
		const std::vector<EntityRecord> records = m_eco->CaptureChunk(key);
		m_store.Save(key, SerializeRecords(records));
		m_eco->UnloadChunk(key);
		if (l)
			m_terrain.EvictChunk(l->cx, l->cz);
		m_loaded.Erase(key);
		m_unloaded.push_back(key);
	}
}

void Ocean::Update(const math::FVector3 &focus, float) {
	const int32_t ccx = Terrain::ChunkOf(focus.x), ccz = Terrain::ChunkOf(focus.z);
	RequestLoads(ccx, ccz);
	IntegrateGenerated();
	UnloadFar(ccx, ccz);
}

void Ocean::SaveAll() {
	for (const auto &[key, l] : m_loaded) {
		const std::vector<EntityRecord> records = m_eco->CaptureChunk(key);
		m_store.Save(key, SerializeRecords(records));
	}
	m_store.Flush();
}

std::vector<std::pair<ChunkKey, sdl3::MeshData>> Ocean::DrainMeshes() {
	std::vector<std::pair<ChunkKey, sdl3::MeshData>> out;
	std::swap(out, m_readyMeshes);
	return out;
}

std::vector<ChunkKey> Ocean::DrainUnloaded() {
	std::vector<ChunkKey> out;
	std::swap(out, m_unloaded);
	return out;
}

} // namespace abyssale
