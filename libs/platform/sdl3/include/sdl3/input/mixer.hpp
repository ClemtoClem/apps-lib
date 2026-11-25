#pragma once
#include <SDL3_mixer/SDL_mixer.h>

#include <core/core.hpp>
#include "core/wrapper.hpp"
#include "sdl3/input/audio.hpp"
#include "sdl3/io/iostream.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

/// Identifiant d'un périphérique audio.
using AudioDeviceID = SDL_AudioDeviceID;

/// SDL3_mixer version number compiled into the library (MIX_Version).
[[nodiscard]] inline int MixerVersion() { return MIX_Version(); }

/// Number of audio decoders compiled into SDL3_mixer.
[[nodiscard]] inline int MixAudioDecoders() { return MIX_GetNumAudioDecoders(); }

/// Name of decoder `index` (empty if out of range).
[[nodiscard]] inline StringView MixAudioDecoderName(int index) { return StringView(MIX_GetAudioDecoder(index)); }

/// Convert a duration between sample frames and milliseconds for a given sample rate.
[[nodiscard]] inline Sint64 MixFramesToMs(int sampleRate, Sint64 frames) { return MIX_FramesToMS(sampleRate, frames); }
[[nodiscard]] inline Sint64 MixMsToFrames(int sampleRate, Sint64 ms) { return MIX_MSToFrames(sampleRate, ms); }

// Forward declarations
class Mixer;
class MixAudio;
class MixTrack;
class MixGroup;

/// Gains gauche/droite d'une sortie stéréo forcée (miroir de MIX_StereoGains).
/// Gains stéréo (gauche/droite) d'une piste.
struct MixStereoGains {
	float left = 1.f;
	float right = 1.f;

	[[nodiscard]] constexpr operator MIX_StereoGains() const { return {left, right}; }
};

// ============================================================================
// MixGroup — RAII MIX_Group (groupe de pistes partageant un contrôle commun)
// ============================================================================

class MixGroup : public Wrapper<MIX_Group, MIX_DestroyGroup> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] SDL_PropertiesID Properties() const { return mHandle ? MIX_GetGroupProperties(mHandle) : 0; }
	[[nodiscard]] MIX_Mixer *Mixer() const { return mHandle ? MIX_GetGroupMixer(mHandle) : nullptr; }

	/// Callback de post-mixage du groupe (forme brute : l'appelant gère la durée de vie).
	bool SetPostMixCallback(MIX_GroupMixCallback cb, void *userdata) {
		return mHandle && MIX_SetGroupPostMixCallback(mHandle, cb, userdata);
	}
};

// ============================================================================
// MixerContext — RAII MIX_Init / MIX_Quit
// ============================================================================

/// Contexte SDL3_mixer (RAII sur MIX_Init/MIX_Quit).
class MixerContext {
    bool m_owns = false;

public:
    MixerContext() = default;
    ~MixerContext();

    MixerContext(const MixerContext &) = delete;
    MixerContext &operator=(const MixerContext &) = delete;
    MixerContext(MixerContext &&o) noexcept : m_owns(o.m_owns) { o.m_owns = false; }
    MixerContext &operator=(MixerContext &&o) noexcept {
        if (this != &o) {
            if (m_owns)
                MIX_Quit();
            m_owns = o.m_owns;
            o.m_owns = false;
        }
        return *this;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return m_owns; }

    [[nodiscard]] static Result<MixerContext, Error> Create();
};

// ============================================================================
// AudioDecoder — RAII MIX_AudioDecoder: streaming decode-to-buffer, any format
// SDL3_mixer supports (WAV/OGG/FLAC/MP3/...). Unlike MixAudio+MixTrack (which
// hand playback straight to a Mixer), this hands back raw PCM the caller pulls
// on demand — the primitive for feeding decoded audio INTO a custom DSP graph
// instead of straight to the speakers.
// ============================================================================

class AudioDecoder : public Wrapper<MIX_AudioDecoder, MIX_DestroyAudioDecoder> {
public:
    using Wrapper::Wrapper;

    /// Open a decoder for a file on disk. `props` may be SDL_PropertiesID{0} for defaults.
    [[nodiscard]] static Result<AudioDecoder, Error> Create(const String &path, SDL_PropertiesID props = 0);

