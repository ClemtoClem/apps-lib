#pragma once
/**
 * @file types.hpp
 * @brief Constantes et types de base du monde voxel.
 *
 * - Chunk cubique de 32³ voxels stocké à plat (1D) : index = x + z·32 + y·32²
 *   (les colonnes verticales sont contiguës par couches, idéal pour la génération).
 * - Coordonnées de chunk 3D signées ; clé de chunk = clé de Morton 3D (`ChunkKey`).
 */
#include <cstdint>

#include <core/morton.hpp>
#include <math/math.hpp>

namespace voxel {

inline constexpr int kChunkShift = 5;
inline constexpr int kChunkSize = 1 << kChunkShift; // 32
inline constexpr int kChunkMask = kChunkSize - 1;
inline constexpr int kChunkArea = kChunkSize * kChunkSize;
inline constexpr int kChunkVolume = kChunkArea * kChunkSize; // 32768

/// Plage verticale du monde, en chunks : [kMinChunkY, kMaxChunkY].
/// Le fond descend jusqu'au « monde miroir » : après le noyau terrestre et l'inversion
/// de gravité, la géologie remonte à l'envers vers une surface inversée (~ −4 740 000 m).
/// −148 134 chunks ≈ −4 740 288 m, dans la plage ±1 048 576 chunks des clés de Morton 21 bits.
inline constexpr int kMinChunkY = -148'134;
inline constexpr int kMaxChunkY = 7;
inline constexpr int kWorldMinY = kMinChunkY * kChunkSize; // -4 740 288
inline constexpr int kWorldMaxY = (kMaxChunkY + 1) * kChunkSize - 1; // 255

using BlockId = uint16_t;
inline constexpr BlockId kAir = 0;

using ChunkKey = uint64_t;
using math::IVec3;

[[nodiscard]] constexpr int VoxelIndex(int x, int y, int z) noexcept { return x + (z << kChunkShift) + (y << (2 * kChunkShift)); }
[[nodiscard]] constexpr ChunkKey MakeChunkKey(int cx, int cy, int cz) noexcept { return core::morton::Encode3(cx, cy, cz); }
[[nodiscard]] constexpr ChunkKey MakeChunkKey(const IVec3 &c) noexcept { return core::morton::Encode3(c.x, c.y, c.z); }
[[nodiscard]] constexpr IVec3 ChunkCoordOfKey(ChunkKey k) noexcept {
	const auto c = core::morton::Decode3(k);
	return {c.x, c.y, c.z};
}
/// Chunk contenant le voxel monde (x, y, z) — décalage arithmétique = division plancher.
[[nodiscard]] constexpr IVec3 ChunkOfVoxel(int x, int y, int z) noexcept { return {x >> kChunkShift, y >> kChunkShift, z >> kChunkShift}; }
[[nodiscard]] inline IVec3 ChunkOfPosition(const math::FVector3 &p) noexcept {
	return ChunkOfVoxel(math::FastFloor(p.x), math::FastFloor(p.y), math::FastFloor(p.z));
}
[[nodiscard]] inline ChunkKey ChunkKeyOfPosition(const math::FVector3 &p) noexcept { return MakeChunkKey(ChunkOfPosition(p)); }

enum class Face : uint8_t { PosX = 0, NegX, PosY, NegY, PosZ, NegZ };
inline constexpr IVec3 kFaceNormals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

} // namespace voxel
