#include "voxel/world.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <thread>

#include <core/log.hpp>
#include <diagnostics/trace.hpp>

#include "voxel/orientation.hpp"

namespace voxel {

World::World(BlockRegistry &blocks, ecs::World &ecs, jobs::ThreadPool &pool, WorldConfig config)
	: m_blocks(blocks), m_ecs(ecs), m_pool(pool), m_config(std::move(config)) {}

World::~World() {
	m_alive->store(false);
	// Les tâches en vol écrivent dans nos canaux : on attend qu'elles se terminent.
	while (m_pendingLoads.load() > 0 || m_pendingMeshes.load() > 0)
		if (!m_pool.RunOne())
			std::this_thread::yield();
}

bool World::Open() {
	m_storage = std::make_unique<WorldStorage>(m_config.directory, m_pool);
	if (!m_storage->Open())
		return false;
	const std::string seed = m_storage->GetMeta("seed");
	if (seed.empty())
		m_storage->SetMeta("seed", std::to_string(m_config.seed));
	else
		m_config.seed = std::stoull(seed);
	m_tick = std::atoll(m_storage->GetMeta("tick", "0").c_str());
	// Réglages de génération : figés à la création, relus ensuite depuis la base.
	TerrainSettings &t = m_config.terrain;
	if (const std::string js = m_storage->GetMeta("terrain"); !js.empty()) { // réglages complets (relief, structures)
		if (auto parsed = dataio::ParseJson(js))
			t = TerrainFromJson(parsed.Value());
	} else if (const std::string g = m_storage->GetMeta("generator"); !g.empty()) { // monde d'avant la phase 6
		t.type = static_cast<GeneratorType>(std::clamp(std::atoi(g.c_str()), 0, 3));
		t.relief = std::strtof(m_storage->GetMeta("relief", "1").c_str(), nullptr);
		t.creatures = m_storage->GetMeta("creatures", "1") == "1";
		t.trees = m_storage->GetMeta("trees", "1") == "1";
	} else {
		m_storage->SetMeta("generator", std::to_string(int(t.type)));
		m_storage->SetMeta("relief", std::to_string(t.relief));
		m_storage->SetMeta("creatures", t.creatures ? "1" : "0");
		m_storage->SetMeta("trees", t.trees ? "1" : "0");
		m_storage->SetMeta("terrain", dataio::WriteJson(TerrainToJson(t), -1));
	}
	m_terrain = std::make_unique<TerrainGenerator>(m_config.seed, TerrainPalette::Resolve(m_blocks), t, &m_blocks, m_config.structures);
	core::log::Info("monde '{}' ouvert (graine {})", m_config.directory.string(), m_config.seed);
	return true;
}

Chunk *World::GetChunk(ChunkKey key) noexcept {
	auto *p = m_chunks.Find(key);
	return p ? p->get() : nullptr;
}

Chunk *World::ReadyChunk(ChunkKey key) noexcept {
	Chunk *c = GetChunk(key);
	return (c && c->state == ChunkState::Ready) ? c : nullptr;
}

BlockId World::GetBlock(int x, int y, int z) const noexcept {
	const auto *p = m_chunks.Find(MakeChunkKey(ChunkOfVoxel(x, y, z)));
	if (!p || (*p)->state != ChunkState::Ready)
		return kAir;
	return (*p)->Get(x & kChunkMask, y & kChunkMask, z & kChunkMask);
}

uint8_t World::GetOrient(int x, int y, int z) const noexcept {
	const auto *p = m_chunks.Find(MakeChunkKey(ChunkOfVoxel(x, y, z)));
	if (!p || (*p)->state != ChunkState::Ready)
		return 0;
	return (*p)->GetOrient(x & kChunkMask, y & kChunkMask, z & kChunkMask);
}

bool World::IsLoaded(int x, int y, int z) const noexcept {
	const auto *p = m_chunks.Find(MakeChunkKey(ChunkOfVoxel(x, y, z)));
	return p && (*p)->state == ChunkState::Ready;
}

bool World::SetBlock(int x, int y, int z, BlockId id, uint8_t orient) {
	const IVec3 c = ChunkOfVoxel(x, y, z);
	Chunk *chunk = ReadyChunk(MakeChunkKey(c));
	if (!chunk)
		return false;
	const int lx = x & kChunkMask, ly = y & kChunkMask, lz = z & kChunkMask;
	const BlockId old = chunk->Get(lx, ly, lz);
	if (old == id && chunk->GetOrient(lx, ly, lz) == orient)
		return true;
	chunk->Set(lx, ly, lz, id, orient);
	MarkDirty(c);
	// Voxel en bordure : le voisin partage la face (et l'AO) → à remailler aussi.
	if (lx == 0) MarkDirty(c + IVec3{-1, 0, 0});
	if (lx == kChunkMask) MarkDirty(c + IVec3{1, 0, 0});
	if (ly == 0) MarkDirty(c + IVec3{0, -1, 0});
	if (ly == kChunkMask) MarkDirty(c + IVec3{0, 1, 0});
	if (lz == 0) MarkDirty(c + IVec3{0, 0, -1});
	if (lz == kChunkMask) MarkDirty(c + IVec3{0, 0, 1});
	if (onBlockChanged)
		onBlockChanged({x, y, z}, old, id);
	// Un changement peut faire tomber un bloc au-dessus ou couler un liquide voisin.
	ScheduleBlockPhysics({x, y, z});
	return true;
}

size_t World::PlaceStructure(const Structure &st, int x, int y, int z, uint8_t orient) {
	size_t placed = 0;
	const int layerCount = int(st.layers.size());
	const int yaw = orient & 3;
	for (int ly = 0; ly < layerCount; ++ly) {
		const int cy = layerCount - 1 - ly; // les couches sont déclarées du haut vers le bas
		const auto &rows = st.layers[size_t(ly)];
		for (int rz = 0; rz < int(rows.size()); ++rz) {
			const std::string &row = rows[size_t(rz)];
			for (int cx = 0; cx < int(row.size()); ++cx) {
				const char ch = row[size_t(cx)];
				if (ch == ' ')
					continue;
				const auto it = st.palette.find(ch);
				if (it == st.palette.end())
					continue;
				const BlockId id = m_blocks.Find(it->second);
				if (id == kAir)
					continue;
				// Position dans la structure (avant rotation), relative à l'ancre.
				const int lx = cx - st.anchor[0], lz = rz - st.anchor[2];
				// Rotation globale (cap) autour de l'ancre, dans le plan XZ.
				int rx = lx, rzz = lz;
				switch (yaw) {
				case 1: rx = -lz; rzz = lx; break; // est
				case 2: rx = -lx; rzz = -lz; break; // sud
				case 3: rx = lz; rzz = -lx; break; // ouest
				default: break;
				}
				uint8_t o = 0;
				if (const auto oi = st.orient.find(ch); oi != st.orient.end())
					o = oi->second;
				else if (orient != 0)
					o = BlockOrient(yaw); // cap seul (haut inchangé)
				placed += SetBlock(x + rx, y + cy - st.anchor[1], z + rzz, id, o) ? size_t{1} : size_t{0};
			}
		}
	}
	return placed;
}

void World::MarkDirty(const IVec3 &coord) {
	if (Chunk *c = GetChunkAt(coord))
		c->meshDirty = true;
}

int World::SurfaceHeight(int x, int z) const {
	// Altitude théorique du terrain (O(1)), corrigée localement si le chunk est
	// chargé (terrain édité par le joueur). Avec un monde de millions de blocs de
	// profondeur, il ne faut pas balayer toute la colonne.
	const int base = m_terrain ? m_terrain->SurfaceHeight(x, z) : 64;
	for (int y = base + 64; y >= base - 64; --y)
		if (IsLoaded(x, y, z) && m_blocks.IsSolid(GetBlock(x, y, z)))
			return y;
	return base;
}

bool World::InRange(const IVec3 &c, const IVec3 &center, int extra) const noexcept {
	const int dx = c.x - center.x, dz = c.z - center.z, dy = c.y - center.y;
	const int r = m_config.viewDistance + extra;
	return dx * dx + dz * dz <= r * r && std::abs(dy) <= m_config.verticalDistance + extra;
}

// ════════════════════════════════════════════════════════════════════════
// Streaming
// ════════════════════════════════════════════════════════════════════════
void World::Update(const math::FVector3 &focus, float dt) {
	DIAG_ZONE("World::Update");
	++m_tick;
	const IVec3 center = ChunkOfPosition(focus);
	IntegrateLoaded();
	TickBlocks(dt);
	RequestLoads(center);
	UnloadFar(center);
	ScheduleMeshes(center);
	IntegrateMeshes();
	DIAG_COUNTER("chunks.loaded", m_chunks.Size());
	DIAG_COUNTER("chunks.pendingLoads", m_pendingLoads.load());
	DIAG_COUNTER("entities", m_ecs.EntityCount());
}

void World::RequestLoads(const IVec3 &center) {
	DIAG_ZONE("World::RequestLoads");
	if (center != m_lastCenter) {
		// Liste des chunks souhaités, du plus proche au plus lointain (recalculée
		// seulement quand le joueur change de chunk).
		m_lastCenter = center;
		m_wanted.clear();
		const int r = m_config.viewDistance, v = m_config.verticalDistance;
		for (int dy = -v; dy <= v; ++dy) {
			const int cy = center.y + dy;
			if (cy < kMinChunkY || cy > kMaxChunkY)
				continue;
			for (int dz = -r; dz <= r; ++dz)
				for (int dx = -r; dx <= r; ++dx)
					if (dx * dx + dz * dz <= r * r)
						m_wanted.push_back({center.x + dx, cy, center.z + dz});
		}
		std::ranges::sort(m_wanted, [&](const IVec3 &a, const IVec3 &b) {
			auto d = [&](const IVec3 &p) {
				const int dx = p.x - center.x, dy = p.y - center.y, dz = p.z - center.z;
				return dx * dx + dz * dz + dy * dy * 2; // privilégie la couche du joueur
			};
			return d(a) < d(b);
		});
	}
	for (const IVec3 &coord : m_wanted) {
		if (m_pendingLoads.load(std::memory_order_relaxed) >= m_config.maxPendingLoads)
			break;
		const ChunkKey key = MakeChunkKey(coord);
		auto [slot, inserted] = m_chunks.TryEmplace(key);
		if (!inserted)
			continue;
		*slot = std::make_unique<Chunk>(coord);
		m_pendingLoads.fetch_add(1);
		// Requête asynchrone : clé de Morton → module sql (RegionStore) ou génération.
		m_pool.Submit(
			[this, coord, key, alive = m_alive] {
				DIAG_ZONE("World::LoadChunk");
				LoadResult res;
				res.coord = coord;
				if (alive->load()) {
					if (auto blob = m_storage->Regions().Load(key); blob && WorldStorage::DecodeChunk(*blob, res.data)) {
						res.fromDisk = true;
					} else {
						res.data = {};
						res.data.voxels = std::make_unique_for_overwrite<BlockId[]>(kChunkVolume);
						m_terrain->Generate(coord, std::span<BlockId>(res.data.voxels.get(), kChunkVolume), &res.data.entities);
					}
				}
				m_loaded.Push(std::move(res));
				m_pendingLoads.fetch_sub(1);
			},
			jobs::Priority::High);
	}
}

void World::IntegrateLoaded() {
	DIAG_ZONE("World::IntegrateLoaded");
	m_loaded.Drain(m_loadScratch);
	for (LoadResult &r : m_loadScratch) {
		Chunk *chunk = GetChunkAt(r.coord);
		if (!chunk || chunk->state != ChunkState::Loading)
			continue;
		if (r.data.voxels)
			chunk->Adopt(std::move(r.data.voxels));
		else
			chunk->Fill(r.data.fill);
		if (r.data.orients)
			chunk->AdoptOrients(std::move(r.data.orients));
		chunk->SetModified(false);
		chunk->fromDisk = r.fromDisk;
		chunk->state = ChunkState::Ready;
		(r.fromDisk ? m_fromDisk : m_generated)++;
		// Les entités sauvegardées reprennent vie dans l'ECS.
		for (const EntityRecord &rec : r.data.entities) {
			const ecs::Entity e = InstantiateEntity(m_ecs, rec);
			RegisterEntity(e, *chunk);
		}
		chunk->meshDirty = true;
		for (const IVec3 &n : kFaceNormals)
			MarkDirty(r.coord + n);
	}
	m_loadScratch.clear();
}

void World::UnloadFar(const IVec3 &center) {
	DIAG_ZONE("World::UnloadFar");
	std::vector<ChunkKey> victims;
	for (auto &e : m_chunks) {
		Chunk &c = *e.value;
		if (c.state == ChunkState::Ready && !c.meshPending && !InRange(c.Coord(), center, 2))
			victims.push_back(e.key);
		if (static_cast<int>(victims.size()) >= m_config.maxUnloadPerFrame)
			break;
	}
	for (ChunkKey key : victims) {
		if (Chunk *c = GetChunk(key)) {
			UnloadChunk(*c);
			m_chunks.Erase(key);
			m_unloaded.push_back(key);
			++m_unloadedCount;
		}
	}
}

void World::UnloadChunk(Chunk &chunk) {
	DIAG_ZONE("World::UnloadChunk");
	std::vector<EntityRecord> records;
	records.reserve(chunk.entities.size());
	const std::vector<ecs::Entity> ents = chunk.entities;
	for (ecs::Entity e : ents) {
		if (!m_ecs.IsAlive(e))
			continue;
		if (m_ecs.Has<Transient>(e)) { // non sauvegardée : sera réinscrite dans un chunk chargé
			if (GridCell *cell = m_ecs.Get<GridCell>(e))
				cell->slot = UINT32_MAX;
			continue;
		}
		records.push_back(CaptureEntity(m_ecs, e));
		UnregisterEntity(e);
		m_ecs.Despawn(e);
	}
	chunk.entities.clear();
	// Un chunk généré intact et sans entité est régénéré à l'identique : inutile de l'écrire.
	if (chunk.Modified() || !records.empty() || chunk.fromDisk) {
		m_storage->SaveChunk(chunk.Coord(), WorldStorage::EncodeChunk(chunk.RawVoxels(), chunk.UniformValue(), chunk.RawOrients(), records), records, m_tick);
		++m_saved;
	}
}

void World::ScheduleMeshes(const IVec3 &center) {
	DIAG_ZONE("World::ScheduleMeshes");
	if (!m_meshContext)
		return;
	struct Candidate {
		Chunk *chunk;
		int dist;
	};
	std::vector<Candidate> candidates;
	for (auto &e : m_chunks) {
		Chunk &c = *e.value;
		if (c.state != ChunkState::Ready || !c.meshDirty || c.meshPending)
			continue;
		// Attendre que les voisins dans le champ soient prêts : pas de couture visible.
		bool neighborsReady = true;
		for (const IVec3 &n : kFaceNormals) {
			const IVec3 nc = c.Coord() + n;
			if (nc.y < kMinChunkY || nc.y > kMaxChunkY)
				continue;
			Chunk *nb = GetChunkAt(nc);
			if ((nb && nb->state != ChunkState::Ready) || (!nb && InRange(nc, center, 0))) {
				neighborsReady = false;
				break;
			}
		}
		if (!neighborsReady)
			continue;
		const IVec3 d = c.Coord() - center;
		candidates.push_back({&c, d.x * d.x + d.y * d.y + d.z * d.z});
	}
	std::ranges::sort(candidates, {}, &Candidate::dist);
	for (const Candidate &cand : candidates) {
		if (m_pendingMeshes.load(std::memory_order_relaxed) >= m_config.maxPendingMeshes)
			break;
		Chunk &c = *cand.chunk;
		c.meshDirty = false;
		// Chunk vide et voisins sans incidence : maillage vide immédiat.
		if (c.IsEmpty()) {
			m_meshReady.push_back({c.Key(), c.Coord(), c.Version(), {}});
			continue;
		}
		// Copie rembourrée 34³ sur le thread principal → maillage sans verrou.
		auto padded = std::make_unique_for_overwrite<BlockId[]>(kPaddedVolume);
		std::fill_n(padded.get(), kPaddedVolume, kAir);
		auto orients = std::make_unique_for_overwrite<uint8_t[]>(kPaddedVolume);
		std::fill_n(orients.get(), kPaddedVolume, uint8_t{0});
		for (int y = -1; y <= kChunkSize; ++y) {
			const int cy = y < 0 ? -1 : (y >= kChunkSize ? 1 : 0);
			for (int z = -1; z <= kChunkSize; ++z) {
				const int cz = z < 0 ? -1 : (z >= kChunkSize ? 1 : 0);
				for (int x = -1; x <= kChunkSize; ++x) {
					const int cx = x < 0 ? -1 : (x >= kChunkSize ? 1 : 0);
					const Chunk *src = (cx | cy | cz) == 0 ? &c : GetChunkAt(c.Coord() + IVec3{cx, cy, cz});
					if (!src || src->state != ChunkState::Ready)
						continue;
					const size_t idx = static_cast<size_t>(PaddedIndex(x, y, z));
					padded[idx] = src->Get(x & kChunkMask, y & kChunkMask, z & kChunkMask);
					orients[idx] = src->GetOrient(x & kChunkMask, y & kChunkMask, z & kChunkMask);
				}
			}
		}
		c.meshPending = true;
		m_pendingMeshes.fetch_add(1);
		m_pool.Submit(
			[this, key = c.Key(), coord = c.Coord(), version = c.Version(), padded = std::move(padded), orients = std::move(orients), ctx = m_meshContext]() {
				MeshResult r{key, coord, version, {}};
				BuildChunkMesh(std::span<const BlockId>(padded.get(), kPaddedVolume), std::span<const uint8_t>(orients.get(), kPaddedVolume), *ctx, r.mesh);
				m_meshed.Push(std::move(r));
				m_pendingMeshes.fetch_sub(1);
			},
			jobs::Priority::Normal);
	}
}

void World::IntegrateMeshes() {
	std::vector<MeshResult> done;
	m_meshed.Drain(done);
	for (MeshResult &r : done) {
		Chunk *c = GetChunk(r.key);
		if (!c)
			continue; // déchargé entre-temps
		c->meshPending = false;
		m_meshReady.push_back(std::move(r));
	}
}

void World::DrainMeshes(std::vector<MeshResult> &out) {
	out.clear();
	std::swap(out, m_meshReady);
}

void World::DrainUnloaded(std::vector<ChunkKey> &out) {
	out.clear();
	std::swap(out, m_unloaded);
}

void World::RemeshAll() {
	for (auto &e : m_chunks)
		e.value->meshDirty = true;
}

void World::SaveAll() {
	DIAG_ZONE("World::SaveAll");
	for (auto &e : m_chunks) {
		Chunk &c = *e.value;
		if (c.state != ChunkState::Ready)
			continue;
		std::vector<EntityRecord> records;
		for (ecs::Entity ent : c.entities)
			if (m_ecs.IsAlive(ent) && !m_ecs.Has<Transient>(ent))
				records.push_back(CaptureEntity(m_ecs, ent));
		if (c.Modified() || !records.empty() || c.fromDisk) {
			m_storage->SaveChunk(c.Coord(), WorldStorage::EncodeChunk(c.RawVoxels(), c.UniformValue(), c.RawOrients(), records), records, m_tick);
			c.SetModified(false);
			c.fromDisk = true;
			++m_saved;
		}
	}
	m_storage->SetMeta("tick", std::to_string(m_tick));
	m_storage->Flush();
}

// ════════════════════════════════════════════════════════════════════════
// Grille spatiale d'entités
// ════════════════════════════════════════════════════════════════════════
void World::RegisterEntity(ecs::Entity e, Chunk &chunk) {
	GridCell *cell = m_ecs.Get<GridCell>(e);
	if (!cell)
		cell = &m_ecs.Add(e, GridCell{});
	cell->chunk = chunk.Key();
	cell->slot = static_cast<uint32_t>(chunk.entities.size());
	chunk.entities.push_back(e);
}

void World::UnregisterEntity(ecs::Entity e) {
	GridCell *cell = m_ecs.Get<GridCell>(e);
	if (!cell || cell->slot == UINT32_MAX)
		return;
	if (Chunk *chunk = GetChunk(cell->chunk); chunk && cell->slot < chunk->entities.size() && chunk->entities[cell->slot] == e) {
		// Swap-remove O(1) : la dernière entité prend la place, son rang est corrigé.
		const ecs::Entity last = chunk->entities.back();
		chunk->entities[cell->slot] = last;
		chunk->entities.pop_back();
		if (last != e)
			if (GridCell *lc = m_ecs.Get<GridCell>(last))
				lc->slot = cell->slot;
	}
	cell->slot = UINT32_MAX;
}

ecs::Entity World::SpawnEntity(const EntityRecord &record) {
	const ecs::Entity e = InstantiateEntity(m_ecs, record);
	if (Chunk *c = ReadyChunk(ChunkKeyOfPosition(record.position)))
		RegisterEntity(e, *c);
	return e;
}

void World::DespawnEntity(ecs::Entity e) {
	if (!m_ecs.IsAlive(e))
		return;
	UnregisterEntity(e);
	m_ecs.Despawn(e);
}

void World::UpdateEntityGrid() {
	DIAG_ZONE("World::UpdateEntityGrid");
	struct Move {
		ecs::Entity e;
		ChunkKey to;
	};
	// 1) Détection parallèle des franchissements de frontière (lecture seule) :
	//    un tampon local par lot de lignes, aucun verrou partagé.
	struct Range {
		ecs::Archetype *arch;
		size_t first, last;
	};
	constexpr size_t kGrain = 8192;
	std::vector<Range> ranges;
	auto query = m_ecs.MakeQuery<const Transform, const GridCell>();
	for (ecs::Archetype *a : query.MatchedArchetypes())
		for (size_t f = 0; f < a->Size(); f += kGrain)
			ranges.push_back({a, f, std::min(a->Size(), f + kGrain)});
	std::vector<std::vector<Move>> perRange(ranges.size());
	jobs::ParallelFor(m_pool, 0, ranges.size(), 1, [&](size_t i) {
		const Range &r = ranges[i];
		const Transform *tr = r.arch->Data<Transform>();
		const GridCell *gc = r.arch->Data<GridCell>();
		const auto ents = r.arch->Entities();
		auto &out = perRange[i];
		for (size_t row = r.first; row < r.last; ++row) {
			const ChunkKey k = ChunkKeyOfPosition(tr[row].position);
			if (k != gc[row].chunk || gc[row].slot == UINT32_MAX)
				out.push_back({ents[row], k});
		}
	});
	std::vector<Move> list;
	for (auto &v : perRange)
		list.insert(list.end(), v.begin(), v.end());
	// 2) Application séquentielle : désinscription du chunk A, inscription dans B.
	size_t applied = 0;
	for (const Move &m : list) {
		Chunk *target = ReadyChunk(m.to);
		if (!target)
			continue; // chunk cible non chargé : l'entité reste rattachée à son chunk actuel
		UnregisterEntity(m.e);
		RegisterEntity(m.e, *target);
		++applied;
	}
	m_lastGridMoves = applied;
	DIAG_COUNTER("entities.gridMoves", applied);
}

size_t World::QueryEntities(const math::FVector3 &pos, float radius, std::vector<ecs::Entity> &out, std::string_view kind) {
	out.clear();
	const int chunks = std::max(0, static_cast<int>(std::ceil(radius / kChunkSize)));
	const float r2 = radius * radius;
	std::vector<std::pair<float, ecs::Entity>> hits;
	ForEachEntityNear(pos, chunks, [&](ecs::Entity e) {
		const Transform *t = m_ecs.Get<Transform>(e);
		if (!t)
			return;
		const float d2 = (t->position - pos).LengthSq();
		if (d2 > r2)
			return;
		if (!kind.empty()) {
			const Kind *k = m_ecs.Get<Kind>(e);
			if (!k || k->id != kind)
				return;
		}
		hits.emplace_back(d2, e);
	});
	std::ranges::sort(hits, {}, &std::pair<float, ecs::Entity>::first);
	for (auto &[d, e] : hits)
		out.push_back(e);
	return out.size();
}

void World::SimulateEntities(const math::FVector3 &focus, float dt) {
	DIAG_ZONE("World::SimulateEntities");
	const IVec3 center = ChunkOfPosition(focus);
	const int sim = m_config.simulationDistance;
	const int64_t tick = m_tick;
	// Errance : nouvelle direction de temps en temps (parallèle, écrit seulement l'entité courante).
	m_ecs.MakeQuery<Wander, Velocity, Transform>().Without<Transient>().ParEach([&](ecs::Entity e, Wander &w, Velocity &v, Transform &t) {
		w.timer -= dt;
		if (w.timer > 0.f)
			return;
		const float r = math::Hash01(static_cast<int32_t>(e.index), static_cast<int32_t>(tick), 0, w.seed);
		w.timer = 2.f + r * 4.f;
		if (r < 0.35f) {
			v.value.x = v.value.z = 0.f;
			return;
		}
		t.yaw = r * math::TAU * 3.f;
		v.value.x = std::sin(t.yaw) * w.speed;
		v.value.z = -std::cos(t.yaw) * w.speed;
	});
	// Intégration physique des entités des chunks simulés (lecture des voxels seulement).
	m_ecs.MakeQuery<Transform, Velocity, Body>().Without<Transient>().ParEach([&](Transform &t, Velocity &v, Body &b) {
		const IVec3 c = ChunkOfPosition(t.position);
		const int dx = c.x - center.x, dz = c.z - center.z;
		if (dx * dx + dz * dz > sim * sim || std::abs(c.y - center.y) > sim)
			return;
		if (!IsLoaded(math::FastFloor(t.position.x), math::FastFloor(t.position.y), math::FastFloor(t.position.z)))
			return; // ne tombe pas dans un chunk absent
		// Flottabilité : dans un liquide, l'entité remonte doucement vers la surface.
		const int lx = math::FastFloor(t.position.x), ly = math::FastFloor(t.position.y), lz = math::FastFloor(t.position.z);
		const bool inLiquid = IsLoaded(lx, ly, lz) && m_blocks.Get(GetBlock(lx, ly, lz)).IsLiquid();
		if (inLiquid)
			v.value.y = std::min(std::max(v.value.y, 0.f) + 18.f * dt, 3.5f);
		else if (b.gravity)
			v.value.y = std::max(v.value.y - 24.f * dt, -50.f);
		const math::FVector3 wanted = v.value;
		MoveBody(t.position, v.value, b, dt);
		// Bloquée contre une marche d'un bloc : petit saut.
		if (b.onGround && ((wanted.x != 0.f && v.value.x == 0.f) || (wanted.z != 0.f && v.value.z == 0.f))) {
			v.value = {wanted.x, 7.5f, wanted.z};
		}
	});
}

// ════════════════════════════════════════════════════════════════════════
// Physique des blocs : chute (sable/gravier) et écoulement des liquides
// ════════════════════════════════════════════════════════════════════════
bool World::Replaceable(BlockId id) const noexcept {
	if (id == kAir)
		return true;
	const BlockDef &d = m_blocks.Get(id);
	return d.material == Material::Liquid || d.cross || !d.solid;
}

namespace {
/// Clé de hachage d'une position (file de simulation des blocs).
uint64_t PosHash(int x, int y, int z) noexcept {
	uint64_t h = 0x9e3779b97f4a7c15ULL;
	auto mix = [&](int v) { h ^= uint64_t(uint32_t(v)) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
	mix(x);
	mix(y);
	mix(z);
	return h;
}
} // namespace

void World::ScheduleBlockPhysics(const IVec3 &pos) {
	static constexpr IVec3 kAround[7] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (const IVec3 &d : kAround) {
		const IVec3 p = pos + d;
		if (IsLoaded(p.x, p.y, p.z))
			m_blockPhysics.push_back(p);
	}
}

void World::FlowLiquid(const IVec3 &p, const BlockDef &def, uint8_t level) {
	const IVec3 below{p.x, p.y - 1, p.z};
	// 1) Chute dans le vide / un liquide remplaçable.
	if (IsLoaded(below.x, below.y, below.z) && Replaceable(GetBlock(below.x, below.y, below.z))) {
		// Réaction de mélange (lave + eau → obsidienne…).
		const BlockId other = GetBlock(below.x, below.y, below.z);
		if (other != kAir && m_blocks.Get(other).IsLiquid()) {
			for (const auto &mix : def.liquidMix)
				if (const BlockId result = m_blocks.Find(mix.with); result == other && result != kAir) {
					SetBlock(below.x, below.y, below.z, m_blocks.Find(mix.source));
					return;
				}
		}
		const BlockId me = GetBlock(p.x, p.y, p.z);
		SetBlock(below.x, below.y, below.z, me);
		SetBlock(p.x, p.y, p.z, kAir);
		m_liquidLevel.InsertOrAssign(PosHash(below.x, below.y, below.z), level);
		return;
	}
	// 2) Sinon, étalement horizontal (borné par le niveau restant).
	if (level == 0)
		return;
	constexpr IVec3 kSides[4] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}};
	for (const IVec3 &d : kSides) {
		const IVec3 n = p + d;
		if (!IsLoaded(n.x, n.y, n.z))
			continue;
		if (!Replaceable(GetBlock(n.x, n.y, n.z)))
			continue;
		const BlockId me = GetBlock(p.x, p.y, p.z);
		SetBlock(n.x, n.y, n.z, me);
		uint8_t next = uint8_t(level - 1);
		// Source infinie (règle 2×2 de Minecraft) : une source qui se répand régénère une
		// source pleine si la cellule d'arrivée borde au moins deux sources pleines.
		// Réservé aux liquides `renewable` (eau) — la lave s'épuise.
		if (def.renewable && level >= def.liquidRange) {
			int sources = 1; // la cellule d'origine `p` est une source pleine
			for (const IVec3 &d2 : kSides) {
				const IVec3 s = n + d2;
				if (s.x == p.x && s.y == p.y && s.z == p.z)
					continue;
				if (!IsLoaded(s.x, s.y, s.z) || GetBlock(s.x, s.y, s.z) != me)
					continue;
				const uint8_t *sl = m_liquidLevel.Find(PosHash(s.x, s.y, s.z));
				if (!sl || *sl >= def.liquidRange)
					++sources;
			}
			if (sources >= 2)
				next = uint8_t(def.liquidRange);
		}
		m_liquidLevel.InsertOrAssign(PosHash(n.x, n.y, n.z), next);
		break; // une seule cellule par pas
	}
}

