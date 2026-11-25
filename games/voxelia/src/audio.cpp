// Voxelia — musique et sons d'interface (SDL3_mixer via le wrapper sdl3).
#include <format>
#include <algorithm>

#include <sdl3/sdl3.hpp>

#include <core/hash.hpp>
#include <core/log.hpp>
#include <diagnostics/trace.hpp>

#include <dataio/json.hpp>

#include "app.hpp"

namespace voxelia {

namespace {
constexpr float kFadeSpeed = 0.6f; // gain par seconde : fondu enchaîné ≈ 1,7 s
}

bool AudioSystem::Init(const fs::path &musicDir, const fs::path &uiSoundDir) {
	if (!sdl3::WasInit(sdl3::initFlags::AUDIO) && !sdl3::InitSubsystem(sdl3::initFlags::AUDIO)) {
		core::log::Warn("audio indisponible : {}", sdl3::GetError());
		return false;
	}
	auto ctx = sdl3::MixerContext::Create();
	if (!ctx) {
		core::log::Warn("SDL3_mixer : {}", ctx.Error());
		return false;
	}
	m_context.emplace(std::move(ctx.Value()));
	auto mixer = sdl3::Mixer::CreateDevice();
	if (!mixer) {
		core::log::Warn("périphérique audio : {}", mixer.Error());
		m_context.reset();
		return false;
	}
	m_mixer.emplace(std::move(mixer.Value()));
	for (Voice &v : m_voices)
		if (auto t = m_mixer->CreateTrack())
			v.track.emplace(std::move(t.Value()));
	std::error_code ec;
	for (const auto &e : fs::directory_iterator(musicDir, ec)) {
		const auto ext = e.path().extension();
		if (ext != ".ogg" && ext != ".mp3" && ext != ".wav" && ext != ".flac")
			continue;
		if (e.path().stem().string().find("main") != std::string::npos)
			m_menuTrack = e.path();
		else
			m_tracks.push_back(e.path());
	}
	std::ranges::sort(m_tracks);
	if (m_menuTrack.empty() && !m_tracks.empty())
		m_menuTrack = m_tracks.front();
	for (const char *name : {"click_double_on.wav", "click_double_on_2.wav"}) {
		if (fs::exists(uiSoundDir / name)) {
			if (auto a = m_mixer->LoadAudio(String((uiSoundDir / name).string()), true)) {
				m_click.emplace(std::move(a.Value()));
				if (auto t = m_mixer->CreateTrack()) {
					m_clickTrack.emplace(std::move(t.Value()));
					m_clickTrack->SetAudio(*m_click);
				}
			}
			break;
		}
	}
	core::log::Info("audio : {} musique(s) + thème du menu{}", m_tracks.size(), m_click ? ", sons d'interface" : "");
	return true;
}

void AudioSystem::SetVolumes(float master, float music, float effects) {
	m_master = master;
	m_music = music;
	m_effects = effects;
	if (m_clickTrack)
		m_clickTrack->SetGain(m_master * m_effects * 0.6f);
}

void AudioSystem::Start(const fs::path &file, bool loop) {
	if (!m_mixer || file.empty())
		return;
	// La voix courante s'éteint en fondu, l'autre démarre (fondu enchaîné).
	m_voices[m_current].target = 0.f;
	m_current ^= 1;
	Voice &v = m_voices[m_current];
	if (!v.track)
		return;
	v.track->Stop();
	v.audio.reset();
	auto audio = m_mixer->LoadAudio(String(file.string()), false); // décodage en continu
	if (!audio) {
		core::log::Warn("musique {} : {}", file.filename().string(), audio.Error());
		return;
	}
	v.audio.emplace(std::move(audio.Value()));
	v.path = file;
	v.track->SetAudio(*v.audio);
	v.gain = 0.f;
	v.target = 1.f;
	v.track->SetGain(0.f);
	v.track->Play(loop ? -1 : 0);
}

void AudioSystem::PlayMenu() {
	if (!m_mixer || (!m_inGame && m_voices[m_current].target > 0 && m_voices[m_current].path == m_menuTrack))
		return;
	m_inGame = false;
	Start(m_menuTrack, true);
}

void AudioSystem::PlayGame() {
	if (!m_mixer || m_tracks.empty())
		return;
	m_inGame = true;
	Next();
}

void AudioSystem::Next() {
	if (!m_mixer || m_tracks.empty())
		return;
	if (m_shuffle) {
		m_rng = core::Mix64(m_rng + sdl3::GetTicksMS());
		size_t pick = m_rng % m_tracks.size();
		if (m_tracks.size() > 1 && m_tracks[pick] == m_voices[m_current].path)
			pick = (pick + 1) % m_tracks.size();
		m_next = pick;
	} else {
		m_next = (m_next + 1) % m_tracks.size();
	}
	Start(m_tracks[m_next], false);
}

void AudioSystem::Stop() {
	for (Voice &v : m_voices)
		v.target = 0.f;
}

void AudioSystem::Click() {
	if (m_clickTrack && m_master * m_effects > 0.001f)
		m_clickTrack->Play(0);
}

void AudioSystem::Update(float dt) {
	DIAG_ZONE("Audio::Update");
	if (!m_mixer)
		return;
	for (Voice &v : m_voices) {
		if (!v.track)
			continue;
		if (v.gain < v.target)
			v.gain = std::min(v.target, v.gain + kFadeSpeed * dt);
		else if (v.gain > v.target)
			v.gain = std::max(v.target, v.gain - kFadeSpeed * dt);
		v.track->SetGain(v.gain * m_master * m_music);
		if (v.gain <= 0.f && v.target <= 0.f && v.track->IsPlaying())
			v.track->Stop();
	}
	// Fin d'une piste de la playlist : la suivante enchaîne.
	const Voice &cur = m_voices[m_current];
	if (m_inGame && cur.track && cur.target > 0 && !cur.track->IsPlaying())
		Next();
}

std::string AudioSystem::CurrentTrack() const {
	const Voice &v = m_voices[m_current];
	return v.target > 0 ? v.path.stem().string() : std::string();
}

void AudioSystem::IndexSounds(const voxel::ModManager &mods) {
	m_sounds.clear();
	m_soundIds.clear();
	for (const voxel::ModInfo &m : mods.Mods()) {
		auto json = dataio::LoadJsonFile((m.directory / "sounds" / "assets.json").string());
		if (!json)
			continue;
		for (const dataio::NodePtr &e : json.Value()->AsArray()) {
			if ((*e)["type"]->AsString() != "sound")
				continue;
			std::string_view path = (*e)["path"]->AsString();
			if (path.empty() && (*e)["paths"]->IsArray() && (*e)["paths"]->Size() > 0)
				path = (*(*e)["paths"])[0]->AsString();
			if (path.empty())
				continue;
			const std::string id = std::format("{}:{}", m.id, (*e)["id"]->AsString());
			m_sounds[id] = m.directory / path;
			m_soundIds.push_back(id);
		}
	}
	// Fichiers posés directement dans `<mod>/sounds/` (sans déclaration) : « mod:sounds/x.wav ».
	for (const voxel::ModInfo &m : mods.Mods()) {
		std::error_code ec;
		for (const auto &f : fs::directory_iterator(m.directory / "sounds", ec)) {
			const auto ext = f.path().extension();
			if (!f.is_regular_file() || (ext != ".wav" && ext != ".ogg"))
				continue;
			const std::string id = std::format("{}:sounds/{}", m.id, f.path().filename().string());
			if (m_sounds.emplace(id, f.path()).second)
				m_soundIds.push_back(id);
		}
	}
	std::ranges::sort(m_soundIds);
}

std::optional<fs::path> AudioSystem::ResolveSound(std::string_view id) const {
	if (auto it = m_sounds.find(std::string(id)); it != m_sounds.end())
		return it->second;
	// « core:door_wood_open » → fichier du pack rangé par dossier (« core:furniture/door_wood_open »).
	if (const size_t colon = id.find(':'); colon != std::string_view::npos && id.find('/') == std::string_view::npos)
		for (const char *dir : {"furniture", "ui", "Items", "Other"})
			if (auto it = m_sounds.find(std::format("{}:{}/{}", id.substr(0, colon), dir, id.substr(colon + 1))); it != m_sounds.end())
				return it->second;
	return std::nullopt;
}

void AudioSystem::UpdateEmitters(std::span<const SoundSource> sources, const math::FVector3 &listener, float yaw) {
	if (!m_mixer)
		return;
	for (auto &[k, e] : m_emitters)
		e.seen = false;
	const math::FVector3 forward{std::sin(yaw), 0, -std::cos(yaw)}, right{std::cos(yaw), 0, std::sin(yaw)};
	for (const SoundSource &src : sources) {
		Emitter &em = m_emitters[src.key];
		em.seen = true;
		if (em.sound != src.sound || !em.track) { // nouveau son : piste (re)créée
			em.sound = src.sound;
			em.track.reset();
			const auto path = ResolveSound(src.sound);
			if (!path)
				continue;
			auto cached = m_audioCache.find(*path);
			if (cached == m_audioCache.end()) {
				auto audio = m_mixer->LoadAudio(String(path->string()), true);
				if (!audio)
					continue;
				cached = m_audioCache.emplace(*path, std::move(audio.Value())).first;
			}
			auto track = m_mixer->CreateTrack();
			if (!track)
				continue;
			em.track.emplace(std::move(track.Value()));
			em.track->SetAudio(cached->second);
			em.track->Play(src.loop ? -1 : 0);
		}
		const math::FVector3 d = src.position - listener;
		const float att = std::clamp(1.f - d.Length() / std::max(0.5f, src.range), 0.f, 1.f);
		em.track->SetGain(src.volume * att * att * m_master * m_effects);
		em.track->Set3DPosition(d.Dot(right), d.y, -d.Dot(forward));
	}
	std::erase_if(m_emitters, [](auto &kv) {
		if (kv.second.seen)
			return false;
		if (kv.second.track)
			kv.second.track->Stop();
		return true;
	});
}

} // namespace voxelia
