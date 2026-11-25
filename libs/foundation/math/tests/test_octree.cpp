#include <cpp-maker/test.hpp>

#include <array>
#include <cmath>
#include <vector>

#include <math/octree.hpp>

using math::FVector3;

namespace {

std::vector<FVector3> GridVertices(size_t n) {
	std::vector<FVector3> v;
	v.reserve(n * n);
	for (size_t y = 0; y < n; ++y)
		for (size_t x = 0; x < n; ++x)
			v.push_back({float(x), 0.f, float(y)});
	return v;
}

// Grille n×n de cellules → 2 triangles par cellule, sur le plan XZ (y = 0).
std::vector<std::array<uint32_t, 3>> GridTriangles(size_t n) {
	std::vector<std::array<uint32_t, 3>> t;
	t.reserve((n - 1) * (n - 1) * 2);
	for (size_t y = 0; y + 1 < n; ++y)
		for (size_t x = 0; x + 1 < n; ++x) {
			const uint32_t a = uint32_t(y * n + x), b = uint32_t(y * n + x + 1);
			const uint32_t c = uint32_t((y + 1) * n + x), d = uint32_t((y + 1) * n + x + 1);
			t.push_back({a, b, d});
			t.push_back({a, d, c});
		}
	return t;
}

} // namespace

TEST(Octree_Empty) {
	math::Octree tree;
	std::vector<uint32_t> out;
	tree.QueryRay({{0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}}, out);
	tree.QueryPoint({0.f, 0.f, 0.f}, out);
	tree.QueryAabb({{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}}, out);
	CHECK(out.empty());
}

TEST(Octree_SingleTriangleQueries) {
	const std::vector<FVector3> v = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
	const std::vector<std::array<uint32_t, 3>> t = {{{0, 1, 2}}};
	math::Octree tree(v, t);
	std::vector<uint32_t> out;
	// Rayon vertical vers le bas qui traverse le triangle.
	tree.QueryRay({{0.25f, 0.25f, 1.f}, {0.f, 0.f, -1.f}}, out);
	CHECK_EQ(out.size(), size_t(1));
	CHECK_EQ(out[0], uint32_t(0));
	// Rayon à côté.
	out.clear();
	tree.QueryRay({{2.f, 2.f, 1.f}, {0.f, 0.f, -1.f}}, out);
	CHECK(out.empty());
	// Point dans la boîte.
	out.clear();
	tree.QueryPoint({0.5f, 0.5f, 0.f}, out);
	CHECK_EQ(out.size(), size_t(1));
	// Sphère autour.
	out.clear();
	tree.QuerySphere({0.5f, 0.5f, 0.5f}, 1.f, out);
	CHECK_EQ(out.size(), size_t(1));
}

TEST(Octree_SplitSeparatesFarTriangles) {
	// Deux triangles très éloignés, seuil 1 : l'octree doit séparer les octants.
	const std::vector<FVector3> v = {{0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {0.f, 2.f, 0.f},
									 {100.f, 0.f, 0.f}, {102.f, 0.f, 0.f}, {100.f, 2.f, 0.f}};
	const std::vector<std::array<uint32_t, 3>> t = {{{0, 1, 2}}, {{3, 4, 5}}};
	math::Octree tree(v, t, 1);
	const auto contains = [](const std::vector<uint32_t> &vec, uint32_t x) { return std::ranges::find(vec, x) != vec.end(); };
	std::vector<uint32_t> out;
	tree.QueryRay({{1.f, 1.f, 10.f}, {0.f, 0.f, -1.f}}, out);
	CHECK(contains(out, 0));		 // le triangle proche est candidat (éventuellement dupliqué)
	CHECK_FALSE(contains(out, 1));	 // le triangle lointain est exclu
	out.clear();
	tree.QueryRay({{101.f, 1.f, 10.f}, {0.f, 0.f, -1.f}}, out);
	CHECK(contains(out, 1));
	CHECK_FALSE(contains(out, 0));
}

TEST(Octree_GridConsistency) {
	// Grille 8×8 (98 triangles) : un rayon vertical rend un sous-ensemble cohérent
	// (chaque candidat doit réellement avoir sa boîte traversée par le rayon).
	const size_t n = 8;
	const auto v = GridVertices(n);
	const auto t = GridTriangles(n);
	math::Octree tree(v, t, 4);
	// Rayon au centre, vers le bas.
	const math::FRay ray{{3.5f, 10.f, 3.5f}, {0.f, -1.f, 0.f}};
	std::vector<uint32_t> out;
	tree.QueryRay(ray, out);
	CHECK_FALSE(out.empty());
	for (uint32_t ti : out) {
		const auto &tri = t[ti];
		math::FAABB box = {Min(Min(v[tri[0]], v[tri[1]]), v[tri[2]]), Max(Max(v[tri[0]], v[tri[1]]), v[tri[2]])};
		float tmin = 0.f, tmax = 0.f;
		CHECK(math::RayAabbIntersection(ray, box, tmin, tmax));
	}
	// Un rayon hors grille ne rend rien.
	out.clear();
	tree.QueryRay({{100.f, 10.f, 100.f}, {0.f, -1.f, 0.f}}, out);
	CHECK(out.empty());
}
