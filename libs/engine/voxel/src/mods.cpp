#include "voxel/mods.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>

#include <core/log.hpp>

#include "voxel/orientation.hpp"
#include "voxel/storage.hpp"

namespace voxel {
namespace fs = std::filesystem;

size_t ModManager::Discover(const fs::path &modsDir, const std::set<std::string> &disabled) {
	m_mods.clear();
	std::error_code ec;
	if (!fs::is_directory(modsDir, ec)) {
		core::log::Warn("mods : dossier introuvable {}", modsDir.string());
		return 0;
	}
	for (const auto &entry : fs::directory_iterator(modsDir, ec)) {
		if (!entry.is_directory())
			continue;
		ModInfo mod;
		mod.directory = entry.path();
		mod.id = entry.path().filename().string();
		if (auto meta = dataio::LoadJsonFile((entry.path() / "mod.json").string())) {
			const dataio::NodePtr &j = meta.Value();
			mod.id = std::string((*j)["id"]->AsString(mod.id));
			mod.name = std::string((*j)["name"]->AsString(mod.id));
			mod.version = std::string((*j)["version"]->AsString("0.0.0"));
			mod.description = std::string((*j)["description"]->AsString());
			mod.priority = static_cast<int>((*j)["priority"]->AsInt(mod.id == "core" ? -100 : 0));
			mod.author = std::string((*j)["author"]->AsString("inconnu"));
			mod.category = std::string((*j)["category"]->AsString());
			for (const dataio::NodePtr &d : (*j)["dependencies"]->AsArray())
				mod.dependencies.emplace_back(d->AsString());
			for (const dataio::NodePtr &t : (*j)["tags"]->AsArray())
				mod.tags.emplace_back(t->AsString());
			for (const dataio::NodePtr &st : (*j)["settings"]->AsArray()) {
				ModSetting ms;
				ms.id = std::string((*st)["id"]->AsString());
				ms.type = std::string((*st)["type"]->AsString("float"));
				ms.label = std::string((*st)["label"]->AsString(ms.id));
				ms.description = std::string((*st)["description"]->AsString());
				ms.group = std::string((*st)["group"]->AsString("Général"));
				ms.unit = std::string((*st)["unit"]->AsString());
				ms.defaultValue = (*st)["default"];
				ms.min = (*st)["min"]->AsNumber(0);
				ms.max = (*st)["max"]->AsNumber(ms.type == "int" ? 100 : 1);
				ms.advanced = (*st)["advanced"]->AsBool(false);
				if (!ms.id.empty())
					mod.settings.push_back(std::move(ms));
			}
		} else {
			mod.name = mod.id;
			mod.priority = mod.id == "core" ? -100 : 0;
		}
		mod.enabled = !disabled.contains(mod.id);
		mod.hasConfig = fs::exists(entry.path() / "config.json");
		if (fs::exists(entry.path() / "init.script"))
			mod.scripts.push_back(entry.path() / "init.script");
		std::vector<fs::path> extra;
		for (const auto &s : fs::directory_iterator(entry.path() / "scripts", ec))
			if (s.path().extension() == ".script")
				extra.push_back(s.path());
		std::ranges::sort(extra);
		mod.scripts.insert(mod.scripts.end(), extra.begin(), extra.end());
		m_mods.push_back(std::move(mod));
	}
	// `core` d'abord, puis par priorité et par nom : ordre déterministe.
	std::ranges::sort(m_mods, [](const ModInfo &a, const ModInfo &b) { return a.priority != b.priority ? a.priority < b.priority : a.id < b.id; });
	return m_mods.size();
}

const ModInfo *ModManager::Find(std::string_view id) const noexcept {
	for (const ModInfo &m : m_mods)
		if (m.id == id)
			return &m;
	return nullptr;
}

const Structure *ModManager::FindStructure(std::string_view id) const noexcept {
	for (const Structure &s : m_structures)
		if (s.id == id)
			return &s;
	return nullptr;
}

size_t ModManager::LoadContent(BlockRegistry &blocks) {
	size_t total = 0;
	m_config = dataio::Node::MakeObject();
	m_textureAliases.Clear();
	for (ModInfo &mod : m_mods) {
		if (!mod.enabled)
			continue;
		mod.blockCount = 0;
		auto loadBlocks = [&](const fs::path &file) {
			auto r = dataio::LoadJsonFile(file.string());
			if (!r) {
				core::log::Warn("mod {} : {}", mod.id, r.Error().message);
				return;
			}
			mod.blockCount += blocks.LoadDefinitions(r.Value(), mod.id, mod.directory);
		};
		std::error_code ec;
		if (fs::exists(mod.directory / "blocks.json"))
			loadBlocks(mod.directory / "blocks.json");
		std::vector<fs::path> files;
		for (const auto &f : fs::directory_iterator(mod.directory / "blocks", ec))
			if (f.path().extension() == ".json")
				files.push_back(f.path());
		std::ranges::sort(files);
		for (const auto &f : files)
			loadBlocks(f);
		total += mod.blockCount;

		if (auto cfg = dataio::LoadJsonFile((mod.directory / "config.json").string())) {
			const dataio::NodePtr values = (*cfg.Value())["values"]->IsObject() ? (*cfg.Value())["values"] : cfg.Value();
			for (const auto &[key, value] : values->AsObject())
				m_config->Set(key, value); // remplacement par clé de premier niveau
		}
		if (auto assets = dataio::LoadJsonFile((mod.directory / "textures" / "assets.json").string())) {
			for (const dataio::NodePtr &a : assets.Value()->AsArray()) {
				if ((*a)["type"]->AsString() != "texture")
					continue;
				TextureSource src;
				src.id = BlockRegistry::Qualify((*a)["id"]->AsString(), mod.id);
				src.baseDir = mod.directory;
				if (const std::string_view p = (*a)["path"]->AsString(); !p.empty())
					src.path = mod.directory / p;
				src.compose = std::string((*a)["compose"]->AsString());
				src.biomeTint = std::string((*a)["biomeTint"]->AsString());
				src.reflectivity = std::clamp((*a)["reflectivity"]->AsFloat(0.f), 0.f, 1.f);
				if (const dataio::NodePtr proc = a->Get("procedural")) {
					src.procedural = true;
					const auto &c = (*proc)["color"]->AsArray();
					for (size_t i = 0; i < 3 && i < c.size(); ++i)
						src.color[i] = static_cast<uint8_t>(c[i]->AsInt());
					src.color[3] = 255;
				}
				m_textureAliases.InsertOrAssign(src.id, std::move(src));
			}
		}
		// Structures préfabriquées (world/structures.json).
		if (auto sr = dataio::LoadJsonFile((mod.directory / "world" / "structures.json").string())) {
			for (const dataio::NodePtr &s : sr.Value()->AsArray()) {
				if ((*s)["type"]->AsString() != "structure")
					continue;
				Structure st;
				st.id = BlockRegistry::Qualify((*s)["id"]->AsString(), mod.id);
				const auto &a = (*s)["anchor"]->AsArray();
				for (size_t i = 0; i < 3 && i < a.size(); ++i)
					st.anchor[i] = int(a[i]->AsInt());
				st.replaceSolid = (*s)["replaceSolid"]->AsBool(false);
				for (const auto &[ch, name] : (*s)["palette"]->AsObject()) {
					if (ch.empty())
						continue;
					// Valeur « bloc » ou « bloc@orientation » (nom ou angles « x,y,z »).
					const std::string_view raw = name->AsString();
					const size_t at = raw.find('@');
					st.palette[ch[0]] = BlockRegistry::Qualify(raw.substr(0, at), mod.id);
					if (at != std::string_view::npos) {
						const std::string_view spec = raw.substr(at + 1);
						if (auto o = OrientFromName(spec)) {
							st.orient[ch[0]] = *o;
						} else {
							const auto first = spec.find(','), last = spec.rfind(',');
							if (first != std::string_view::npos && last != first) {
								const auto num = [](std::string_view t) { return std::strtof(std::string(t).c_str(), nullptr); };
								st.orient[ch[0]] = OrientFromAngles({num(spec.substr(0, first)), num(spec.substr(first + 1, last - first - 1)),
																	 num(spec.substr(last + 1))});
							}
						}
					}
				}
				for (const dataio::NodePtr &layer : (*s)["layers"]->AsArray()) {
					std::vector<std::string> rows;
					for (const dataio::NodePtr &row : layer->AsArray())
						rows.emplace_back(row->AsString());
					st.layers.push_back(std::move(rows));
				}
				m_structures.push_back(std::move(st));
			}
		}
		core::log::Info("mod {} ({}) : {} bloc(s), {} script(s)", mod.id, mod.version, mod.blockCount, mod.scripts.size());
	}
	return total;
}

std::optional<TextureSource> ModManager::ResolveTexture(std::string_view qualified) const {
	if (const TextureSource *alias = m_textureAliases.Find(qualified))
		return *alias;
	const size_t colon = qualified.find(':');
	const std::string_view modId = colon == std::string_view::npos ? std::string_view("core") : qualified.substr(0, colon);
	const std::string name(colon == std::string_view::npos ? qualified : qualified.substr(colon + 1));
	auto search = [&](const ModInfo &mod) -> std::optional<TextureSource> {
		// Un nom portant déjà son extension (« crystal_dirt.png ») est un chemin
		// relatif : ne pas y recoller un second « .png ».
		const bool hasExt = name.ends_with(".png");
		for (const char *sub : {"textures/blocks", "textures", "textures/items", "textures/environment"}) {
			const fs::path p = hasExt ? mod.directory / sub / name : mod.directory / sub / (name + ".png");
			std::error_code ec;
			if (fs::exists(p, ec)) {
				TextureSource src;
				src.id = std::string(qualified);
				src.path = p;
				src.baseDir = mod.directory;
				return src;
			}
		}
		return std::nullopt;
	};
	if (const ModInfo *mod = Find(modId))
		if (auto r = search(*mod))
			return r;
	for (const ModInfo &mod : m_mods) // un mod peut référencer les textures d'un autre
		if (auto r = search(mod))
			return r;
	return std::nullopt;
}

void ModManager::SyncToDatabase(WorldStorage &storage) const {
	sql::Database &db = storage.Db();
	auto put = db.Prepare("INSERT OR REPLACE INTO mods VALUES (?, ?, ?, ?, ?)");
	if (put) {
		for (const ModInfo &m : m_mods) {
			const sql::Value row[] = {m.id, m.name, m.version, m.enabled, m.priority};
			(void)db.Execute(put.Value(), row);
		}
	}
	// Configuration aplatie : « player.speed » → « 4.3 » (texte JSON).
	auto flatten = [&](auto &self, const dataio::NodePtr &node, const std::string &prefix) -> void {
		if (node->IsObject()) {
			for (const auto &[k, v] : node->AsObject())
				self(self, v, prefix.empty() ? std::string(k) : prefix + "." + k);
			return;
		}
		storage.SetConfig(prefix, node->IsString() ? std::string(node->AsString()) : dataio::WriteJson(node, -1), "mods");
	};
	flatten(flatten, m_config, "");
}

} // namespace voxel
