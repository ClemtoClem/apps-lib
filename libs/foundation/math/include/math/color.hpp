#pragma once
/**
 * @file color.hpp
 * @brief Couleur RGBA 8 bits, conversions HSV/HSL, palette nommée, analyse de texte
 *        (« #rrggbb », noms), conversion sRGB ↔ linéaire et dégradé de couleurs multi-points.
 *
 *   const math::Color c = math::Color::FromName("tomato").value_or(math::Color{});
 *   const math::Hsv   h = math::ColorToHsv(c);
 *   math::ColorGradient g; g.AddPoint({0.f, {255, 0, 0}}); g.AddPoint({1.f, {0, 0, 255}});
 *   const math::Color mid = g.Sample(0.5f);
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "math.hpp"

namespace math {

/// Couleur RGBA 8 bits (ordre mémoire r, g, b, a). La conversion vers `uint32_t` donne
/// 0xAABBGGRR (octet faible = rouge), l'ordre attendu par SDL_GPU/OpenGL.
struct Color {
	uint8_t r = 255, g = 255, b = 255, a = 255;
	constexpr Color() = default;
	constexpr Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}
	/// Depuis un entier 0xAABBGGRR.
	constexpr explicit Color(uint32_t abgr)
		: r(uint8_t(abgr & 0xFFu)), g(uint8_t((abgr >> 8) & 0xFFu)), b(uint8_t((abgr >> 16) & 0xFFu)), a(uint8_t((abgr >> 24) & 0xFFu)) {}

	constexpr operator uint32_t() const noexcept { return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r); }

	[[nodiscard]] constexpr Color WithAlpha(uint8_t a_) const noexcept { return {r, g, b, a_}; }
	[[nodiscard]] constexpr Color Opaque() const noexcept { return {r, g, b, uint8_t(255)}; }
	[[nodiscard]] Color Lerp(Color o, float t) const noexcept {
		const auto chan = [](uint8_t x, uint8_t y, float k) {
			return uint8_t(float(x) + (float(y) - float(x)) * k + 0.5f);
		};
		return {chan(r, o.r, t), chan(g, o.g, t), chan(b, o.b, t), chan(a, o.a, t)};
	}
	[[nodiscard]] constexpr bool operator==(const Color &) const = default;

	/// Nom CSS le plus proche (recherche insensible à la casse) ; std::nullopt si inconnu.
	[[nodiscard]] static std::optional<Color> FromName(std::string_view name);
	/// Analyse « #rrggbb », « #rrggbbaa » ou « rrggbb » (le « # » est facultatif).
	[[nodiscard]] static std::optional<Color> FromHex(std::string_view hex);
};

/// Couleur HSV : teinte ∈ [0, 360), saturation et brillance ∈ [0, 100] (pourcentages).
struct Hsv {
	float hue = 0.f, saturation = 0.f, brightness = 0.f;
	constexpr bool operator==(const Hsv &) const = default;
};

/// Couleur HSL : teinte ∈ [0, 360), saturation et luminosité ∈ [0, 1].
struct Hsl {
	float hue = 0.f, saturation = 0.f, lightness = 0.f;
	constexpr bool operator==(const Hsl &) const = default;
};

[[nodiscard]] Hsv ColorToHsv(Color c) noexcept;
[[nodiscard]] Color HsvToColor(Hsv hsv) noexcept;
[[nodiscard]] Hsl ColorToHsl(Color c) noexcept;
[[nodiscard]] Color HslToColor(Hsl hsl) noexcept;

/// Composante sRGB → linéaire (approximation par puissance 2.2, comme Fyrox).
[[nodiscard]] inline float SrgbToLinear(float c) noexcept { return std::pow(c, 2.2f); }
/// Composante linéaire → sRGB (approximation par puissance 1/2.2).
[[nodiscard]] inline float LinearToSrgb(float c) noexcept { return std::pow(c, 1.0f / 2.2f); }
[[nodiscard]] inline Color SrgbToLinear(Color c) noexcept {
	const auto ch = [](uint8_t v) { return uint8_t(std::pow(float(v) / 255.f, 2.2f) * 255.f + 0.5f); };
	return {ch(c.r), ch(c.g), ch(c.b), c.a};
}
[[nodiscard]] inline Color LinearToSrgb(Color c) noexcept {
	const auto ch = [](uint8_t v) { return uint8_t(std::pow(float(v) / 255.f, 1.0f / 2.2f) * 255.f + 0.5f); };
	return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

/// Point d'un dégradé : couleur à la position normalisée `location`.
struct GradientPoint {
	float location = 0.f;
	Color color{};
};

/// Dégradé linéaire multi-points. Les points sont maintenus triés par `location` ;
/// l'échantillonnage est linéaire entre deux points et **borné** aux extrémités.
struct ColorGradient {
	std::vector<GradientPoint> points;

	/// Ajoute un point (inséré en ordre croissant de `location`).
	void AddPoint(GradientPoint p) { points.insert(std::ranges::lower_bound(points, p.location, {}, &GradientPoint::location), p); }
	/// Couleur en `location` (points bornés ; vide → blanc, un seul point → sa couleur).
	[[nodiscard]] Color Sample(float location) const noexcept;
	[[nodiscard]] bool Empty() const noexcept { return points.empty(); }
	void Clear() noexcept { points.clear(); }
};

} // namespace math
