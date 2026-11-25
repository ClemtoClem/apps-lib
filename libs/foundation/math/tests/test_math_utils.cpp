#include <cpp-maker/test.hpp>

#include <array>
#include <cmath>

#include <math/math.hpp>

TEST(MathUtils_WrapAngleAndStep) {
	CHECK_NEAR(math::WrapAngle(0.f), 0.f, 1e-6f);
	CHECK_NEAR(math::WrapAngle(1.5f * math::PI), -0.5f * math::PI, 1e-5f);
	CHECK_NEAR(math::WrapAngle(-1.5f * math::PI), 0.5f * math::PI, 1e-5f);
	CHECK_NEAR(math::Wrapf(3.5f, 0.f, 1.f), 0.5f, 1e-6f);
	CHECK_NEAR(math::Wrapf(-0.25f, 0.f, 1.f), 0.75f, 1e-6f);
	CHECK_NEAR(math::RoundToStep(3.2f, 0.5f), 3.0f, 1e-6f);
	CHECK_NEAR(math::RoundToStep(3.3f, 0.5f), 3.5f, 1e-6f);
	CHECK_NEAR(math::RoundToStep(7.f, 0.f), 7.f, 1e-6f); // pas nul : inchangé
}

TEST(MathUtils_SmoothAngle) {
	math::SmoothAngle s;
	s.angle = 0.f;
	s.target = 1.f;
	s.speed = 2.f;
	CHECK_NEAR(s.Update(0.1f), 0.2f, 1e-6f);
	for (int i = 0; i < 100; ++i)
		s.Update(0.1f);
	CHECK_NEAR(s.angle, 1.f, 1e-5f); // converge vers la cible
	// Plus court arc : de ~π à ~−π, l'angle doit passer par +π (augmenter), pas par 0.
	math::SmoothAngle w;
	w.angle = math::PI - 0.1f;
	w.target = -math::PI + 0.1f;
	w.speed = 1.f;
	w.Update(0.05f);
	CHECK(w.angle > math::PI - 0.1f);
}

TEST(MathUtils_AabbSplitAndSphere) {
	const math::FAABB box{{0.f, 0.f, 0.f}, {2.f, 2.f, 2.f}};
	const auto sub = box.Split();
	CHECK(sub[0] == math::FAABB({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}));
	CHECK(sub[7] == math::FAABB({1.f, 1.f, 1.f}, {2.f, 2.f, 2.f}));
	CHECK(box.IntersectsSphere({0.5f, 0.5f, 0.5f}, 0.1f));
	CHECK_FALSE(box.IntersectsSphere({5.f, 5.f, 5.f}, 0.1f));
}

TEST(MathUtils_RayAabbIntersection) {
	const math::FAABB box{{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}};
	float tmin = 0.f, tmax = 0.f;
	CHECK(math::RayAabbIntersection({{0.5f, 0.5f, 5.f}, {0.f, 0.f, -1.f}}, box, tmin, tmax));
	CHECK_NEAR(tmin, 4.f, 1e-5f);
	CHECK_NEAR(tmax, 5.f, 1e-5f);
	CHECK_FALSE(math::RayAabbIntersection({{0.5f, 0.5f, 5.f}, {0.f, 1.f, 0.f}}, box, tmin, tmax)); // parallèle, à côté
}

TEST(MathUtils_RotationOrder) {
	using namespace math;
	// YXZ doit coïncider avec FromEuler (lacet Y, tangage X, roulis Z).
	const FQuaternion a = QuatFromEuler({0.f, PI * 0.5f, 0.f}, RotationOrder::YXZ);
	const FQuaternion b = FQuaternion::FromEuler(0.f, PI * 0.5f, 0.f);
	CHECK(a == b);
	// XYZ : tangage 90° autour de X envoie (0,1,0) vers (0,0,1).
	const FQuaternion c = QuatFromEuler({PI * 0.5f, 0.f, 0.f}, RotationOrder::XYZ);
	const FVector3 r = c.Rotate({0.f, 1.f, 0.f});
	CHECK_NEAR(r.z, 1.f, 1e-5f);
	CHECK_NEAR(r.y, 0.f, 1e-5f);
}
