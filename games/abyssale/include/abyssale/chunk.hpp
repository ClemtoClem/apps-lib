#pragma once
/**
 * @file chunk.hpp
 * @brief Découpage du monde en chunks : types de cartes (templates), disposition
 *        labyrinthique générée à partir de la graine, et clés de Morton.
 *
 * Le monde d'Abyssale est une grille infinie de chunks carrés (côté `kChunkSize`).
 * Au centre (0, 0) se trouve la carte d'entrée ; autour, un labyrinthe de chunks
 * (algorithme *recursive backtracker*) attribue à chaque cellule un template
 * (barrière de coraux, épave de paquebot, …) et ferme certaines frontières par des
 * falaises. Certaines frontières fermées sont percées d'un tunnel. Au-delà du
 * labyrinthe s'étend un océan ouvert, généré paresseusement.
 *
 * Chaque arête d'un chunk porte un état (2 bits) :
 * `0` ouverte (on nage librement), `1` falaise (à contourner), `2` falaise percée
 * d'un tunnel (à traverser).
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include <core/morton.hpp>

namespace abyssale {

/// Clé d'un chunk : coordonnées (cx, cz) encodées en ordre Z (Morton 2D).
using ChunkKey = uint64_t;
[[nodiscard]] constexpr ChunkKey MakeChunkKey(int32_t cx, int32_t cz) noexcept { return core::morton::Encode2(cx, cz); }

/// Types de cartes. `Entry` est la carte d'origine (forêt de varech, grotte, épave…) ;
/// `OpenOcean` est l'océan clairsemé qui entoure le labyrinthe.
enum class ChunkKind : uint8_t {
	Entry = 0,
	CoralBarrier,   ///< barrière de coraux
	KelpForest,     ///< forêt de varech géante
	Hydrothermal,   ///< champ de sources hydrothermales
	WhaleGraveyard, ///< cimetière de cétacés
	Shipwreck,      ///< épave de paquebot
	StatueGarden,   ///< jardin de statues
	RayGathering,   ///< rassemblement de raies
	BrineLake,      ///< lac de saumure
	OpenOcean,      ///< océan ouvert (au-delà du labyrinthe)
	Count
};
inline constexpr size_t kChunkKindCount = size_t(ChunkKind::Count);

/// Nom lisible d'un template (menu, statistiques).
[[nodiscard]] std::string_view ChunkKindName(ChunkKind kind) noexcept;

/// Directions d'arête (ordre des bits).
enum class Edge : uint8_t { North = 0, East = 1, South = 2, West = 3 };

/// État d'une arête.
enum class EdgeState : uint8_t { Open = 0, Wall = 1, Tunnel = 2 };

/// Masque/lecture des 2 bits d'une arête dans `ChunkInfo::edges`.
inline constexpr uint8_t kEdgeMask(Edge e) noexcept { return uint8_t(3u << (uint8_t(e) * 2u)); }
[[nodiscard]] constexpr EdgeState EdgeAt(uint8_t edges, Edge e) noexcept { return EdgeState((edges >> (uint8_t(e) * 2u)) & 3u); }
constexpr void EdgeSet(uint8_t &edges, Edge e, EdgeState s) noexcept {
	edges = uint8_t((edges & ~kEdgeMask(e)) | (uint8_t(s) << (uint8_t(e) * 2u)));
}

/// Description déterministe d'une cellule de la grille.
struct ChunkInfo {
	ChunkKind kind = ChunkKind::OpenOcean;
	uint8_t edges = 0; ///< 2 bits par arête (N, E, S, W) : Open / Wall / Tunnel
};

/// Disposition du monde : labyrinthe borné (côté `2*kRadius+1` chunks) centré sur
/// la carte d'entrée, puis océan ouvert à l'infini.
class Layout {
public:
	explicit Layout(uint64_t seed);

	[[nodiscard]] ChunkInfo At(int32_t cx, int32_t cz) const noexcept;
	[[nodiscard]] EdgeState EdgeStateAt(int32_t cx, int32_t cz, Edge e) const noexcept { return EdgeAt(At(cx, cz).edges, e); }
	/// Une arête fermée bloque-t-elle le passage (falaise ou falaise percée) ?
	[[nodiscard]] bool EdgeClosed(int32_t cx, int32_t cz, Edge e) const noexcept { return EdgeStateAt(cx, cz, e) != EdgeState::Open; }
	[[nodiscard]] bool EdgeTunnel(int32_t cx, int32_t cz, Edge e) const noexcept { return EdgeStateAt(cx, cz, e) == EdgeState::Tunnel; }
	/// Vrai si la cellule appartient au labyrinthe (sinon : océan ouvert).
	[[nodiscard]] bool InMaze(int32_t cx, int32_t cz) const noexcept { return std::abs(cx) <= m_radius && std::abs(cz) <= m_radius; }
	[[nodiscard]] int Radius() const noexcept { return m_radius; }
	[[nodiscard]] uint64_t Seed() const noexcept { return m_seed; }

private:
	int m_radius;
	uint64_t m_seed;
	std::vector<ChunkInfo> m_cells; ///< grille (2r+1)² : index (gx + gz*(2r+1))
};

} // namespace abyssale
