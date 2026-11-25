// Définitions de sdl3/mixer.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <sdl3/sdl3.hpp>
#include "sdl3/input/mixer.hpp"

namespace sdl3 {

// ── MixerContext ─────────────────────────────────────────────────────────────

MixerContext::~MixerContext() {
    if (m_owns)
        MIX_Quit();
}

Result<MixerContext, Error> MixerContext::Create() {
    MixerContext ctx;
    ctx.m_owns = MIX_Init();
    if (!ctx)
        return Err(GetError());
    return Ok(std::move(ctx));
}

// ── AudioDecoder ─────────────────────────────────────────────────────────────

Result<AudioDecoder, Error> AudioDecoder::Create(const String &path, SDL_PropertiesID props) {
    auto *d = MIX_CreateAudioDecoder(path.c_str(), props);
    if (!d)
        return Err(GetError());
    return Ok(AudioDecoder(d));
}

Result<AudioDecoder, Error> AudioDecoder::CreateIO(IOStream &src, bool closeio, SDL_PropertiesID props) {
    if (!src)
        return Err(StringView("AudioDecoder::CreateIO: flux nul"));
    auto *d = MIX_CreateAudioDecoder_IO(src.Get(), closeio, props);
    if (!d)
        return Err(GetError());
    return Ok(AudioDecoder(d));
}

bool AudioDecoder::GetFormat(SDL_AudioSpec &outSpec) const {
    return mHandle && MIX_GetAudioDecoderFormat(mHandle, &outSpec);
}

int AudioDecoder::Decode(void *buffer, int buflen, const SDL_AudioSpec *spec) {
    return mHandle ? MIX_DecodeAudio(mHandle, buffer, buflen, spec) : -1;
}

// ── MixTrack ─────────────────────────────────────────────────────────────────

bool MixTrack::Play(int loops) {
    if (!mHandle)
        return false;
    MIX_SetTrackLoops(mHandle, loops);
    return MIX_PlayTrack(mHandle, 0);
}

bool MixTrack::PlayStreaming() {
    if (!mHandle)
        return false;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetBooleanProperty(props, MIX_PROP_PLAY_HALT_WHEN_EXHAUSTED_BOOLEAN, false);
    bool ok = MIX_PlayTrack(mHandle, props);
    SDL_DestroyProperties(props);
    return ok;
}

void MixTrack::Untag(const String &t) {
    if (mHandle)
        MIX_UntagTrack(mHandle, t.c_str());
}

// ── Mixer ────────────────────────────────────────────────────────────────────

Result<Mixer, StringView> Mixer::CreateDevice(AudioDeviceID devid, const AudioSpec &spec) {
    SDL_AudioSpec sdlSpec = spec.ToSDL();
    auto *m = MIX_CreateMixerDevice(devid, &sdlSpec);
    if (!m)
        return Err(GetError());
    return Ok(Mixer(m));
}

Result<Mixer, Error> Mixer::Create(const AudioSpec &spec) {
    SDL_AudioSpec sdlSpec = spec.ToSDL();
    auto *m = MIX_CreateMixer(&sdlSpec);
    if (!m)
        return Err(GetError());
    return Ok(Mixer(m));
}

Result<MixTrack, StringView> Mixer::CreateTrack() {
    auto *t = MIX_CreateTrack(mHandle);
    if (!t)
        return Err(GetError());
    return Ok(MixTrack(t));
}

Result<MixAudio, StringView> Mixer::LoadAudio(const String &path, bool predecode) {
    auto *a = MIX_LoadAudio(mHandle, path.c_str(), predecode);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::Load(Mixer &mixer, const String &path, bool predecode) {
    auto *a = MIX_LoadAudio(mixer.Get(), path.c_str(), predecode);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::FromPcm(Mixer &mixer, const void *data, size_t size, const SDL_AudioSpec &spec) {
    auto *a = MIX_LoadRawAudio(mixer.Get(), data, size, &spec);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<WavData, StringView> AudioDecoder::DecodeFile(const String &path) {
    auto decoder = Create(path);
    if (decoder.IsError())
        return Err(GetError());
    WavData out{};
    if (!decoder.Value().GetFormat(out.spec))
        return Err(GetError());
    uint8_t buffer[16384];
    for (;;) {
        const int got = decoder.Value().Decode(buffer, int(sizeof(buffer)), &out.spec);
        if (got < 0)
            return Err(GetError());
        if (got == 0)
            break;
        out.samples.insert(out.samples.end(), buffer, buffer + got);
    }
    return Ok(std::move(out));
}

bool MixTrack::Set3DPosition(float x, float y, float z) {
    const MIX_Point3D point{x, y, z};
    return mHandle && MIX_SetTrack3DPosition(mHandle, &point);
}

bool MixTrack::Clear3DPosition() {
    return mHandle && MIX_SetTrack3DPosition(mHandle, nullptr);
}

// ── MixTrack : tags / groupe ─────────────────────────────────────────────────

std::vector<String> MixTrack::Tags() const {
    if (!mHandle)
        return {};
    int count = 0;
    char **tags = MIX_GetTrackTags(mHandle, &count);
    if (!tags)
        return {};
    std::vector<String> out;
    for (char **t = tags; *t; ++t)
        out.emplace_back(*t);
    SDL_free(tags);
    return out;
}

// ── MixAudio : chargement avancé ─────────────────────────────────────────────

Result<MixAudio, StringView> MixAudio::LoadWithProperties(SDL_PropertiesID props) {
    auto *a = MIX_LoadAudioWithProperties(props);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::LoadIO(Mixer &mixer, IOStream &src, bool predecode, bool closeio) {
    if (!src)
        return Err(StringView("MixAudio::LoadIO: flux nul"));
    auto *a = MIX_LoadAudio_IO(mixer.Get(), src.Get(), predecode, closeio);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::LoadNoCopy(Mixer &mixer, const void *data, size_t size, bool freeWhenDone) {
    auto *a = MIX_LoadAudioNoCopy(mixer.Get(), data, size, freeWhenDone);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::LoadRawIO(Mixer &mixer, IOStream &src, const SDL_AudioSpec &spec, bool closeio) {
    if (!src)
        return Err(StringView("MixAudio::LoadRawIO: flux nul"));
    auto *a = MIX_LoadRawAudio_IO(mixer.Get(), src.Get(), &spec, closeio);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::LoadRawNoCopy(Mixer &mixer, const void *data, size_t size,
                                                     const SDL_AudioSpec &spec, bool freeWhenDone) {
    auto *a = MIX_LoadRawAudioNoCopy(mixer.Get(), data, size, &spec, freeWhenDone);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

Result<MixAudio, StringView> MixAudio::CreateSineWave(Mixer &mixer, int hz, float amplitude, Sint64 ms) {
    auto *a = MIX_CreateSineWaveAudio(mixer.Get(), hz, amplitude, ms);
    if (!a)
        return Err(GetError());
    return Ok(MixAudio(a));
}

// ── Mixer : groupes / tags ───────────────────────────────────────────────────

Result<MixGroup, StringView> Mixer::CreateGroup() {
    auto *g = MIX_CreateGroup(mHandle);
    if (!g)
        return Err(GetError());
    return Ok(MixGroup(g));
}

std::vector<MIX_Track *> Mixer::GetTaggedTracks(const String &tag) const {
    if (!mHandle)
        return {};
    int count = 0;
    MIX_Track **tracks = MIX_GetTaggedTracks(mHandle, tag.c_str(), &count);
    if (!tracks)
        return {};
    std::vector<MIX_Track *> out;
    for (MIX_Track **t = tracks; *t; ++t)
        out.push_back(*t);
    SDL_free(tracks);
    return out;
}

} // namespace sdl3
