#pragma once
/**
 * @file range.hpp
 * @brief Intervalle numérique possédant ses bornes : normalisation (min ≤ max garantie),
 *        appartenance, saturation et interpolation. Pour un tirage aléatoire, composer avec
 *        `math::Rng` : `range.Sample(rng.NextFloat())`.
 */
#include <cstdint>

#include "math.hpp"

namespace math {

template <typename T>
struct NumericRange {
	T min = 0, max = 0;
	constexpr NumericRange() = default;
	/// Les bornes sont ordonnées (l'ordre fourni est indifférent).
	constexpr NumericRange(T a, T b) : min(a < b ? a : b), max(a < b ? b : a) {}

	/// Sature `v` dans [min, max].
	[[nodiscard]] constexpr T Clamp(T v) const noexcept { return math::Clamp(v, min, max); }
	[[nodiscard]] constexpr bool Contains(T v) const noexcept { return v >= min && v <= max; }
	/// Longueur de l'intervalle (≥ 0).
	[[nodiscard]] constexpr T Size() const noexcept { return max - min; }
	/// Interpolation de `t` ∈ [0, 1] sur l'intervalle (tronquée pour les types entiers).
	[[nodiscard]] constexpr T Sample(float t) const noexcept { return min + T(float(max - min) * t); }
	constexpr bool operator==(const NumericRange &) const = default;
};

using FRange = NumericRange<float>;
using IRange = NumericRange<int32_t>;

} // namespace math
