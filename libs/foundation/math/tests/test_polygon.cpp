// Tests de math/polygon.hpp et des ajouts vectoriels (Rotation, distances, Cross 2D).
#include <cpp-maker/test.hpp>

#include <cmath>
#include <vector>

#include <math/polygon.hpp>

namespace {

float TrianglesArea(const std::vector<math::FVector2> &pts, const std::vector<uint32_t> &idx) {
	float area = 0.f;
	for (size_t i = 0; i < idx.size(); i += 3) {
		const float a = (pts[idx[i + 1]] - pts[idx[i]]).Cross(pts[idx[i + 2]] - pts[idx[i]]) * 0.5f;
		CHECK(a > -1e-6f); // sens trigonométrique
		area += a;
	}
	return area;
}

} // namespace

TEST(Polygon_AreaAndContainment) {
	const std::vector<math::FVector2> square = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
	CHECK_NEAR(math::SignedArea(square), 4.f, 1e-6f);
	const std::vector<math::FVector2> cw(square.rbegin(), square.rend());
	CHECK_NEAR(math::SignedArea(cw), -4.f, 1e-6f);
	CHECK(math::PointInPolygon(square, {1, 1}));
	CHECK(!math::PointInPolygon(square, {3, 1}));
	CHECK(math::OnPolygonBorder(square, {2.f, 1.f}, 1e-4f));
	CHECK(!math::OnPolygonBorder(square, {1.f, 1.f}, 1e-4f));
}

TEST(Polygon_TriangulateConcaveWithHoles) {
	// « U » concave : 10 sommets, aire 5.
	const std::vector<math::FVector2> u = {{0, 0}, {3, 0}, {3, 3}, {2, 3}, {2, 1}, {1, 1}, {1, 3}, {0, 3}};
	std::vector<uint32_t> idx;
	REQUIRE(math::TriangulatePolygon(u, {}, idx));
	CHECK_EQ(idx.size(), size_t(6 * 3));
	CHECK_NEAR(TrianglesArea(u, idx), math::SignedArea(u), 1e-4f);
	// Carré 4×4 percé de deux trous 1×1 (sens quelconque) : aire 14.
	const std::vector<math::FVector2> outer = {{0, 0}, {0, 4}, {4, 4}, {4, 0}}; // horaire
	const std::vector<std::vector<math::FVector2>> holes = {{{1, 1}, {2, 1}, {2, 2}, {1, 2}}, {{2.5f, 2.5f}, {2.5f, 3.5f}, {3.5f, 3.5f}, {3.5f, 2.5f}}};
	std::vector<math::FVector2> all = outer;
	for (const auto &h : holes)
		all.insert(all.end(), h.begin(), h.end());
	idx.clear();
	REQUIRE(math::TriangulatePolygon(outer, holes, idx));
	CHECK_NEAR(TrianglesArea(all, idx), 14.f, 1e-3f);
}

TEST(Math_RotationAndDistances) {
	const math::FMatrix4 r = math::FMatrix4::Rotation({0.f, 0.f, 2.f}, math::PI * 0.5f);
	const math::FVector3 p = r.TransformPoint({1.f, 0.f, 0.f});
	CHECK_NEAR(p.x, 0.f, 1e-6f);
	CHECK_NEAR(p.y, 1.f, 1e-6f); // quart de tour trigonométrique autour de +Z
	const math::FVector3 d = (math::FMatrix4::Translation({5.f, 0.f, 0.f}) * r).TransformDirection({1.f, 0.f, 0.f});
	CHECK_NEAR(d.y, 1.f, 1e-6f); // translation ignorée
	CHECK_NEAR(math::FVector3(1, 2, 2).Distance({}), 3.f, 1e-6f);
	const math::FMatrix4 m = math::FMatrix4::Translation({1.f, -2.f, 3.f}) * r * math::FMatrix4::Scale({2.f, 3.f, 4.f});
	const math::FVector3 q = m.Inverse().TransformPoint(m.TransformPoint({0.3f, 0.7f, -1.1f}));
	CHECK_NEAR(q.x, 0.3f, 1e-5f);
	CHECK_NEAR(q.z, -1.1f, 1e-5f);
	CHECK_EQ(math::FVector2(1, 0).Cross({0, 1}), 1.f);
	CHECK_EQ(math::Max(2, 5), 5);
	CHECK_EQ(math::Min(2.f, -1.f), -1.f);
}
