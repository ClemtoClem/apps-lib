#include <cpp-maker/test.hpp>

#include <sandbox/document/tilemap.hpp>

using namespace sandbox::tilemap;

TEST(Tilemap_TerrainAndAutotile) {
	Tilemap map(8, 8);
	map.AddPattern(1, {1, 1, 1, 1, 1, 1, 1, 1, 1}); // herbe pleine
	map.AddPattern(1, {2, 2, 2, 1, 1, 1, 1, 1, 1}); // herbe bordée de terre au-dessus
	map.AddPattern(2, {2, 2, 2, 2, 2, 2, 2, 2, 2}); // terre pleine
	// Herbe seule : motif plein.
	map.SetTerrain(5, 5, 1);
	const auto p = map.Pattern(5, 5);
	REQUIRE(p.has_value());
	CHECK(*p == (TilePattern{1, 1, 1, 1, 1, 1, 1, 1, 1}));
	// Terre au-dessus : l'herbe devient bordée.
	map.SetTerrain(5, 4, 2);
	const auto q = map.Pattern(5, 5);
	REQUIRE(q.has_value());
	CHECK(*q == (TilePattern{2, 2, 2, 1, 1, 1, 1, 1, 1}));
}

TEST(Tilemap_EmptyAndOutOfBounds) {
	Tilemap map(4, 4);
	map.AddPattern(1, {1, 1, 1, 1, 1, 1, 1, 1, 1});
	CHECK(map.Terrain(0, 0) == 0); // vide
	CHECK_FALSE(map.Pattern(0, 0).has_value()); // vide → pas de motif
	CHECK_FALSE(map.Pattern(-1, 0).has_value()); // hors bornes
	CHECK_FALSE(map.Pattern(4, 4).has_value());
	// Terrain sans motif dans le tileset → pas de motif.
	map.SetTerrain(1, 1, 9);
	CHECK_FALSE(map.Pattern(1, 1).has_value());
}
