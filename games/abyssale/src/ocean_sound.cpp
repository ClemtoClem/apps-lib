#include "abyssale/ocean_sound.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include <sdl3/input/audio.hpp>

#include "abyssale/config.hpp"

namespace abyssale {

namespace {

constexpr size_t kBlock = 512;	 ///< échantillons stéréo traités par bloc (bus de réverbération)
constexpr float kMaster = 0.78f; ///< niveau nominal du prototype (≈ −18 LUFS)
/// Gamme pentatonique de ré majeur, en demi-tons au-dessus du la 440.
constexpr std::array<int, 9> kNotes{0, 2, 5, 7, 9, 12, 14, 17, 19};
/// Nappe : quintes empilées sur ré.
constexpr std::array<float, 5> kPad{73.42f, 110.f, 164.81f, 246.94f, 369.99f};

} // namespace

OceanSynth::OceanSynth(float sampleRate, uint64_t seed)
	: m_rate(sampleRate), m_rng(seed),
	  m_rumbleNoise{audio::Noise(audio::NoiseColor::Brown, 11u), audio::Noise(audio::NoiseColor::Brown, 23u)},
	  m_washNoise{audio::Noise(audio::NoiseColor::Brown, 37u), audio::Noise(audio::NoiseColor::Brown, 53u)}, m_reverb(sampleRate) {
	for (int c = 0; c < 2; ++c) {
		m_rumbleFilter[c].Design(audio::BiQuadKind::LowPass, m_rate, 150.f, 0.7f);
		m_washFilter[c].Design(audio::BiQuadKind::BandPass, m_rate, 420.f, 0.5f);
		m_rushFilter[c].Design(audio::BiQuadKind::BandPass, m_rate, 220.f, 0.9f);
	}
	for (size_t i = 0; i < kPad.size(); ++i) { // chaque note : un sinus et son double désaccordé
		m_padFreq[i * 2] = kPad[i];
		m_padFreq[i * 2 + 1] = kPad[i] + Random(0.03f, 0.11f);
	}
	m_reverb.SetRoomSize(0.92f);
	m_reverb.SetDamping(0.6f);
	m_reverb.SetMix(1.f); // bus entièrement réverbéré (dosé au mélange)
	m_send.resize(kBlock);
	m_fadeIn.SetTarget(1.f);
}

void OceanSynth::SetState(const SoundState &state) noexcept {
	if (state.exhale && !m_state.exhale) { // bouffée du plongeur : rafale serrée de bulles
		float t = m_time + 0.05f;
		for (int i = 0; i < 6 + int(Random(0.f, 4.f)); ++i) {
			const float f = Random(420.f, 1300.f);
			m_pending.push_back({t, {f, f * Random(1.4f, 2.2f), 0.05f + 40.f / f, Random(0.04f, 0.09f), 0.006f, Random(-0.3f, 0.3f), 0.12f}, 0});
			t += Random(0.03f, 0.09f);
		}
	}
	m_state = state;
}

void OceanSynth::TriggerWhale() noexcept {
	float t = m_time;
	for (int n = 2 + int(Random(0.f, 2.f)); n > 0; --n) { // phrase de 2 ou 3 glissandos lents
		const float f = Random(170.f, 380.f), d = Random(1.6f, 3.2f);
		m_pending.push_back({t, {f, f * Random(0.6f, 1.7f), d, 0.08f, d * 0.4f, Random(-0.7f, 0.7f), 2.f}, 2});
		t += d * 0.8f;
	}
	m_nextWhale = t + Random(20.f, 50.f);
}

void OceanSynth::Schedule(float now) noexcept {
	if (now >= m_nextBubbles) { // 1 à 6 bulles : petites notes montantes, les petites plus aiguës et brèves
		float t = now;
		const int n = 1 + int(Random(0.f, 1.f) * Random(0.f, 1.f) * 6.f);
		for (int i = 0; i < n; ++i) {
			const float f = Random(380.f, 1300.f);
			m_pending.push_back({t, {f, f * Random(1.4f, 2.2f), 0.05f + 40.f / f, Random(0.03f, 0.08f), 0.006f, Random(-0.7f, 0.7f), 0.12f}, 0});
			t += Random(0.07f, 0.2f);
		}
		// Plus de bulles autour du plongeur : rafales plus fréquentes.
		const float busy = 1.f + 0.3f * float(std::min(m_state.nearbyBubbles, 10));
		m_nextBubbles = t + Random(0.5f, 5.f) / busy;
	}
	if (now >= m_nextChime) {
		const float f = 440.f * std::pow(2.f, float(kNotes[size_t(Random(0.f, 8.999f))]) / 12.f);
		m_pending.push_back({now, {f, f, 5.f, Random(0.02f, 0.04f), 0.03f, Random(-0.7f, 0.7f), 1.2f}, 1});
		m_nextChime = now + Random(2.5f, 9.f);
	}
	if (now >= m_nextWhale)
		TriggerWhale();
	// Voix dont l'heure est venue.
	for (size_t i = 0; i < m_pending.size();) {
		if (m_pending[i].at > now) {
			++i;
			continue;
		}
		const Pending p = m_pending[i];
		m_pending[i] = m_pending.back();
		m_pending.pop_back();
		if (p.pool == 0)
			m_bubbles.Play(p.note);
		else if (p.pool == 1)
			m_chimes.Play(p.note);
		else
			m_whale.Play(p.note);
	}
}

void OceanSynth::Render(std::span<float> stereo) noexcept {
	const float dt = 1.f / m_rate;
	const size_t frames = stereo.size() / 2;
	for (size_t start = 0; start < frames; start += kBlock) {
		const size_t n = std::min(kBlock, frames - start);
		Schedule(m_time);
		// Paramètres pilotés par le jeu (lissés).
		const float s = std::clamp(1.f - m_state.depth / 30.f, 0.f, 1.f); // proche de la surface
		m_washGain.SetTarget(0.25f + 0.55f * s * s);
		const float v = std::min(m_state.swimSpeed, 30.f);
		m_rushGain.SetTarget(0.5f * std::pow(std::min(v / 9.f, 1.f), 1.2f));
		m_rushFreq.SetTarget(220.f + v * 40.f);
		const float rushF = m_rushFreq.Next(m_rate / float(n)); // une étape par bloc
		for (auto &f : m_rushFilter)
			f.Design(audio::BiQuadKind::BandPass, m_rate, rushF, 0.9f);

		for (size_t i = 0; i < n; ++i) {
			const float t = m_time;
			// Grondement : niveau 0,34 modulé par deux LFO lents.
			const float rumbleGain = 0.34f + 0.11f * std::sin(t * 0.07f * kTau) + 0.08f * std::sin(t * 0.113f * kTau);
			// Ressac : deux LFO sur l'onde, niveau selon la profondeur.
			const float waveGain = 1.f + 0.3f * std::sin(t * 0.09f * kTau) + 0.25f * std::sin(t * 0.151f * kTau);
			const float wash = m_washGain.Next(m_rate) * waveGain;
			const float rush = m_rushGain.Next(m_rate);
			float out[2];
			for (int c = 0; c < 2; ++c) {
				const float rn = m_rumbleNoise[c].Next(), wn = m_washNoise[c].Next();
				out[c] = m_rumbleFilter[c].Step(rn) * rumbleGain + m_washFilter[c].Step(wn) * wash + m_rushFilter[c].Step(wn) * rush;
			}
			// Nappe : sinus désaccordés par paires (battements lents).
			float pad = 0.f;
			for (size_t k = 0; k < m_padFreq.size(); ++k) {
				m_padPhase[k] += m_padFreq[k] * dt;
				m_padPhase[k] -= std::floor(m_padPhase[k]);
				pad += std::sin(m_padPhase[k] * kTau) * (0.022f - float(k / 2) * 0.003f);
			}
			float l = out[0] + pad, r = out[1] + pad, send = pad * 1.5f;
			m_bubbles.Mix(m_rate, l, r, send);
			m_chimes.Mix(m_rate, l, r, send);
			m_whale.Mix(m_rate, l, r, send);
			stereo[(start + i) * 2] = l;
			stereo[(start + i) * 2 + 1] = r;
			m_send[i] = send;
			m_time += dt;
		}
		m_reverb.Process(m_send.data(), n);
		for (size_t i = 0; i < n; ++i) {
			const float gain = kMaster * m_volume.Next(m_rate) * m_fadeIn.Next(m_rate);
			float l = (stereo[(start + i) * 2] + m_send[i] * 0.5f) * gain;
			float r = (stereo[(start + i) * 2 + 1] + m_send[i] * 0.5f) * gain;
			m_limiter.Process(l, r, m_rate);
			stereo[(start + i) * 2] = l;
			stereo[(start + i) * 2 + 1] = r;
		}
	}
}

// ── Sortie ────────────────────────────────────────────────────────────────────

struct OceanSound::Impl {
	static constexpr int kRate = 48000;
	OceanSynth synth{float(kRate)};
	std::optional<sdl3::AudioStream> stream;
	std::vector<float> buffer;
};

OceanSound::OceanSound() : m_impl(std::make_unique<Impl>()) {}
OceanSound::~OceanSound() = default;

bool OceanSound::Open() {
	const sdl3::AudioSpec spec{sdl3::AudioFormat::F32, 2, Impl::kRate};
	auto stream = sdl3::AudioStream::OpenPlayback(spec);
	if (stream.IsError())
		return false;
	m_impl->stream.emplace(std::move(stream.Value()));
	m_impl->stream->Resume();
	return true;
}

void OceanSound::SetVolume(float volume) { m_impl->synth.SetVolume(volume); }
float OceanSound::Volume() const noexcept { return m_impl->synth.Volume(); }

void OceanSound::Update(const SoundState &state) {
	Impl &s = *m_impl;
	s.synth.SetState(state);
	if (!s.stream)
		return;
	constexpr int kLeadBytes = int(0.08f * float(Impl::kRate)) * 2 * int(sizeof(float));
	for (int guard = 0; guard < 16 && s.stream->Queued() < kLeadBytes; ++guard) {
		s.buffer.resize(kBlock * 2);
		s.synth.Render(s.buffer);
		(void)s.stream->PutData(std::span<const float>(s.buffer));
	}
}

} // namespace abyssale
