#pragma once
/**
 * @file noise.hpp
 * @brief Bruits procéduraux déterministes : simplex 2D/3D (permutation 512 octets
 *        dérivée d'une graine), fBm et ridged multifractal. Sans état global.
 */
#include <array>
#include <cstdint>

namespace math {

class SimplexNoise {
	std::array<uint8_t, 512> m_perm{};

public:
	explicit SimplexNoise(uint64_t seed = 0) noexcept;
	/// Bruit simplex 2D dans [-1, 1].
	[[nodiscard]] float Noise2(float x, float y) const noexcept;
	/// Bruit simplex 3D dans [-1, 1].
	[[nodiscard]] float Noise3(float x, float y, float z) const noexcept;
	/// Mouvement brownien fractionnaire 2D normalisé dans [-1, 1].
	[[nodiscard]] float Fbm2(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const noexcept;
	[[nodiscard]] float Fbm3(float x, float y, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const noexcept;
	/// Multifractal « ridged » 2D dans [0, 1] (crêtes de montagnes).
	[[nodiscard]] float Ridged2(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const noexcept;
};

/// Générateur pseudo-aléatoire rapide (xoshiro256**), déterministe par graine.
class Rng {
	uint64_t m_s[4];

public:
	explicit Rng(uint64_t seed = 0x9e3779b97f4a7c15ULL) noexcept;
	[[nodiscard]] uint64_t Next() noexcept;
	[[nodiscard]] float NextFloat() noexcept { return static_cast<float>(Next() >> 40) * (1.0f / 16777216.0f); }
	[[nodiscard]] int NextInt(int lo, int hi) noexcept {
		return lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1));
	}
};

/// Hachage entier 3D → [0, 1) (placement déterministe d'arbres, minerais…).
[[nodiscard]] constexpr float Hash01(int32_t x, int32_t y, int32_t z, uint64_t seed) noexcept {
	uint64_t h = seed ^ (static_cast<uint64_t>(static_cast<uint32_t>(x)) * 0x9E3779B185EBCA87ULL);
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(y)) * 0xC2B2AE3D27D4EB4FULL;
	h ^= static_cast<uint64_t>(static_cast<uint32_t>(z)) * 0x165667B19E3779F9ULL;
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);
}

} // namespace math
