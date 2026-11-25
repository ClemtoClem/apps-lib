// Tests d'engine.audio : signaux synthétiques, propriétés mesurables (fréquence, niveau,
// atténuation, retard), sans périphérique audio.
#include <cpp-maker/test.hpp>

#include <cmath>
#include <numbers>
#include <vector>

#include <audio/dsp.hpp>

namespace {

constexpr float kRate = 48000.f;

std::vector<float> Sine(float freq, size_t n, float amplitude = 1.f) {
	std::vector<float> s(n);
	for (size_t i = 0; i < n; ++i)
		s[i] = amplitude * std::sin(2.f * std::numbers::pi_v<float> * freq * float(i) / kRate);
	return s;
}

/// Nombre de passages par zéro montants (≈ nombre de périodes).
int RisingCrossings(const std::vector<float> &s) {
	int n = 0;
	for (size_t i = 1; i < s.size(); ++i)
		if (s[i - 1] < 0.f && s[i] >= 0.f)
			++n;
	return n;
}

std::vector<float> Render(audio::Waveform shape, float freq, size_t n) {
	audio::Oscillator osc;
	osc.shape = shape;
	osc.frequency = freq;
	osc.amplitude = 1.f;
	std::vector<float> out(n);
	osc.Render(out, kRate);
	return out;
}

} // namespace

TEST(Oscillator_FrequencyAndRange) {
	for (const audio::Waveform shape : {audio::Waveform::Sine, audio::Waveform::Square, audio::Waveform::Sawtooth, audio::Waveform::Triangle}) {
		const std::vector<float> s = Render(shape, 480.f, size_t(kRate)); // une seconde
		const int periods = RisingCrossings(s);
		CHECK(periods >= 478 && periods <= 482);
		CHECK(audio::Peak(s) <= 1.25f); // PolyBLEP : léger dépassement au plus
	}
	// Niveaux attendus : sinus 1/√2, carré ≈ 1.
	CHECK(std::abs(audio::Rms(Render(audio::Waveform::Sine, 440.f, 48000)) - 0.7071f) < 0.01f);
	CHECK(audio::Rms(Render(audio::Waveform::Square, 440.f, 48000)) > 0.95f);
	// Bruit : centré, couvre [-1, 1], et désactivé → silence.
	const std::vector<float> noise = Render(audio::Waveform::Noise, 0.f, 48000);
	double mean = 0.0;
	for (const float v : noise)
		mean += double(v);
	CHECK(std::abs(mean) / 48000.0 < 0.02);
	CHECK(audio::Peak(noise) > 0.99f);
	audio::Oscillator off;
	off.enabled = false;
	CHECK_EQ(off.Next(kRate), 0.f);
}

/// Niveau (dB) le plus fort entre deux harmoniques de `f0` : du repliement de spectre.
float WorstAlias(std::vector<float> s, float f0) {
	audio::ApplyWindow(s, audio::WindowFunction::Blackman);
	const std::vector<float> db = audio::MagnitudeDb(audio::Fft(s));
	const std::vector<float> freqs = audio::BinFrequencies(s.size(), kRate);
	float worst = -200.f;
	for (size_t i = 1; i < db.size(); ++i) {
		const float ratio = freqs[i] / f0;
		if (std::abs(ratio - std::round(ratio)) > 0.15f)
			worst = std::max(worst, db[i]);
	}
	return worst;
}

TEST(Oscillator_PolyBlepReducesAliasing) {
	// Dent de scie à 440 Hz : PolyBLEP contre la rampe naïve (2t − 1), même phase. (Une
	// fréquence qui divise 48 kHz replierait sur ses propres harmoniques : indiscernable.)
	const float f0 = 440.f;
	std::vector<float> naive(4096);
	for (size_t i = 0; i < naive.size(); ++i)
		naive[i] = 2.f * std::fmod(float(i) * f0 / kRate, 1.f) - 1.f;
	const float blep = WorstAlias(Render(audio::Waveform::Sawtooth, f0, 4096), f0);
	CHECK(blep < WorstAlias(naive, f0) - 15.f); // ≈ 23 dB de repliement en moins (mesuré)
}

