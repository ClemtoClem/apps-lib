// Tests des briques de synthèse : bruits colorés, lissage, enveloppe, voix, panoramique, limiteur.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <vector>

#include <audio/synth.hpp>

namespace {
constexpr float kRate = 48000.f;

/// Énergie relative des hautes fréquences : moyenne |x[n] − x[n−1]| / moyenne |x[n]|.
float Roughness(audio::Noise noise) {
	float prev = noise.Next(), diff = 0.f, level = 0.f;
	for (int i = 0; i < 48000; ++i) {
		const float x = noise.Next();
		diff += std::abs(x - prev);
		level += std::abs(x);
		prev = x;
	}
	return diff / std::max(level, 1e-6f);
}
} // namespace

TEST(Synth_NoiseColorsAreOrderedAndBounded) {
	audio::Noise white(audio::NoiseColor::White), pink(audio::NoiseColor::Pink), brown(audio::NoiseColor::Brown);
	for (int i = 0; i < 10000; ++i) {
		const float a = white.Next(), b = pink.Next(), c = brown.Next();
		CHECK(a >= -1.f && a <= 1.f && b >= -1.f && b <= 1.f && c >= -1.f && c <= 1.f);
	}
	// Le bruit brun est le plus « grave », le blanc le plus « aigu ».
	const float rw = Roughness(audio::Noise(audio::NoiseColor::White)), rp = Roughness(audio::Noise(audio::NoiseColor::Pink)),
				rb = Roughness(audio::Noise(audio::NoiseColor::Brown));
	CHECK(rw > rp);
	CHECK(rp > rb);
}

TEST(Synth_SmootherAndEnvelope) {
	audio::Smoother s(0.f, 0.1f);
	s.SetTarget(1.f);
	float v = 0.f;
	for (int i = 0; i < int(kRate * 0.1f); ++i) // une constante de temps : ≈ 63 %
		v = s.Next(kRate);
	CHECK_NEAR(v, 0.632f, 0.02f);

	audio::Envelope env;
	env.Trigger(0.5f, 0.01f, 0.05f);
	float peak = 0.f;
	int samples = 0;
	while (env.Active() && samples < int(kRate)) {
		peak = std::max(peak, env.Next(kRate));
		++samples;
	}
	CHECK_NEAR(peak, 0.5f, 1e-3f);
	CHECK(!env.Active());					  // s'est éteinte
	CHECK(samples < int(kRate * 0.5f));	  // en quelques constantes de temps
}

TEST(Synth_VoiceGlidesAndPoolSteals) {
	audio::Voice v;
	v.Play({.from = 200.f, .to = 800.f, .duration = 0.2f, .level = 0.3f, .attack = 0.005f, .pan = 1.f, .send = 0.5f});
	float l = 0.f, r = 0.f, send = 0.f, peak = 0.f;
	for (int i = 0; i < int(kRate * 0.2f); ++i) {
		float a = 0.f, b = 0.f, c = 0.f;
		v.Mix(kRate, a, b, c);
		l += std::abs(a);
		r += std::abs(b);
		send += std::abs(c);
		peak = std::max(peak, std::abs(b));
	}
	CHECK(peak <= 0.3f + 1e-4f);
	CHECK(r > l * 100.f); // panoramique à droite
	CHECK(send > 0.f);

	audio::VoicePool<2> pool;
	for (int i = 0; i < 3; ++i)
		pool.Play({.from = 300.f, .to = 300.f, .duration = 1.f, .level = 0.1f});
	CHECK_EQ(pool.ActiveCount(), 2u); // la plus ancienne a été volée
}

TEST(Synth_PanAndLimiter) {
	const audio::StereoGain c = audio::Pan(0.f), left = audio::Pan(-1.f);
	CHECK_NEAR(c.left * c.left + c.right * c.right, 1.f, 1e-5f); // puissance constante
	CHECK_NEAR(left.left, 1.f, 1e-5f);
	CHECK_NEAR(left.right, 0.f, 1e-5f);

	audio::Limiter lim(0.5f, 0.2f);
	float worst = 0.f;
	for (int i = 0; i < 4800; ++i) {
		float a = 2.f * std::sin(float(i) * 0.05f), b = -1.5f;
		lim.Process(a, b, kRate);
		worst = std::max({worst, std::abs(a), std::abs(b)});
	}
	CHECK(worst <= 0.5f + 1e-5f);
}
