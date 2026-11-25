#include "sandbox/game/sound.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <sdl3/input/audio.hpp>
#include <sdl3/input/mixer.hpp>

namespace sandbox::game {

namespace {

constexpr int kRate = 48000;
constexpr size_t kBlock = 1024;		  ///< échantillons par bloc et par voix
constexpr float kLeadSeconds = 0.08f; ///< avance de la file de sortie

} // namespace

struct SoundSystem::Voice {
	int id = 0;
	std::string file;
	std::unique_ptr<sdl3::AudioDecoder> decoder;
	float volume = 1.f;
	bool loop = false;
	std::optional<math::FVector3> position;
	bool finished = false;
	float pitch = 1.f;	 ///< vitesse de lecture (1 = normale)
	bool paused = false; ///< en pause : ne consomme pas de données
	float radius = 2.f;	 ///< distance de pleine intensité (mètres)
	float rolloff = 0.25f;
	std::vector<float> samples; ///< échantillons décodés en attente (mono)
	float readPos = 0.f;		///< position de lecture fractionnaire (rééchantillonnage)
};

SoundSystem::SoundSystem(std::filesystem::path folder) : m_folder(std::move(folder)) {}

SoundSystem::~SoundSystem() = default;

bool SoundSystem::Open() {
	if (m_stream)
		return true;
	auto mixer = sdl3::MixerContext::Create();
	if (mixer.IsError())
		return false;
	m_mixer = std::make_unique<sdl3::MixerContext>(std::move(mixer.Value()));
	auto stream = sdl3::AudioStream::OpenPlayback(sdl3::AudioSpec{sdl3::AudioFormat::F32, 2, kRate});
	if (stream.IsError())
		return false;
	m_stream = std::make_unique<sdl3::AudioStream>(std::move(stream.Value()));
	(void)m_stream->Resume();
	return true;
}

std::vector<std::string> SoundSystem::Files() const {
	std::vector<std::string> files;
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(m_folder, ec)) {
		const std::string ext = entry.path().extension().string();
		if (ext == ".ogg" || ext == ".mp3" || ext == ".wav" || ext == ".flac")
			files.push_back(entry.path().filename().string());
	}
	std::ranges::sort(files);
	return files;
}

bool SoundSystem::Restart(Voice &voice) const {
	voice.decoder.reset();
	if (!m_mixer)
		return false;
	// « fichier.ogg » : dans le dossier des sons ; un chemin (« assets/sounds/x.ogg ») : tel quel.
	const std::filesystem::path given(voice.file);
	std::error_code ec;
	const std::filesystem::path path = given.has_parent_path() && std::filesystem::exists(given, ec) ? given : m_folder / given;
	auto decoder = sdl3::AudioDecoder::Create(String(path.string()));
	if (decoder.IsError())
		return false;
	voice.decoder = std::make_unique<sdl3::AudioDecoder>(std::move(decoder.Value()));
	return true;
}

bool SoundSystem::DecodeBlock(Voice &voice) const {
	// Décode un bloc de kBlock échantillons mono et l'ajoute au tampon de la voix.
	// Faux à la fin du fichier (sans boucle).
	const size_t old = voice.samples.size();
	voice.samples.resize(old + kBlock);
	const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
	size_t got = 0;
	while (got < kBlock) {
		const int bytes = voice.decoder->Decode(voice.samples.data() + old + got, int((kBlock - got) * sizeof(float)), &spec);
		if (bytes > 0) {
			got += size_t(bytes) / sizeof(float);
			continue;
		}
		if (!voice.loop || !Restart(voice)) { // fin du fichier (ou fichier illisible)
			voice.samples.resize(old + got);
			return false;
		}
	}
	voice.samples.resize(old + got);
	return true;
}

int SoundSystem::Play(const std::string &file, float volume, bool loop, std::optional<math::FVector3> position) {
	if (file.empty())
		return 0;
	auto voice = std::make_unique<Voice>();
	voice->id = m_nextId++;
	voice->file = file;
	voice->volume = volume;
	voice->loop = loop;
	voice->position = position;
	if (m_stream && !Restart(*voice))
		return 0; // fichier illisible
	const int id = voice->id;
	m_voices.push_back(std::move(voice));
	return id;
}

void SoundSystem::Stop(int id) {
	std::erase_if(m_voices, [id](const std::unique_ptr<Voice> &v) { return v->id == id; });
}

void SoundSystem::StopAll() { m_voices.clear(); }

bool SoundSystem::IsPlaying(int id) const {
	return std::ranges::any_of(m_voices, [id](const std::unique_ptr<Voice> &v) { return v->id == id && !v->finished; });
}

void SoundSystem::SetVolume(int voice, float volume) {
	for (auto &v : m_voices)
		if (v->id == voice)
			v->volume = std::max(0.f, volume);
}

void SoundSystem::SetPitch(int voice, float pitch) {
	for (auto &v : m_voices)
		if (v->id == voice)
			v->pitch = std::clamp(pitch, 0.25f, 4.f);
}

