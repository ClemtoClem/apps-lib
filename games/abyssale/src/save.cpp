#include "abyssale/save.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <ranges>

#include <core/string.hpp>
#include <dataio/json.hpp>

namespace abyssale {

namespace fs = std::filesystem;

namespace {
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

std::optional<WorldMeta> WorldMeta::Load(const fs::path &worldDir) {
	auto r = dataio::LoadJsonFile((worldDir / "world.json").string());
	if (!r)
		return std::nullopt;
	const dataio::NodePtr &j = r.Value();
	WorldMeta w;
	ReadField(j, "name", w.name);
	w.seed = static_cast<uint64_t>((*j)["seed"]->AsNumber(0));
	ReadField(j, "quality", w.quality);
	w.position.x = (*j)["px"]->AsFloat(0.f);
	w.position.y = (*j)["py"]->AsFloat(-30.f);
	w.position.z = (*j)["pz"]->AsFloat(0.f);
	ReadField(j, "yaw", w.yaw);
	ReadField(j, "pitch", w.pitch);
	ReadField(j, "created", w.created);
	ReadField(j, "lastPlayed", w.lastPlayed);
	ReadField(j, "playSeconds", w.playSeconds);
	return w;
}

bool WorldMeta::Save(const fs::path &worldDir) const {
	dataio::NodePtr j = dataio::Node::MakeObject();
	j->Set("name", dataio::Node::MakeString(name));
	j->Set("seed", dataio::Node::MakeFloat(double(seed)));
	j->Set("quality", dataio::Node::MakeInt(int64_t(quality)));
	j->Set("px", dataio::Node::MakeFloat(position.x));
	j->Set("py", dataio::Node::MakeFloat(position.y));
	j->Set("pz", dataio::Node::MakeFloat(position.z));
	j->Set("yaw", dataio::Node::MakeFloat(yaw));
	j->Set("pitch", dataio::Node::MakeFloat(pitch));
	j->Set("created", dataio::Node::MakeString(created));
	j->Set("lastPlayed", dataio::Node::MakeString(lastPlayed));
	j->Set("playSeconds", dataio::Node::MakeFloat(playSeconds));
	std::error_code ec;
	fs::create_directories(worldDir, ec);
	return dataio::SaveJsonFile((worldDir / "world.json").string(), j);
}

std::vector<std::pair<std::string, WorldMeta>> ListWorlds(const fs::path &worldsDir) {
	std::vector<std::pair<std::string, WorldMeta>> out;
	std::error_code ec;
	if (!fs::exists(worldsDir, ec))
		return out;
	for (const auto &e : fs::directory_iterator(worldsDir, ec)) {
		if (!e.is_directory())
			continue;
		WorldMeta meta = WorldMeta::Load(e.path()).value_or(WorldMeta{});
		if (!fs::exists(e.path() / "world.json"))
			meta.name = e.path().filename().string();
		out.emplace_back(e.path().filename().string(), std::move(meta));
	}
	std::ranges::sort(out, [](const auto &a, const auto &b) { return a.second.lastPlayed > b.second.lastPlayed; });
	return out;
}

bool DeleteWorld(const fs::path &worldsDir, const std::string &folder) {
	std::error_code ec;
	fs::remove_all(worldsDir / folder, ec);
	return !ec;
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

} // namespace abyssale
