#pragma once
/**
 * @file bezier.hpp
 * @brief Courbes et carreaux de Bézier (algorithme de de Casteljau) : position et dérivées,
 *        pour tout type de point muni de + et de * float (FVector2, FVector3…).
 *
 *   const FVector3 p = math::Bezier<FVector3>(points, t);              // courbe
 *   const FVector3 d = math::BezierDerivative<FVector3>(points, t);    // tangente
 *   const FVector3 s = math::Bezier2<FVector3, 4, 4>(patch, {u, v});   // carreau patch[v][u]
 *   const auto [du, dv] = math::Bezier2Jacobian<FVector3, 4, 4>(patch, {u, v});
 */
#include <array>
#include <span>
#include <vector>

#include "math.hpp"

namespace math {

/// Point de la courbe de contrôle `p` en `t` (∈ [0, 1]).
template <typename V> [[nodiscard]] V Bezier(std::span<const V> p, float t) {
	if (p.empty())
		return V{};
	std::vector<V> work(p.begin(), p.end());
	for (size_t n = work.size(); n > 1; --n)
		for (size_t i = 0; i + 1 < n; ++i)
			work[i] = work[i] * (1.f - t) + work[i + 1] * t;
	return work.front();
}

/// Dérivée d'ordre `order` (1 : tangente) de la courbe en `t`.
template <typename V> [[nodiscard]] V BezierDerivative(std::span<const V> p, float t, int order = 1) {
	std::vector<V> d(p.begin(), p.end());
	for (int k = 0; k < order; ++k) {
		if (d.size() < 2)
			return V{};
		const float n = float(d.size() - 1); // degré courant
		for (size_t i = 0; i + 1 < d.size(); ++i)
			d[i] = (d[i + 1] - d[i]) * n;
		d.pop_back();
	}
	return Bezier<V>(d, t);
}

/// Point du carreau `p[ligne][colonne]` (D1 lignes selon t.y, D0 colonnes selon t.x).
template <typename V, size_t D0, size_t D1> [[nodiscard]] V Bezier2(const V (&p)[D1][D0], FVector2 t) {
	std::array<V, D1> column;
	for (size_t j = 0; j < D1; ++j)
		column[j] = Bezier<V>(std::span<const V>(p[j], D0), t.x);
	return Bezier<V>(column, t.y);
}

/// Dérivées partielles du carreau : ∂/∂t.x puis ∂/∂t.y (normale = produit vectoriel des deux).
template <typename V, size_t D0, size_t D1> [[nodiscard]] std::array<V, 2> Bezier2Jacobian(const V (&p)[D1][D0], FVector2 t) {
	std::array<V, D1> alongX, pointsX;
	for (size_t j = 0; j < D1; ++j) {
		alongX[j] = BezierDerivative<V>(std::span<const V>(p[j], D0), t.x);
		pointsX[j] = Bezier<V>(std::span<const V>(p[j], D0), t.x);
	}
	return {Bezier<V>(alongX, t.y), BezierDerivative<V>(pointsX, t.y)};
}

} // namespace math
