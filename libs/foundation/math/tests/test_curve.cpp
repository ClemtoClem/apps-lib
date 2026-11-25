#include <cpp-maker/test.hpp>

#include <cmath>

#include <math/curve.hpp>

TEST(Curve_LinearAndConstant) {
	math::Curve c;
	CHECK_NEAR(c.ValueAt(0.5f), 0.f, 1e-6f); // vide → 0
	c.AddKey({0.f, 0.f, math::CurveKeyKind::Linear});
	CHECK_NEAR(c.ValueAt(0.5f), 0.f, 1e-6f); // une seule clé → sa valeur
	c.AddKey({1.f, 10.f, math::CurveKeyKind::Linear});
	CHECK_NEAR(c.ValueAt(0.5f), 5.f, 1e-5f);
	CHECK_NEAR(c.ValueAt(-1.f), 0.f, 1e-6f); // borné à gauche
	CHECK_NEAR(c.ValueAt(2.f), 10.f, 1e-6f); // borné à droite
	// Créneau : la clé de gauche en Constant fige la valeur.
	math::Curve step;
	step.AddKey({0.f, 1.f, math::CurveKeyKind::Constant});
	step.AddKey({1.f, 2.f, math::CurveKeyKind::Constant});
	CHECK_NEAR(step.ValueAt(0.25f), 1.f, 1e-6f);
	CHECK_NEAR(step.ValueAt(0.75f), 1.f, 1e-6f);
}

TEST(Curve_CubicHermite) {
	// Tangentes nulles : s-courbe lisse (équivalente au smoothstep).
	math::Curve c;
	c.AddKey({0.f, 0.f, math::CurveKeyKind::Cubic, 0.f, 0.f});
	c.AddKey({1.f, 1.f, math::CurveKeyKind::Cubic, 0.f, 0.f});
	CHECK_NEAR(c.ValueAt(0.f), 0.f, 1e-6f);
	CHECK_NEAR(c.ValueAt(0.5f), 0.5f, 1e-5f);
	CHECK_NEAR(c.ValueAt(1.f), 1.f, 1e-6f);
	CHECK_NEAR(c.ValueAt(0.25f), 0.15625f, 1e-5f); // -2t³ + 3t²
	CHECK_NEAR(c.ValueAt(0.75f), 0.84375f, 1e-5f);
	// Pente nulle aux extrémités → dérivée positive au centre.
	CHECK(c.ValueAt(0.6f) > c.ValueAt(0.4f));
}

TEST(Curve_MixedKindsAndHint) {
	math::Curve c;
	c.AddKey({0.f, 0.f, math::CurveKeyKind::Cubic, 0.f, 1.f});
	c.AddKey({1.f, 10.f, math::CurveKeyKind::Linear}); // clé droite non cubique : tangente droite nulle
	c.AddKey({2.f, 20.f, math::CurveKeyKind::Constant});
	// Segment 0..1 : cubique avec tangente droite (b.leftTangent = 0).
	CHECK_NEAR(c.ValueAt(0.f), 0.f, 1e-6f);
	// Segment 1..2 : linéaire (clé gauche Linear).
	CHECK_NEAR(c.ValueAt(1.5f), 15.f, 1e-5f);
	// Cache de segment : balayage monotone cohérent avec et sans cache.
	size_t hint = 0;
	float prev = -1e30f;
	for (float t = 0.f; t <= 2.f; t += 0.05f) {
		const float withHint = c.ValueAt(t, hint);
		const float noHint = c.ValueAt(t);
		CHECK_NEAR(withHint, noHint, 1e-5f);
		CHECK(withHint >= prev - 1e-5f);
		prev = withHint;
	}
}