void SoundSystem::SetPause(int voice, bool paused) {
	for (auto &v : m_voices)
		if (v->id == voice)
			v->paused = paused;
}

bool SoundSystem::IsPaused(int voice) const {
	for (const auto &v : m_voices)
		if (v->id == voice)
			return v->paused;
	return false;
}

void SoundSystem::SetDistance(int voice, float radius, float rolloff) {
	for (auto &v : m_voices)
		if (v->id == voice) {
			v->radius = std::max(0.f, radius);
			v->rolloff = std::max(0.f, rolloff);
		}
}

void SoundSystem::SetPosition(int id, const math::FVector3 &position) {
	for (auto &v : m_voices)
		if (v->id == id)
			v->position = position;
}

void SoundSystem::SetListener(const math::FVector3 &position, const math::FVector3 &right) noexcept {
	m_listener = position;
	m_right = right.Normalized();
}

void SoundSystem::SetReverb(float mix, float roomSize, float damping) {
	m_reverbMix = std::clamp(mix, 0.f, 1.f);
	m_reverb.SetRoomSize(roomSize);
	m_reverb.SetDamping(damping);
	m_reverb.SetMix(1.f); // pur réverbéré, mélangé ici-même
}

std::pair<float, float> SoundSystem::SpatialGains(const math::FVector3 &listener, const math::FVector3 &right, const math::FVector3 &source,
												  float volume, float radius, float rolloff) noexcept {
	const math::FVector3 d = source - listener;
	const float distance = d.Length();
	// Pleine intensité jusqu'à `radius`, puis décroissance douce selon `rolloff`.
	const float attenuation = 1.f / (1.f + rolloff * std::max(0.f, distance - radius));
	const float pan = distance > 1e-3f ? std::clamp(d.Dot(right) / distance, -1.f, 1.f) * 0.8f : 0.f;
	const float g = volume * attenuation;
	return {g * std::sqrt(0.5f * (1.f - pan)), g * std::sqrt(0.5f * (1.f + pan))};
}

void SoundSystem::Update() {
	if (!m_stream) {
		std::erase_if(m_voices, [](const std::unique_ptr<Voice> &v) { return !v->loop; }); // muet : les sons brefs s'achèvent
		return;
	}
	const int lead = int(kLeadSeconds * float(kRate) * 2.f * sizeof(float));
	std::array<float, kBlock * 2> mix{};
	for (int guard = 0; guard < 16 && m_stream->Queued() < lead; ++guard) {
		mix.fill(0.f);
		for (auto &v : m_voices) {
			if (v->finished || !v->decoder || v->paused)
				continue;
			const auto [left, right] = v->position ? SpatialGains(m_listener, m_right, *v->position, v->volume, v->radius, v->rolloff)
												   : std::pair{v->volume * 0.7071f, v->volume * 0.7071f};
			// Rééchantillonnage linéaire selon `pitch` (1 : lecture normale).
			for (size_t out = 0; out < kBlock; ++out) {
				while (size_t(v->readPos) + 2 > v->samples.size()) {
					if (!DecodeBlock(*v)) { // fin du fichier
						v->finished = true;
						break;
					}
				}
				if (v->finished)
					break;
				const size_t idx = size_t(v->readPos);
				const float frac = v->readPos - float(idx);
				const float s = v->samples[idx] + (v->samples[idx + 1] - v->samples[idx]) * frac;
				mix[2 * out] += s * left;
				mix[2 * out + 1] += s * right;
				v->readPos += v->pitch;
			}
			// Compacte les échantillons déjà lus (conserve la partie fractionnaire).
			const size_t consumed = size_t(v->readPos);
			if (consumed > 0 && consumed <= v->samples.size()) {
				v->samples.erase(v->samples.begin(), v->samples.begin() + std::ptrdiff_t(consumed));
				v->readPos -= float(consumed);
			}
		}
		for (float &s : mix)
			s = std::clamp(s, -1.f, 1.f);
		// Réverbération globale (Freeverb) sur la somme mono, mélangée en stéréo.
		if (m_reverbMix > 0.f) {
			std::array<float, kBlock> mono{};
			for (size_t i = 0; i < kBlock; ++i)
				mono[i] = (mix[2 * i] + mix[2 * i + 1]) * 0.5f;
			m_reverb.Process(mono.data(), kBlock);
			for (size_t i = 0; i < kBlock; ++i) {
				const float wet = mono[i];
				mix[2 * i] = mix[2 * i] * (1.f - m_reverbMix) + wet * m_reverbMix;
				mix[2 * i + 1] = mix[2 * i + 1] * (1.f - m_reverbMix) + wet * m_reverbMix;
			}
		}
		m_stream->PutData(std::span<const float>(mix));
	}
	std::erase_if(m_voices, [](const std::unique_ptr<Voice> &v) { return v->finished; });
}

} // namespace sandbox::game
