// Voxelia — réglages persistants et chargement du contenu des mods.
#include <algorithm>
#include <cctype>
#include <cmath>

#include <core/clock.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>
#include <core/string.hpp>
#include <diagnostics/trace.hpp>

#include "app.hpp"

namespace voxelia {

namespace {

constexpr int kTex = voxel::kTextureSize;				// résolution des textures de blocs
constexpr size_t kLayerBytes = size_t(kTex) * kTex * 4; // octets par couche (RGBA8)

/// Texture de remplacement : couleur dérivée du nom + léger motif (bloc sans image).
sdl3::Surface Placeholder(std::string_view name) {
	const uint64_t h = core::HashString(name);
	const auto r = uint8_t(70 + (h & 0x7f)), g = uint8_t(70 + ((h >> 8) & 0x7f)), b = uint8_t(70 + ((h >> 16) & 0x7f));
	sdl3::Surface img = sdl3::Surface::Solid(kTex, kTex, r, g, b).UnwrapOr(sdl3::Surface{});
	for (int y = 0; y < kTex; ++y)
		for (int x = 0; x < kTex; ++x)
			if (((x ^ y) & 3) == 0) {
				uint8_t *p = &img.Pixels()[size_t(y * kTex + x) * 4];
				p[0] = uint8_t(p[0] * 0.85f);
				p[1] = uint8_t(p[1] * 0.85f);
				p[2] = uint8_t(p[2] * 0.85f);
			}
	return img;
}

std::array<uint8_t, 3> BiomeTint(std::string_view kind) {
	if (kind == "foliage")
		return {96, 166, 64};
	if (kind == "water")
		return {63, 118, 228};
	return {125, 185, 85}; // herbe tempérée
}
/// Image kTex×kTex d'une couche (`texture` ou `texture|teinte`) : procédurale, composée ou chargée.
std::optional<sdl3::Surface> RasterizeLayer(const voxel::ModManager &mods, std::string_view key) {
	const size_t bar = key.find('|');
	const std::string_view texture = std::string_view(key).substr(0, bar);
	const std::string_view blockTint = bar == std::string::npos ? std::string_view{} : std::string_view(key).substr(bar + 1);

	std::optional<sdl3::Surface> img;
	std::string_view tint = blockTint;
	if (texture.find('^') != std::string_view::npos) {
		// Composition « a^b^op(...) » : les termes qualifiés (« core:sand ») sont
		// résolus par le gestionnaire de mods, les chemins relatifs par le composeur.
		sdl3::SurfaceResolver resolve = [&mods](std::string_view term) -> Result<sdl3::Surface, StringView> {
			const auto src = mods.ResolveTexture(term);
			if (!src)
				return Err(StringView("terme de texture inconnu"));
			if (src->procedural)
				return sdl3::Surface::Solid(kTex, kTex, src->color[0], src->color[1], src->color[2], 255);
			if (!src->compose.empty())
				return sdl3::ComposeSurface(src->compose, src->baseDir, kTex);
			return sdl3::LoadImageRgba(src->path); // pleine résolution (frame/resize s'appliquent ensuite)
		};
		if (auto r = sdl3::ComposeSurface(texture, {}, kTex, resolve))
			img = std::move(r.Value());
	} else if (auto src = mods.ResolveTexture(texture)) {
		if (src->procedural) {
			if (auto r = sdl3::Surface::Solid(kTex, kTex, src->color[0], src->color[1], src->color[2], 255))
				img = std::move(r.Value());
		} else if (!src->compose.empty()) {
			if (auto r = sdl3::ComposeSurface(src->compose, src->baseDir, kTex))
				img = std::move(r.Value());
		} else if (auto loaded = sdl3::LoadImageRgba(src->path)) {
			if (auto r = sdl3::SquareThumbnail(*loaded, kTex))
				img = std::move(r.Value());
		}
		if (!src->biomeTint.empty())
			tint = src->biomeTint;
	}
	if (img && !tint.empty()) {
		const auto t = BiomeTint(tint);
		sdl3::Tint(*img, t[0], t[1], t[2]);
	}
	return img;
}

} // namespace

bool Content::Load(const fs::path &modsDir, const std::set<std::string> &disabled, jobs::ThreadPool &pool) {
	DIAG_ZONE("Content::Load");
	core::Stopwatch sw;
	blocks = voxel::BlockRegistry();
	skins.clear();
	mods.Discover(modsDir, disabled);
	const size_t blockCount = mods.LoadContent(blocks);

	// 0) Skins du joueur : chaque fichier `textures/skins/skin*.png` d'un mod
	//    devient un skin (les masques, boutons et calques de transformation sont
	//    exclus). Les scripts des mods peuvent en ajouter d'autres via voxel.skin.
	for (const voxel::ModInfo &mod : mods.Mods()) {
		if (!mod.enabled)
			continue;
		std::error_code ec;
		std::vector<fs::path> files;
		for (const auto &f : fs::directory_iterator(mod.directory / "textures" / "skins", ec))
			if (f.path().extension() == ".png")
				files.push_back(f.path());
		std::ranges::sort(files);
		for (const fs::path &f : files) {
			const std::string stem = f.stem().string();
			if (!stem.starts_with("skin"))
				continue;
			const std::string lower = String(stem).ToLower();
			if (lower.find("mask") != std::string::npos || lower.find("button") != std::string::npos ||
				lower.find("transform") != std::string::npos)
				continue;
			const std::string id = mod.id + ":skins/" + stem;
			RegisterSkin({id, SkinName(id), id, mod.id});
		}
	}
	// Skins mis en avant par la configuration (« skins.featured »), y compris hors du
	// dossier skins (steve, alex) ; ils passent en tête, les numérotés suivent dans l'ordre.
	std::vector<std::string> featured;
	for (const dataio::NodePtr &f : mods.Config()->At("skins.featured")->AsArray())
		featured.emplace_back(f->AsString());
	for (const std::string &id : featured)
		if (!FindSkin(id) && SkinTexturePath(id))
			RegisterSkin({id, SkinName(id), id, id.substr(0, id.find(':'))});
	auto rank = [&](const SkinDef &sk) -> std::pair<int64_t, std::string> {
		if (const auto it = std::ranges::find(featured, sk.id); it != featured.end())
			return {it - featured.begin() - 1'000'000, sk.id};
		const size_t dot = sk.id.rfind("skin.");
		if (dot != std::string::npos) {
			const char *num = sk.id.c_str() + dot + 5;
			if (std::isdigit(static_cast<unsigned char>(*num)))
				return {std::atoll(num), sk.id};
		}
		return {1'000'000'000, sk.id};
	};
	std::ranges::stable_sort(skins, [&](const SkinDef &a, const SkinDef &b) { return rank(a) < rank(b); });

	// Recettes de fabrication (recipes/*.json des mods).
	recipes.clear();
	for (const voxel::ModInfo &mod : mods.Mods()) {
		if (!mod.enabled)
			continue;
		std::error_code ec;
		for (const auto &f : fs::directory_iterator(mod.directory / "recipes", ec))
			if (f.path().extension() == ".json") {
				auto rd = dataio::LoadJsonFile(f.path().string());
				if (!rd)
					continue;
				for (const dataio::NodePtr &j : rd.Value()->AsArray()) {
					if ((*j)["type"]->AsString() != "recipe")
						continue;
					Recipe rec;
					rec.id = std::string((*j)["id"]->AsString());
					for (const dataio::NodePtr &p : (*j)["pattern"]->AsArray())
						rec.pattern.emplace_back(p->AsString());
					for (const auto &[ch, name] : (*j)["key"]->AsObject())
						if (!ch.empty())
							rec.key[ch[0]] = std::string(name->AsString());
					for (const dataio::NodePtr &ing : (*j)["ingredients"]->AsArray())
						rec.ingredients.emplace_back(ing->AsString());
					if (const dataio::NodePtr res = j->Get("result"); res && !res->IsNone()) {
						if (res->IsString())
							rec.result = std::string(res->AsString());
						else {
							rec.result = std::string((*res)["item"]->AsString());
							rec.count = int((*res)["count"]->AsInt(1));
						}
					}
					if (!rec.result.empty())
						recipes.push_back(std::move(rec));
				}
			}
	}
	core::log::Info("recettes : {} chargée(s)", recipes.size());
	LoadItems();
	LoadEntityRigs();
	LoadContentDefinitions();

	const size_t layerNames = BuildLayers(pool);
	loadSeconds = sw.Seconds();
	core::log::Info("contenu : {} mod(s), {} bloc(s), {} texture(s) ({} manquante(s)) en {:.2f} s", mods.Mods().size(), blockCount,
					layerNames, missingTextures, loadSeconds);
	return true;
}

size_t Content::BuildLayers(jobs::ThreadPool &pool) {
	// 1) Noms de textures uniques → index de couche.
	layerOf.Clear();
	std::vector<std::string> names;
	for (size_t id = 1; id < blocks.Count(); ++id) {
		voxel::BlockDef &def = blocks.Mutable(voxel::BlockId(id));
		// Réflectance héritée de la texture principale si le bloc n'en définit pas.
		if (def.reflectivity <= 0.f)
			if (auto src = mods.ResolveTexture(def.textures[0]))
				def.reflectivity = src->reflectivity;
		for (size_t f = 0; f < 6; ++f) {
			// Teinte de biome du bloc (feuillages, herbes) : une couche par (texture, teinte).
			const std::string key = def.biomeTint.empty() ? def.textures[f] : def.textures[f] + "|" + def.biomeTint;
			auto [slot, inserted] = layerOf.TryEmplace(key, uint16_t(names.size()));
			if (inserted)
				names.push_back(key);
			def.layers[f] = *slot;
		}
	}
	// 2) Rastérisation parallèle (images de mods parfois énormes : bandes animées).
	layerCount = uint32_t(std::max<size_t>(names.size(), 1));
	textureLayers.assign(size_t(layerCount) * kLayerBytes, 255);
	std::atomic<size_t> missing{0};
	jobs::ParallelFor(pool, 0, names.size(), 8, [&](size_t i) {
		std::optional<sdl3::Surface> img = RasterizeLayer(mods, names[i]);
		if (!img) {
			img = Placeholder(names[i]);
			if (++missing <= 12)
				core::log::Debug("texture introuvable : {}", names[i]);
		}
		std::memcpy(textureLayers.data() + i * kLayerBytes, img->Pixels(), kLayerBytes);
	});
	missingTextures = missing.load();

	// 3) Tables de maillage et barre d'outils par défaut (blocs pleins courants).
	meshContext = voxel::MeshContext::Build(blocks);
	syncedBlocks = blocks.Count();
	hotbarCandidates.clear();
	for (const char *n : {"core:grass", "core:dirt", "core:stone", "core:cobble", "core:oak_planks", "core:oak_log", "core:glass",
						  "core:bricks", "core:glowstone", "core:sand", "core:water", "core:oak_leaves"}) {
		if (const voxel::BlockId id = blocks.Find(n); id != voxel::kAir)
			hotbarCandidates.push_back(id);
		if (hotbarCandidates.size() == 9)
			break;
	}
	return names.size();
}

bool Content::ReloadDefinitions(jobs::ThreadPool &pool) {
	DIAG_ZONE("Content::ReloadDefinitions");
	// Même registre : un bloc redéfini garde son identifiant (les chunks restent valides),
	// un nouveau bloc est ajouté à la fin.
	const size_t before = blocks.Count();
	mods.LoadContent(blocks);
	BuildLayers(pool);
	LoadItems();
	LoadEntityRigs();
	LoadContentDefinitions();
	core::log::Info("contenu rechargé : {} bloc(s) ({} nouveau(x)), {} couche(s) de texture", blocks.Count(), blocks.Count() - before, layerCount);
	return true;
}

bool Content::SyncNewBlocks() {
	if (blocks.Count() == syncedBlocks)
		return false;
	bool newLayers = false;
	for (size_t id = syncedBlocks; id < blocks.Count(); ++id) {
		voxel::BlockDef &def = blocks.Mutable(voxel::BlockId(id));
		for (size_t f = 0; f < 6; ++f) {
			auto [slot, inserted] = layerOf.TryEmplace(def.textures[f], uint16_t(layerCount));
			if (inserted) {
				std::optional<sdl3::Surface> img;
				if (auto src = mods.ResolveTexture(def.textures[f]); src && !src->path.empty())
					if (auto loaded = sdl3::LoadImageRgba(src->path))
						if (auto r = sdl3::SquareThumbnail(*loaded, kTex))
							img = std::move(r.Value());
				if (!img)
					img = Placeholder(def.textures[f]);
				const std::vector<uint8_t> rgba = img->ToRgba8();
				textureLayers.insert(textureLayers.end(), rgba.begin(), rgba.end());
				++layerCount;
				newLayers = true;
			}
			def.layers[f] = *slot;
		}
	}
	syncedBlocks = blocks.Count();
	meshContext = voxel::MeshContext::Build(blocks);
	return newLayers;
}

void Content::RegisterSkin(SkinDef skin) {
	for (SkinDef &s : skins) {
		if (s.id == skin.id) {
			s = std::move(skin);
			return;
		}
	}
	skins.push_back(std::move(skin));
}

const SkinDef *Content::FindSkin(std::string_view id) const noexcept {
	for (const SkinDef &s : skins)
		if (s.id == id)
			return &s;
	return nullptr;
}

std::optional<fs::path> Content::SkinTexturePath(std::string_view texture) const {
	if (auto src = mods.ResolveTexture(texture); src && !src->path.empty())
		return src->path;
	return std::nullopt;
}

const EntityRig *Content::FindEntityRig(std::string_view kind) const noexcept {
	const auto it = entityRigs.find(kind);
	return it == entityRigs.end() ? nullptr : &it->second;
}

/// Qualifie un identifiant (« ghast » → « core:ghast ») sans toucher aux déjà qualifiés.
static std::string Qualify(std::string_view mod, std::string_view id) {
	return id.find(':') == std::string_view::npos ? std::string(mod) + ":" + std::string(id) : std::string(id);
}

void Content::LoadEntityRigs() {
	entityRigs.clear();
	rigModels.clear();
	for (const voxel::ModInfo &mod : mods.Mods()) {
		if (!mod.enabled)
			continue;
		std::error_code ec;
		// Rigs des modèles (`models/*.json`) : parties, pivots, animations.
		for (const auto &f : fs::directory_iterator(mod.directory / "models", ec))
			if (f.path().extension() == ".json") {
				auto parsed = dataio::LoadJsonFile(f.path().string());
				if (!parsed)
					continue;
				for (const dataio::NodePtr &def : parsed.Value()->AsArray()) {
					if ((*def)["format"]->AsString() != "rig")
						continue;
					auto m = voxel::ParseRig(def);
					if (!m)
						continue;
					const std::string id = Qualify(mod.id, (*def)["id"]->AsString());
					m.Value().id = id;
					rigModels[id] = std::move(m.Value());
				}
			}
		// Définitions de créatures (`creatures/*.json`) : kind → rig + skin.
		for (const auto &f : fs::directory_iterator(mod.directory / "creatures", ec))
			if (f.path().extension() == ".json") {
				auto parsed = dataio::LoadJsonFile(f.path().string());
				if (!parsed)
					continue;
				for (const dataio::NodePtr &def : parsed.Value()->AsArray()) {
					const dataio::NodePtr &rig = (*def)["rig"];
					if (!rig->IsObject())
						continue;
					EntityRig er;
					er.model = Qualify(mod.id, (*rig)["model"]->AsString());
					er.scale = (*rig)["scale"]->AsFloat(1.f);
					if (const dataio::NodePtr &skinList = (*rig)["skins"]; skinList->IsArray() && skinList->Size() > 0)
						er.skin = std::string((*skinList)[0]->AsString());
					if (er.model.empty() && er.skin.empty())
						continue;
					entityRigs[Qualify(mod.id, (*def)["id"]->AsString())] = std::move(er);
				}
			}
	}
	core::log::Info("rigs : {} entité(s), {} modèle(s)", entityRigs.size(), rigModels.size());
}

void Content::LoadContentDefinitions() {
	contentDefs.clear();
	for (const voxel::ModInfo &mod : mods.Mods()) {
		if (!mod.enabled)
			continue;
		std::error_code ec;
		auto load = [&](const fs::path &file, std::string_view category) {
			auto parsed = dataio::LoadJsonFile(file.string());
			if (!parsed)
				return;
			for (const dataio::NodePtr &def : parsed.Value()->AsArray()) {
				const std::string_view id = (*def)["id"]->AsString();
				if (id.empty())
					continue;
				contentDefs[std::string(category) + ":" + voxel::BlockRegistry::Qualify(id, mod.id)] = def;
			}
		};
		if (fs::exists(mod.directory / "blocks.json"))
			load(mod.directory / "blocks.json", "block");
		for (const auto &f : fs::directory_iterator(mod.directory / "blocks", ec))
			if (f.path().extension() == ".json")
				load(f.path(), "block");
		for (const auto &f : fs::directory_iterator(mod.directory / "items", ec))
			if (f.path().extension() == ".json")
				load(f.path(), "item");
		for (const auto &f : fs::directory_iterator(mod.directory / "recipes", ec))
			if (f.path().extension() == ".json")
				load(f.path(), "recipe");
		for (const auto &f : fs::directory_iterator(mod.directory / "creatures", ec))
			if (f.path().extension() == ".json")
				load(f.path(), "entity");
	}
	core::log::Info("définitions : {} (bloc/objet/recette/entité)", contentDefs.size());
}

std::optional<sdl3::Surface> Content::EntitySkinImage(const EntityRig &rig) const {
	if (rig.skin.empty())
		return std::nullopt;
	// Découpe « base^op(...)^fichier… » en termes séparés par « ^ ».
	std::vector<std::string_view> parts;
	std::string_view s(rig.skin);
	size_t start = 0;
	for (size_t i = 0; i <= s.size(); ++i)
		if (i == s.size() || s[i] == '^') {
			if (i > start)
				parts.push_back(s.substr(start, i - start));
			start = i + 1;
		}
	if (parts.empty())
		return std::nullopt;
	// Base (terme qualifié ou chemin relatif).
	auto src = mods.ResolveTexture(parts[0]);
	std::optional<sdl3::Surface> img;
	if (src && !src->path.empty()) {
		if (auto r = sdl3::LoadImageRgba(src->path))
			img = std::move(r.Value());
	} else if (src && !src->compose.empty()) {
		if (auto r = sdl3::ComposeSurface(src->compose, src->baseDir, 64))
			img = std::move(r.Value());
	}
	if (!img)
		return std::nullopt;
	// Opérations et superpositions.
	for (size_t i = 1; i < parts.size(); ++i) {
		const std::string_view p = parts[i];
		const size_t open = p.find('(');
		if (open != std::string_view::npos && p.ends_with(')')) { // op « nom(args) »
			const std::string_view op = p.substr(0, open);
			std::vector<float> args;
			std::string_view body = p.substr(open + 1, p.size() - open - 2);
			size_t j = 0;
			while (j < body.size()) {
				while (j < body.size() && (body[j] == ' ' || body[j] == ','))
					++j;
				char *end = nullptr;
				const std::string token(body.substr(j));
				const float v = std::strtof(token.c_str(), &end);
				if (end == token.c_str())
					break;
				args.push_back(v);
				j += size_t(end - token.c_str());
			}
			if (op == "colorize" && args.size() >= 4)
				sdl3::Colorize(*img, uint8_t(std::clamp(args[0], 0.f, 255.f)), uint8_t(std::clamp(args[1], 0.f, 255.f)),
							   uint8_t(std::clamp(args[2], 0.f, 255.f)), args[3]);
			else if (op == "tint" && args.size() >= 3)
				sdl3::Tint(*img, uint8_t(std::clamp(args[0], 0.f, 255.f)), uint8_t(std::clamp(args[1], 0.f, 255.f)),
						  uint8_t(std::clamp(args[2], 0.f, 255.f)));
			else if (op == "grayscale")
				sdl3::Grayscale(*img);
		} else { // superposition d'un terme (fichier relatif ou qualifié), même taille
			if (auto topSrc = mods.ResolveTexture(p); topSrc && !topSrc->path.empty())
				if (auto top = sdl3::LoadImageRgba(topSrc->path); top && top->GetWidth() == img->GetWidth() && top->GetHeight() == img->GetHeight())
					sdl3::Overlay(*img, *top);
		}
	}
	return img;
}

} // namespace voxelia

namespace voxelia {

size_t Content::ReloadTextureFiles(std::span<const fs::path> files) {
	size_t updated = 0;
	for (const auto &e : layerOf) {
		const std::string_view texture = std::string_view(e.key).substr(0, e.key.find('|'));
		const auto src = mods.ResolveTexture(texture);
		if (!src)
			continue;
		const bool touched = std::ranges::any_of(files, [&](const fs::path &f) {
			std::error_code ec;
			return (!src->path.empty() && fs::equivalent(src->path, f, ec)) ||
				   (!src->compose.empty() && src->compose.find(f.filename().string()) != std::string::npos);
		});
		if (!touched)
			continue;
		if (auto img = RasterizeLayer(mods, e.key); img && size_t(img->GetWidth()) * size_t(img->GetHeight()) * 4 == kLayerBytes) {
			std::memcpy(textureLayers.data() + size_t(e.value) * kLayerBytes, img->Pixels(), kLayerBytes);
			++updated;
		}
	}
	return updated;
}

// ── Objets des mods (items/*.json) ──────────────────────────────────────────

bool ItemDef::HasTag(std::string_view t) const noexcept { return std::ranges::find(tags, t) != tags.end(); }

float ItemDef::MiningSpeed(std::string_view bestTool) const noexcept {
	if (!tool)
		return 1.f;					// main nue
	if (bestTool.empty())			// le bloc se mine à la main : tout outil aide
		return tool->speed;
	if (bestTool == tool->kind)		// bon outil
		return tool->speed;
	return 1.f;						// mauvais outil : comme à la main
}

std::optional<ArmorSlot> ParseArmorSlot(std::string_view id) noexcept {
	if (id == "head")
		return ArmorSlot::Head;
	if (id == "chest")
		return ArmorSlot::Chest;
	if (id == "legs")
		return ArmorSlot::Legs;
	if (id == "feet")
		return ArmorSlot::Feet;
	return std::nullopt;
}

void Content::LoadItems() {
	DIAG_ZONE("Content::LoadItems");
	// Les identifiants restent stables au rechargement : un objet existant garde son indice.
	size_t loaded = 0;
	for (const voxel::ModInfo &mod : mods.Mods()) {
		if (!mod.enabled)
			continue;
		std::error_code ec;
		std::vector<fs::path> files;
		for (const auto &f : fs::directory_iterator(mod.directory / "items", ec))
			if (f.path().extension() == ".json")
				files.push_back(f.path());
		std::ranges::sort(files);
		for (const fs::path &file : files) {
			auto doc = dataio::LoadJsonFile(file.string());
			if (!doc)
				continue;
			for (const dataio::NodePtr &j : doc.Value()->AsArray()) {
				const std::string_view type = (*j)["type"]->AsString();
				if ((type != "item" && type != "tool") || (*j)["id"]->AsString().empty())
					continue;
				ItemDef def;
				def.name = voxel::BlockRegistry::Qualify((*j)["id"]->AsString(), mod.id);
				def.displayName = std::string((*j)["displayName"]->AsString(def.name));
				def.mod = mod.id;
				for (const dataio::NodePtr &t : (*j)["tags"]->AsArray())
					def.tags.emplace_back(t->AsString());
				def.maxStack = int(std::clamp<int64_t>((*j)["maxStack"]->AsInt(type == "tool" ? 1 : 64), 1, 999));
				if (const dataio::NodePtr tool = j->Get("tool"); tool && tool->IsObject()) {
					ToolDef td;
					td.kind = std::string((*tool)["kind"]->AsString());
					td.speed = std::max(0.1f, (*tool)["speed"]->AsFloat(1.f));
					td.damage = int(std::max<int64_t>(0, (*tool)["damage"]->AsInt(1)));
					td.level = int((*tool)["level"]->AsInt(0));
					def.tool = std::move(td);
				}
				if (const dataio::NodePtr armor = j->Get("armor"); armor && armor->IsObject()) {
					def.armorSlot = std::string((*armor)["slot"]->AsString());
					def.armorPoints = int((*armor)["points"]->AsInt(0));
				}
				// Icône : « core:items/leather_helmet » (ou nom de texture du mod).
				const std::string icon = std::string((*j)["icon"]->AsString(def.name));
				std::optional<sdl3::Surface> img;
				for (std::string candidate : {icon, voxel::BlockRegistry::Qualify(icon, mod.id)}) {
					if (auto src = mods.ResolveTexture(candidate); src && !src->path.empty())
						if (auto r = sdl3::LoadImageRgba(src->path))
							img = std::move(r.Value());
					if (!img)
						if (const size_t colon = candidate.find(':'); colon != std::string::npos) {
							const fs::path p = mod.directory / "textures" / (candidate.substr(colon + 1) + ".png");
							if (fs::exists(p, ec))
								if (auto r = sdl3::LoadImageRgba(p))
									img = std::move(r.Value());
						}
					if (img)
						break;
				}
				if (img) {
					const sdl3::Surface small = sdl3::SquareThumbnail(*img, 16).UnwrapOr(sdl3::Surface{});
					def.icon = small.ToRgba8();
					uint64_t acc[3] = {}, n = 0;
					for (size_t p = 0; p + 3 < def.icon.size(); p += 4)
						if (def.icon[p + 3] > 128) {
							for (size_t k = 0; k < 3; ++k)
								acc[k] += def.icon[p + k];
							++n;
						}
					if (n)
						def.color = sdl3::Rgba(uint8_t(acc[0] / n), uint8_t(acc[1] / n), uint8_t(acc[2] / n));
				}
				if (auto it = itemIds.find(def.name); it != itemIds.end())
					items[it->second - kItemBase] = std::move(def);
				else {
					itemIds.emplace(def.name, ItemId(kItemBase + items.size()));
					items.push_back(std::move(def));
				}
				++loaded;
			}
		}
	}
	core::log::Info("objets : {} chargé(s)", loaded);
}

ItemId Content::FindItem(std::string_view name) const {
	if (name.empty())
		return 0;
	if (const voxel::BlockId b = blocks.Find(name); b != voxel::kAir)
		return ItemId(b);
	if (auto it = itemIds.find(name); it != itemIds.end())
		return it->second;
	if (name.find(':') == std::string_view::npos)
		if (auto it = itemIds.find("core:" + std::string(name)); it != itemIds.end())
			return it->second;
	return 0;
}

const ItemDef *Content::Item(ItemId id) const noexcept {
	if (id < kItemBase || id - kItemBase >= items.size())
		return nullptr;
	return &items[id - kItemBase];
}

std::string Content::ItemName(ItemId id) const {
	if (IsBlockItem(id))
		return id < blocks.Count() ? blocks.Get(voxel::BlockId(id)).name : std::string();
	const ItemDef *d = Item(id);
	return d ? d->name : std::string();
}

std::string Content::ItemLabel(ItemId id) const {
	if (IsBlockItem(id))
		return id < blocks.Count() ? blocks.Get(voxel::BlockId(id)).displayName : std::string();
	const ItemDef *d = Item(id);
	return d ? d->displayName : std::string();
}

int Content::MaxStack(ItemId id) const noexcept {
	if (const ItemDef *d = Item(id))
		return d->maxStack;
	return 64;
}

} // namespace voxelia
