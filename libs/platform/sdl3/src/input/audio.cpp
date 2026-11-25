// Définitions de sdl3/audio.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <sdl3/sdl3.hpp>
#include "sdl3/input/audio.hpp"

namespace sdl3 {

std::vector<AudioDeviceInfo> EnumeratePlaybackDevices() {
    int count = 0;
    SDL_AudioDeviceID *ids = SDL_GetAudioPlaybackDevices(&count);
    std::vector<AudioDeviceInfo> devices;
    if (!ids)
        return devices;
    devices.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const char *name = SDL_GetAudioDeviceName(ids[i]);
        devices.push_back({ids[i], String(name ? name : "")});
    }
    SDL_free(ids);
    return devices;
}

std::vector<AudioDeviceInfo> EnumerateRecordingDevices() {
    int count = 0;
    SDL_AudioDeviceID *ids = SDL_GetAudioRecordingDevices(&count);
    std::vector<AudioDeviceInfo> devices;
    if (!ids)
        return devices;
    devices.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const char *name = SDL_GetAudioDeviceName(ids[i]);
        devices.push_back({ids[i], String(name ? name : "")});
    }
    SDL_free(ids);
    return devices;
}

// ── AudioDevice (RAII SDL_OpenAudioDevice) ───────────────────────────────────

AudioDevice::~AudioDevice() {
    Close();
}

Result<AudioDevice, Error> AudioDevice::Open(SDL_AudioDeviceID devid, const AudioSpec &spec) {
    SDL_AudioSpec s = spec.ToSDL();
    SDL_AudioDeviceID id = SDL_OpenAudioDevice(devid, &s);
    if (id == 0)
        return Err(GetError());
    return Ok(AudioDevice(id));
}

Result<AudioDevice, Error> AudioDevice::OpenPlayback(const AudioSpec &spec) {
    return Open(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec);
}

Result<AudioDevice, Error> AudioDevice::OpenRecording(const AudioSpec &spec) {
    return Open(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, spec);
}

void AudioDevice::Close() noexcept {
    if (m_id != 0) {
        SDL_CloseAudioDevice(m_id);
        m_id = 0;
    }
}

// ── AudioStream ──────────────────────────────────────────────────────────────

Result<AudioStream, StringView> AudioStream::OpenPlayback(const AudioSpec &spec) {
    return OpenPlayback(spec, SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
}

Result<AudioStream, StringView> AudioStream::OpenPlayback(const AudioSpec &spec, SDL_AudioDeviceID deviceId) {
    SDL_AudioSpec s = spec.ToSDL();
    auto *st = SDL_OpenAudioDeviceStream(deviceId, &s, nullptr, nullptr);
    if (!st)
        return Err(GetError());
    return Ok(AudioStream(st));
}

Result<AudioStream, StringView> AudioStream::OpenRecording(const AudioSpec &spec) {
    return OpenRecording(spec, SDL_AUDIO_DEVICE_DEFAULT_RECORDING);
}

Result<AudioStream, StringView> AudioStream::OpenRecording(const AudioSpec &spec, SDL_AudioDeviceID deviceId) {
    SDL_AudioSpec s = spec.ToSDL();
    auto *st = SDL_OpenAudioDeviceStream(deviceId, &s, nullptr, nullptr);
    if (!st)
        return Err(GetError());
    return Ok(AudioStream(st));
}

Result<AudioStream, Error> AudioStream::Create(const AudioSpec &src, const AudioSpec &dst) {
    SDL_AudioSpec s = src.ToSDL(), d = dst.ToSDL();
    auto *st = SDL_CreateAudioStream(&s, &d);
    if (!st)
        return Err(GetError());
    return Ok(AudioStream(st));
}

void AudioStream::Unbind() {
    if (mHandle)
        SDL_UnbindAudioStream(mHandle);
}

Result<WavData, StringView> LoadWav(const String &path) {
    SDL_AudioSpec spec{};
    uint8_t *buf = nullptr;
    uint32_t len = 0;
    if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len))
        return Err(GetError());
    WavData wd;
    wd.spec = spec;
    wd.samples.assign(buf, buf + len);
    SDL_free(buf);
    return Ok(std::move(wd));
}

} // namespace sdl3
