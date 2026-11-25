#include "audio/synth.hpp"

#include <algorithm>

namespace audio {

float Noise::White() noexcept {
	// xorshift32 → [−1, 1)
	m_state ^= m_state << 13;
	m_state ^= m_state >> 17;
	m_state ^= m_state << 5;
	return float(m_state) * (2.f / 4294967296.f) - 1.f;
}

float Noise::Next() noexcept {
	const float w = White();
	switch (m_color) {
	case NoiseColor::White: return w;
	case NoiseColor::Pink: // filtre économique de Paul Kellett
		m_b0 = 0.99765f * m_b0 + w * 0.0990460f;
		m_b1 = 0.96300f * m_b1 + w * 0.2965164f;
		m_b2 = 0.57000f * m_b2 + w * 1.0526913f;
		return std::clamp((m_b0 + m_b1 + m_b2 + w * 0.1848f) * 0.25f, -1.f, 1.f);
	case NoiseColor::Brown: // intégrateur à fuite (prototype Web Audio : (b + 0.02 w) / 1.02)
		m_brown = (m_brown + 0.02f * w) / 1.02f;
		return std::clamp(m_brown * 3.5f, -1.f, 1.f);
	}
	return w;
}

void Envelope::Trigger(float level, float attack, float release) noexcept {
	m_level = level;
	m_attack = std::max(attack, 1e-4f);
	m_release = std::max(release, 1e-3f);
	m_stage = Stage::Attack;
}

float Envelope::Next(float sampleRate) noexcept {
	switch (m_stage) {
	case Stage::Idle: return 0.f;
	case Stage::Attack:
		m_value += m_level / (m_attack * sampleRate);
		if (m_value >= m_level) {
			m_value = m_level;
			m_stage = Stage::Release;
		}
		return m_value;
	case Stage::Release:
		m_value *= std::exp(-1.f / (m_release * sampleRate));
		if (m_value < m_level * 1e-3f + 1e-6f) {
			m_value = 0.f;
			m_stage = Stage::Idle;
		}
		return m_value;
	}
	return 0.f;
}

StereoGain Pan(float pan) noexcept {
	const float a = (std::clamp(pan, -1.f, 1.f) + 1.f) * 0.25f * 3.14159265f;
	return {std::cos(a), std::sin(a)};
}

void Limiter::Process(float &left, float &right, float sampleRate) noexcept {
	const float peak = std::max(std::abs(left), std::abs(right));
	const float wanted = peak > m_threshold ? m_threshold / peak : 1.f;
	if (wanted < m_gain)
		m_gain = wanted; // attaque immédiate : aucune crête ne passe
	else
		m_gain += (wanted - m_gain) * (1.f - std::exp(-1.f / (m_release * sampleRate)));
	left *= m_gain;
	right *= m_gain;
}

void Voice::Play(const Note &note) noexcept {
	m_note = note;
	m_note.from = std::max(note.from, 1.f);
	m_note.to = std::max(note.to, 1.f);
	m_note.duration = std::max(note.duration, 1e-3f);
	m_age = 0.f;
	m_gain = Pan(note.pan);
	m_osc.shape = shape;
	m_osc.amplitude = 1.f;
	m_osc.frequency = m_note.from;
	// Extinction : la queue de l'enveloppe tient dans la durée restante (≈ 5 constantes).
	m_env.Trigger(note.level, note.attack, std::max(note.duration - note.attack, 1e-3f) / 5.f);
}

void Voice::Mix(float sampleRate, float &left, float &right, float &send) noexcept {
	const float t = std::min(m_age / m_note.duration, 1.f);
	m_osc.frequency = m_note.from * std::pow(m_note.to / m_note.from, t); // rampe exponentielle
	const float s = m_osc.Next(sampleRate) * m_env.Next(sampleRate);
	left += s * m_gain.left;
	right += s * m_gain.right;
	send += s * m_note.send;
	m_age += 1.f / sampleRate;
}

} // namespace audio
