#pragma once
/**
 * @file synth.hpp
 * @brief Briques de synthèse temps réel (sans entrée-sortie), à la manière d'un graphe Web Audio :
 *        bruits colorés, lissage de paramètres, enveloppes, voix à glissando, réservoir de voix,
 *        panoramique et limiteur. Tout est calculé échantillon par échantillon en `float`.
 *
 *   audio::VoicePool<4> bubbles;                     // 4 voix réutilisées à tour de rôle
 *   bubbles.Play({.from = 600.f, .to = 1100.f, .duration = 0.12f, .level = 0.05f, .attack = 0.006f, .pan = -0.3f});
 *   float l = 0.f, r = 0.f;
 *   bubbles.Mix(rate, l, r);                          // ajoute la contribution de l'échantillon
 */
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "audio/dsp.hpp"

namespace audio {

// ── Bruits colorés ──────────────────────────────────────────────────────────

enum class NoiseColor : uint8_t {
	White, ///< spectre plat
	Pink,  ///< −3 dB par octave (filtre de Kellett)
	Brown, ///< −6 dB par octave (intégrateur à fuite) : grondement, ressac
};

/// Générateur de bruit coloré, d'amplitude crête ≈ 1.
class Noise {
public:
	explicit Noise(NoiseColor color = NoiseColor::White, uint32_t seed = 0x9E3779B9u) noexcept : m_color(color), m_state(seed ? seed : 1u) {}
	[[nodiscard]] float Next() noexcept;
	[[nodiscard]] NoiseColor Color() const noexcept { return m_color; }

private:
	[[nodiscard]] float White() noexcept;
	NoiseColor m_color;
	uint32_t m_state;
	float m_b0 = 0.f, m_b1 = 0.f, m_b2 = 0.f, m_brown = 0.f;
};

// ── Paramètres et enveloppes ────────────────────────────────────────────────

/// Paramètre lissé : rejoint sa cible exponentiellement (constante de temps `tau`, en
/// secondes), comme `AudioParam.setTargetAtTime` — pas de clic aux changements brusques.
class Smoother {
public:
	explicit Smoother(float value = 0.f, float tau = 0.05f) noexcept : m_value(value), m_target(value), m_tau(tau) {}
	void SetTarget(float target) noexcept { m_target = target; }
	void SetTau(float tau) noexcept { m_tau = tau; }
	void Jump(float value) noexcept { m_value = m_target = value; }
	[[nodiscard]] float Next(float sampleRate) noexcept {
		const float k = m_tau > 0.f ? 1.f - std::exp(-1.f / (m_tau * sampleRate)) : 1.f;
		m_value += (m_target - m_value) * k;
		return m_value;
	}
	[[nodiscard]] float Value() const noexcept { return m_value; }
	[[nodiscard]] float Target() const noexcept { return m_target; }

private:
	float m_value, m_target, m_tau;
};

/// Enveloppe « percussive » : montée linéaire jusqu'à `level` en `attack` secondes, puis
/// extinction exponentielle de constante `release` (silence après ≈ 5 constantes).
class Envelope {
public:
	void Trigger(float level, float attack, float release) noexcept;
	[[nodiscard]] float Next(float sampleRate) noexcept;
	[[nodiscard]] bool Active() const noexcept { return m_stage != Stage::Idle; }

private:
	enum class Stage : uint8_t { Idle, Attack, Release };
	Stage m_stage = Stage::Idle;
	float m_value = 0.f, m_level = 0.f, m_attack = 0.f, m_release = 0.1f;
};

// ── Panoramique et dynamique ────────────────────────────────────────────────

/// Gains gauche / droite à puissance constante pour `pan` ∈ [−1, 1].
struct StereoGain {
	float left, right;
};
[[nodiscard]] StereoGain Pan(float pan) noexcept;

/// Limiteur crête stéréo : attaque quasi instantanée, relâchement lent ; le signal ne dépasse
/// jamais `threshold` en amplitude (équivalent d'un compresseur à fort taux).
class Limiter {
public:
	explicit Limiter(float threshold = 0.5f, float releaseSeconds = 0.3f) noexcept : m_threshold(threshold), m_release(releaseSeconds) {}
	void SetThreshold(float threshold) noexcept { m_threshold = threshold; }
	void SetRelease(float releaseSeconds) noexcept { m_release = releaseSeconds; }
	void Process(float &left, float &right, float sampleRate) noexcept;
	[[nodiscard]] float Gain() const noexcept { return m_gain; }

private:
	float m_threshold, m_release, m_gain = 1.f;
};

// ── Voix ────────────────────────────────────────────────────────────────────

/// Note jouée par une voix : glissando exponentiel `from` → `to` sur `duration`, enveloppe
/// montant à `level` en `attack` puis s'éteignant sur le reste de la durée.
struct Note {
	float from = 440.f, to = 440.f; ///< Hz
	float duration = 0.5f;			///< s
	float level = 0.1f;
	float attack = 0.01f; ///< s
	float pan = 0.f;	  ///< −1 … 1
	float send = 0.f;	  ///< part envoyée au bus d'effet (réverbération)
};

class Voice {
public:
	Waveform shape = Waveform::Sine;
	void Play(const Note &note) noexcept;
	/// Ajoute l'échantillon courant au mélange (sec gauche / droite, bus d'effet mono).
	void Mix(float sampleRate, float &left, float &right, float &send) noexcept;
	[[nodiscard]] bool Active() const noexcept { return m_env.Active(); }

private:
	Oscillator m_osc;
	Envelope m_env;
	Note m_note;
	float m_age = 0.f;
	StereoGain m_gain{0.7071f, 0.7071f};
};

/// Réservoir de voix réutilisées à tour de rôle (la plus ancienne est volée).
template <size_t N> class VoicePool {
public:
	explicit VoicePool(Waveform shape = Waveform::Sine) noexcept {
		for (Voice &v : m_voices)
			v.shape = shape;
	}
	void Play(const Note &note) noexcept {
		m_voices[m_next].Play(note);
		m_next = (m_next + 1) % N;
	}
	void Mix(float sampleRate, float &left, float &right, float &send) noexcept {
		for (Voice &v : m_voices)
			if (v.Active())
				v.Mix(sampleRate, left, right, send);
	}
	[[nodiscard]] size_t ActiveCount() const noexcept {
		size_t n = 0;
		for (const Voice &v : m_voices)
			n += v.Active() ? size_t{1} : size_t{0};
		return n;
	}

private:
	std::array<Voice, N> m_voices{};
	size_t m_next = 0;
};

} // namespace audio