void World::TickBlocks(float dt) {
	constexpr size_t kBudget = 512;
	size_t processed = 0;
	// 1) File d'événements (SetBlock) : détecte les blocs qui se mettent à tomber
	//    et déclenche l'écoulement des liquides.
	while (m_blockPhysicsHead < m_blockPhysics.size() && processed < kBudget) {
		const IVec3 p = m_blockPhysics[m_blockPhysicsHead++];
		++processed;
		if (!IsLoaded(p.x, p.y, p.z))
			continue;
		const BlockId id = GetBlock(p.x, p.y, p.z);
		if (id == kAir)
			continue;
		const BlockDef &def = m_blocks.Get(id);
		if (def.falling) {
			// Démarre une chute si la case du dessous est libre et qu'on n'est pas déjà en vol.
			const IVec3 below{p.x, p.y - 1, p.z};
			const uint64_t key = PosHash(p.x, p.y, p.z);
			if (IsLoaded(below.x, below.y, below.z) && Replaceable(GetBlock(below.x, below.y, below.z)) && !m_falling.Contains(key)) {
				m_falling.InsertOrAssign(key, FallState{});
				m_fallingActive.push_back(p);
			}
		} else if (def.IsLiquid()) {
			// Viscosité : un pas d'écoulement tous les `liquidTick` secondes.
			const uint64_t key = PosHash(p.x, p.y, p.z);
			const uint64_t interval = std::max<uint64_t>(1, uint64_t(def.liquidTick * 60.f));
			const uint64_t now = uint64_t(std::max<int64_t>(m_tick, 0));
			if (const uint64_t *next = m_liquidNext.Find(key); next && now < *next)
				continue; // pas encore à échéance
			m_liquidNext.InsertOrAssign(key, now + interval);
			uint8_t level = def.liquidRange;
			if (const uint8_t *l = m_liquidLevel.Find(key))
				level = *l;
			FlowLiquid(p, def, level);
		}
	}
	// 2) Gravité : avance les blocs en chute libre (progressive, accélérée).
	AdvanceFalling(dt);
	// Compacte périodiquement la file (évite une croissance sans fin).
	if (m_blockPhysicsHead > 8192 && m_blockPhysicsHead * 2 > m_blockPhysics.size()) {
		m_blockPhysics.erase(m_blockPhysics.begin(), m_blockPhysics.begin() + std::ptrdiff_t(m_blockPhysicsHead));
		m_blockPhysicsHead = 0;
	}
}

