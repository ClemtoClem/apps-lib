#pragma once
/**
 * @file audio.hpp
 * @brief Audio (SDL_audio.h) : formats, périphériques, flux et lecture de WAV.
 */
#include <SDL3/SDL.h>
#include <span>
#include <vector>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// AudioFormat
// ============================================================================

/// Format d'échantillon audio.
enum class AudioFormat : Uint32 {
    UNKNOWN = SDL_AUDIO_UNKNOWN, ///< inconnu
    U8      = SDL_AUDIO_U8,	 ///< entier non signé 8 bits
    S8      = SDL_AUDIO_S8,	 ///< entier signé 8 bits
    S16     = SDL_AUDIO_S16,	 ///< entier signé 16 bits
    S32     = SDL_AUDIO_S32,	 ///< entier signé 32 bits
    F32     = SDL_AUDIO_F32,	 ///< flottant 32 bits
};

namespace detail {
    constexpr SDL_AudioFormat ToSDL(AudioFormat e) {
        return static_cast<SDL_AudioFormat>(e);
    }
}

// ============================================================================
// AudioSpec
// ============================================================================

/// Spécification d'un flux audio (format, canaux, fréquence).
struct AudioSpec {
    AudioFormat format = AudioFormat::F32; ///< format d'échantillon
    int channels = 2;					   ///< nombre de canaux
    int freq = 44100;					   ///< fréquence d'échantillonnage (Hz)

    /// Conversion vers le type SDL brut.
    [[nodiscard]] SDL_AudioSpec ToSDL() const { return {detail::ToSDL(format), channels, freq}; }

    /// Mono signé 16 bits.
    [[nodiscard]] static AudioSpec MonoS16(int hz = 44100) { return {AudioFormat::S16, 1, hz}; }
    /// Stéréo flottant 32 bits.
    [[nodiscard]] static AudioSpec StereoF32(int hz = 44100) { return {AudioFormat::F32, 2, hz}; }
    /// Stéréo signé 16 bits.
    [[nodiscard]] static AudioSpec StereoS16(int hz = 44100) { return {AudioFormat::S16, 2, hz}; }
};

// ============================================================================
// AudioDeviceInfo — entrée d'énumération (SDL_GetAudioPlaybackDevices/RecordingDevices)
// ============================================================================

/// Entrée d'énumération d'un périphérique audio.
struct AudioDeviceInfo {
    SDL_AudioDeviceID id = 0; ///< identifiant du périphérique
    String name;			  ///< nom lisible
};

/// Énumère les périphériques de lecture physiques (hors SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK).
[[nodiscard]] std::vector<AudioDeviceInfo> EnumeratePlaybackDevices();

/// Énumère les périphériques d'enregistrement physiques (hors SDL_AUDIO_DEVICE_DEFAULT_RECORDING).
[[nodiscard]] std::vector<AudioDeviceInfo> EnumerateRecordingDevices();

// ============================================================================
// AudioDevice — RAII SDL_OpenAudioDevice / SDL_CloseAudioDevice
// ============================================================================

/// Périphérique audio ouvert (RAII sur SDL_OpenAudioDevice / SDL_CloseAudioDevice).
class AudioDevice {
    SDL_AudioDeviceID m_id = 0;

public:
    AudioDevice() noexcept = default;
    /// Enveloppe un périphérique déjà ouvert (adopte la propriété).
    constexpr explicit AudioDevice(SDL_AudioDeviceID id) noexcept : m_id(id) {}

    /// Ferme le périphérique.
    ~AudioDevice();

    AudioDevice(const AudioDevice &) = delete;
    AudioDevice &operator=(const AudioDevice &) = delete;
    AudioDevice(AudioDevice &&o) noexcept : m_id(o.m_id) { o.m_id = 0; }
    AudioDevice &operator=(AudioDevice &&o) noexcept {
        if (this != &o) {
            Close();
            m_id = o.m_id;
            o.m_id = 0;
        }
        return *this;
    }

    /// Ouvre un périphérique audio (devid physique ou SDL_AUDIO_DEVICE_DEFAULT_*).
    [[nodiscard]] static Result<AudioDevice, Error> Open(SDL_AudioDeviceID devid, const AudioSpec &spec);
    /// Ouvre le périphérique de lecture par défaut.
    [[nodiscard]] static Result<AudioDevice, Error> OpenPlayback(const AudioSpec &spec);
    /// Ouvre le périphérique d'enregistrement par défaut.
    [[nodiscard]] static Result<AudioDevice, Error> OpenRecording(const AudioSpec &spec);

    /// Ferme le périphérique.
    void Close() noexcept;

    /// Met en pause la lecture/enregistrement.
    bool Pause() noexcept { return m_id != 0 && SDL_PauseAudioDevice(m_id); }
    /// Reprend la lecture/enregistrement.
    bool Resume() noexcept { return m_id != 0 && SDL_ResumeAudioDevice(m_id); }
    /// Vrai si le périphérique est en pause.
    [[nodiscard]] bool Paused() const noexcept { return m_id != 0 && SDL_AudioDevicePaused(m_id); }

    /// Fixe le gain (volume) du périphérique.
    bool SetGain(float gain) noexcept { return m_id != 0 && SDL_SetAudioDeviceGain(m_id, gain); }
    /// Gain courant.
    [[nodiscard]] float Gain() const noexcept { return m_id != 0 ? SDL_GetAudioDeviceGain(m_id) : 1.f; }

