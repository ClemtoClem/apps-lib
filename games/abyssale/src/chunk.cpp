#include "abyssale/chunk.hpp"

#include <algorithm>
#include <array>

#include <math/noise.hpp>

namespace abyssale {

std::string_view ChunkKindName(ChunkKind kind) noexcept {
	switch (kind) {
	case ChunkKind::Entry: return "Carte d'entrée";
	case ChunkKind::CoralBarrier: return "Barrière de coraux";
	case ChunkKind::KelpForest: return "Forêt de varech";
	case ChunkKind::Hydrothermal: return "Sources hydrothermales";
	case ChunkKind::WhaleGraveyard: return "Cimetière de cétacés";
	case ChunkKind::Shipwreck: return "Épave de paquebot";
	case ChunkKind::StatueGarden: return "Jardin de statues";
	case ChunkKind::RayGathering: return "Rassemblement de raies";
	case ChunkKind::BrineLake: return "Lac de saumure";
	case ChunkKind::OpenOcean: return "Océan ouvert";
	default: return "?";
	}
}

namespace {

/// Direction opposée (N↔S, E↔O).
constexpr Edge Opposite(Edge e) noexcept { return Edge(uint8_t(e) ^ 2u); }

/// Deltas (gx, gz) d'une arête.
constexpr std::array<std::pair<int, int>, 4> kDeltas{{{0, -1}, {1, 0}, {0, 1}, {-1, 0}}};

} // namespace

Layout::Layout(uint64_t seed) : m_seed(seed) {
	// Labyrinthe de côté `side` chunks (impair pour centrer la carte d'entrée).
	constexpr int kRadius = 10;
	m_radius = kRadius;
	const int side = 2 * kRadius + 1;
	const int center = kRadius; // (gx, gz) du chunk monde (0, 0)

	math::Rng rng(seed);
	const auto idx = [&](int gx, int gz) { return size_t(gz) * size_t(side) + size_t(gx); };

	m_cells.assign(size_t(side) * side, ChunkInfo{});
	// Toutes les arêtes commencent fermées (falaises).
	for (ChunkInfo &c : m_cells)
		c.edges = 0b01010101u;

	// ── Recursive backtracker : arbre couvrant, une arête ouverte par cellule ──
	std::vector<uint8_t> visited(size_t(side) * side, 0);
	std::vector<std::pair<int, int>> stack;
	visited[idx(center, center)] = 1;
	stack.push_back({center, center});
	while (!stack.empty()) {
		const auto [gx, gz] = stack.back();
		// Voisins non visités, dans un ordre mélangé par le tirage.
		std::array<int, 4> dirs{0, 1, 2, 3};
		for (int i = 3; i > 0; --i) {
			const int j = rng.NextInt(0, i);
			std::swap(dirs[size_t(i)], dirs[size_t(j)]);
		}
		bool advanced = false;
		for (const int d : dirs) {
			const int nx = gx + kDeltas[size_t(d)].first, nz = gz + kDeltas[size_t(d)].second;
			if (nx < 0 || nz < 0 || nx >= side || nz >= side || visited[idx(nx, nz)])
				continue;
			// Ouvre l'arête des deux côtés.
			EdgeSet(m_cells[idx(gx, gz)].edges, Edge(d), EdgeState::Open);
			EdgeSet(m_cells[idx(nx, nz)].edges, Opposite(Edge(d)), EdgeState::Open);
			visited[idx(nx, nz)] = 1;
			stack.push_back({nx, nz});
			advanced = true;
			break;
		}
		if (!advanced)
			stack.pop_back();
	}

	// ── Attribution des templates : la carte d'entrée au centre, les 8 autres ──
	// répartis par sacs (aucun template ne domine localement).
	std::vector<ChunkKind> bag;
	for (uint8_t k = uint8_t(ChunkKind::CoralBarrier); k <= uint8_t(ChunkKind::BrineLake); ++k)
		bag.push_back(ChunkKind(k));
	auto drawKind = [&]() {
		if (bag.empty())
			for (uint8_t k = uint8_t(ChunkKind::CoralBarrier); k <= uint8_t(ChunkKind::BrineLake); ++k)
				bag.push_back(ChunkKind(k));
		const size_t i = size_t(rng.NextInt(0, int(bag.size()) - 1));
		const ChunkKind k = bag[i];
		bag.erase(bag.begin() + std::ptrdiff_t(i));
		return k;
	};
	m_cells[idx(center, center)].kind = ChunkKind::Entry;
	for (int gz = 0; gz < side; ++gz)
		for (int gx = 0; gx < side; ++gx)
			if (gx != center || gz != center)
				m_cells[idx(gx, gz)].kind = drawKind();

	// ── Tunnels : une partie des falaises sont percées d'un passage ──
	// Traité une fois par arête (côté Nord et Est seulement).
	for (int gz = 0; gz < side; ++gz)
		for (int gx = 0; gx < side; ++gx) {
			ChunkInfo &c = m_cells[idx(gx, gz)];
			for (const Edge e : {Edge::North, Edge::East}) {
				if (EdgeAt(c.edges, e) != EdgeState::Wall)
					continue;
				const int nx = gx + kDeltas[size_t(uint8_t(e))].first, nz = gz + kDeltas[size_t(uint8_t(e))].second;
				const bool border = nx < 0 || nz < 0 || nx >= side || nz >= side;
				// Les falaises de la bordure sont percées régulièrement pour relier l'océan
				// ouvert ; à l'intérieur, une sur six environ devient un tunnel.
				const bool tunnel = border ? ((gx + gz) % 3 == 0) : (rng.NextFloat() < 0.16f);
				if (!tunnel)
					continue;
				EdgeSet(c.edges, e, EdgeState::Tunnel);
				if (!border)
					EdgeSet(m_cells[idx(nx, nz)].edges, Opposite(e), EdgeState::Tunnel);
			}
		}
}

ChunkInfo Layout::At(int32_t cx, int32_t cz) const noexcept {
	if (!InMaze(cx, cz))
		return ChunkInfo{ChunkKind::OpenOcean, 0}; // océan ouvert : aucune falaise
	const int side = 2 * m_radius + 1;
	const int gx = int(cx) + m_radius, gz = int(cz) + m_radius;
	return m_cells[size_t(gz) * size_t(side) + size_t(gx)];
}

} // namespace abyssale