void World::AdvanceFalling(float dt) {
	constexpr float kGravity = 9.81f;	  // 1 bloc = 1 m³ : accélération en m/s²
	constexpr float kFallTerminal = 40.f; // vitesse limite de chute (m/s)
	for (size_t i = 0; i < m_fallingActive.size();) {
		const IVec3 p = m_fallingActive[i];
		const uint64_t key = PosHash(p.x, p.y, p.z);
		FallState *state = m_falling.Find(key);
		const BlockId id = GetBlock(p.x, p.y, p.z);
		if (!state || id == kAir || !m_blocks.Get(id).falling) { // bloc retiré / changé
			if (state)
				m_falling.Erase(key);
			m_fallingActive[i] = m_fallingActive.back();
			m_fallingActive.pop_back();
			continue;
		}
		FallState st = *state;
		const IVec3 below{p.x, p.y - 1, p.z};
		const bool canFall = IsLoaded(below.x, below.y, below.z) && Replaceable(GetBlock(below.x, below.y, below.z));
		if (!canFall) { // atterri sur un support
			m_falling.Erase(key);
			ScheduleBlockPhysics(p); // les blocs au-dessus peuvent à leur tour tomber
			m_fallingActive[i] = m_fallingActive.back();
			m_fallingActive.pop_back();
			continue;
		}
		st.speed = std::min(st.speed + kGravity * dt, kFallTerminal);
		st.progress += st.speed * dt;
		if (st.progress < 1.0f) { // pas encore franchi un mètre : on reste en vol
			*state = st;
			++i;
			continue;
		}
		// Une cellule de chute : report du surplus dans la cellule suivante.
		st.progress -= 1.0f;
		SetBlock(below.x, below.y, below.z, id);
		SetBlock(p.x, p.y, p.z, kAir);
		m_falling.Erase(key);
		m_falling.InsertOrAssign(PosHash(below.x, below.y, below.z), st);
		m_fallingActive[i] = below;
		++i;
	}
}