    /// Open a decoder from an IO stream. `closeio = true` transfers ownership of `src` to SDL.
    [[nodiscard]] static Result<AudioDecoder, Error> CreateIO(IOStream &src, bool closeio = false,
                                                              SDL_PropertiesID props = 0);

    /// Query the format the decoder will produce (may not match the source file's format).
    [[nodiscard]] bool GetFormat(SDL_AudioSpec &outSpec) const;

    /// Decode up to `buflen` bytes into `buffer`, converted to `spec` if given (nullptr = decoder's native
    /// format). Returns the number of bytes actually written, 0 at end-of-stream, -1 on error.
    [[nodiscard]] int Decode(void *buffer, int buflen, const SDL_AudioSpec *spec = nullptr);

    /// Décode ENTIÈREMENT un fichier son (WAV, OGG, MP3, FLAC… selon les
    /// décodeurs de SDL3_mixer) en PCM, dans le format natif du décodeur.
    /// Sûr hors du fil principal une fois MixerContext créé.
    [[nodiscard]] static Result<WavData, StringView> DecodeFile(const String &path);

    /// Propriétés du décodeur (SDL_PropertiesID).
    [[nodiscard]] SDL_PropertiesID Properties() const {
        return mHandle ? MIX_GetAudioDecoderProperties(mHandle) : 0;
    }
};

// ============================================================================
// MixTrack — RAII MIX_Track (one playback channel)
// ============================================================================

class MixTrack : public Wrapper<MIX_Track, MIX_DestroyTrack> {
public:
    using Wrapper::Wrapper;

    bool SetAudio(MixAudio &audio);

    /// Feed this track from a live SDL_AudioStream (e.g. a synthesized/streamed
    /// PCM source) instead of a decoded MIX_Audio file. The stream is polled by
    /// the mixer as it plays; keep pushing data into it via AudioStream::PutData().
    bool SetAudioStream(AudioStream &stream) { return mHandle && MIX_SetTrackAudioStream(mHandle, stream.Get()); }

    bool SetLoops(int loops) { return mHandle && MIX_SetTrackLoops(mHandle, loops); }

    /// Play the track (loops = 0 → play once, -1 → loop forever).
    bool Play(int loops = 0);

    /// Starts playback for a track fed from a live/incremental SDL_AudioStream
    /// (set via setAudioStream()) — e.g. synthesized audio pushed in as it's
    /// generated, rather than a complete clip. MIX_PlayTrack's default
    /// (MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN = true) would otherwise mark
    /// the track stopped the instant the mixer finds the stream momentarily
    /// empty (almost immediately, since the producer hasn't caught up yet) —
    /// this sets that property to false so the mixer just contributes silence
    /// until more data arrives instead of halting for good. Loop count doesn't
    /// apply to AudioStream inputs, so there's no `loops` parameter here.
    bool PlayStreaming();

    /// Stop the track, optionally with a fade-out over `fadeFrames` sample frames.
    bool Stop(Sint64 fadeFrames = 0) { return mHandle && MIX_StopTrack(mHandle, fadeFrames); }

    bool Pause() { return mHandle && MIX_PauseTrack(mHandle); }
    bool Resume() { return mHandle && MIX_ResumeTrack(mHandle); }

    bool SetGain(float gain) { return mHandle && MIX_SetTrackGain(mHandle, gain); }
    [[nodiscard]] float Gain() const { return mHandle ? MIX_GetTrackGain(mHandle) : 1.f; }

    bool SetFrequencyRatio(float ratio) { return mHandle && MIX_SetTrackFrequencyRatio(mHandle, ratio); }
    [[nodiscard]] float FrequencyRatio() const { return mHandle ? MIX_GetTrackFrequencyRatio(mHandle) : 1.f; }

    [[nodiscard]] bool IsPlaying() const { return mHandle && MIX_TrackPlaying(mHandle); }
    [[nodiscard]] bool IsPaused() const { return mHandle && MIX_TrackPaused(mHandle); }

