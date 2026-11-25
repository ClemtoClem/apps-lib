#pragma once
/**
 * @file terrain.hpp
 * @brief Relief du fond océanique : continu à travers les chunks, tuilé sans couture.
 *
 * Le terrain n'est plus un monde fermé : c'est une fonction analytique et déterministe
 * de (x, z) et de la graine. Le fond de base (plateau rocheux, dunes) est une fonction
 * globale continue ; chaque chunk y creuse ses cuvettes (lieux remarquables) qui
 * s'estompent à ses frontières, et les arêtes fermées du labyrinthe (cf. chunk.hpp)
 * font monter des falaises jusqu'à la surface, percées d'encoches pour les tunnels.
 *
 * Pour rester rapide, chaque chunk échantillonne cette fonction dans une grille mise en
 * cache (altitude, ouverture, couverture) : les requêtes `Height`/`Openness`/`Cover`
 * deviennent des interpolations bilinéaires O(1) (comme le monde d'origine).
 */
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

#include <math/math.hpp>
#include <math/noise.hpp>
#include <sdl3/render/mesh3d.hpp>

#include "abyssale/chunk.hpp"
#include "abyssale/config.hpp"

namespace abyssale {

/// Lieu remarquable (centre local au chunk, rayon de la cuvette).
struct Landmark {
	math::FVector2 center;
	float radius;
};

/// Lieux de la carte d'entrée (repère local, m).
namespace landmarks {
inline constexpr Landmark kKelp{{0.f, 0.f}, 34.f};
inline constexpr Landmark kGrotto{{60.f, -46.f}, 20.f};
inline constexpr Landmark kWreck{{-60.f, 40.f}, 24.f};
inline constexpr Landmark kSponges{{50.f, 52.f}, 20.f};
inline constexpr Landmark kFluke{{-52.f, -48.f}, 22.f};
inline constexpr Landmark kMeadow{{-4.f, 64.f}, 20.f};
inline constexpr std::array<Landmark, 6> kAll{kKelp, kGrotto, kWreck, kSponges, kFluke, kMeadow};
} // namespace landmarks

/// Lieux (repère local au chunk) d'un template de carte.
[[nodiscard]] std::span<const Landmark> LandmarksFor(ChunkKind kind);

class Terrain {
public:
	static constexpr int kGrid = 257; ///< sommets par côté du maillage de rendu (1,17 m)
	static constexpr int kCollisionGrid = 129; ///< sommets par côté de la grille de collision (2,34 m)

	/// Construit le relief et la disposition du monde à partir de la graine.
	explicit Terrain(uint64_t seed = 20261121);
	Terrain(uint64_t seed, const Layout &layout);

	/// Altitude du fond (falaise comprise).
	[[nodiscard]] float Height(float x, float z) const noexcept;
	/// Altitude du fond hors falaises (plateau + cuvettes).
	[[nodiscard]] float FloorHeight(float x, float z) const noexcept;
	/// Normale du fond (différences centrales).
	[[nodiscard]] math::FVector3 Normal(float x, float z) const noexcept;
	/// 0 : plateau ou falaise, 1 : fond de canyon (pour la végétation et les placements).
	[[nodiscard]] float Openness(float x, float z) const noexcept;
	/// Couverture du tapis végétal magenta (0 : sable nu, 1 : tapis dense).
	[[nodiscard]] float Cover(float x, float z) const noexcept;
	/// Pente : 0 (plat) à 1 (vertical).
	[[nodiscard]] float Slope(float x, float z) const noexcept { return 1.f - Normal(x, z).y; }

	/// Maillage de rendu du chunk (cx, cz) (repère du monde).
	[[nodiscard]] sdl3::MeshData BuildChunkMesh(int32_t cx, int32_t cz) const;
	/// Triangles de collision (grossiers) du chunk, repère du monde.
	[[nodiscard]] std::vector<math::FVector3> BuildChunkCollision(int32_t cx, int32_t cz) const;

	/// Libère la grille en cache du chunk (appelé au déchargement du chunk).
	void EvictChunk(int32_t cx, int32_t cz);

	/// Origine (coin sud-ouest) d'un chunk en coordonnées monde.
	[[nodiscard]] static float ChunkOrigin(int32_t c) noexcept { return float(c) * kChunkSize - kHalfExtent; }
	/// Index du chunk contenant `x` (ou `z`).
	[[nodiscard]] static int32_t ChunkOf(float x) noexcept;

	[[nodiscard]] static float Step() noexcept;

	[[nodiscard]] uint64_t Seed() const noexcept { return m_seed; }
	[[nodiscard]] const Layout &GetLayout() const noexcept { return m_layout; }

private:
	/// Grille échantillonnée d'un chunk (altitude, ouverture, couverture).
	struct ChunkGrid {
		std::vector<float> height, open, cover;
	};
	/// Ouverture locale des cuvettes (0..1), bornée aux frontières du chunk.
	[[nodiscard]] float CanyonOpenness(float x, float z) const noexcept;
	[[nodiscard]] float FloorHeightAt(float x, float z, float open) const noexcept;
	[[nodiscard]] float HeightWithWalls(float x, float z, float floor) const noexcept;
	[[nodiscard]] float CoverAt(float x, float z, float open) const noexcept;
	[[nodiscard]] const ChunkGrid &EnsureChunk(int32_t cx, int32_t cz) const;
	[[nodiscard]] float Sample(const std::vector<float> &grid, float x, float z) const noexcept;

	uint64_t m_seed;
	Layout m_layout;
	math::SimplexNoise m_noise;
	mutable std::mutex m_cacheMutex;
	mutable std::map<ChunkKey, std::shared_ptr<ChunkGrid>> m_cache;
};

} // namespace abyssale