std::vector<IVec3> World::Explode(const math::FVector3 &center, float power, float damage, bool destroyBlocks) {
	const int cx = math::FastFloor(center.x), cy = math::FastFloor(center.y), cz = math::FastFloor(center.z);
	const float radiusSq = power * power;
	std::vector<IVec3> chained;
	// Directions quasi-uniformes (sphère de Fibonacci) des rayons de souffle.
	static const std::vector<math::FVector3> kRays = [] {
		constexpr int kN = 512;
		std::vector<math::FVector3> r;
		r.reserve(kN);
		for (int i = 0; i < kN; ++i) {
			const float y = 1.f - 2.f * (float(i) + 0.5f) / float(kN);
			const float rad = std::sqrt(std::max(0.f, 1.f - y * y));
			const float phi = math::TAU * float(i) * 0.6180339887f; // angle d'or
			r.push_back({rad * std::cos(phi), y, rad * std::sin(phi)});
		}
		return r;
	}();
	core::RobinMap<uint64_t, bool> destroyed;
	for (size_t i = 0; i < kRays.size(); ++i) {
		const math::FVector3 dir = kRays[i];
		// Intensité initiale déterministe (0,7..1,3 × puissance), comme Minecraft.
		float intensity = power * (0.7f + 0.6f * math::Hash01(int32_t(i), cx, cy, uint64_t(cz)));
		math::FVector3 pos = center;
		while (intensity > 0.f) {
			pos += dir * 0.3f;
			intensity -= 0.225f; // atténuation de l'air (0,75 × 0,3)
			if ((pos - center).LengthSq() > radiusSq)
				break;
			const int x = math::FastFloor(pos.x), y = math::FastFloor(pos.y), z = math::FastFloor(pos.z);
			if (!IsLoaded(x, y, z))
				break;
			const BlockId id = GetBlock(x, y, z);
			if (id == kAir)
				continue;
			const BlockDef &d = m_blocks.Get(id);
			if (!d.IsLiquid())
				intensity -= (d.blastResistance / 5.f + 0.3f) * 0.3f;
			if (intensity <= 0.f)
				break;
			if (!destroyBlocks)
				continue;
			const uint64_t k = PosHash(x, y, z);
			if (destroyed.Find(k))
				continue;
			destroyed.InsertOrAssign(k, true);
			SetBlock(x, y, z, kAir);
			if (d.explosionRadius > 0.f && (x != cx || y != cy || z != cz))
				chained.push_back({x, y, z});
		}
	}
	// Dégâts et recul des entités (retombée linéaire avec la distance).
	const float reach = power * 2.f;
	std::vector<ecs::Entity> near;
	QueryEntities(center, reach, near);
	for (const ecs::Entity e : near) {
		auto *t = m_ecs.Get<voxel::Transform>(e);
		if (!t)
			continue;
		const float dist = (t->position - center).Length();
		const float exposure = 1.f - std::clamp(dist / reach, 0.f, 1.f);
		if (exposure <= 0.f)
			continue;
		if (auto *h = m_ecs.Get<voxel::Health>(e))
			h->current = std::max(0.f, h->current - damage * exposure);
		if (auto *v = m_ecs.Get<voxel::Velocity>(e)) {
			math::FVector3 dir = t->position - center;
			dir.y += 0.3f; // léger soulèvement
			if (dir.LengthSq() < 1e-4f)
				dir = {0.f, 1.f, 0.f};
			v->value += dir.Normalized() * (power * 1.6f * exposure + 0.5f);
		}
	}
	return chained;
}