    [[nodiscard]] Sint64 Remaining() const { return mHandle ? MIX_GetTrackRemaining(mHandle) : 0; }
    [[nodiscard]] Sint64 GetPosition() const { return mHandle ? MIX_GetTrackPlaybackPosition(mHandle) : 0; }

    /// Seek the track to a sample-frame position.
    bool SetPlaybackPosition(Sint64 frames) { return mHandle && MIX_SetTrackPlaybackPosition(mHandle, frames); }
    [[nodiscard]] int Loops() const { return mHandle ? MIX_GetTrackLoops(mHandle) : 0; }
    /// Remaining sample frames of an active fade (0 = no fade in progress).
    [[nodiscard]] Sint64 FadeFrames() const { return mHandle ? MIX_GetTrackFadeFrames(mHandle) : 0; }

    /// Position 3D actuelle (NONE si le son n'est pas spatialisé).
    [[nodiscard]] Option<MIX_Point3D> Get3DPosition() const {
        if (!mHandle) return NONE;
        MIX_Point3D position{};
        if (!MIX_GetTrack3DPosition(mHandle, &position)) return NONE;
        return Some(position);
    }

    /// Convert a duration between sample frames and milliseconds for this track.
    [[nodiscard]] Sint64 FramesToMs(Sint64 frames) const { return mHandle ? MIX_TrackFramesToMS(mHandle, frames) : 0; }
    [[nodiscard]] Sint64 MsToFrames(Sint64 ms) const { return mHandle ? MIX_TrackMSToFrames(mHandle, ms) : 0; }

    /// Position 3D de la source (repère de l'auditeur : x à droite, y en haut,
    /// z vers l'arrière) — SDL3_mixer en déduit panoramique et atténuation.
    bool Set3DPosition(float x, float y, float z);
    /// Retire la position 3D (son « dans la tête », non spatialisé).
    bool Clear3DPosition();

    bool Tag(const String &t) { return mHandle && MIX_TagTrack(mHandle, t.c_str()); }
    void Untag(const String &t);

    // ── Groupe / source / sortie ──────────────────────────────────────────────

    bool SetGroup(MixGroup &group) { return mHandle && MIX_SetTrackGroup(mHandle, group.Get()); }

    /// Alimente la piste depuis un flux (fichier décodé à la volée).
    bool SetIOStream(IOStream &src, bool closeio) {
        return mHandle && MIX_SetTrackIOStream(mHandle, src.Get(), closeio);
    }
    /// Alimente la piste depuis un flux PCM brut (format imposé).
    bool SetRawIOStream(IOStream &src, const SDL_AudioSpec &spec, bool closeio) {
        return mHandle && MIX_SetTrackRawIOStream(mHandle, src.Get(), &spec, closeio);
    }
    /// Force une sortie stéréo avec gains gauche/droite (panoramique).
    bool SetStereo(const MixStereoGains &gains) {
        MIX_StereoGains g = gains;
        return mHandle && MIX_SetTrackStereo(mHandle, &g);
    }
    /// Réordonne les canaux de sortie (carte canaux SDL).
    bool SetOutputChannelMap(std::span<const int> map) {
        return mHandle && MIX_SetTrackOutputChannelMap(mHandle, map.data(), int(map.size()));
    }

    [[nodiscard]] MIX_Mixer *GetMixer() const { return mHandle ? MIX_GetTrackMixer(mHandle) : nullptr; }
    [[nodiscard]] MIX_Audio *GetAudio() const { return mHandle ? MIX_GetTrackAudio(mHandle) : nullptr; }
    [[nodiscard]] SDL_AudioStream *GetAudioStream() const { return mHandle ? MIX_GetTrackAudioStream(mHandle) : nullptr; }
    [[nodiscard]] SDL_PropertiesID Properties() const { return mHandle ? MIX_GetTrackProperties(mHandle) : 0; }

    /// Étiquettes (tags) associées à la piste.
    [[nodiscard]] std::vector<String> Tags() const;

    // ── Callbacks (forme brute : l'appelant gère la durée de vie du userdata) ─

