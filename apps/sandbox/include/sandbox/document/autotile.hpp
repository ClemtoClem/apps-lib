#pragma once
/**
 * @file autotile.hpp
 * @brief Autotile : choisit, pour une tuile d'un terrain donné, le premier motif dont les
 *        bordures coïncident avec celles de ses voisins déjà posés. Algorithme pur (aucun
 *        rendu), porté de `fyrox-autotile` (motif `PatternBits` 3×3, bits de bordure).
 *
 *   // Herbe (1), terre (2) et herbe bordée de terre au-dessus.
 *   const tilemap::TilePattern grass{1,1,1,1,1,1,1,1,1};
 *   const tilemap::TilePattern grassDirtTop{2,2,2,1,1,1,1,1,1};
 *   const std::array<const TilePattern*, 4> n = {&grass, &dirt, &grass, &grass}; // g,h,b,d
 *   const auto p = tilemap::Autotile({&grass, &grassDirtTop}, n); // → grassDirtTop
 */
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace sandbox::tilemap {

/// Motif d'une tuile : masque 3×3 (ligne par ligne), le centre `bits[4]` = terrain.
struct TilePattern {
	std::array<int8_t, 9> bits{};
	constexpr int8_t Terrain() const noexcept { return bits[4]; }
	constexpr bool operator==(const TilePattern &) const = default;

	/// Construit depuis 9 valeurs (ligne par ligne : haut, milieu, bas).
	constexpr TilePattern(int8_t a, int8_t b, int8_t c, int8_t d, int8_t e, int8_t f, int8_t g, int8_t h, int8_t i)
		: bits{a, b, c, d, e, f, g, h, i} {}
	constexpr TilePattern() = default;
};

/// Côtés cardinaux : 0 gauche, 1 haut, 2 bas, 3 droite — indices de leurs 3 bits de bordure.
inline constexpr std::array<std::array<size_t, 3>, 4> kEdgeBits = {{{0, 3, 6}, {0, 1, 2}, {6, 7, 8}, {2, 5, 8}}};

/// Les 3 bits de la bordure `side` du motif.
constexpr std::array<int8_t, 3> Edge(const TilePattern &p, size_t side) noexcept {
	return {p.bits[kEdgeBits[side][0]], p.bits[kEdgeBits[side][1]], p.bits[kEdgeBits[side][2]]};
}

/// Côté opposé (gauche ↔ droite, haut ↔ bas).
constexpr size_t OppositeSide(size_t side) noexcept { return side ^ 3u; }

/// Deux motifs adjacents (bordure `side` de `a` contre `b`) sont-ils compatibles ?
constexpr bool Compatible(const TilePattern &a, const TilePattern &b, size_t side) noexcept {
	const std::array<int8_t, 3> ea = Edge(a, side), eb = Edge(b, OppositeSide(side));
	return ea[0] == eb[0] && ea[1] == eb[1] && ea[2] == eb[2];
}

/// Choisit le premier motif de `candidates` (par ordre de priorité) compatible avec les
/// voisins posés `neighbors` (gauche, haut, bas, droite ; nullptr = pas de voisin).
/// `candidates` doivent tous être du même terrain (leur centre). NONE si impossible.
[[nodiscard]] inline std::optional<TilePattern> Autotile(std::span<const TilePattern> candidates,
														 std::array<const TilePattern *, 4> neighbors) {
	for (const TilePattern &p : candidates) {
		bool ok = true;
		for (size_t side = 0; side < 4 && ok; ++side)
			if (const TilePattern *n = neighbors[side]; n)
				ok = Compatible(p, *n, side);
		if (ok)
			return p;
	}
	return std::nullopt;
}

} // namespace sandbox::tilemap
