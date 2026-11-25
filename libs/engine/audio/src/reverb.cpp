#include "audio/reverb.hpp"

namespace audio {

namespace {

/// Longueurs de retard (échantillons à 44 100 Hz), accordées par Schroeder.
constexpr float kCombTuning[8] = {1116.f, 1188.f, 1277.f, 1356.f, 1422.f, 1491.f, 1557.f, 1617.f};
constexpr float kAllpassTuning[4] = {556.f, 441.f, 341.f, 225.f};

} // namespace

void Reverb::Rebuild() noexcept {
	// Retour des peignes : 0.7 (petite pièce) à 0.98 (grande pièce).
	m_feedback = 0.7f + m_roomSize * 0.28f;
	for (size_t i = 0; i < kCombs; ++i) {
		const size_t len = std::max<size_t>(2, size_t(kCombTuning[i] * m_rate / 44100.f));
		m_comb[i].assign(len, 0.f);
		m_combIndex[i] = 0;
		m_combFilter[i] = 0.f;
	}
	for (size_t i = 0; i < kAllpasses; ++i) {
		const size_t len = std::max<size_t>(2, size_t(kAllpassTuning[i] * m_rate / 44100.f));
		m_allpass[i].assign(len, 0.f);
		m_allpassIndex[i] = 0;
	}
}

void Reverb::Reset() noexcept {
	for (auto &buf : m_comb)
		std::fill(buf.begin(), buf.end(), 0.f);
	for (auto &buf : m_allpass)
		std::fill(buf.begin(), buf.end(), 0.f);
	m_combFilter.fill(0.f);
}

void Reverb::Process(float *samples, size_t n) noexcept {
	const float damp1 = m_damping, damp2 = 1.f - m_damping;
	for (size_t s = 0; s < n; ++s) {
		const float input = samples[s];
		float wet = 0.f;
		// 8 filtres en peigne (retour filtré passe-bas).
		for (size_t i = 0; i < kCombs; ++i) {
			const size_t idx = m_combIndex[i];
			const float out = m_comb[i][idx];
			m_combFilter[i] = out * damp2 + m_combFilter[i] * damp1;
			m_comb[i][idx] = input + m_combFilter[i] * m_feedback;
			if (++m_combIndex[i] >= m_comb[i].size())
				m_combIndex[i] = 0;
			wet += out;
		}
		// 4 passe-tout (diffusion).
		for (size_t i = 0; i < kAllpasses; ++i) {
			const size_t idx = m_allpassIndex[i];
			const float bufout = m_allpass[i][idx];
			const float in = wet; // entrée du passe-tout
			wet = -in + bufout;
			m_allpass[i][idx] = in + bufout * 0.5f;
			if (++m_allpassIndex[i] >= m_allpass[i].size())
				m_allpassIndex[i] = 0;
		}
		samples[s] = input * (1.f - m_mix) + wet * m_mix;
	}
}

} // namespace audio