// ════════════════════════════════════════════════════════════════════════
// Physique
// ════════════════════════════════════════════════════════════════════════
bool World::BoxIntersectsSolid(const math::FAABB &box) const {
	float contact = 0;
	return Collide(box, -1, 0.f, contact);
}

bool World::PointInSolid(const math::FVector3 &p) const {
	const int x = math::FastFloor(p.x), y = math::FastFloor(p.y), z = math::FastFloor(p.z);
	const BlockId id = GetBlock(x, y, z);
	if (!m_blocks.IsSolid(id))
		return false;
	const std::vector<BlockBox> &col = m_blocks.Get(id).collision;
	if (col.empty())
		return true; // cube plein
	const math::FVector3 c{float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f};
	for (const BlockBox &b : col) {
		const float x0 = c.x + std::min(b[0], b[3]), x1 = c.x + std::max(b[0], b[3]);
		const float y0 = c.y + std::min(b[1], b[4]), y1 = c.y + std::max(b[1], b[4]);
		const float z0 = c.z + std::min(b[2], b[5]), z1 = c.z + std::max(b[2], b[5]);
		if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1 && p.z >= z0 && p.z <= z1)
			return true;
	}
	return false;
}

bool World::Collide(const math::FAABB &box, int axis, float dir, float &contact) const {
	const int x0 = math::FastFloor(box.min.x), x1 = math::FastFloor(box.max.x - 1e-4f);
	const int y0 = math::FastFloor(box.min.y), y1 = math::FastFloor(box.max.y - 1e-4f);
	const int z0 = math::FastFloor(box.min.z), z1 = math::FastFloor(box.max.z - 1e-4f);
	bool hit = false;
	contact = dir > 0 ? 1e30f : -1e30f;
	auto test = [&](const math::FVector3 &lo, const math::FVector3 &hi) {
		if (box.min.x >= hi.x || box.max.x <= lo.x || box.min.y >= hi.y || box.max.y <= lo.y || box.min.z >= hi.z || box.max.z <= lo.z)
			return;
		hit = true;
		if (axis >= 0)
			contact = dir > 0 ? std::min(contact, lo[axis]) : std::max(contact, hi[axis]);
	};
	// Une rangée de plus en Y : les boîtes d'un bloc peuvent déborder (barrière haute de
	// 1,5 bloc, étagère accrochée sous le bloc).
	for (int y = y0 - 1; y <= y1 + 1; ++y)
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x) {
				const BlockId id = GetBlock(x, y, z);
				if (!m_blocks.IsSolid(id))
					continue;
				const std::vector<BlockBox> &col = m_blocks.Get(id).collision;
				const math::FVector3 c{float(x) + 0.5f, float(y) + 0.5f, float(z) + 0.5f};
				if (col.empty()) {
					if (y >= y0 && y <= y1)
						test(c - math::FVector3(0.5f), c + math::FVector3(0.5f));
				} else {
					for (const BlockBox &b : col)
						test(c + math::FVector3{b[0], b[1], b[2]}, c + math::FVector3{b[3], b[4], b[5]});
				}
				if (hit && axis < 0)
					return true;
			}
	return hit;
}

