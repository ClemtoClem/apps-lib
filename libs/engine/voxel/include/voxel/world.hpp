#pragma once
/**
 * @file world.hpp
 * @brief Monde voxel : table de hachage spatiale des chunks actifs, chargement et
 *        sauvegarde asynchrones sur le module `sql`, grille spatiale d'entités par
 *        chunk, maillage en tâche de fond, physique AABB et lancer de rayon.
 *
 * Flux de chargement / sauvegarde
 * 1. Le joueur avance : `Update(focus)` calcule les chunks à afficher (cylindre
 *    de rayon `viewDistance`, triés par distance) et ceux à décharger.
 * 2. Requête DB : un thread du pool calcule la clé de Morton et interroge le
 *    `RegionStore` (module sql). Absent → génération procédurale.
 * 3. Désérialisation : les voxels remplissent le tableau plat 1D ; les entités
 *    sauvegardées sont décodées en `EntityRecord` puis, sur le thread principal,
 *    réinstanciées en composants ECS.
 * 4. Mise en cache : le chunk est inséré dans la `core::RobinMap` active (clé de Morton).
 * 5. Déchargement : les chunks hors du champ de vision sont sérialisés (voxels +
 *    entités), envoyés à la base de manière asynchrone puis retirés de la table.
 *
 * Grille spatiale d'entités
 * - Chaque chunk maintient la liste des entités qu'il contient ; le composant
 *   `GridCell` mémorise le chunk et le rang → retrait O(1) par swap-remove.
 * - `UpdateEntityGrid` détecte en parallèle les entités qui ont franchi une
 *   frontière de chunk, puis les désinscrit de A et les inscrit dans B.
 * - `ForEachEntityNear(pos, n)` interroge le chunk courant et ses voisins dans un
 *   rayon de `n` chunks (configurable) : chaque voisin est une recherche O(1).
 */
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <core/robin_map.hpp>
#include <ecs/ecs.hpp>
#include <jobs/jobs.hpp>
#include <math/math.hpp>

#include "blocks.hpp"
#include "chunk.hpp"
#include "entities.hpp"
#include "mesher.hpp"
#include "mods.hpp"
#include "storage.hpp"
#include "terrain.hpp"

namespace voxel {

struct WorldConfig {
	std::filesystem::path directory = "data/voxelia/worlds/default";
	uint64_t seed = 1337;
	TerrainSettings terrain;	// lu dans la base si le monde existe déjà
	StructureLookup structures; // structures des règles de génération (mods)
	int viewDistance = 8;		 // rayon horizontal de chargement (chunks)
	int verticalDistance = 4;	 // rayon vertical (chunks)
	int simulationDistance = 4;	 // rayon de simulation des entités (chunks)
	int entityQueryRadius = 1;	 // rayon par défaut des recherches de voisinage (chunks)
	int maxPendingLoads = 48;	 // requêtes DB/génération simultanées
	int maxPendingMeshes = 24;	 // maillages simultanés
	int maxIntegratePerFrame = 32;
	int maxUnloadPerFrame = 32;
};

struct RayHit {
	IVec3 block;
	IVec3 normal; // face touchée
	BlockId id = kAir;
	float distance = 0.f;
};

struct MeshResult {
	ChunkKey key;
	IVec3 coord;
	uint32_t version;
	MeshData mesh;
};

struct WorldStats {
	size_t loadedChunks = 0;
	size_t materializedChunks = 0;
	size_t pendingLoads = 0;
	size_t pendingMeshes = 0;
	size_t entities = 0;
	size_t gridMoves = 0;
	uint64_t chunksLoadedFromDisk = 0;
	uint64_t chunksGenerated = 0;
	uint64_t chunksSaved = 0;
	uint64_t chunksUnloaded = 0;
	size_t memoryBytes = 0;
};

class World {
public:
	World(BlockRegistry &blocks, ecs::World &ecs, jobs::ThreadPool &pool, WorldConfig config);
	~World();
	World(const World &) = delete;
	World &operator=(const World &) = delete;

	/// Ouvre la base du monde ; lit/écrit la graine. Appeler `SetMeshContext` ensuite.
	bool Open();
	/// Sauvegarde tous les chunks chargés modifiés ou peuplés, puis la base (synchrone).
	void SaveAll();
	/// Tables de rendu des blocs (après résolution des textures).
	void SetMeshContext(std::shared_ptr<const MeshContext> ctx) noexcept { m_meshContext = std::move(ctx); }

	/// Streaming : requêtes, intégration des résultats, déchargements, maillages.
	/// `dt` (secondes) pilote la physique des blocs (chute, écoulement) de façon
	/// indépendante de la fréquence d'images.
	void Update(const math::FVector3 &focus, float dt);

