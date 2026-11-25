#pragma once
/**
 * @file rect.hpp
 * @brief Rectangle axis-aligned 2D générique `Rect<T>` (position `x, y` = coin supérieur
 *        gauche, taille `w, h`) : appartenance, intersection, gonflement, découpage,
 *        déplacement, construction par deux coins. Base des mises en page UI et des
 *        boîtes englobantes 2D (équivalent du `Rect` de Fyrox).
 */
#include <cstdint>

#include "math.hpp"

namespace math {

template <typename T>
struct Rect {
	T x = 0, y = 0, w = 0, h = 0;
	constexpr Rect() = default;
	constexpr Rect(T x_, T y_, T w_, T h_) : x(x_), y(y_), w(w_), h(h_) {}

	[[nodiscard]] constexpr T Left() const noexcept { return x; }
	[[nodiscard]] constexpr T Right() const noexcept { return x + w; }
	[[nodiscard]] constexpr T Top() const noexcept { return y; }
	[[nodiscard]] constexpr T Bottom() const noexcept { return y + h; }
	[[nodiscard]] constexpr T Width() const noexcept { return w; }
	[[nodiscard]] constexpr T Height() const noexcept { return h; }

	/// Le point (px, py) est-il dans le rectangle (bords compris) ?
	[[nodiscard]] constexpr bool Contains(T px, T py) const noexcept { return px >= x && px <= x + w && py >= y && py <= y + h; }
	/// Le rectangle `o` est-il entièrement contenu ?
	[[nodiscard]] constexpr bool Contains(const Rect &o) const noexcept {
		return o.x >= x && o.y >= y && o.Right() <= Right() && o.Bottom() <= Bottom();
	}
	/// Intersection stricte (les bords ne se touchent pas).
	[[nodiscard]] constexpr bool Intersects(const Rect &o) const noexcept {
		return x < o.Right() && Right() > o.x && y < o.Bottom() && Bottom() > o.y;
	}

	/// Élargit de `dx` de chaque côté horizontal et `dy` de chaque côté vertical.
	[[nodiscard]] constexpr Rect Inflate(T dx, T dy) const noexcept { return {x - dx, y - dy, w + dx * 2, h + dy * 2}; }
	/// Translate de (dx, dy).
	[[nodiscard]] constexpr Rect Translate(T dx, T dy) const noexcept { return {x + dx, y + dy, w, h}; }
	[[nodiscard]] constexpr Rect WithPosition(T x_, T y_) const noexcept { return {x_, y_, w, h}; }
	[[nodiscard]] constexpr Rect WithSize(T w_, T h_) const noexcept { return {x, y, w_, h_}; }

	/// Intersection de deux rectangles ; vide (`w == 0 && h == 0`) s'ils ne se recouvrent pas.
	[[nodiscard]] constexpr Rect ClipBy(const Rect &o) const noexcept {
		const T nx = Max(x, o.x), ny = Max(y, o.y);
		const T nr = Min(Right(), o.Right()), nb = Min(Bottom(), o.Bottom());
		if (nr <= nx || nb <= ny)
			return {};
		return {nx, ny, nr - nx, nb - ny};
	}

	/// Rectangle englobant deux coins (l'ordre est indifférent).
	[[nodiscard]] static constexpr Rect FromPoints(T x0, T y0, T x1, T y1) noexcept {
		const T nx = Min(x0, x1), ny = Min(y0, y1);
		return {nx, ny, Max(x0, x1) - nx, Max(y0, y1) - ny};
	}

	constexpr bool operator==(const Rect &) const = default;
};

using FRect = Rect<float>;
using IRect = Rect<int32_t>;

} // namespace math
