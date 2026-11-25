#include <cpp-maker/test.hpp>

#include <array>
#include <vector>

#include <sandbox/document/autotile.hpp>

using namespace sandbox::tilemap;

TEST(Autotile_EdgeAndCompatible) {
	const TilePattern grass{1, 1, 1, 1, 1, 1, 1, 1, 1};
	const TilePattern dirt{2, 2, 2, 2, 2, 2, 2, 2, 2};
	const TilePattern grassDirtTop{2, 2, 2, 1, 1, 1, 1, 1, 1};
	// Terrain = centre.
	CHECK_EQ(int(grass.Terrain()), 1);
	CHECK_EQ(int(dirt.Terrain()), 2);
	// Bordure.
	const auto e0 = Edge(grass, 0u);
	CHECK(e0[0] == 1 && e0[1] == 1 && e0[2] == 1);
	const auto e1 = Edge(grassDirtTop, 1u);
	CHECK(e1[0] == 2 && e1[1] == 2 && e1[2] == 2);
	// Compatibilité.
	CHECK(Compatible(grass, grass, 0u));
	CHECK_FALSE(Compatible(grass, dirt, 0u));
	CHECK(Compatible(grassDirtTop, dirt, 1u));	   // herbe bordée de terre, terre au-dessus
	CHECK_FALSE(Compatible(grassDirtTop, grass, 1u)); // bord haut = terre ≠ herbe
}

TEST(Autotile_PicksFirstLegalPattern) {
	const TilePattern grass{1, 1, 1, 1, 1, 1, 1, 1, 1};
	const TilePattern grassDirtTop{2, 2, 2, 1, 1, 1, 1, 1, 1};
	const TilePattern dirt{2, 2, 2, 2, 2, 2, 2, 2, 2};
	// Herbe entourée de terre en haut, herbe ailleurs : il faut le motif bordé.
	const std::vector<TilePattern> candidates = {grass, grassDirtTop};
	// Rangée d'herbe sous la terre : les côtés sont aussi « herbe sous terre » (les coins
	// haut gauche/droit valent 2), le bas reste de l'herbe.
	const std::array<const TilePattern *, 4> neighbors = {&grassDirtTop, &dirt, &grass, &grassDirtTop};
	const auto p = Autotile(candidates, neighbors);
	REQUIRE(p.has_value());
	CHECK(*p == grassDirtTop); // l'herbe pleine est incompatible avec la terre au-dessus
	// Sans voisin : le premier candidat (herbe pleine) suffit.
	const std::array<const TilePattern *, 4> none = {nullptr, nullptr, nullptr, nullptr};
	const auto q = Autotile(candidates, none);
	REQUIRE(q.has_value());
	CHECK(*q == grass);
}

TEST(Autotile_NoMatchReturnsNone) {
	const TilePattern grass{1, 1, 1, 1, 1, 1, 1, 1, 1};
	const TilePattern dirt{2, 2, 2, 2, 2, 2, 2, 2, 2};
	// Seule de l'herbe est disponible, mais un voisin est de la terre : aucun motif ne convient.
	const std::vector<TilePattern> candidates = {grass};
	const std::array<const TilePattern *, 4> neighbors = {&dirt, nullptr, nullptr, nullptr};
	CHECK_FALSE(Autotile(candidates, neighbors).has_value());
}
