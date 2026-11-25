#pragma once
/**
 * @file reverb.hpp
 * @brief Réverbération de Schroeder (Freeverb) : 8 filtres en peigne amortis + 4 passe-tout,
 *        en mono. Pure (aucun rendu) — porte de la réverbération de `fyrox-sound` (effets).
 *
 *   audio::Reverb reverb(48000.f);
 *   reverb.SetRoomSize(0.7f); reverb.SetDamping(0.5f); reverb.SetMix(0.3f);
 *   reverb.Process(samples, n); // entrée et sortie se recouvrent
 */
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace audio {

/// Réverbération de Schroeder (Freeverb) mono. Les lignes de retard sont accordées pour
/// 44 100 Hz et mises à l'échelle de la fréquence d'échantillonnage.
class Reverb {
public:
	explicit Reverb(float sampleRate = 48000.f) : m_rate(sampleRate) { Rebuild(); }

	/// Humidité : part de signal réverbéré dans la sortie (0..1).
	void SetMix(float mix) noexcept { m_mix = std::clamp(mix, 0.f, 1.f); }
	/// Taille de la pièce (0..1) : règle le temps de décroissance.
	void SetRoomSize(float size) noexcept {
		m_roomSize = std::clamp(size, 0.f, 1.f);
		Rebuild();
	}
	/// Amortissement des aigus (0..1).
	void SetDamping(float damping) noexcept { m_damping = std::clamp(damping, 0.f, 1.f); }
	/// Remet les lignes de retard à zéro.
	void Reset() noexcept;

	/// Traite `n` échantillons mono (les buffers d'entrée et de sortie se recouvrent).
	void Process(float *samples, size_t n) noexcept;

private:
	void Rebuild() noexcept;

	static constexpr size_t kCombs = 8, kAllpasses = 4;
	float m_rate = 48000.f;
	float m_mix = 0.5f, m_roomSize = 0.5f, m_damping = 0.5f, m_feedback = 0.84f;
	std::array<std::vector<float>, kCombs> m_comb;
	std::array<float, kCombs> m_combFilter{};
	std::array<size_t, kCombs> m_combIndex{};
	std::array<std::vector<float>, kAllpasses> m_allpass;
	std::array<size_t, kAllpasses> m_allpassIndex{};
};

} // namespace audio
