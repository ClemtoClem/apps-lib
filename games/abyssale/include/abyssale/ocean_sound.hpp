#pragma once
/**
 * @file ocean_sound.hpp
 * @brief Ambiance sonore sous-marine générée en temps réel (aucun fichier audio).
 *
 * `OceanSynth` (pur, testable) reprend les couches du prototype web d'abyssal-garden :
 * - grondement : bruit brun filtré passe-bas (150 Hz), niveau modulé par deux LFO lents ;
 * - ressac : bruit brun en passe-bande (420 Hz), plus présent près de la surface ;
 * - courant : passe-bande dont la fréquence et le niveau suivent la vitesse du plongeur ;
 * - nappe : quintes empilées sur ré, chaque note étant une paire de sinus légèrement
 *   désaccordés qui battent lentement ;
 * - voix ponctuelles (`audio::VoicePool`) : rafales de bulles, carillons pentatoniques,
 *   phrases de baleine en glissandos ;
 * le tout dans une réverbération (`audio::Reverb`) puis un limiteur (`audio::Limiter`).
 * Le volume général se règle de 0 à 1 (fondu, sans clic). `OceanSound` pousse les
 * échantillons dans un `sdl3::AudioStream`.
 */
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <audio/dsp.hpp>
#include <audio/reverb.hpp>
#include <audio/synth.hpp>
#include <math/noise.hpp>

namespace abyssale {

/// État du jeu qui pilote le son.
struct SoundState {
	float swimSpeed = 0.f; ///< m/s
	float depth = 20.f;	   ///< m sous la surface
	int nearbyBubbles = 0; ///< bulles à quelques mètres du plongeur
	bool exhale = false;   ///< le plongeur expire (bouffée de bulles)
};

class OceanSynth {
public:
	explicit OceanSynth(float sampleRate = 48000.f, uint64_t seed = 0x0CEA);

	void SetState(const SoundState &state) noexcept;
	/// Volume général (0 : silence, 1 : niveau nominal du prototype).
	void SetVolume(float volume) noexcept { m_volume.SetTarget(std::clamp(volume, 0.f, 1.f)); }
	[[nodiscard]] float Volume() const noexcept { return m_volume.Target(); }
	/// Lance une phrase de baleine (sinon toutes les 20 à 50 s).
	void TriggerWhale() noexcept;
	/// Remplit `stereo` (échantillons entrelacés gauche, droite).
	void Render(std::span<float> stereo) noexcept;

	[[nodiscard]] float SampleRate() const noexcept { return m_rate; }

private:
	[[nodiscard]] float Random(float lo, float hi) noexcept { return lo + (hi - lo) * m_rng.NextFloat(); }
	void Schedule(float now) noexcept; ///< déclenche les voix dont l'heure est venue

	float m_rate;
	math::Rng m_rng;
	SoundState m_state;
	float m_time = 0.f;

	// Couches continues (bruits décorrélés gauche / droite).
	audio::Noise m_rumbleNoise[2], m_washNoise[2];
	audio::BiQuad m_rumbleFilter[2], m_washFilter[2], m_rushFilter[2];
	audio::Smoother m_washGain{0.4f, 0.5f}, m_rushGain{0.f, 0.3f}, m_rushFreq{220.f, 0.3f};
	std::array<float, 10> m_padPhase{};
	std::array<float, 10> m_padFreq{};

	// Voix ponctuelles.
	audio::VoicePool<4> m_bubbles{audio::Waveform::Sine};
	audio::VoicePool<3> m_chimes{audio::Waveform::Sine};
	audio::VoicePool<2> m_whale{audio::Waveform::Triangle};
	struct Pending {
		float at;
		audio::Note note;
		int pool; ///< 0 bulles, 1 carillons, 2 baleine
	};
	std::vector<Pending> m_pending;
	float m_nextBubbles = 1.5f, m_nextChime = 5.f, m_nextWhale = 12.f;

	audio::Reverb m_reverb;
	audio::Limiter m_limiter{0.5f, 0.3f}; // −6 dBFS
	audio::Smoother m_volume{0.6f, 0.08f};
	audio::Smoother m_fadeIn{0.f, 1.f}; // montée initiale d'une seconde
	std::vector<float> m_send;			///< bus de réverbération (mono) du bloc en cours
};

/// Sortie audio : garde ~80 ms d'avance dans le flux, produites par `OceanSynth`.
class OceanSound {
public:
	OceanSound();
	~OceanSound();
	OceanSound(const OceanSound &) = delete;
	OceanSound &operator=(const OceanSound &) = delete;

	/// Ouvre la sortie par défaut ; faux si aucun périphérique (le jeu reste silencieux).
	bool Open();
	void Update(const SoundState &state);
	void SetVolume(float volume);
	[[nodiscard]] float Volume() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace abyssale
