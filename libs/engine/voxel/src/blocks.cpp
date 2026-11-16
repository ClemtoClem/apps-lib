#include "voxel/blocks.hpp"

#include <algorithm>
#include <cstdlib>

#include <core/log.hpp>
#include <fstream>
#include <iterator>

#include "voxel/model.hpp"
#include "voxel/orientation.hpp"

namespace voxel {

bool BlockDef::HasTag(std::string_view t) const noexcept { return std::ranges::find(tags, t) != tags.end(); }

BlockRegistry::BlockRegistry() {
	BlockDef air;
	air.name = "core:air";
	air.displayName = "Air";
	air.material = Material::Air;
	air.solid = false;
	Register(std::move(air));
}

std::string BlockRegistry::Qualify(std::string_view name, std::string_view modId) {
	if (name.find(':') != std::string_view::npos)
		return std::string(name);
	return std::string(modId) + ":" + std::string(name);
}

BlockId BlockRegistry::Register(BlockDef def) {
	BlockId id;
	if (const BlockId *existing = m_byName.Find(def.name)) {
		id = *existing;
		m_defs[id] = std::move(def);
	} else {
		if (m_defs.size() >= 0xFFFF) {
			core::log::Error("blocs : limite de 65535 types atteinte");
			return kAir;
		}
		id = static_cast<BlockId>(m_defs.size());
		m_byName.InsertOrAssign(def.name, id);
		m_defs.push_back(std::move(def));
		m_opaque.push_back(0);
		m_solid.push_back(0);
		m_material.push_back(Material::Air);
	}
	const BlockDef &d = m_defs[id];
	m_opaque[id] = d.IsOpaque();
	m_solid[id] = d.solid;
	m_material[id] = d.material;
	return id;
}

namespace {

std::vector<BlockBox> BoxList(const dataio::Json &list) {
	std::vector<BlockBox> out;
	for (const ModelBox &b : ParseBoxList(list)) {
		const math::FVector3 to = b.To();
		out.push_back({b.from.x, b.from.y, b.from.z, to.x, to.y, to.z});
	}
	return out;
}

/// Triangles d'un maillage .obj / .b3d (unités bloc).
std::vector<BlockTriangle> LoadMesh(const std::filesystem::path &file, bool b3d) {
	std::ifstream f(file, std::ios::binary);
	if (!f)
		return {};
	const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	Result<Model, std::string> model = b3d ? ParseB3d(std::span(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size())) : ParseObj(bytes);
	if (!model) {
		core::log::Warn("maillage {} : {}", file.string(), model.Error());
		return {};
	}
	std::vector<ModelQuad> quads;
	BuildQuads(model.Value(), {}, quads);
	std::vector<BlockTriangle> out;
	out.reserve(quads.size());
	for (const ModelQuad &q : quads) {
		BlockTriangle t;
		for (int k = 0; k < 3; ++k) {
			t.position[size_t(k * 3)] = q.p[k].x;
			t.position[size_t(k * 3 + 1)] = q.p[k].y;
			t.position[size_t(k * 3 + 2)] = q.p[k].z;
			t.uv[size_t(k * 2)] = q.uv[k].x;
			t.uv[size_t(k * 2 + 1)] = q.uv[k].y;
		}
		out.push_back(t);
	}
	return out;
}

} // namespace

bool BlockRegistry::Connects(const ConnectRule &rule, BlockId other) const noexcept {
	if (other == kAir || other >= m_defs.size())
		return false;
	const BlockDef &d = m_defs[other];
	for (const std::string &t : rule.to) {
		if (t == "#solid") {
			if (m_opaque[other])
				return true;
		} else if (t.starts_with('#')) {
			if (d.HasTag(std::string_view(t).substr(1)))
				return true;
		} else if (t == d.name) {
			return true;
		}
	}
	return false;
}

size_t BlockRegistry::LoadDefinitions(const dataio::Json &list, std::string_view modId, const std::filesystem::path &baseDir) {
	size_t n = 0;
	for (const dataio::Json &j : list.AsArray()) {
		const std::string_view rawId = j["id"].AsString();
		if (rawId.empty())
			continue;
		BlockDef def;
		def.name = Qualify(rawId, modId);
		def.mod = std::string(modId);
		def.displayName = std::string(j["displayName"].AsString(rawId));
		const dataio::Json &g = j["graphics"];
		const std::string_view mat = g["material"].AsString("OPAQUE");
		if (mat == "CUTOUT")
			def.material = Material::Cutout;
		else if (mat == "TRANSLUCENT")
			def.material = Material::Translucent;
		else if (mat == "TRANSPARENT")
			def.material = j["tags"].IsArray() && std::ranges::any_of(j["tags"].AsArray(), [](const dataio::Json &t) { return t.AsString() == "fluid"; })
							   ? Material::Liquid
							   : Material::Translucent;
		const std::string single(g["texture"].AsString());
		def.textures.fill(single.empty() ? def.name : single);
		if (const dataio::Json *tex = g.Find("textures")) {
			auto pick = [&](std::string_view key, std::string_view fallback) {
				const std::string_view v = (*tex)[key].AsString();
				return std::string(v.empty() ? fallback : v);
			};
			const std::string side = pick("side", def.textures[0]);
			def.textures[static_cast<int>(Face::PosY)] = pick("top", side);
			def.textures[static_cast<int>(Face::NegY)] = pick("bottom", side);
			def.textures[static_cast<int>(Face::PosX)] = pick("east", side);
			def.textures[static_cast<int>(Face::NegX)] = pick("west", side);
			def.textures[static_cast<int>(Face::PosZ)] = pick("south", side);
			def.textures[static_cast<int>(Face::NegZ)] = pick("north", side);
		}
		for (auto &t : def.textures)
			t = Qualify(t, modId);
		def.light = static_cast<uint8_t>(std::clamp<int64_t>(g["light"].AsInt(0), 0, 15));
		def.biomeTint = std::string(g["biomeTint"].AsString());
		def.reflectivity = std::clamp(g["reflectivity"].AsFloat(0.f), 0.f, 1.f);
		for (const dataio::Json &t : j["tags"].AsArray())
			def.tags.emplace_back(t.AsString());
		const dataio::Json &gp = j["gameplay"];
		def.hardness = gp["hardness"].AsFloat(1.0f);
		def.bestTool = std::string(gp["tool"].AsString());
		if (const std::string_view snd = j["sounds"].AsString(); !snd.empty()) {
			const size_t colon = snd.find(':');
			def.soundGroup = std::string(colon == std::string_view::npos ? snd : snd.substr(colon + 1));
		}
		def.solid = gp["solid"].AsBool(def.material != Material::Liquid && def.material != Material::Cutout);
		def.cross = def.material == Material::Cutout && (def.HasTag("plant") || def.HasTag("flower") || def.HasTag("crop") || def.HasTag("sapling"));
		if (j["shape"].AsString() == "cross" && def.material == Material::Cutout)
			def.cross = true;
		if (def.material == Material::Cutout && !def.cross)
			def.solid = gp["solid"].AsBool(true); // feuilles : pleines mais à trous
		// Liquides : écoulement, viscosité, réactions.
		if (const dataio::Json *liq = j.Find("liquid"); liq && liq->IsObject()) {
			def.liquidRange = static_cast<uint8_t>(std::clamp<int64_t>((*liq)["range"].AsInt(0), 0, 15));
			def.liquidTick = (*liq)["tick"].AsFloat(1.f);
			def.renewable = (*liq)["renewable"].AsBool(false);
			def.flowingTexture = std::string((*liq)["flowingTexture"].AsString());
			for (const dataio::Json &m : (*liq)["mix"].AsArray()) {
				BlockDef::LiquidMix mix;
				mix.with = std::string(m["with"].AsString());
				mix.source = std::string(m["source"].AsString());
				mix.flowing = std::string(m["flowing"].AsString());
				if (!mix.with.empty())
					def.liquidMix.push_back(std::move(mix));
			}
		}
		// Blocs soumis à la gravité (sable, gravier).
		if (const dataio::Json *sim = j.Find("simulation"); sim && (*sim)["falling"]["enabled"].AsBool(false))
			def.falling = true;
		// Explosions : résistance aux explosions et blocs explosifs (TNT).
		def.blastResistance = gp["blastResistance"].AsFloat(std::max(0.f, def.hardness));
		if (const dataio::Json *ex = gp.Find("explosion"); ex && ex->IsObject()) {
			def.explosionRadius = (*ex)["radius"].AsFloat(0.f);
			def.explosionDelay = (*ex)["delay"].AsFloat(0.f);
		}
		// Forme : nodebox, maillage, collision, connectivité.
		if (const dataio::Json *nb = j.Find("nodebox"))
			def.boxes = BoxList(nb->IsObject() ? (*nb)["fixed"] : *nb);
		const std::string_view type = j["type"].AsString();
		if ((type == "obj" || type == "b3d") && !j["path"].AsString().empty() && !baseDir.empty())
			def.mesh = LoadMesh(baseDir / j["path"].AsString(), type == "b3d");
		if (def.HasShape())
			def.cross = false;
		const dataio::Json &col = j["physics"]["collision"];
		if (col.IsObject()) {
			const std::string_view shape = col["shape"].AsString(col["type"].AsString());
			if (shape == "FROM_NODEBOX")
				def.collision = def.boxes;
			else if (const dataio::Json *fixed = col.Find("fixed"))
				def.collision = BoxList(*fixed);
		} else if (!def.boxes.empty()) {
			def.collision = def.boxes; // nodebox : la collision suit la forme
		}
		if (const dataio::Json *con = j.Find("connects"); con && con->IsObject()) {
			for (const dataio::Json &t : (*con)["to"].AsArray()) {
				const std::string_view v = t.AsString();
				def.connect.to.push_back(v.starts_with('#') ? std::string(v) : Qualify(v, modId));
			}
			static constexpr std::pair<const char *, Face> kFaces[] = {{"east", Face::PosX}, {"west", Face::NegX}, {"up", Face::PosY},
																	   {"down", Face::NegY}, {"south", Face::PosZ}, {"north", Face::NegZ}};
			for (const auto &[key, face] : kFaces)
				if (const dataio::Json *boxes = con->Find(key))
					def.connect.faces[size_t(face)] = BoxList(*boxes);
		}
		// Interaction (clic droit) : bascule, action (s'asseoir, dormir), fonction de script.
		if (const dataio::Json *in = j.Find("interact"); in && in->IsObject()) {
			auto qualified = [&](std::string_view v) { return v.empty() ? std::string() : Qualify(v, modId); };
			def.interact.toggle = qualified((*in)["toggle"].AsString());
			def.interact.action = std::string((*in)["action"].AsString());
			def.interact.call = std::string((*in)["call"].AsString());
			const std::string_view sound = (*in)["sound"].AsString();
			def.interact.sound = sound.empty() ? std::string() : Qualify(sound, modId);
		}
		if (const dataio::Json *parts = j.Find("parts"); parts && parts->IsArray())
			for (const dataio::Json &pj : parts->AsArray()) {
				BlockPart part;
				const auto &o = pj["offset"].AsArray();
				for (size_t a = 0; a < 3 && a < o.size(); ++a)
					part.offset[a] = int(o[a].AsInt());
				part.block = Qualify(pj["block"].AsString(), modId);
				if (!pj["block"].AsString().empty())
					def.parts.push_back(std::move(part));
			}
		if (const dataio::Json *po = j.Find("partOf"); po && po->IsArray() && po->AsArray().size() >= 3) {
			def.isPart = true;
			for (size_t a = 0; a < 3; ++a)
				def.partOf[a] = int(po->AsArray()[a].AsInt());
		}
		if (const std::string_view icon = j["icon"].AsString(); !icon.empty())
			def.icon = Qualify(icon, modId);
		def.item = j["item"].AsBool(true);
		// Orientation (placement) : « orientation: { auto, default, allowed, forbidden } ».
		if (const dataio::Json &orient = j["orientation"]; orient.IsObject()) {
			def.orientAuto = orient["auto"].AsBool(true);
			if (const std::string_view d = orient["default"].AsString(); !d.empty())
				if (auto o = OrientFromName(d))
					def.orientDefault = *o;
			auto fill = [&](const char *key, std::vector<uint8_t> &dst) {
				for (const dataio::Json &v : orient[key].AsArray())
					if (auto o = OrientFromName(v.AsString()))
						dst.push_back(*o);
					else if (const auto comma = std::string_view(v.AsString()).find(','); comma != std::string_view::npos) {
						const std::string_view s = v.AsString();
						const auto num = [](std::string_view x) { return std::strtof(std::string(x).c_str(), nullptr); };
						const auto a = s.find(','), b = s.rfind(',');
						dst.push_back(OrientFromAngles({num(s.substr(0, a)), num(s.substr(a + 1, b - a - 1)), num(s.substr(b + 1))}));
					}
			};
			fill("allowed", def.orientAllowed);
			fill("forbidden", def.orientForbidden);
		}
		Register(std::move(def));
		++n;
	}
	return n;
}

BlockId BlockRegistry::Find(std::string_view name) const noexcept {
	const BlockId *id = m_byName.Find(name);
	if (!id && name.find(':') == std::string_view::npos) {
		const std::string q = "core:" + std::string(name);
		id = m_byName.Find(std::string_view(q));
	}
	return id ? *id : kAir;
}

BlockId BlockRegistry::FindOrCreate(std::string_view name) {
	if (name.empty() || name == "core:air" || name == "air")
		return kAir;
	if (const BlockId id = Find(name); id != kAir)
		return id;
	BlockDef def;
	def.name = name.find(':') == std::string_view::npos ? "core:" + std::string(name) : std::string(name);
	def.displayName = def.name;
	def.textures.fill(def.name);
	def.mod = def.name.substr(0, def.name.find(':'));
	return Register(std::move(def));
}

} // namespace voxel