void World::MoveBody(math::FVector3 &position, math::FVector3 &velocity, Body &body, float dt) const {
	if (!body.collide) {
		position += velocity * dt;
		return;
	}
	body.onGround = false;
	const math::FVector3 delta = velocity * dt;
	// Sous-pas : jamais plus de 0,45 bloc par axe (pas de tunnel à travers un mur).
	const float maxStep = std::max({std::fabs(delta.x), std::fabs(delta.y), std::fabs(delta.z)});
	const int steps = std::max(1, static_cast<int>(std::ceil(maxStep / 0.45f)));
	const math::FVector3 stepDelta = delta / static_cast<float>(steps);
	for (int s = 0; s < steps; ++s) {
		for (const int a : {1, 0, 2}) { // Y d'abord : appui stable au sol
			const float d = stepDelta[a];
			if (d == 0.f)
				continue;
			math::FVector3 next = position;
			next[a] += d;
			const math::FAABB box{next - body.halfExtents, next + body.halfExtents};
			float contact = 0;
			if (!Collide(box, a, d, contact)) {
				position = next;
				continue;
			}
			// Contact : se coller à la face de la boîte touchée (cube plein ou forme du bloc).
			const float snapped = d > 0.f ? contact - body.halfExtents[a] - 1e-3f : contact + body.halfExtents[a] + 1e-3f;
			// Jamais en arrière (déjà en contact) ni au-delà du pas demandé.
			position[a] = d > 0.f ? std::clamp(snapped, position[a], next[a]) : std::clamp(snapped, next[a], position[a]);
			if (a == 1 && d < 0.f)
				body.onGround = true;
			velocity[a] = 0.f;
		}
	}
}

