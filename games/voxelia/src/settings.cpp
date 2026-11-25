// Voxelia — persistance des réglages globaux, des réglages de monde et des paramètres de mods.
#include <algorithm>
#include <chrono>
#include <format>

#include <core/string.hpp>

#include "app.hpp"

namespace voxelia {

namespace {
/// Lecture/écriture symétriques d'un champ (évite de dupliquer la liste des clés).
template <typename Fn> void VisitSettings(Settings &s, Fn &&f) {
	f("viewDistance", s.viewDistance);
	f("verticalDistance", s.verticalDistance);
	f("fov", s.fov);
	f("vsync", s.vsync);
	f("fogStart", s.fogStart);
	f("brightness", s.brightness);
	f("showEntities", s.showEntities);
	f("entityRenderRadius", s.entityRenderRadius);
	f("maxEntities", s.maxEntities);
	f("textureLodInterval", s.textureLodInterval);
	f("reflectionBounces", s.reflectionBounces);
	f("sensitivityMilli", s.sensitivity);
	f("invertY", s.invertY);
	f("flySpeed", s.flySpeed);
	f("sprintFlySpeed", s.sprintFlySpeed);
	f("masterVolume", s.masterVolume);
	f("musicVolume", s.musicVolume);
	f("effectsVolume", s.effectsVolume);
	f("menuMusic", s.menuMusic);
	f("shuffleMusic", s.shuffleMusic);
	f("uiScale", s.uiScale);
	f("showDebug", s.showDebug);
	f("showTips", s.showTips);
	f("menuBackground", s.menuBackground);
	f("simulationDistance", s.simulationDistance);
	f("entityQueryRadius", s.entityQueryRadius);
	f("maxPendingLoads", s.maxPendingLoads);
	f("maxPendingMeshes", s.maxPendingMeshes);
	f("uploadBudgetMB", s.uploadBudgetMB);
}

template <typename T> void ReadField(const dataio::NodePtr &j, const char *key, T &v) {
	const dataio::NodePtr f = j->Get(key);
	if (!f)
		return;
	if constexpr (std::is_same_v<T, bool>)
		v = f->AsBool(v);
	else if constexpr (std::is_same_v<T, int>)
		v = int(f->AsInt(v));
	else if constexpr (std::is_same_v<T, float>)
		v = f->AsFloat(v);
	else if constexpr (std::is_same_v<T, double>)
		v = f->AsNumber(v);
	else
		v = std::string(f->AsString(v));
}
} // namespace

Settings Settings::Load(const fs::path &file) {
	Settings s;
	auto r = dataio::LoadJsonFile(file.string());
	if (!r)
		return s;
	const dataio::NodePtr &j = r.Value();
	VisitSettings(s, [&](const char *key, auto &v) { ReadField(j, key, v); });
	if (const dataio::NodePtr old = j->Get("sensitivity"); old && !j->Get("sensitivityMilli")) // ancien format (radians/pixel)
		s.sensitivity = old->AsFloat(0.0025f) * 1000.f;
	s.lastWorld = std::string((*j)["lastWorld"]->AsString(s.lastWorld));
	s.playerSkin = std::string((*j)["playerSkin"]->AsString(s.playerSkin));
	for (const dataio::NodePtr &m : (*j)["disabledMods"]->AsArray())
		s.disabledMods.emplace(m->AsString());
	return s;
}

bool Settings::Save(const fs::path &file) const {
	dataio::NodePtr j = dataio::Node::MakeObject();
	Settings copy = *this;
	VisitSettings(copy, [&](const char *key, auto &v) {
		using T = std::decay_t<decltype(v)>;
		if constexpr (std::is_same_v<T, bool>)
			j->Set(key, dataio::Node::MakeBool(v));
		else if constexpr (std::is_same_v<T, int>)
			j->Set(key, dataio::Node::MakeInt(int64_t(v)));
		else
			j->Set(key, dataio::Node::MakeFloat(v));
	});
	j->Set("lastWorld", dataio::Node::MakeString(lastWorld));
	j->Set("playerSkin", dataio::Node::MakeString(playerSkin));
	dataio::NodePtr mods = dataio::Node::MakeArray();
	for (const auto &m : disabledMods)
		mods->Push(dataio::Node::MakeString(m));
	j->Set("disabledMods", std::move(mods));
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	return dataio::SaveJsonFile(file.string(), j);
}

std::optional<WorldSettings> WorldSettings::Load(const fs::path &dir) {
	auto r = dataio::LoadJsonFile((dir / "world.json").string());
	if (!r)
		return std::nullopt;
	const dataio::NodePtr &j = r.Value();
	WorldSettings w;
	ReadField(j, "name", w.name);
	ReadField(j, "description", w.description);
	ReadField(j, "gameMode", w.gameMode);
	ReadField(j, "cheats", w.cheats);
	ReadField(j, "generator", w.generator);
	ReadField(j, "relief", w.relief);
	if (const dataio::NodePtr tj = j->Get("terrain"); tj && tj->IsObject())
		w.terrain = dataio::WriteJson(tj, -1);
	w.seed = static_cast<uint64_t>((*j)["seed"]->AsNumber(0));
	ReadField(j, "creatures", w.creatures);
	ReadField(j, "trees", w.trees);
	ReadField(j, "dayCycle", w.dayCycle);
	ReadField(j, "dayLengthMinutes", w.dayLengthMinutes);
	ReadField(j, "startTime", w.startTime);
	ReadField(j, "created", w.created);
	ReadField(j, "lastPlayed", w.lastPlayed);
	ReadField(j, "playSeconds", w.playSeconds);
	return w;
}

bool WorldSettings::Save(const fs::path &dir) const {
	dataio::NodePtr j = dataio::Node::MakeObject();
	j->Set("name", dataio::Node::MakeString(name));
	j->Set("description", dataio::Node::MakeString(description));
	j->Set("gameMode", dataio::Node::MakeInt(int64_t(gameMode)));
	j->Set("cheats", dataio::Node::MakeBool(cheats));
	j->Set("generator", dataio::Node::MakeInt(int64_t(generator)));
	j->Set("relief", dataio::Node::MakeFloat(relief));
	if (!terrain.empty())
		if (auto tj = dataio::ParseJson(terrain))
			j->Set("terrain", tj.Value());
	j->Set("seed", dataio::Node::MakeFloat(double(seed)));
	j->Set("creatures", dataio::Node::MakeBool(creatures));
	j->Set("trees", dataio::Node::MakeBool(trees));
	j->Set("dayCycle", dataio::Node::MakeBool(dayCycle));
	j->Set("dayLengthMinutes", dataio::Node::MakeInt(int64_t(dayLengthMinutes)));
	j->Set("startTime", dataio::Node::MakeFloat(startTime));
	j->Set("created", dataio::Node::MakeString(created));
	j->Set("lastPlayed", dataio::Node::MakeString(lastPlayed));
	j->Set("playSeconds", dataio::Node::MakeFloat(playSeconds));
	std::error_code ec;
	fs::create_directories(dir, ec);
	return dataio::SaveJsonFile((dir / "world.json").string(), j);
}

voxel::TerrainSettings WorldSettings::Terrain() const {
	if (!terrain.empty())
		if (auto tj = dataio::ParseJson(terrain)) { // réglages de l'outil de génération
			voxel::TerrainSettings t = voxel::TerrainFromJson(tj.Value());
			t.creatures = creatures;
			t.trees = trees;
			return t;
		}
	voxel::TerrainSettings t;
	t.type = static_cast<voxel::GeneratorType>(std::clamp(generator, 0, 3));
	t.relief = relief;
	t.creatures = creatures;
	t.trees = trees;
	return t;
}

std::string WorldFolderName(std::string_view name) {
	std::string out;
	for (size_t i = 0; i < name.size();) {
		const char32_t cp = unicode::DecodeUtf8(name, i);
		if ((cp >= U'a' && cp <= U'z') || (cp >= U'0' && cp <= U'9'))
			out.push_back(char(cp));
		else if (cp >= U'A' && cp <= U'Z')
			out.push_back(char(cp - U'A' + U'a'));
		else if (std::u32string_view(U"éèêë").find(cp) != std::u32string_view::npos)
			out.push_back('e');
		else if (std::u32string_view(U"àâä").find(cp) != std::u32string_view::npos)
			out.push_back('a');
		else if (std::u32string_view(U"îï").find(cp) != std::u32string_view::npos)
			out.push_back('i');
		else if (std::u32string_view(U"ôö").find(cp) != std::u32string_view::npos)
			out.push_back('o');
		else if (std::u32string_view(U"ùûü").find(cp) != std::u32string_view::npos)
			out.push_back('u');
		else if (cp == U'ç')
			out.push_back('c');
		else if (!out.empty() && out.back() != '-')
			out.push_back('-');
	}
	while (!out.empty() && out.back() == '-')
		out.pop_back();
	return out.empty() ? "monde" : out;
}

std::string NowText() {
	const auto now = std::chrono::zoned_time(std::chrono::current_zone(), std::chrono::system_clock::now());
	return std::format("{:%d/%m/%Y %H:%M}", now.get_local_time());
}

// ── Paramètres des mods ───────────────────────────────────────────────────
void ModSettingsStore::Load(const fs::path &file) {
	auto r = dataio::LoadJsonFile(file.string());
	m_values = r && r.Value()->IsObject() ? r.Value() : dataio::Node::MakeObject();
}

bool ModSettingsStore::Save(const fs::path &file) const {
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	return dataio::SaveJsonFile(file.string(), m_values);
}

dataio::NodePtr ModSettingsStore::Get(const voxel::ModInfo &mod, const voxel::ModSetting &s) const {
	if (const dataio::NodePtr m = m_values->Get(mod.id))
		if (const dataio::NodePtr v = m->Get(s.id))
			return v;
	return s.defaultValue;
}

void ModSettingsStore::Set(const std::string &mod, const std::string &id, dataio::NodePtr value) {
	dataio::NodePtr m = m_values->Get(mod);
	if (!m) {
		m = dataio::Node::MakeObject();
		m_values->Set(mod, m);
	}
	m->Set(id, std::move(value));
}

void ModSettingsStore::Reset(const std::string &mod, const std::string &id) {
	if (const dataio::NodePtr m = m_values->Get(mod))
		m->Remove(id);
}

void ModSettingsStore::ResetMod(const std::string &mod) { m_values->Remove(mod); }

bool ModSettingsStore::IsOverridden(const std::string &mod, const std::string &id) const {
	const dataio::NodePtr m = m_values->Get(mod);
	return m && m->Get(id);
}

} // namespace voxelia
