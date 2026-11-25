#pragma once
/**
 * @file tilemap.hpp
 * @brief Carte de tuiles : grille de terrains avec auto-tuilage. Le motif de chaque cellule
 *        est choisi par compatibilité de bordures avec ses voisins (cf. `autotile.hpp`).
 *        Pur (aucun rendu) — l'application dessine les tuiles et propose l'outil d'édition.
 *
 *   tilemap::Tilemap map(16, 16);
 *   map.AddPattern(1, {1,1,1,1,1,1,1,1,1});         // herbe pleine
 *   map.AddPattern(1, {2,2,2,1,1,1,1,1,1});         // herbe bordée de terre au-dessus
 *   map.AddPattern(2, {2,2,2,2,2,2,2,2,2});         // terre pleine
 *   map.SetTerrain(5, 5, 1); map.SetTerrain(5, 4, 2);
 *   map.Pattern(5, 5); // → herbe bordée de terre (le voisin du haut est de la terre)
 */
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sandbox/document/autotile.hpp"

namespace sandbox::tilemap {

/// Carte de tuiles : grille de terrains (0 = vide), avec motifs résolus à la demande.
class Tilemap {
public:
	Tilemap(int width, int height) : m_w(width), m_h(height), m_grid(size_t(width) * size_t(height), 0) {}

	/// Pose le terrain `terrain` d'une cellule (0 = vide).
	void SetTerrain(int x, int y, int8_t terrain) {
		if (InBounds(x, y))
			m_grid[size_t(y) * size_t(m_w) + size_t(x)] = terrain;
	}
	[[nodiscard]] int8_t Terrain(int x, int y) const noexcept {
		return InBounds(x, y) ? m_grid[size_t(y) * size_t(m_w) + size_t(x)] : 0;
	}
	/// Motif résolu de la cellule (par compatibilité avec ses voisins) ; NONE si vide ou sans motif.
	[[nodiscard]] std::optional<TilePattern> Pattern(int x, int y) const;
	/// Ajoute un motif candidat pour un terrain (le premier ajouté sert de motif « plein »).
	void AddPattern(int8_t terrain, TilePattern pattern) { m_tileset[terrain].push_back(pattern); }
	[[nodiscard]] int Width() const noexcept { return m_w; }
	[[nodiscard]] int Height() const noexcept { return m_h; }

private:
	[[nodiscard]] bool InBounds(int x, int y) const noexcept { return x >= 0 && y >= 0 && x < m_w && y < m_h; }
	[[nodiscard]] const std::vector<TilePattern> *PatternsFor(int8_t terrain) const;

	int m_w, m_h;
	std::vector<int8_t> m_grid;
	std::unordered_map<int8_t, std::vector<TilePattern>> m_tileset;
};

inline const std::vector<TilePattern> *Tilemap::PatternsFor(int8_t terrain) const {
	const auto it = m_tileset.find(terrain);
	return it != m_tileset.end() && !it->second.empty() ? &it->second : nullptr;
}

inline std::optional<TilePattern> Tilemap::Pattern(int x, int y) const {
	if (!InBounds(x, y))
		return std::nullopt;
	const int8_t terrain = Terrain(x, y);
	const std::vector<TilePattern> *candidates = PatternsFor(terrain);
	if (!candidates)
		return std::nullopt;
	// Voisins (gauche, haut, bas, droite) : leur motif « plein » (premier ajouté).
	static constexpr int kDx[4] = {-1, 0, 0, 1}, kDy[4] = {0, -1, 1, 0};
	std::array<const TilePattern *, 4> neighbors{};
	for (size_t i = 0; i < neighbors.size(); ++i) {
		const int nx = x + kDx[i], ny = y + kDy[i];
		if (!InBounds(nx, ny))
			continue;
		const std::vector<TilePattern> *n = PatternsFor(Terrain(nx, ny));
		if (n)
			neighbors[i] = &n->front();
	}
	return Autotile(*candidates, neighbors);
}

} // namespace sandbox::tilemap