    /// Identifiant SDL du périphérique.
    [[nodiscard]] SDL_AudioDeviceID Get() const noexcept { return m_id; }
    /// Vrai si le périphérique est ouvert.
    [[nodiscard]] explicit operator bool() const noexcept { return m_id != 0; }
};

// ============================================================================
// AudioStream — RAII wrapper around SDL_AudioStream
// ============================================================================

/// Flux audio (RAII sur SDL_AudioStream).
class AudioStream : public Wrapper<SDL_AudioStream, SDL_DestroyAudioStream> {
public:
    using Wrapper::Wrapper;

    /// Ouvre un flux connecté au périphérique de lecture par défaut.
    [[nodiscard]] static Result<AudioStream, StringView> OpenPlayback(const AudioSpec &spec);

    /// Ouvre un flux connecté à un périphérique de lecture précis (de EnumeratePlaybackDevices()).
    [[nodiscard]] static Result<AudioStream, StringView> OpenPlayback(const AudioSpec &spec,
                                                                       SDL_AudioDeviceID deviceId);

    /// Ouvre un flux connecté au périphérique d'enregistrement par défaut.
    [[nodiscard]] static Result<AudioStream, StringView> OpenRecording(const AudioSpec &spec);

    /// Ouvre un flux connecté à un périphérique d'enregistrement précis (de EnumerateRecordingDevices()).
    [[nodiscard]] static Result<AudioStream, StringView> OpenRecording(const AudioSpec &spec,
                                                                        SDL_AudioDeviceID deviceId);

    /// Crée un flux autonome de conversion de format (non lié à un périphérique).
    [[nodiscard]] static Result<AudioStream, Error> Create(const AudioSpec &src, const AudioSpec &dst);

    // ── Data I/O ─────────────────────────────────────────────────────────────

    /// Envoie des données dans le flux.
    bool PutData(const void *data, int len) { return mHandle && SDL_PutAudioStreamData(mHandle, data, len); }
    /// Envoie un span d'échantillons dans le flux.
    template <typename T> bool PutData(std::span<const T> data) { return PutData(data.data(), int(data.size_bytes())); }

    /// Récupère des données converties du flux (nombre d'octets lus, ou -1).
    int GetData(void *data, int len) { return mHandle ? SDL_GetAudioStreamData(mHandle, data, len) : -1; }
    /// Récupère un span de données converties.
    template <typename T> int GetData(std::span<T> data) { return GetData(data.data(), int(data.size_bytes())); }

    /// Octets disponibles à la lecture.
    [[nodiscard]] int Available() const { return mHandle ? SDL_GetAudioStreamAvailable(mHandle) : 0; }

    /// Octets encore en file (émis mais non consommés) — pour réguler la latence d'un producteur.
    [[nodiscard]] int Queued() const { return mHandle ? SDL_GetAudioStreamQueued(mHandle) : 0; }

    /// Vide le flux (attend que tout soit consommé).
    bool Flush() { return mHandle && SDL_FlushAudioStream(mHandle); }
    /// Efface le contenu du flux.
    bool Clear() { return mHandle && SDL_ClearAudioStream(mHandle); }

    // ── Playback control ─────────────────────────────────────────────────────

    /// Reprend la lecture sur le périphérique lié.
    bool Resume() { return mHandle && SDL_ResumeAudioStreamDevice(mHandle); }
    /// Met en pause la lecture sur le périphérique lié.
    bool Pause() { return mHandle && SDL_PauseAudioStreamDevice(mHandle); }
    /// Fixe le gain du flux.
    bool SetGain(float g) { return mHandle && SDL_SetAudioStreamGain(mHandle, g); }

    /// Gain courant du flux.
    [[nodiscard]] float Gain() const { return mHandle ? SDL_GetAudioStreamGain(mHandle) : 1.f; }

    // ── Binding ──────────────────────────────────────────────────────────────

    /// Lie le flux à un périphérique.
    bool Bind(SDL_AudioDeviceID devid) { return mHandle && SDL_BindAudioStream(devid, mHandle); }
    /// Délie le flux de son périphérique.
    void Unbind();
};

// ============================================================================
// WavData — fichier WAV chargé (non-RAII ; possède le tampon d'échantillons)
// ============================================================================

/// Fichier WAV chargé en mémoire.
struct WavData {
    SDL_AudioSpec spec;			 ///< spécification audio
    std::vector<uint8_t> samples; ///< échantillons bruts

    /// Octets par trame (un échantillon par canal).
    [[nodiscard]] int FrameSize() const noexcept { return int(SDL_AUDIO_BYTESIZE(static_cast<unsigned>(spec.format))) * spec.channels; }
    /// Durée du son, en secondes (0 si le format est inconnu).
    [[nodiscard]] double DurationSeconds() const noexcept {
        const int frame = FrameSize();
        return frame > 0 && spec.freq > 0 ? double(samples.size()) / double(frame) / double(spec.freq) : 0.0;
    }
};

/// Charge un fichier WAV en mémoire.
[[nodiscard]] Result<WavData, StringView> LoadWav(const String &path);

} // namespace sdl3
