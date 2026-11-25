#include <cpp-maker/test.hpp>

#include <cmath>
#include <vector>

#include <audio/reverb.hpp>

using audio::Reverb;

TEST(Reverb, ImpulseDecays) {
	Reverb reverb(48000.f);
	reverb.SetMix(0.5f);
	reverb.SetRoomSize(0.5f);
	reverb.SetDamping(0.5f);
	std::vector<float> buf(96000, 0.f); // 2 secondes
	buf[0] = 1.f;						 // impulsion
	reverb.Process(buf.data(), buf.size());
	// Queue de réverbération non nulle après l'impulsion.
	float tail = 0.f;
	for (size_t i = 100; i < buf.size(); ++i)
		tail += std::fabs(buf[i]);
	CHECK(tail > 0.01f);
	// Décroissance : la fin est bien plus faible que les premiers échos (~1215 échantillons).
	float early = 0.f, late = 0.f;
	for (size_t i = 1200; i < 4000; ++i)
		early = std::max(early, std::fabs(buf[i]));
	for (size_t i = buf.size() - 1000; i < buf.size(); ++i)
		late = std::max(late, std::fabs(buf[i]));
	CHECK(late < early);
	CHECK(late < 0.01f);
}

TEST(Reverb, DryMixPassesThrough) {
	Reverb reverb(48000.f);
	reverb.SetMix(0.f);
	std::vector<float> buf(1000, 0.5f);
	const std::vector<float> orig = buf;
	reverb.Process(buf.data(), buf.size());
	for (size_t i = 0; i < buf.size(); ++i)
		CHECK_NEAR(buf[i], orig[i], 1e-6f); // sec : signal inchangé
}

TEST(Reverb, ResetClearsTail) {
	Reverb reverb(48000.f);
	reverb.SetMix(1.f);
	std::vector<float> buf(1000, 1.f);
	reverb.Process(buf.data(), buf.size()); // charge les lignes de retard
	reverb.Reset();
	std::vector<float> silence(1000, 0.f);
	reverb.Process(silence.data(), silence.size());
	for (float s : silence)
		CHECK_NEAR(s, 0.f, 1e-6f); // après Reset, rien ne sort
}
