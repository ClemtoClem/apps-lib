#include <cpp-maker/test.hpp>

#include <cmath>

#include <sandbox/game/animation.hpp>

using namespace sandbox::game;

TEST(Animation_CurvePlaybackAndLoop) {
	Animation a;
	math::Curve c;
	c.AddKey({0.f, 0.f, math::CurveKeyKind::Linear});
	c.AddKey({1.f, 10.f, math::CurveKeyKind::Linear});
	a.AddTrack(std::move(c));
	CHECK_NEAR(a.Duration(), 1.f, 1e-6f);
	CHECK_NEAR(a.Value(0), 0.f, 1e-6f);
	a.Tick(0.25f);
	CHECK_NEAR(a.Time(), 0.25f, 1e-6f);
	CHECK_NEAR(a.Value(0), 2.5f, 1e-4f);
	// Boucle : au-delà de la durée, on repart de zéro.
	a.looped = true;
	a.Tick(1.f); // 0,25 + 1 = 1,25 → 0,25
	CHECK_NEAR(a.Time(), 0.25f, 1e-5f);
	CHECK(a.playing);
}

TEST(Animation_StopsAtEndWithoutLoop) {
	Animation a;
	math::Curve c;
	c.AddKey({0.f, 0.f, math::CurveKeyKind::Linear});
	c.AddKey({2.f, 4.f, math::CurveKeyKind::Linear});
	a.AddTrack(std::move(c));
	a.Tick(3.f); // dépasse la durée 2
	CHECK_NEAR(a.Time(), 2.f, 1e-6f);
	CHECK_FALSE(a.playing); // arrêtée en fin de piste
	CHECK_NEAR(a.Value(0), 4.f, 1e-6f);
}

TEST(Animation_SignalsFireWhenCrossed) {
	Animation a;
	math::Curve c;
	c.AddKey({0.f, 0.f, math::CurveKeyKind::Linear});
	c.AddKey({2.f, 2.f, math::CurveKeyKind::Linear});
	a.AddTrack(std::move(c));
	a.AddSignal({"milieu", 1.f});
	a.AddSignal({"fin", 1.9f});
	a.Tick(0.5f);
	CHECK(a.TakeSignals().empty());
	a.Tick(0.6f); // franchit « milieu » (1,0)
	const auto s = a.TakeSignals();
	CHECK_EQ(s.size(), size_t(1));
	CHECK_EQ(s[0], "milieu");
	// Boucle : en repassant par zéro, « fin » est franchi.
	a.looped = true;
	a.Tick(0.5f); // 1,1 + 0,5 = 1,6 (pas encore « fin » à 1,9)
	CHECK(a.TakeSignals().empty());
	a.Tick(0.5f); // 1,6 + 0,5 = 2,1 → 0,1 : franchit « fin » (1,9)
	const auto s2 = a.TakeSignals();
	CHECK_EQ(s2.size(), size_t(1));
	CHECK_EQ(s2[0], "fin");
}

TEST(SpriteSheet_FrameUvAndPlayback) {
	SpriteSheetAnimation s;
	s.columns = 2;
	s.rows = 2;
	s.frameCount = 4;
	// Rectangle UV de chaque trame (grille 2×2, normalisé).
	const math::FRect f0 = s.FrameUv(0);
	CHECK_NEAR(f0.x, 0.f, 1e-6f);
	CHECK_NEAR(f0.y, 0.f, 1e-6f);
	CHECK_NEAR(f0.w, 0.5f, 1e-6f);
	const math::FRect f3 = s.FrameUv(3);
	CHECK_NEAR(f3.x, 0.5f, 1e-6f);
	CHECK_NEAR(f3.y, 0.5f, 1e-6f);
	// Lecture : 12 trames/s → 0,3 s = 3,6 trames.
	s.Tick(0.3f);
	CHECK_NEAR(s.Frame(), 3.6f, 1e-4f);
}

TEST(SpriteSheet_SignalsAndLoop) {
	SpriteSheetAnimation s;
	s.speed = 10.f; // 10 trames/s
	s.frameCount = 4;
	s.signals.push_back({"pas", 2.f});
	s.Tick(0.3f); // 3 trames : franchit 2
	const auto fired = s.TakeSignals();
	CHECK_EQ(fired.size(), size_t(1));
	CHECK_EQ(fired[0], "pas");
	// Boucle : au-delà de 4 trames, on repart.
	s.Tick(0.2f); // 5 → 1 (boucle), ne franchit rien
	CHECK(s.TakeSignals().empty());
	CHECK_NEAR(s.Frame(), 1.f, 1e-4f);
}