    bool SetCookedCallback(MIX_TrackMixCallback cb, void *userdata) {
        return mHandle && MIX_SetTrackCookedCallback(mHandle, cb, userdata);
    }
    bool SetRawCallback(MIX_TrackMixCallback cb, void *userdata) {
        return mHandle && MIX_SetTrackRawCallback(mHandle, cb, userdata);
    }
    bool SetStoppedCallback(MIX_TrackStoppedCallback cb, void *userdata) {
        return mHandle && MIX_SetTrackStoppedCallback(mHandle, cb, userdata);
    }
};

// ============================================================================
// MixAudio — RAII MIX_Audio (loaded audio data, shared between mixers)
// ============================================================================

class MixAudio : public Wrapper<MIX_Audio, MIX_DestroyAudio> {
public:
    using Wrapper::Wrapper;

    /// Load an audio file from disk. `predecode = true` decodes immediately.
    [[nodiscard]] static Result<MixAudio, StringView> Load(Mixer &mixer, const String &path, bool predecode = false);

    /// Audio à partir d'échantillons PCM DÉJÀ décodés (copiés) — p. ex. un
    /// son chargé en arrière-plan par audio::AudioClipResource.
    [[nodiscard]] static Result<MixAudio, StringView> FromPcm(Mixer &mixer, const void *data, size_t size,
                                                             const SDL_AudioSpec &spec);

    [[nodiscard]] Sint64 Duration() const { return mHandle ? MIX_GetAudioDuration(mHandle) : 0; }

    /// Convert a duration between sample frames and milliseconds for this audio.
    [[nodiscard]] Sint64 FramesToMs(Sint64 frames) const { return mHandle ? MIX_AudioFramesToMS(mHandle, frames) : 0; }
    [[nodiscard]] Sint64 MsToFrames(Sint64 ms) const { return mHandle ? MIX_AudioMSToFrames(mHandle, ms) : 0; }

    [[nodiscard]] bool GetFormat(SDL_AudioSpec &outSpec) const { return mHandle && MIX_GetAudioFormat(mHandle, &outSpec); }
    [[nodiscard]] SDL_PropertiesID Properties() const { return mHandle ? MIX_GetAudioProperties(mHandle) : 0; }

    // ── Chargement avancé ────────────────────────────────────────────────────

    /// Charge via un groupe de propriétés (MIX_PROP_AUDIO_*).
    [[nodiscard]] static Result<MixAudio, StringView> LoadWithProperties(SDL_PropertiesID props);

    /// Charge depuis un flux (fichier décodé).
    [[nodiscard]] static Result<MixAudio, StringView> LoadIO(Mixer &mixer, IOStream &src, bool predecode, bool closeio);

    /// Charge depuis un tampon PCM DÉJÀ décodé, SANS copie (SDL en prend possession si free_when_done).
    [[nodiscard]] static Result<MixAudio, StringView> LoadNoCopy(Mixer &mixer, const void *data, size_t size,
                                                                 bool freeWhenDone);

    /// Charge depuis un flux PCM brut (format imposé).
    [[nodiscard]] static Result<MixAudio, StringView> LoadRawIO(Mixer &mixer, IOStream &src, const SDL_AudioSpec &spec,
                                                                bool closeio);

    /// Charge depuis un tampon PCM brut, sans copie.
    [[nodiscard]] static Result<MixAudio, StringView> LoadRawNoCopy(Mixer &mixer, const void *data, size_t size,
                                                                    const SDL_AudioSpec &spec, bool freeWhenDone);

    /// Onde sinusoïdale pure (durée `ms` en millisecondes).
    [[nodiscard]] static Result<MixAudio, StringView> CreateSineWave(Mixer &mixer, int hz, float amplitude, Sint64 ms);
};

// ============================================================================
// Mixer — RAII MIX_Mixer
// ============================================================================

class Mixer : public Wrapper<MIX_Mixer, MIX_DestroyMixer> {
public:
    using Wrapper::Wrapper;

    /// Create a mixer that feeds the given audio device (most common usage).
    [[nodiscard]] static Result<Mixer, StringView>
    CreateDevice(AudioDeviceID devid = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, const AudioSpec &spec = AudioSpec{});

    /// Create an offline mixer that renders to a memory buffer via generate().
    [[nodiscard]] static Result<Mixer, Error> Create(const AudioSpec &spec);