	// ── Accès aux chunks et voxels ───────────────────────────────────────
	[[nodiscard]] Chunk *GetChunk(ChunkKey key) noexcept;
	[[nodiscard]] Chunk *GetChunkAt(const IVec3 &coord) noexcept { return GetChunk(MakeChunkKey(coord)); }
	[[nodiscard]] Chunk *ReadyChunk(ChunkKey key) noexcept;
	[[nodiscard]] BlockId GetBlock(int x, int y, int z) const noexcept;
	/// Orientation du bloc (0 = identité) ; 0 si le voxel n'est pas chargé.
	[[nodiscard]] uint8_t GetOrient(int x, int y, int z) const noexcept;
	/// Modifie un voxel (chunk chargé requis) ; remaille le chunk et ses voisins touchés.
	bool SetBlock(int x, int y, int z, BlockId id, uint8_t orient = 0);
	/// Place une structure préfabriquée, origine (ancre) en (x, y, z), orientée par `orient`.
	/// Retourne le nombre de blocs posés.
	size_t PlaceStructure(const Structure &st, int x, int y, int z, uint8_t orient = 0);
	[[nodiscard]] bool IsSolid(int x, int y, int z) const noexcept { return m_blocks.IsSolid(GetBlock(x, y, z)); }
	[[nodiscard]] bool IsLoaded(int x, int y, int z) const noexcept;
	[[nodiscard]] size_t LoadedChunkCount() const noexcept { return m_chunks.Size(); }
	template <typename Fn> void ForEachChunk(Fn &&fn) {
		for (auto &e : m_chunks)
			fn(*e.value);
	}

	// ── Entités et grille spatiale ───────────────────────────────────────
	ecs::Entity SpawnEntity(const EntityRecord &record);
	void DespawnEntity(ecs::Entity e);
	/// Recalcule l'appartenance des entités aux chunks (après le déplacement).
	void UpdateEntityGrid();
	/// Simule les créatures des chunks proches (errance, gravité, collisions) en parallèle.
	void SimulateEntities(const math::FVector3 &focus, float dt);
	/// Simule la chute des blocs (sable, gravier) et l'écoulement des liquides
	/// (viscosité), un budget de cellules par image.
	void TickBlocks(float dt);
	/// Explosion réaliste (type Minecraft) : rayons échantillonnés, atténuation par la
	/// résistance aux explosions des blocs, destruction et dégâts/recul des entités.
	/// Retourne les blocs explosifs atteints (TNT) à enchaîner. `destroyBlocks` = faux
	/// pour ne pas casser de blocs (règle « mobGriefing »).
	std::vector<IVec3> Explode(const math::FVector3 &center, float power, float damage, bool destroyBlocks = true);
	/// Appelle `fn(ecs::Entity)` pour chaque entité des chunks à moins de `radius`
	/// chunks de `pos` (radius < 0 : `entityQueryRadius` de la configuration).
	template <typename Fn> void ForEachEntityNear(const math::FVector3 &pos, int radius, Fn &&fn) {
		if (radius < 0)
			radius = m_config.entityQueryRadius;
		const IVec3 c = ChunkOfPosition(pos);
		for (int dy = -radius; dy <= radius; ++dy)
			for (int dz = -radius; dz <= radius; ++dz)
				for (int dx = -radius; dx <= radius; ++dx)
					if (Chunk *ch = GetChunk(MakeChunkKey(c.x + dx, c.y + dy, c.z + dz)))
						for (ecs::Entity e : ch->entities)
							fn(e);
	}
	/// Entités à moins de `radius` mètres (filtre exact sur la position), triées par distance.
	size_t QueryEntities(const math::FVector3 &pos, float radius, std::vector<ecs::Entity> &out, std::string_view kind = {});

	// ── Physique et rayons ───────────────────────────────────────────────
	/// Déplace une boîte (centre `position`, demi-étendues) en balayage axe par axe.
	void MoveBody(math::FVector3 &position, math::FVector3 &velocity, Body &body, float dt) const;
	[[nodiscard]] std::optional<RayHit> Raycast(const math::FVector3 &origin, const math::FVector3 &dir, float maxDistance) const;
	[[nodiscard]] bool BoxIntersectsSolid(const math::FAABB &box) const;
	/// Vrai si le point `p` est **dans** une boîte de collision d'un bloc solide (et pas
	/// seulement dans sa cellule) : vérifie les AABB de `BlockDef::collision`, ou le cube
	/// plein s'il n'y en a pas. Permet de distinguer « devant une porte » de « dans la porte ».
	[[nodiscard]] bool PointInSolid(const math::FVector3 &p) const;
	/// Collision avec les boîtes des blocs solides (cube plein, ou `BlockDef::collision`) ;
	/// `axis` ≥ 0 : `contact` reçoit le plan de contact le plus proche dans le sens `dir`.
	[[nodiscard]] bool Collide(const math::FAABB &box, int axis, float dir, float &contact) const;

