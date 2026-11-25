#pragma once
/**
 * @file sound.hpp
 * @brief Sons du mode Jeu : voix décodées en flux (`sdl3::AudioDecoder`, SDL3_mixer), mélangées
 *        en stéréo et envoyées à une `sdl3::AudioStream`. Une voix spatiale est atténuée avec la
 *        distance et répartie entre gauche et droite selon sa direction vue par l'auditeur.
 *
 * Sans périphérique audio (`Open` faux), les voix sont acceptées mais restent muettes : le jeu
 * et ses tests fonctionnent de la même façon.
 */
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <math/math.hpp>

#include <audio/reverb.hpp>

namespace sdl3 {
class AudioDecoder;
class AudioStream;
class MixerContext;
} // namespace sdl3

namespace sandbox::game {

class SoundSystem {
public:
	explicit SoundSystem(std::filesystem::path folder = "assets/sounds");
	~SoundSystem();
	SoundSystem(const SoundSystem &) = delete;
	SoundSystem &operator=(const SoundSystem &) = delete;

	/// Ouvre la sortie (SDL audio initialisé) ; faux si indisponible.
	bool Open();
	[[nodiscard]] bool IsOpen() const noexcept { return m_stream != nullptr; }
	/// Fichiers jouables du dossier (ogg, mp3, wav, flac), triés.
	[[nodiscard]] std::vector<std::string> Files() const;

	/// Lance `file` ; `position` : voix spatiale. Rend l'identifiant de la voix (0 : échec).
	int Play(const std::string &file, float volume, bool loop, std::optional<math::FVector3> position = std::nullopt);
	void Stop(int voice);
	void StopAll();
	[[nodiscard]] bool IsPlaying(int voice) const;
	void SetPosition(int voice, const math::FVector3 &position);
	void SetVolume(int voice, float volume);
	/// Vitesse de lecture (1 = normale, 0,5 = une octave plus bas) — rééchantillonnage linéaire.
	void SetPitch(int voice, float pitch);
	/// Met en pause / reprend une voix (les voix en pause ne consomment pas de données).
	void SetPause(int voice, bool paused);
	[[nodiscard]] bool IsPaused(int voice) const;
	/// Courbe de distance de la voix spatiale : pleine intensité jusqu'à `radius` mètres,
	/// puis décroissance `1 / (1 + rolloff · (distance − radius))`.
	void SetDistance(int voice, float radius, float rolloff);
	void SetListener(const math::FVector3 &position, const math::FVector3 &right) noexcept;
	/// Réverbération globale (Freeverb) : `mix` humidité (0 = désactivée), `roomSize` et `damping` (0..1).
	void SetReverb(float mix, float roomSize, float damping);
	/// Remplit la file de sortie (à appeler à chaque image).
	void Update();
	[[nodiscard]] size_t VoiceCount() const noexcept { return m_voices.size(); }

	/// Gains gauche / droite d'une voix spatiale (atténuation et répartition), exposés pour les tests.
	[[nodiscard]] static std::pair<float, float> SpatialGains(const math::FVector3 &listener, const math::FVector3 &right, const math::FVector3 &source,
															  float volume, float radius = 2.f, float rolloff = 0.25f) noexcept;

private:
	struct Voice;
	bool Restart(Voice &voice) const;
	bool DecodeBlock(Voice &voice) const;

	std::filesystem::path m_folder;
	std::unique_ptr<sdl3::MixerContext> m_mixer;
	std::unique_ptr<sdl3::AudioStream> m_stream;
	std::vector<std::unique_ptr<Voice>> m_voices;
	math::FVector3 m_listener{}, m_right{1.f, 0.f, 0.f};
	audio::Reverb m_reverb;
	float m_reverbMix = 0.f;
	int m_nextId = 1;
};

} // namespace sandbox::game