    // ── Tracks ───────────────────────────────────────────────────────────────

    [[nodiscard]] Result<MixTrack, StringView> CreateTrack();

    // ── Audio loading ────────────────────────────────────────────────────────

    [[nodiscard]] Result<MixAudio, StringView> LoadAudio(const String &path, bool predecode = false);

    /// Fire-and-forget playback of an audio object (no track management needed).
    bool PlayAudio(MixAudio &audio) { return mHandle && MIX_PlayAudio(mHandle, audio.Get()); }

    // ── Global controls ──────────────────────────────────────────────────────

    bool SetGain(float gain) { return mHandle && MIX_SetMixerGain(mHandle, gain); }
    [[nodiscard]] float Gain() const { return mHandle ? MIX_GetMixerGain(mHandle) : 1.f; }

    bool SetFrequencyRatio(float ratio) { return mHandle && MIX_SetMixerFrequencyRatio(mHandle, ratio); }
    [[nodiscard]] float FrequencyRatio() const { return mHandle ? MIX_GetMixerFrequencyRatio(mHandle) : 1.f; }

    bool StopAll(Sint64 fadeOutMs = 0) { return mHandle && MIX_StopAllTracks(mHandle, fadeOutMs); }
    bool PauseAll() { return mHandle && MIX_PauseAllTracks(mHandle); }
    bool ResumeAll() { return mHandle && MIX_ResumeAllTracks(mHandle); }

    // ── Groupes ───────────────────────────────────────────────────────────────

    [[nodiscard]] Result<MixGroup, StringView> CreateGroup();

    // ── Rendu hors ligne / verrou ─────────────────────────────────────────────

    /// Rendu hors ligne : génère jusqu'à `buflen` octets PCM dans `buffer`.
    /// Retourne le nombre d'octets produits (0 = plus rien à générer, -1 = erreur).
    [[nodiscard]] int Generate(void *buffer, int buflen) { return mHandle ? MIX_Generate(mHandle, buffer, buflen) : -1; }

    void Lock() { if (mHandle) MIX_LockMixer(mHandle); }
    void Unlock() { if (mHandle) MIX_UnlockMixer(mHandle); }

    // ── Requêtes ──────────────────────────────────────────────────────────────

    [[nodiscard]] bool GetFormat(SDL_AudioSpec &outSpec) const { return mHandle && MIX_GetMixerFormat(mHandle, &outSpec); }
    [[nodiscard]] SDL_PropertiesID Properties() const { return mHandle ? MIX_GetMixerProperties(mHandle) : 0; }

    // ── Lecture par étiquette (tags) ──────────────────────────────────────────

    bool PlayTag(const String &tag, SDL_PropertiesID options = 0) {
        return mHandle && MIX_PlayTag(mHandle, tag.c_str(), options);
    }
    bool StopTag(const String &tag, Sint64 fadeOutMs = 0) { return mHandle && MIX_StopTag(mHandle, tag.c_str(), fadeOutMs); }
    bool PauseTag(const String &tag) { return mHandle && MIX_PauseTag(mHandle, tag.c_str()); }
    bool ResumeTag(const String &tag) { return mHandle && MIX_ResumeTag(mHandle, tag.c_str()); }
    bool SetTagGain(const String &tag, float gain) { return mHandle && MIX_SetTagGain(mHandle, tag.c_str(), gain); }

    /// Toutes les pistes portant l'étiquette `tag`.
    [[nodiscard]] std::vector<MIX_Track *> GetTaggedTracks(const String &tag) const;

    // ── Callback post-mixage (forme brute) ────────────────────────────────────

    bool SetPostMixCallback(MIX_PostMixCallback cb, void *userdata) {
        return mHandle && MIX_SetPostMixCallback(mHandle, cb, userdata);
    }
};

// ============================================================================
// MixAudio deferred impl (needs Mixer to be complete)
// ============================================================================



// ============================================================================
// MixTrack deferred impl (needs MixAudio to be complete)
// ============================================================================

inline bool MixTrack::SetAudio(MixAudio &audio) { return mHandle && MIX_SetTrackAudio(mHandle, audio.Get()); }

} // namespace sdl3