	// ── Résultats pour le rendu ──────────────────────────────────────────
	void DrainMeshes(std::vector<MeshResult> &out);
	void DrainUnloaded(std::vector<ChunkKey> &out);

	// ── Divers ───────────────────────────────────────────────────────────
	[[nodiscard]] WorldStats Stats() const;
	[[nodiscard]] WorldConfig &Config() noexcept { return m_config; }
	[[nodiscard]] WorldStorage &Storage() noexcept { return *m_storage; }
	[[nodiscard]] const TerrainGenerator &Terrain() const noexcept { return *m_terrain; }
	[[nodiscard]] BlockRegistry &Blocks() noexcept { return m_blocks; }
	[[nodiscard]] ecs::World &Ecs() noexcept { return m_ecs; }
	[[nodiscard]] int64_t Tick() const noexcept { return m_tick; }
	/// Altitude du sol en (x, z) (chunks chargés, sinon générateur).
	[[nodiscard]] int SurfaceHeight(int x, int z) const;
	/// Recharge tous les chunks (après un changement de contenu).
	void RemeshAll();

	/// Notifié à chaque modification de bloc (journal, événements de mods).
	std::function<void(const IVec3 &pos, BlockId oldId, BlockId newId)> onBlockChanged;

private:
	struct LoadResult {
		IVec3 coord;
		DecodedChunk data;
		bool fromDisk = false;
	};

	void RequestLoads(const IVec3 &center);
	void IntegrateLoaded();
	void UnloadFar(const IVec3 &center);
	void UnloadChunk(Chunk &chunk);
	void ScheduleMeshes(const IVec3 &center);
	void IntegrateMeshes();
	void MarkDirty(const IVec3 &coord);
	void RegisterEntity(ecs::Entity e, Chunk &chunk);
	void UnregisterEntity(ecs::Entity e);
	[[nodiscard]] bool InRange(const IVec3 &c, const IVec3 &center, int extra) const noexcept;
	/// Programme la simulation de `pos` et de ses voisins (chute / écoulement).
	void ScheduleBlockPhysics(const IVec3 &pos);
	/// Un bloc peut-il être remplacé par un liquide ou un bloc qui tombe ?
	[[nodiscard]] bool Replaceable(BlockId id) const noexcept;
	/// Écoule une cellule liquide : chute puis étalement (un pas), borné par `level`.
	void FlowLiquid(const IVec3 &p, const BlockDef &def, uint8_t level);
	/// Avance les blocs en chute libre (gravité en m/s², une cellule par image au plus).
	void AdvanceFalling(float dt);

	BlockRegistry &m_blocks;
	ecs::World &m_ecs;
	jobs::ThreadPool &m_pool;
	WorldConfig m_config;
	std::unique_ptr<WorldStorage> m_storage;
	std::unique_ptr<TerrainGenerator> m_terrain;
	std::shared_ptr<const MeshContext> m_meshContext;

	core::RobinMap<ChunkKey, std::unique_ptr<Chunk>> m_chunks;
	jobs::Channel<LoadResult> m_loaded;
	jobs::Channel<MeshResult> m_meshed;
	std::vector<LoadResult> m_loadScratch;
	std::vector<MeshResult> m_meshReady;
	std::vector<ChunkKey> m_unloaded;
	std::vector<IVec3> m_wanted;
	IVec3 m_lastCenter{INT32_MIN, INT32_MIN, INT32_MIN};
	std::vector<IVec3> m_blockPhysics;   // file des cellules à simuler (chute / écoulement)
	size_t m_blockPhysicsHead = 0;		 // index de lecture de la file
	core::RobinMap<uint64_t, uint64_t> m_liquidNext; // prochain tick d'écoulement par cellule (viscosité)
	core::RobinMap<uint64_t, uint8_t> m_liquidLevel; // niveau d'étalement restant par cellule
	struct FallState {
		float speed = 0.f;	 // vitesse de chute (m/s), accélérée par la gravité
		float progress = 0.f; // avancement dans la cellule courante (0..1)
	};
	core::RobinMap<uint64_t, FallState> m_falling; // état de chute par cellule (sable, gravier)
	std::vector<IVec3> m_fallingActive;			   // cellules actuellement en chute libre
	std::atomic<int> m_pendingLoads{0};
	std::atomic<int> m_pendingMeshes{0};
	int64_t m_tick = 0;
	size_t m_lastGridMoves = 0;
	uint64_t m_fromDisk = 0, m_generated = 0, m_saved = 0, m_unloadedCount = 0;
	std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);
};

} // namespace voxel
