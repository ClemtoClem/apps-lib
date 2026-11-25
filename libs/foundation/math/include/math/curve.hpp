#pragma once
/**
 * @file curve.hpp
 * @brief Courbe d'animation à clés 1D : clés triées par position, interpolation
 *        Constante (créneau) / Linéaire / Cubique (Hermite). Échantillonnage à cache de
 *        segment pour les lectures consécutives (comme le `Curve` de Fyrox).
 *
 *   math::Curve c;
 *   c.AddKey({0.f, 0.f, math::CurveKeyKind::Cubic, 0.f, 2.f});   // tangente droite 2
 *   c.AddKey({1.f, 1.f, math::CurveKeyKind::Cubic, 2.f, 0.f});
 *   size_t hint = 0;
 *   const float v = c.ValueAt(0.5f, hint);   // interpolation cubique de Hermite
 */
#include <algorithm>
#include <cstdint>
#include <vector>

namespace math {

/// Mode d'interpolation d'une clé (déterminé par la clé de GAUCHE de l'intervalle).
enum class CurveKeyKind : uint8_t {
	Constant, ///< créneau : conserve la valeur de gauche
	Linear,	  ///< interpolation linéaire
	Cubic,	  ///< Hermite : tangentes gauche/droite (pentes, dérivée valeur/location)
};

/// Clé d'une courbe : valeur à la position `location` (croissante dans la courbe).
struct CurveKey {
	float location = 0.f;
	float value = 0.f;
	CurveKeyKind kind = CurveKeyKind::Linear;
	float leftTangent = 0.f;  ///< pente entrante (Cubic uniquement)
	float rightTangent = 0.f; ///< pente sortante (Cubic uniquement)
};

/// Courbe 1D. Les clés restent triées par `location` ; l'échantillonnage hors bornes
/// est saturé aux valeurs extrêmes.
struct Curve {
	std::vector<CurveKey> keys;

	/// Ajoute une clé (insérée en ordre croissant de `location`).
	void AddKey(CurveKey key) { keys.insert(std::ranges::lower_bound(keys, key.location, {}, &CurveKey::location), key); }

	/// Valeur en `location`, `hint` servant de cache (index du segment gauche, mis à jour).
	[[nodiscard]] float ValueAt(float location, size_t &hint) const noexcept {
		if (keys.empty())
			return 0.f;
		if (keys.size() == 1)
			return keys.front().value;
		if (location <= keys.front().location)
			return keys.front().value;
		if (location >= keys.back().location)
			return keys.back().value;
		size_t left = Span(location, hint);
		hint = left;
		return Interpolate(keys[left], keys[left + 1], location);
	}

	/// Valeur en `location` (sans cache).
	[[nodiscard]] float ValueAt(float location) const noexcept {
		size_t hint = 0;
		return ValueAt(location, hint);
	}

	[[nodiscard]] bool Empty() const noexcept { return keys.empty(); }
	void Clear() noexcept { keys.clear(); }

private:
	// Index du segment [left, left+1] contenant `location` (bornes internes garanties).
	[[nodiscard]] size_t Span(float location, size_t hint) const noexcept {
		const size_t n = keys.size();
		if (hint + 1 < n && keys[hint].location <= location && location <= keys[hint + 1].location)
			return hint;
		if (hint > 0 && hint < n && keys[hint - 1].location <= location && location <= keys[hint].location)
			return hint - 1;
		const auto it = std::ranges::lower_bound(keys, location, {}, &CurveKey::location);
		const size_t right = size_t(it - keys.begin());
		return right == 0 ? 0 : (right >= n ? n - 2 : right - 1);
	}

	[[nodiscard]] static float Interpolate(const CurveKey &a, const CurveKey &b, float location) noexcept {
		const float span = b.location - a.location;
		const float t = span > 0.f ? (location - a.location) / span : 0.f;
		switch (a.kind) {
			case CurveKeyKind::Constant: return a.value;
			case CurveKeyKind::Linear: return a.value + (b.value - a.value) * t;
			case CurveKeyKind::Cubic: {
				const float rb = b.kind == CurveKeyKind::Cubic ? b.leftTangent : 0.f;
				const float t2 = t * t, t3 = t2 * t;
				const float h00 = 2.f * t3 - 3.f * t2 + 1.f;
				const float h10 = t3 - 2.f * t2 + t;
				const float h01 = -2.f * t3 + 3.f * t2;
				const float h11 = t3 - t2;
				return h00 * a.value + h10 * span * a.rightTangent + h01 * b.value + h11 * span * rb;
			}
		}
		return a.value;
	}
};

} // namespace math
