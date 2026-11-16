#pragma once
/**
 * @file morton.hpp
 * @brief Clés de Morton (Z-order) 2D/3D pour coordonnées signées.
 *
 * 3D : 21 bits par axe (±1 048 576 chunks), 63 bits utiles. Les coordonnées sont
 * biaisées de 2^20 pour rester positives, donc l'ordre des clés suit l'ordre Z
 * spatial : deux chunks proches ont des clés proches → localité dans les index
 * ordonnés (B+arbre) et les pages de la base de données.
 */
#include <cstdint>

namespace core::morton {

inline constexpr int32_t kBias3 = 1 << 20;
inline constexpr int32_t kBias2 = 1 << 30;

[[nodiscard]] constexpr uint64_t Spread3(uint64_t v) noexcept {
	v &= 0x1fffff;
	v = (v | v << 32) & 0x1f00000000ffffULL;
	v = (v | v << 16) & 0x1f0000ff0000ffULL;
	v = (v | v << 8) & 0x100f00f00f00f00fULL;
	v = (v | v << 4) & 0x10c30c30c30c30c3ULL;
	v = (v | v << 2) & 0x1249249249249249ULL;
	return v;
}
[[nodiscard]] constexpr uint32_t Compact3(uint64_t v) noexcept {
	v &= 0x1249249249249249ULL;
	v = (v ^ (v >> 2)) & 0x10c30c30c30c30c3ULL;
	v = (v ^ (v >> 4)) & 0x100f00f00f00f00fULL;
	v = (v ^ (v >> 8)) & 0x1f0000ff0000ffULL;
	v = (v ^ (v >> 16)) & 0x1f00000000ffffULL;
	v = (v ^ (v >> 32)) & 0x1fffff;
	return static_cast<uint32_t>(v);
}
[[nodiscard]] constexpr uint64_t Spread2(uint64_t v) noexcept {
	v &= 0xffffffffULL;
	v = (v | v << 16) & 0x0000ffff0000ffffULL;
	v = (v | v << 8) & 0x00ff00ff00ff00ffULL;
	v = (v | v << 4) & 0x0f0f0f0f0f0f0f0fULL;
	v = (v | v << 2) & 0x3333333333333333ULL;
	v = (v | v << 1) & 0x5555555555555555ULL;
	return v;
}
[[nodiscard]] constexpr uint32_t Compact2(uint64_t v) noexcept {
	v &= 0x5555555555555555ULL;
	v = (v ^ (v >> 1)) & 0x3333333333333333ULL;
	v = (v ^ (v >> 2)) & 0x0f0f0f0f0f0f0f0fULL;
	v = (v ^ (v >> 4)) & 0x00ff00ff00ff00ffULL;
	v = (v ^ (v >> 8)) & 0x0000ffff0000ffffULL;
	v = (v ^ (v >> 16)) & 0x00000000ffffffffULL;
	return static_cast<uint32_t>(v);
}

/// Encode (x, y, z) signés ∈ [-2^20, 2^20) en clé 63 bits.
[[nodiscard]] constexpr uint64_t Encode3(int32_t x, int32_t y, int32_t z) noexcept {
	return Spread3(static_cast<uint32_t>(x + kBias3)) | (Spread3(static_cast<uint32_t>(y + kBias3)) << 1) |
		   (Spread3(static_cast<uint32_t>(z + kBias3)) << 2);
}
struct Coord3 {
	int32_t x, y, z;
	constexpr bool operator==(const Coord3 &) const = default;
};
[[nodiscard]] constexpr Coord3 Decode3(uint64_t key) noexcept {
	return {static_cast<int32_t>(Compact3(key)) - kBias3, static_cast<int32_t>(Compact3(key >> 1)) - kBias3,
			static_cast<int32_t>(Compact3(key >> 2)) - kBias3};
}

[[nodiscard]] constexpr uint64_t Encode2(int32_t x, int32_t z) noexcept {
	return Spread2(static_cast<uint32_t>(x + kBias2)) | (Spread2(static_cast<uint32_t>(z + kBias2)) << 1);
}
struct Coord2 {
	int32_t x, z;
	constexpr bool operator==(const Coord2 &) const = default;
};
[[nodiscard]] constexpr Coord2 Decode2(uint64_t key) noexcept {
	return {static_cast<int32_t>(Compact2(key)) - kBias2, static_cast<int32_t>(Compact2(key >> 1)) - kBias2};
}

static_assert(Decode3(Encode3(-5, 17, 1000)) == Coord3{-5, 17, 1000});
static_assert(Decode3(Encode3(-(1 << 20), (1 << 20) - 1, 0)) == Coord3{-(1 << 20), (1 << 20) - 1, 0});
static_assert(Decode2(Encode2(-123456, 98765)) == Coord2{-123456, 98765});

} // namespace core::morton
