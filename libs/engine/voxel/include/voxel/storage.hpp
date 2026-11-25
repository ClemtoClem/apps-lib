#pragma once
/**
 * @file storage.hpp
 * @brief Persistance d'un monde sur le module `sql`.
 *
 * - **Blobs de chunks** : `sql::RegionStore` (fichiers de régions partitionnés par
 *   clé de Morton, journal en ajout seul, accès asynchrone).
 *   Blob = version, voxels RLE (ou valeur uniforme), entités sérialisées.
 * - **Base relationnelle** (`world.vqdb`, instantané CRC) :
 *   - `meta(key TEXT PK, value TEXT)` : graine, position du joueur, heure… ;
 *   - `chunks(key INT PK, zkey INT INDEX, cx, cy, cz, entities INT, tick INT)` :
 *     chunks sauvegardés ; `zkey` (= clé de Morton) en B+arbre pour les
 *     recherches par boîte ;
 *   - `entities(id INT PK, chunk INT INDEX, kind TEXT, x, y, z REAL)` : index
 *     spatial des entités des chunks **déchargés** (recherche sans charger) ;
 *   - `config(key TEXT PK, value TEXT, mod TEXT)` : configuration fusionnée des mods ;
 *   - `events(id INT PK, tick INT INDEX, chunk INT INDEX, type TEXT, mod TEXT, data TEXT)` :
 *     journal des événements du jeu et des mods ;
 *   - `mods(id TEXT PK, name TEXT, version TEXT, enabled INT, priority INT)`.
 */
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jobs/jobs.hpp>
#include <sql/database.hpp>
#include <sql/region_store.hpp>

#include "entities.hpp"
#include "types.hpp"

namespace voxel {

struct DecodedChunk {
	std::unique_ptr<BlockId[]> voxels; // nul si uniforme
	std::unique_ptr<uint8_t[]> orients; // orientations par voxel (nul si toutes nulles)
	BlockId fill = kAir;
	std::vector<EntityRecord> entities;
};

struct EntityHit {
	int64_t id;
	ChunkKey chunk;
	std::string kind;
	math::FVector3 position;
};

class WorldStorage {
public:
	WorldStorage(std::filesystem::path directory, jobs::ThreadPool &pool);
	~WorldStorage();

	/// Ouvre (ou crée) la base et les régions. Retourne faux si l'instantané est corrompu.
	bool Open();
	/// Attend les écritures en cours et écrit l'instantané de la base.
	bool Flush();

	[[nodiscard]] sql::Database &Db() noexcept { return m_db; }
	[[nodiscard]] sql::RegionStore &Regions() noexcept { return *m_regions; }
	[[nodiscard]] const std::filesystem::path &Directory() const noexcept { return m_dir; }

	// ── Blobs (thread-safe, sans état) ───────────────────────────────────
	[[nodiscard]] static std::vector<uint8_t> EncodeChunk(const BlockId *voxels, BlockId fill, const uint8_t *orients,
														 std::span<const EntityRecord> entities);
	[[nodiscard]] static bool DecodeChunk(std::span<const uint8_t> blob, DecodedChunk &out);

	/// Sauvegarde asynchrone d'un chunk + mise à jour des tables `chunks` et `entities`
	/// (thread principal).
	void SaveChunk(const IVec3 &coord, std::vector<uint8_t> blob, std::span<const EntityRecord> entities, int64_t tick);

	// ── Recherches ───────────────────────────────────────────────────────
	/// Chunks sauvegardés dont les coordonnées sont dans la boîte (incluse).
	[[nodiscard]] std::vector<ChunkKey> SavedChunksInBox(const IVec3 &min, const IVec3 &max);
	/// Entités des chunks déchargés dans la boîte de chunks, filtrées par type (vide = tous).
	[[nodiscard]] std::vector<EntityHit> FindStoredEntities(const IVec3 &minChunk, const IVec3 &maxChunk, std::string_view kind = {});

	// ── Méta, configuration, événements ──────────────────────────────────
	void SetMeta(std::string_view key, std::string_view value);
	[[nodiscard]] std::string GetMeta(std::string_view key, std::string_view fallback = {});
	void SetConfig(std::string_view key, std::string_view value, std::string_view mod);
	[[nodiscard]] std::optional<std::string> GetConfig(std::string_view key);
	void LogEvent(int64_t tick, ChunkKey chunk, std::string_view type, std::string_view mod, std::string_view data);
	/// Ne garde que les `keep` événements les plus récents.
	void TrimEvents(size_t keep);

private:
	std::filesystem::path m_dir;
	jobs::ThreadPool &m_pool;
	sql::Database m_db;
	std::unique_ptr<sql::RegionStore> m_regions;
	sql::PreparedStatement m_upsertChunk, m_deleteEntities, m_insertEntity, m_insertEvent, m_setMeta, m_getMeta, m_setConfig,
		m_getConfig, m_chunksInRange, m_entitiesInChunk;
	int64_t m_nextEntityId = 1;
	int64_t m_nextEventId = 1;
};

} // namespace voxel
