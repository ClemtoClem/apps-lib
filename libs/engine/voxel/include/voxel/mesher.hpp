#pragma once
/**
 * @file mesher.hpp
 * @brief Maillage glouton (« greedy meshing ») avec occlusion ambiante par sommet.
 *
 * - Entrée : volume **rembourré** 34³ (le chunk + une couche de chaque voisin),
 *   copié sur le thread principal → le maillage s'exécute ensuite sans verrou
 *   sur le pool de threads.
 * - Faces cachées supprimées, faces coplanaires de même texture et de même AO
 *   fusionnées en un seul quad (typiquement 10 à 30× moins de sommets).
 * - Diagonale du quad choisie selon l'AO (pas d'artefact d'anisotropie).
 * - Deux maillages : opaque/découpé (alpha testé) et translucide (eau, verre).
 * - Sommet compact de 12 octets ; index implicites (quad i → sommets 4i..4i+3).
 * - Formes détaillées (nodebox, maillages .obj/.b3d, connectivité) : troisième maillage
 *   en flottants (`DetailVertex`, 24 octets), mêmes index implicites ; les triangles
 *   sont des quads dégénérés (sommet 3 = sommet 2).
 */
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "blocks.hpp"
#include "types.hpp"

namespace voxel {

inline constexpr int kPadded = kChunkSize + 2; // 34
inline constexpr int kPaddedVolume = kPadded * kPadded * kPadded;
[[nodiscard]] constexpr int PaddedIndex(int x, int y, int z) noexcept { return (x + 1) + (z + 1) * kPadded + (y + 1) * kPadded * kPadded; }

struct ChunkVertex {
	uint8_t x, y, z, face; // position locale (0..32), face (0..5, 6 = végétation en croix)
	uint8_t u, v, ao, light;
	uint16_t layer, pad;
};
static_assert(sizeof(ChunkVertex) == 12);

/// Sommet d'une forme détaillée (position locale au chunk en flottants, UV en unités bloc).
struct DetailVertex {
	float x, y, z, u, v;
	uint16_t layer;
	uint16_t faceLight; // face (0..6) | lumière émise << 8
};
static_assert(sizeof(DetailVertex) == 24);

struct MeshData {
	std::vector<ChunkVertex> opaque;
	std::vector<ChunkVertex> translucent;
	std::vector<DetailVertex> detail; // formes nodebox / maillages (alpha testé)
	[[nodiscard]] bool Empty() const noexcept { return opaque.empty() && translucent.empty() && detail.empty(); }
};

/// Tables immuables dérivées du registre (partagées par les threads de maillage).
struct MeshContext {
	std::vector<uint8_t> opaque, visible, cross, light, detail; // detail : forme nodebox / maillage
	std::vector<Material> material;
	std::vector<std::array<uint16_t, 6>> layers;
	std::vector<uint8_t> reflectivity; // réflectance 0..255 (métaux, verre, glace)
	// Formes détaillées (vides pour les cubes) et connectivité précalculée.
	std::vector<std::vector<BlockBox>> boxes;
	std::vector<std::vector<BlockTriangle>> meshes;
	std::vector<ConnectRule> connect;
	std::vector<std::vector<uint8_t>> connectsTo; // [bloc][voisin] (vide : aucune règle)
	[[nodiscard]] static std::shared_ptr<const MeshContext> Build(const BlockRegistry &registry);
};

/// Maille un volume rembourré (kPaddedVolume voxels, index `PaddedIndex`), orienté par
/// `orient` (kPaddedVolume, 0 = identité ; vide = aucune orientation).
void BuildChunkMesh(std::span<const BlockId> padded, std::span<const uint8_t> orient, const MeshContext &ctx, MeshData &out);

/// Boîtes de la forme d'un bloc compte tenu de ses voisins (`neighbors` : un bloc par face,
/// ordre `Face`) — base + boîtes des faces connectées.
void ShapeBoxes(const MeshContext &ctx, BlockId block, const std::array<BlockId, 6> &neighbors, std::vector<BlockBox> &out);

} // namespace voxel