std::optional<RayHit> World::Raycast(const math::FVector3 &origin, const math::FVector3 &dirIn, float maxDistance) const {
	const math::FVector3 dir = dirIn.Normalized();
	IVec3 p = math::FloorToInt(origin);
	const IVec3 step{dir.x > 0 ? 1 : -1, dir.y > 0 ? 1 : -1, dir.z > 0 ? 1 : -1};
	auto inv = [](float v) { return std::fabs(v) < 1e-9f ? 1e30f : std::fabs(1.f / v); };
	const math::FVector3 tDelta{inv(dir.x), inv(dir.y), inv(dir.z)};
	auto first = [&](float o, int cell, float dcomp) {
		const float boundary = dcomp > 0 ? static_cast<float>(cell + 1) - o : o - static_cast<float>(cell);
		return boundary * inv(dcomp);
	};
	math::FVector3 tMax{first(origin.x, p.x, dir.x), first(origin.y, p.y, dir.y), first(origin.z, p.z, dir.z)};
	IVec3 normal{};
	float t = 0.f;
	while (t <= maxDistance) {
		const BlockId b = GetBlock(p.x, p.y, p.z);
		if (b != kAir && m_blocks.MaterialOf(b) != Material::Liquid)
			return RayHit{p, normal, b, t};
		if (tMax.x < tMax.y && tMax.x < tMax.z) {
			p.x += step.x;
			t = tMax.x;
			tMax.x += tDelta.x;
			normal = {-step.x, 0, 0};
		} else if (tMax.y < tMax.z) {
			p.y += step.y;
			t = tMax.y;
			tMax.y += tDelta.y;
			normal = {0, -step.y, 0};
		} else {
			p.z += step.z;
			t = tMax.z;
			tMax.z += tDelta.z;
			normal = {0, 0, -step.z};
		}
	}
	return std::nullopt;
}

WorldStats World::Stats() const {
	WorldStats s;
	s.loadedChunks = m_chunks.Size();
	for (const auto &e : m_chunks) {
		s.memoryBytes += e.value->MemoryBytes();
		s.materializedChunks += !e.value->IsUniform();
	}
	s.pendingLoads = static_cast<size_t>(m_pendingLoads.load());
	s.pendingMeshes = static_cast<size_t>(m_pendingMeshes.load());
	s.entities = m_ecs.EntityCount();
	s.gridMoves = m_lastGridMoves;
	s.chunksLoadedFromDisk = m_fromDisk;
	s.chunksGenerated = m_generated;
	s.chunksSaved = m_saved;
	s.chunksUnloaded = m_unloadedCount;
	return s;
}

} // namespace voxel
