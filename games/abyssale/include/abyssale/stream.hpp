#pragma once
/**
 * @file stream.hpp
 * @brief Monde océanique en streaming : grille de chunks générée autour du plongeur
 *        sur des threads (`foundation.jobs`), décor et faune peuplés par chunk, et
 *        persistance des entités mobiles sur `foundation.sql` (`sql::RegionStore`).
 *
 * Le flux suit le modèle de `engine.voxel` : `Update(focus)` calcule les chunks
 * souhaités (carré de rayon `kChunkRadius`), demande leur génération au pool,
 * intègre les résultats sur le thread principal (faune ECS, maillage de terrain),
 * puis décharge les chunks lointains (faune sérialisée, terrain libéré).
 */
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <vector>

#include <core/robin_map.hpp>
#include <jobs/jobs.hpp>
#include <sql/region_store.hpp>
#include <sdl3/render/mesh3d.hpp>

#include "abyssale/chunk.hpp"
#include "abyssale/config.hpp"
#include "abyssale/ecosystem.hpp"
#include "abyssale/terrain.hpp"
#include "abyssale/world.hpp"

namespace abyssale {

/// Rayon de chargement des chunks autour du plongeur (terrain, décor et faune).
inline constexpr int kChunkRadius = 1;
/// Facteur appliqué aux compteurs de qualité par chunk (le monde original était un chunk).
inline constexpr float kQualityScale = 0.2f;
/// Nombre de chunks intégrés par image (la génération est amortie sur plusieurs images).
inline constexpr int kMaxIntegratePerFrame = 2;

/// Résultat de la génération d'un chunk (produit sur un thread, intégré sur le principal).
struct ChunkGen {
	ChunkKey key = 0;
	int32_t cx = 0, cz = 0;
	ChunkKind kind = ChunkKind::OpenOcean;
	sdl3::MeshData terrain;			 ///< maillage de rendu du terrain
	std::unique_ptr<World> decor;	 ///< décor du chunk (à peupler dans l'ECS)
};

/// Monde océanique en streaming.
class Ocean {
public:
	Ocean(const Quality &quality, const Shapes &shapes, uint64_t seed, std::filesystem::path dir, jobs::ThreadPool &pool,
		  const math::FVector3 &diverStart, float startTime);
	~Ocean();
	Ocean(const Ocean &) = delete;
	Ocean &operator=(const Ocean &) = delete;

	/// Ouvre le magasin de chunks (crée le dossier). Faux en cas d'échec.
	bool Open();
	/// Avance le streaming : charge les chunks proches, décharge les lointains.
	void Update(const math::FVector3 &focus, float dt);
	/// Sauvegarde la faune de tous les chunks chargés et vide le magasin.
	void SaveAll();

	[[nodiscard]] Ecosystem &Eco() noexcept { return *m_eco; }
	[[nodiscard]] const Terrain &GetTerrain() const noexcept { return m_terrain; }
	[[nodiscard]] const Layout &GetLayout() const noexcept { return m_terrain.GetLayout(); }
	[[nodiscard]] uint64_t Seed() const noexcept { return m_seed; }
	[[nodiscard]] const Quality &GetQuality() const noexcept { return m_quality; }
	[[nodiscard]] size_t LoadedChunkCount() const noexcept { return m_loaded.Size(); }

	/// Maillages de terrain prêts à être téléversés (chunk key → MeshData).
	[[nodiscard]] std::vector<std::pair<ChunkKey, sdl3::MeshData>> DrainMeshes();
	/// Clés des chunks déchargés (leurs maillages GPU sont à libérer).
	[[nodiscard]] std::vector<ChunkKey> DrainUnloaded();

	/// Dernier index du chunk contenant `focus` (pour le rendu des maillages proches).
	[[nodiscard]] ChunkInfo InfoAt(float x, float z) const noexcept { return m_terrain.GetLayout().At(Terrain::ChunkOf(x), Terrain::ChunkOf(z)); }
private:
	struct Loaded {
		int32_t cx, cz;
		ChunkKind kind;
	};
	void RequestLoads(int32_t ccx, int32_t ccz);
	void IntegrateGenerated();
	void UnloadFar(int32_t ccx, int32_t ccz);

	Quality m_quality;
	const Shapes &m_shapes;
	uint64_t m_seed;
	std::filesystem::path m_dir;
	jobs::ThreadPool &m_pool;
	Terrain m_terrain;
	std::unique_ptr<Ecosystem> m_eco;
	sql::RegionStore m_store;

	core::RobinMap<ChunkKey, Loaded> m_loaded;
	jobs::Channel<ChunkGen> m_generated;
	std::deque<ChunkGen> m_deferred; ///< résultats générés en attente d'intégration
	std::vector<ChunkGen> m_genScratch;
	std::vector<std::pair<ChunkKey, sdl3::MeshData>> m_readyMeshes;
	std::vector<ChunkKey> m_unloaded;
	std::atomic<int> m_pending{0};
};

} // namespace abyssale
