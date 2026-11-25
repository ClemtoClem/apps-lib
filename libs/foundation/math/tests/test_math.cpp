#include <cpp-maker/test.hpp>

#include <algorithm>

#include <math/bezier.hpp>
#include <math/math.hpp>
#include <math/noise.hpp>

TEST(Math_FrustumAndNoise) {
	const math::FMatrix4 vp = math::FMatrix4::Perspective(1.2f, 1.6f, 0.1f, 100.f) * math::FMatrix4::LookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
	const math::FFrustum f = math::FFrustum::FromMatrix(vp);
	CHECK(f.Intersects({{-1, -1, -10}, {1, 1, -8}}));
	CHECK_FALSE(f.Intersects({{-1, -1, 8}, {1, 1, 10}}));
	CHECK_FALSE(f.Intersects({{-1, -1, -200}, {1, 1, -150}}));
	math::SimplexNoise n(42);
	float lo = 1, hi = -1;
	for (int i = 0; i < 10000; ++i) {
		const float v = n.Noise3(float(i) * 0.37f, float(i % 97) * 0.21f, float(i % 13) * 0.5f);
		lo = std::min(lo, v);
		hi = std::max(hi, v);
	}
	CHECK_GE(lo, -1.01f);
	CHECK_LE(hi, 1.01f);
	CHECK_EQ(math::SimplexNoise(7).Noise2(1.5f, 2.5f), math::SimplexNoise(7).Noise2(1.5f, 2.5f));
}


TEST(Math_VectorsAndFreeFunctions) {
	using namespace math;
	FVector2 a{3.f, 4.f};
	CHECK_NEAR(Length(a), 5.f, 1e-6f);
	CHECK_NEAR(Normalize(a)[1], 0.8f, 1e-6f);
	a[0] = 1.f;
	CHECK_NEAR(a.x, 1.f, 1e-6f);
	CHECK((FVector2{2.f, 3.f} * FVector2{4.f, 5.f}) == FVector2(8.f, 15.f));
	CHECK(FVector2(1.f, 0.f).Perp() == FVector2(0.f, 1.f));
	CHECK_NEAR(Rotate2D({1.f, 0.f}, PI * 0.5f).y, 1.f, 1e-6f);
	CHECK(IVec2(1, 2) + IVec2(3, 4) == IVec2(4, 6));
	const FVector3 x{1.f, 0.f, 0.f}, y{0.f, 1.f, 0.f};
	CHECK(Cross(x, y) == FVector3(0.f, 0.f, 1.f));
	CHECK_NEAR(Angle(x, y), PI * 0.5f, 1e-6f);
	CHECK_NEAR(Reflect({1.f, -1.f, 0.f}, y).y, 1.f, 1e-6f);
	CHECK_NEAR(Project({2.f, 3.f, 0.f}, x).x, 2.f, 1e-6f);
	const FVector3 mid = Slerp(x, y, 0.5f);
	CHECK_NEAR(mid.x, std::sqrt(0.5f), 1e-5f);
	CHECK_NEAR(mid.Length(), 1.f, 1e-5f);
	CHECK(TriangleNormal({0, 0, 0}, {1, 0, 0}, {0, 1, 0}) == FVector3(0.f, 0.f, 1.f));
	CHECK_NEAR(Radians(180.f), PI, 1e-6f);
	CHECK_NEAR(Degrees(PI), 180.f, 1e-4f);
	CHECK_NEAR((FVector4{0.f, 3.f, 0.f, 4.f}).Normalized()[3], 0.8f, 1e-6f);
}

TEST(Math_QuaternionsAndMatrices) {
	using namespace math;
	const FQuaternion q = FQuaternion::AxisAngle({0.f, 1.f, 0.f}, PI * 0.5f);
	const FVector3 v = q.Rotate({1.f, 0.f, 0.f});
	CHECK_NEAR(v.z, -1.f, 1e-5f);
	// Matrice équivalente au quaternion, et à Rotation().
	const FVector3 m = q.ToMatrix().TransformDirection({1.f, 0.f, 0.f});
	CHECK_NEAR(m.z, v.z, 1e-5f);
	const FVector3 r = FMatrix4::Rotation({0.f, 1.f, 0.f}, PI * 0.5f).TransformDirection({1.f, 0.f, 0.f});
	CHECK_NEAR(r.z, v.z, 1e-5f);
	CHECK_NEAR((q * q.Inverse()).w, 1.f, 1e-5f);
	CHECK_NEAR(q.Conjugate().y, -q.y, 1e-7f);
	const FQuaternion half = FQuaternion::Slerp(FQuaternion::Identity(), q, 0.5f);
	CHECK_NEAR(half.Rotate({1.f, 0.f, 0.f}).z, -std::sqrt(0.5f), 1e-5f); // 45°
	CHECK_NEAR(FQuaternion::FromEuler(0.f, PI * 0.5f, 0.f).Rotate({1.f, 0.f, 0.f}).z, -1.f, 1e-5f);
	const FMatrix4 trs = TRS({1.f, 2.f, 3.f}, q, {2.f, 2.f, 2.f});
	const FVector3 p = trs.TransformPoint({1.f, 0.f, 0.f});
	CHECK_NEAR(p.x, 1.f, 1e-5f);
	CHECK_NEAR(p.z, 1.f, 1e-5f); // 3 − 2
	CHECK_NEAR(trs.Transposed().Transposed().m[3][0], 1.f, 1e-6f);
	const FMatrix4 vp = FMatrix4::Ortho(-1.f, 1.f, -1.f, 1.f, -1.f, 1.f);
	const FVector3 s = ProjectToViewport({0.f, 1.f, 0.f}, vp, {0.f, 0.f}, {200.f, 100.f});
	CHECK_NEAR(s.x, 100.f, 1e-3f);
	CHECK_NEAR(s.y, 0.f, 1e-3f); // haut de la fenêtre
}

TEST(Math_Bezier) {
	using namespace math;
	const std::array<FVector2, 3> curve{{{0.f, 0.f}, {1.f, 2.f}, {2.f, 0.f}}};
	const FVector2 top = Bezier<FVector2>(curve, 0.5f);
	CHECK_NEAR(top.x, 1.f, 1e-6f);
	CHECK_NEAR(top.y, 1.f, 1e-6f);
	const FVector2 tangent = BezierDerivative<FVector2>(curve, 0.5f);
	CHECK_NEAR(tangent.y, 0.f, 1e-6f); // sommet : tangente horizontale
	CHECK_NEAR(tangent.x, 2.f, 1e-6f);
	CHECK_NEAR(BezierDerivative<FVector2>(curve, 0.f, 2).y, -8.f, 1e-5f);
	// Carreau plan z = 0 : normale (produit des dérivées) selon z.
	const FVector3 patch[2][2] = {{{0, 0, 0}, {1, 0, 0}}, {{0, 1, 0}, {1, 1, 0}}};
	const FVector3 c = Bezier2<FVector3, 2, 2>(patch, {0.25f, 0.75f});
	CHECK_NEAR(c.x, 0.25f, 1e-6f);
	CHECK_NEAR(c.y, 0.75f, 1e-6f);
	const auto j = Bezier2Jacobian<FVector3, 2, 2>(patch, {0.5f, 0.5f});
	CHECK(Cross(j[0], j[1]) == FVector3(0.f, 0.f, 1.f));
}