TEST(Spectrum_FftPeakAndWindows) {
	// 3 kHz sur 1024 points à 48 kHz : case 64 exactement.
	std::vector<float> s = Sine(3000.f, 1024);
	const std::vector<audio::Complex> spectrum = audio::Fft(s);
	REQUIRE(spectrum.size() == 1024u);
	const std::vector<float> db = audio::MagnitudeDb(spectrum);
	REQUIRE(db.size() == 512u);
	size_t peak = 0;
	for (size_t i = 1; i < db.size(); ++i)
		if (db[i] > db[peak])
			peak = i;
	CHECK_EQ(peak, 64u);
	CHECK(std::abs(db[64]) < 0.5f); // amplitude 1 → 0 dB
	CHECK(db[200] < -100.f);
	CHECK(std::abs(audio::BinFrequencies(1024, kRate)[64] - 3000.f) < 0.01f);
	CHECK(audio::Fft(std::vector<float>(1000)).empty()); // pas une puissance de 2
	// Fenêtres : nulles aux bords (Hann, Blackman), 0,08 pour Hamming, intactes sinon.
	for (const auto &[fn, edge] : {std::pair{audio::WindowFunction::Hann, 0.f}, {audio::WindowFunction::Blackman, 0.f},
								  {audio::WindowFunction::Hamming, 0.08f}, {audio::WindowFunction::Rectangular, 1.f}}) {
		std::vector<float> ones(64, 1.f);
		audio::ApplyWindow(ones, fn);
		CHECK(std::abs(ones.front() - edge) < 1e-4f && std::abs(ones.back() - edge) < 1e-4f);
		CHECK(ones[32] > 0.95f);
	}
}

TEST(Levels_RmsPeakDb) {
	const std::vector<float> s = Sine(1000.f, 4800, 0.5f);
	CHECK(std::abs(audio::Rms(s) - 0.5f / std::sqrt(2.f)) < 0.005f);
	CHECK(std::abs(audio::Peak(s) - 0.5f) < 0.001f);
	CHECK(std::abs(audio::ToDb(0.5f) - (-6.0206f)) < 0.01f);
	CHECK_EQ(audio::ToDb(0.f), -120.f);
	CHECK_EQ(audio::Rms({}), 0.f);
}

TEST(Filters_BiQuadAttenuation) {
	const auto gain = [](audio::BiQuadKind kind, float cutoff, float freq) {
		audio::BiQuad filter;
		filter.Design(kind, kRate, cutoff);
		std::vector<float> s = Sine(freq, 9600);
		filter.Process(s);
		return audio::Rms(std::span(s).subspan(4800)) / 0.7071f; // régime établi
	};
	CHECK(gain(audio::BiQuadKind::LowPass, 1000.f, 100.f) > 0.95f);
	CHECK(gain(audio::BiQuadKind::LowPass, 1000.f, 10000.f) < 0.02f);
	CHECK(gain(audio::BiQuadKind::HighPass, 1000.f, 100.f) < 0.02f);
	CHECK(gain(audio::BiQuadKind::HighPass, 1000.f, 10000.f) > 0.95f);
	CHECK(std::abs(gain(audio::BiQuadKind::LowPass, 1000.f, 1000.f) - 0.7071f) < 0.03f); // −3 dB à la coupure
	CHECK(gain(audio::BiQuadKind::BandPass, 1000.f, 1000.f) > 0.9f);
}

TEST(Effects_SoftClipDelayRing) {
	CHECK_EQ(audio::SoftClip(0.5f), 0.5f); // transparent sous le seuil
	CHECK(audio::SoftClip(5.f) <= 1.f && audio::SoftClip(5.f) > 0.95f); // borné à 1
	CHECK_EQ(audio::SoftClip(-5.f), -audio::SoftClip(5.f));

	audio::DelayLine delay(100);
	delay.SetDelay(10);
	std::vector<float> impulse(40, 0.f);
	impulse[0] = 1.f;
	delay.Process(impulse);
	CHECK_EQ(impulse[10], 1.f); // impulsion retardée de 10 échantillons
	CHECK_EQ(impulse[0], 0.f);
	delay.Clear();
	delay.feedback = 0.5f;
	std::vector<float> echo(40, 0.f);
	echo[0] = 1.f;
	delay.Process(echo);
	CHECK_EQ(echo[20], 0.5f); // deuxième écho atténué de moitié

	audio::RingBuffer ring(8);
	const float a[] = {1, 2, 3};
	ring.Push(a);
	float last[5];
	ring.Latest(last);
	CHECK(last[0] == 0.f && last[1] == 0.f && last[2] == 1.f && last[4] == 3.f);
	const float b[] = {4, 5, 6, 7, 8, 9, 10};
	ring.Push(b);
	CHECK_EQ(ring.Size(), 8u);
	float tail[3];
	ring.Latest(tail);
	CHECK(tail[0] == 8.f && tail[2] == 10.f);
}
