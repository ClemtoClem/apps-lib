#include "voxel/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>

namespace voxel {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;

math::FVector3 Vec3(const dataio::Json &j, math::FVector3 def = {}) {
	if (!j.IsArray() || j.Size() < 3)
		return def;
	return {j[0].AsFloat(), j[1].AsFloat(), j[2].AsFloat()};
}

dataio::Json Round(float v) { return dataio::Json(std::round(double(v) * 10000.0) / 10000.0); }

dataio::Json ToArray(const math::FVector3 &v) {
	dataio::Json a = dataio::Json::MakeArray();
	a.Push(Round(v.x));
	a.Push(Round(v.y));
	a.Push(Round(v.z));
	return a;
}

/// Clés d'un objet JSON hors `known` → JSON compact (vide s'il n'y en a pas).
std::string ExtraKeys(const dataio::Json &obj, std::initializer_list<std::string_view> known) {
	if (!obj.IsObject())
		return {};
	dataio::Json extra = dataio::Json::MakeObject();
	bool any = false;
	for (const auto &[k, v] : obj.AsObject())
		if (std::ranges::find(known, std::string_view(k)) == known.end()) {
			extra.Set(k, v);
			any = true;
		}
	return any ? dataio::WriteJson(extra, -1) : std::string();
}

/// Fusionne les clés de `extra` (JSON compact) dans `obj`.
void MergeExtra(dataio::Json &obj, std::string_view extra) {
	if (extra.empty())
		return;
	if (auto parsed = dataio::ParseJson(extra); parsed && parsed.Value().IsObject())
		for (const auto &[k, v] : parsed.Value().AsObject())
			obj.Set(k, v);
}

void RemoveKey(dataio::Json &obj, std::string_view key) {
	if (!obj.IsObject())
		return;
	auto &o = obj.MutObject();
	std::erase_if(o, [&](const auto &kv) { return kv.first == key; });
}

ModelPart &SinglePart(Model &m, std::string name) {
	m.parts.push_back({});
	m.parts.back().name = std::move(name);
	return m.parts.back();
}

} // namespace

const char *ModelFormatLabel(ModelFormat format) noexcept {
	switch (format) {
	case ModelFormat::Cube: return "cube";
	case ModelFormat::Cross: return "croix";
	case ModelFormat::Nodebox: return "nodebox";
	case ModelFormat::Boxes: return "json";
	case ModelFormat::Rig: return "rig";
	case ModelFormat::Obj: return "obj";
	case ModelFormat::B3d: return "b3d";
	}
	return "?";
}

// ── Model ────────────────────────────────────────────────────────────────────

AnimationTrack *Animation::Track(std::string_view part, bool create) {
	for (AnimationTrack &t : tracks)
		if (t.part == part)
			return &t;
	if (!create)
		return nullptr;
	tracks.push_back({std::string(part), {}, {}});
	return &tracks.back();
}

const AnimationTrack *Animation::Track(std::string_view part) const {
	for (const AnimationTrack &t : tracks)
		if (t.part == part)
			return &t;
	return nullptr;
}

int Model::FindPart(std::string_view name) const noexcept {
	for (size_t i = 0; i < parts.size(); ++i)
		if (parts[i].name == name)
			return int(i);
	return -1;
}

ModelBounds Model::Bounds() const noexcept {
	std::vector<ModelQuad> quads;
	BuildQuads(*this, {}, quads);
	if (quads.empty())
		return {};
	ModelBounds b{quads[0].p[0], quads[0].p[0]};
	for (const ModelQuad &q : quads)
		for (const math::FVector3 &p : q.p) {
			b.min = {std::min(b.min.x, p.x), std::min(b.min.y, p.y), std::min(b.min.z, p.z)};
			b.max = {std::max(b.max.x, p.x), std::max(b.max.y, p.y), std::max(b.max.z, p.z)};
		}
	return b;
}

size_t Model::BoxCount() const noexcept {
	size_t n = 0;
	for (const ModelPart &p : parts)
		n += p.boxes.size();
	return n;
}

size_t Model::TriangleCount() const noexcept {
	size_t n = 0;
	for (const ModelPart &p : parts)
		n += p.indices.size() / 3;
	return n;
}

bool Model::HasMeshes() const noexcept {
	return std::ranges::any_of(parts, [](const ModelPart &p) { return !p.indices.empty(); });
}

// ── Lecture ──────────────────────────────────────────────────────────────────

std::vector<ModelBox> ParseBoxList(const dataio::Json &list) {
	std::vector<ModelBox> out;
	for (const dataio::Json &b : list.AsArray()) {
		if (!b.IsArray() || b.Size() < 6)
			continue;
		const math::FVector3 a{b[0].AsFloat(), b[1].AsFloat(), b[2].AsFloat()};
		const math::FVector3 c{b[3].AsFloat(), b[4].AsFloat(), b[5].AsFloat()};
		ModelBox box;
		box.from = {std::min(a.x, c.x), std::min(a.y, c.y), std::min(a.z, c.z)};
		box.size = math::FVector3{std::max(a.x, c.x), std::max(a.y, c.y), std::max(a.z, c.z)} - box.from;
		out.push_back(box);
	}
	return out;
}

Result<Model, std::string> ParseNodebox(const dataio::Json &nodebox) {
	const dataio::Json *list = &nodebox;
	if (nodebox.IsObject()) {
		list = nodebox.Find("fixed");
		if (!list)
			return Err(std::string("nodebox sans liste « fixed »"));
	}
	if (!list->IsArray())
		return Err(std::string("nodebox : liste de boîtes attendue"));
	Model m;
	m.format = ModelFormat::Nodebox;
	SinglePart(m, "nodebox").boxes = ParseBoxList(*list);
	if (m.parts[0].boxes.empty())
		return Err(std::string("nodebox vide"));
	return Ok(std::move(m));
}

Model MakeCubeModel() {
	Model m;
	m.format = ModelFormat::Cube;
	ModelBox b;
	b.from = {-0.5f, -0.5f, -0.5f};
	SinglePart(m, "cube").boxes.push_back(b);
	return m;
}

Model MakeCrossModel() {
	Model m;
	m.format = ModelFormat::Cross;
	ModelPart &p = SinglePart(m, "cross");
	const float h = 0.5f;
	const math::FVector3 corners[2][4] = {
		{{-h, h, -h}, {h, h, h}, {h, -h, h}, {-h, -h, -h}},
		{{-h, h, h}, {h, h, -h}, {h, -h, -h}, {-h, -h, h}},
	};
	const math::FVector2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
	for (const auto &plane : corners) {
		const uint32_t base = uint32_t(p.vertices.size());
		for (int i = 0; i < 4; ++i)
			p.vertices.push_back({plane[i], uv[i]});
		for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
			p.indices.push_back(base + i);
	}
	return m;
}

Result<Model, std::string> ParseRig(const dataio::Json &def) {
	const dataio::Json *parts = def.Find("parts");
	if (!parts || !parts->IsArray())
		return Err(std::string("rig sans « parts »"));
	Model m;
	m.id = std::string(def["id"].AsString());
	m.format = ModelFormat::Rig;
	m.unitsPerBlock = 16.f;
	m.boxUv = true;
	if (const dataio::Json &ts = def["textureSize"]; ts.IsArray() && ts.Size() >= 2) {
		m.textureWidth = int(std::max<int64_t>(1, ts[0].AsInt(64)));
		m.textureHeight = int(std::max<int64_t>(1, ts[1].AsInt(32)));
	} else {
		m.textureWidth = 64;
		m.textureHeight = 32;
	}
	for (const dataio::Json &pj : parts->AsArray()) {
		ModelPart p;
		p.name = std::string(pj["name"].AsString());
		if (p.name.empty())
			p.name = std::format("part{}", m.parts.size());
		p.pivot = Vec3(pj["pivot"]);
		p.rotation = Vec3(pj["rotation"]);
		for (const dataio::Json &bj : pj["boxes"].AsArray()) {
			ModelBox b;
			b.from = Vec3(bj["from"]);
			b.size = Vec3(bj["size"], {1, 1, 1});
			if (const dataio::Json &uv = bj["uv"]; uv.IsArray() && uv.Size() >= 2)
				b.uv = {uv[0].AsFloat(), uv[1].AsFloat()};
			b.inflate = bj["inflate"].AsFloat(0.f);
			b.mirror = bj["mirror"].AsBool(false);
			b.extra = ExtraKeys(bj, {"from", "size", "uv", "inflate", "mirror"});
			p.boxes.push_back(std::move(b));
		}
		p.extra = ExtraKeys(pj, {"name", "pivot", "rotation", "boxes"});
		m.parts.push_back(std::move(p));
	}
	if (const dataio::Json *anims = def.Find("animations"))
		m.animations = ParseAnimations(*anims);
	if (const dataio::Json *me = def.Find("mechanic"); me && me->IsObject()) {
		ModelMechanic mm;
		mm.type = std::string((*me)["type"].AsString());
		mm.animation = std::string((*me)["animation"].AsString());
		mm.toggle = (*me)["toggle"].AsBool(true);
		if (!mm.type.empty())
			m.mechanic = std::move(mm);
	}
	return Ok(std::move(m));
}

Result<Model, std::string> ParseBoxesModel(const dataio::Json &def) {
	const dataio::Json &data = def["data"];
	const dataio::Json *boxes = data.IsObject() ? data.Find("boxes") : def.Find("boxes");
	if (!boxes || !boxes->IsArray())
		return Err(std::string("modèle json sans « data.boxes »"));
	Model m;
	m.id = std::string(def["id"].AsString());
	m.format = ModelFormat::Boxes;
	SinglePart(m, "boxes").boxes = ParseBoxList(*boxes);
	if (const dataio::Json *anims = def.Find("animations"))
		m.animations = ParseAnimations(*anims);
	return Ok(std::move(m));
}

Result<Model, std::string> ParseObj(std::string_view text) {
	Model m;
	m.format = ModelFormat::Obj;
	std::vector<math::FVector3> positions;
	std::vector<math::FVector2> uvs;
	ModelPart *part = nullptr;
	auto current = [&]() -> ModelPart & {
		if (!part) {
			m.parts.push_back({});
			m.parts.back().name = "mesh";
			part = &m.parts.back();
		}
		return *part;
	};
	auto number = [](std::string_view &s) {
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
			s.remove_prefix(1);
		const char *b = s.data(), *e = s.data() + s.size();
		char *end = nullptr;
		std::string tmp(b, size_t(std::min<ptrdiff_t>(e - b, 64)));
		const float v = std::strtof(tmp.c_str(), &end);
		const size_t used = size_t(end - tmp.c_str());
		s.remove_prefix(std::min(used, s.size()));
		return v;
	};
	size_t lineNo = 0;
	while (!text.empty()) {
		const size_t nl = text.find('\n');
		std::string_view line = text.substr(0, nl);
		text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
		++lineNo;
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
			line.remove_prefix(1);
		if (line.starts_with("v ")) {
			line.remove_prefix(2);
			const float x = number(line), y = number(line), z = number(line);
			positions.push_back({x, y, z});
		} else if (line.starts_with("vt ")) {
			line.remove_prefix(3);
			const float u = number(line), v = number(line);
			uvs.push_back({u, 1.f - v});
		} else if (line.starts_with("o ") || line.starts_with("g ")) {
			std::string name(line.substr(2));
			while (!name.empty() && name.back() == ' ')
				name.pop_back();
			m.parts.push_back({});
			m.parts.back().name = name.empty() ? std::format("part{}", m.parts.size()) : name;
			part = &m.parts.back();
		} else if (line.starts_with("f ")) {
			line.remove_prefix(2);
			std::vector<MeshVertex> poly;
			while (!line.empty()) {
				while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
					line.remove_prefix(1);
				if (line.empty())
					break;
				const size_t sp = line.find_first_of(" \t");
				std::string_view tok = line.substr(0, sp);
				line.remove_prefix(sp == std::string_view::npos ? line.size() : sp);
				const size_t s1 = tok.find('/');
				const long vi = std::strtol(std::string(tok.substr(0, s1)).c_str(), nullptr, 10);
				long ti = 0;
				if (s1 != std::string_view::npos) {
					const std::string_view rest = tok.substr(s1 + 1);
					const size_t s2 = rest.find('/');
					if (s2 != 0)
						ti = std::strtol(std::string(rest.substr(0, s2)).c_str(), nullptr, 10);
				}
				const long pIndex = vi < 0 ? long(positions.size()) + vi : vi - 1;
				const long tIndex = ti < 0 ? long(uvs.size()) + ti : ti - 1;
				if (pIndex < 0 || size_t(pIndex) >= positions.size())
					return Err(std::format("obj ligne {} : sommet {} inconnu", lineNo, vi));
				MeshVertex v{positions[size_t(pIndex)], {}};
				if (ti != 0 && tIndex >= 0 && size_t(tIndex) < uvs.size())
					v.uv = uvs[size_t(tIndex)];
				poly.push_back(v);
			}
			if (poly.size() < 3)
				continue;
			ModelPart &p = current();
			const uint32_t base = uint32_t(p.vertices.size());
			p.vertices.insert(p.vertices.end(), poly.begin(), poly.end());
			for (uint32_t i = 1; i + 1 < uint32_t(poly.size()); ++i) { // éventail
				p.indices.push_back(base);
				p.indices.push_back(base + i);
				p.indices.push_back(base + i + 1);
			}
		}
	}
	std::erase_if(m.parts, [](const ModelPart &p) { return p.indices.empty(); });
	if (m.parts.empty())
		return Err(std::string("obj sans face"));
	return Ok(std::move(m));
}

// ── Blitz3D (.b3d) ───────────────────────────────────────────────────────────

namespace {

struct B3dReader {
	std::span<const uint8_t> bytes;
	size_t pos = 0;
	bool ok = true;

	template <typename T> T Read() {
		T v{};
		if (pos + sizeof(T) > bytes.size()) {
			ok = false;
			pos = bytes.size();
			return v;
		}
		std::memcpy(&v, bytes.data() + pos, sizeof(T));
		pos += sizeof(T);
		return v;
	}
	std::string CString() {
		std::string s;
		while (pos < bytes.size() && bytes[pos] != 0)
			s.push_back(char(bytes[pos++]));
		++pos;
		return s;
	}
	bool Tag(char out[5], uint32_t &len) {
		if (pos + 8 > bytes.size())
			return false;
		std::memcpy(out, bytes.data() + pos, 4);
		out[4] = 0;
		pos += 4;
		len = Read<uint32_t>();
		return ok;
	}
};

struct B3dTransform {
	math::FVector3 position{};
	math::FVector3 scale{1, 1, 1};
	math::FVector4 rotation{1, 0, 0, 0}; // w, x, y, z
	const B3dTransform *parent = nullptr;

	[[nodiscard]] math::FVector3 Apply(math::FVector3 v) const {
		v = {v.x * scale.x, v.y * scale.y, v.z * scale.z};
		// q v q* (w = rotation.x, axe = y, z, w)
		const float qw = rotation.x;
		const math::FVector3 u{rotation.y, rotation.z, rotation.w};
		const math::FVector3 t = u.Cross(v) * 2.f;
		v = v + t * qw + u.Cross(t);
		v = v + position;
		return parent ? parent->Apply(v) : v;
	}
};

void ReadB3dNode(B3dReader &r, size_t end, const B3dTransform *parent, Model &m) {
	B3dTransform tr;
	tr.parent = parent;
	const std::string name = r.CString();
	tr.position = {r.Read<float>(), r.Read<float>(), r.Read<float>()};
	tr.scale = {r.Read<float>(), r.Read<float>(), r.Read<float>()};
	tr.rotation = {r.Read<float>(), r.Read<float>(), r.Read<float>(), r.Read<float>()};
	while (r.ok && r.pos + 8 <= end) {
		char tag[5];
		uint32_t len = 0;
		if (!r.Tag(tag, len))
			break;
		const size_t chunkEnd = std::min(r.pos + len, end);
		if (std::strcmp(tag, "MESH") == 0) {
			ModelPart p;
			p.name = name.empty() ? std::format("mesh{}", m.parts.size()) : name;
			(void)r.Read<int32_t>(); // brosse
			std::vector<MeshVertex> verts;
			while (r.ok && r.pos + 8 <= chunkEnd) {
				char sub[5];
				uint32_t slen = 0;
				if (!r.Tag(sub, slen))
					break;
				const size_t subEnd = std::min(r.pos + slen, chunkEnd);
				if (std::strcmp(sub, "VRTS") == 0) {
					const int32_t flags = r.Read<int32_t>();
					const int32_t sets = r.Read<int32_t>();
					const int32_t setSize = r.Read<int32_t>();
					const size_t stride = size_t(12 + ((flags & 1) ? 12 : 0) + ((flags & 2) ? 16 : 0)) + size_t(std::max(0, sets * setSize)) * 4;
					while (r.ok && stride > 0 && r.pos + stride <= subEnd) {
						MeshVertex v{};
						v.position = {r.Read<float>(), r.Read<float>(), r.Read<float>()};
						v.position = tr.Apply(v.position);
						if (flags & 1)
							r.pos += 12;
						if (flags & 2)
							r.pos += 16;
						if (sets > 0 && setSize >= 2) {
							v.uv = {r.Read<float>(), r.Read<float>()};
							r.pos += size_t(sets * setSize - 2) * 4;
						} else {
							r.pos += size_t(std::max(0, sets * setSize)) * 4;
						}
						verts.push_back(v);
					}
				} else if (std::strcmp(sub, "TRIS") == 0) {
					(void)r.Read<int32_t>(); // brosse
					while (r.ok && r.pos + 12 <= subEnd) {
						const int32_t a = r.Read<int32_t>(), b = r.Read<int32_t>(), c = r.Read<int32_t>();
						if (a >= 0 && b >= 0 && c >= 0 && size_t(std::max({a, b, c})) < verts.size()) {
							p.indices.push_back(uint32_t(a));
							p.indices.push_back(uint32_t(b));
							p.indices.push_back(uint32_t(c));
						}
					}
				}
				r.pos = subEnd;
			}
			p.vertices = std::move(verts);
			if (!p.indices.empty())
				m.parts.push_back(std::move(p));
		} else if (std::strcmp(tag, "NODE") == 0) {
			ReadB3dNode(r, chunkEnd, &tr, m);
		}
		r.pos = chunkEnd;
	}
}

} // namespace

Result<Model, std::string> ParseB3d(std::span<const uint8_t> bytes) {
	B3dReader r{bytes};
	char tag[5];
	uint32_t len = 0;
	if (!r.Tag(tag, len) || std::strcmp(tag, "BB3D") != 0)
		return Err(std::string("b3d : en-tête BB3D absent"));
	const size_t end = std::min(bytes.size(), r.pos + len);
	(void)r.Read<int32_t>(); // version
	Model m;
	m.format = ModelFormat::B3d;
	while (r.ok && r.pos + 8 <= end) {
		if (!r.Tag(tag, len))
			break;
		const size_t chunkEnd = std::min(end, r.pos + len);
		if (std::strcmp(tag, "NODE") == 0)
			ReadB3dNode(r, chunkEnd, nullptr, m);
		r.pos = chunkEnd;
	}
	if (m.parts.empty())
		return Err(std::string("b3d sans maillage"));
	return Ok(std::move(m));
}

std::vector<ModelBox> ParseCollision(const dataio::Json &blockDef, const Model &shape) {
	const dataio::Json &col = blockDef["physics"]["collision"];
	if (!col.IsObject())
		return {};
	const std::string_view kind = col["shape"].AsString(col["type"].AsString());
	if (kind == "FROM_NODEBOX") {
		std::vector<ModelBox> out;
		for (const ModelPart &p : shape.parts)
			out.insert(out.end(), p.boxes.begin(), p.boxes.end());
		return out;
	}
	if (const dataio::Json *fixed = col.Find("fixed"))
		return ParseBoxList(*fixed);
	return {};
}

std::vector<Animation> ParseAnimations(const dataio::Json &list) {
	std::vector<Animation> out;
	auto keys = [](const dataio::Json &arr) {
		std::vector<Keyframe> k;
		for (const dataio::Json &e : arr.AsArray())
			if (e.IsArray() && e.Size() >= 4)
				k.push_back({e[0].AsFloat(), {e[1].AsFloat(), e[2].AsFloat(), e[3].AsFloat()}});
		std::ranges::sort(k, {}, &Keyframe::time);
		return k;
	};
	for (const dataio::Json &a : list.AsArray()) {
		Animation anim;
		anim.name = std::string(a["name"].AsString("animation"));
		anim.length = std::max(0.05f, a["length"].AsFloat(1.f));
		anim.loop = a["loop"].AsBool(true);
		for (const dataio::Json &t : a["tracks"].AsArray())
			anim.tracks.push_back({std::string(t["part"].AsString()), keys(t["rotation"]), keys(t["position"])});
		out.push_back(std::move(anim));
	}
	return out;
}

// ── Écriture ─────────────────────────────────────────────────────────────────

dataio::Json BoxListToJson(std::span<const ModelBox> boxes) {
	dataio::Json list = dataio::Json::MakeArray();
	for (const ModelBox &b : boxes) {
		dataio::Json e = dataio::Json::MakeArray();
		const math::FVector3 to = b.To();
		for (float v : {b.from.x, b.from.y, b.from.z, to.x, to.y, to.z})
			e.Push(Round(v));
		list.Push(std::move(e));
	}
	return list;
}

dataio::Json BoxListToJson(const Model &model) {
	std::vector<ModelBox> all;
	for (const ModelPart &p : model.parts)
		for (ModelBox b : p.boxes) {
			b.from = b.from * (1.f / model.unitsPerBlock);
			b.size = b.size * (1.f / model.unitsPerBlock);
			all.push_back(b);
		}
	return BoxListToJson(all);
}

dataio::Json AnimationsToJson(std::span<const Animation> animations) {
	dataio::Json list = dataio::Json::MakeArray();
	auto keys = [](const std::vector<Keyframe> &k) {
		dataio::Json arr = dataio::Json::MakeArray();
		for (const Keyframe &f : k) {
			dataio::Json e = dataio::Json::MakeArray();
			e.Push(Round(f.time));
			e.Push(Round(f.value.x));
			e.Push(Round(f.value.y));
			e.Push(Round(f.value.z));
			arr.Push(std::move(e));
		}
		return arr;
	};
	for (const Animation &a : animations) {
		dataio::Json o = dataio::Json::MakeObject();
		o.Set("name", dataio::Json(a.name));
		o.Set("length", Round(a.length));
		o.Set("loop", dataio::Json(a.loop));
		dataio::Json tracks = dataio::Json::MakeArray();
		for (const AnimationTrack &t : a.tracks) {
			if (t.rotation.empty() && t.position.empty())
				continue;
			dataio::Json tj = dataio::Json::MakeObject();
			tj.Set("part", dataio::Json(t.part));
			if (!t.rotation.empty())
				tj.Set("rotation", keys(t.rotation));
			if (!t.position.empty())
				tj.Set("position", keys(t.position));
			tracks.Push(std::move(tj));
		}
		o.Set("tracks", std::move(tracks));
		list.Push(std::move(o));
	}
	return list;
}

dataio::Json RigToJson(const Model &model, const dataio::Json &original) {
	dataio::Json def = original.IsObject() ? original : dataio::Json::MakeObject();
	if (!def.Find("type"))
		def.Set("type", dataio::Json("model"));
	if (!def.Find("id"))
		def.Set("id", dataio::Json(model.id));
	def.Set("format", dataio::Json("rig"));
	dataio::Json ts = dataio::Json::MakeArray();
	ts.Push(dataio::Json(model.textureWidth));
	ts.Push(dataio::Json(model.textureHeight));
	def.Set("textureSize", std::move(ts));
	dataio::Json parts = dataio::Json::MakeArray();
	for (const ModelPart &p : model.parts) {
		dataio::Json pj = dataio::Json::MakeObject();
		pj.Set("name", dataio::Json(p.name));
		pj.Set("pivot", ToArray(p.pivot));
		if (p.rotation.x != 0 || p.rotation.y != 0 || p.rotation.z != 0)
			pj.Set("rotation", ToArray(p.rotation));
		dataio::Json boxes = dataio::Json::MakeArray();
		for (const ModelBox &b : p.boxes) {
			dataio::Json bj = dataio::Json::MakeObject();
			bj.Set("from", ToArray(b.from));
			bj.Set("size", ToArray(b.size));
			dataio::Json uv = dataio::Json::MakeArray();
			uv.Push(Round(b.uv.x));
			uv.Push(Round(b.uv.y));
			bj.Set("uv", std::move(uv));
			if (b.inflate != 0)
				bj.Set("inflate", Round(b.inflate));
			if (b.mirror)
				bj.Set("mirror", dataio::Json(true));
			MergeExtra(bj, b.extra);
			boxes.Push(std::move(bj));
		}
		pj.Set("boxes", std::move(boxes));
		MergeExtra(pj, p.extra);
		parts.Push(std::move(pj));
	}
	def.Set("parts", std::move(parts));
	if (model.animations.empty())
		RemoveKey(def, "animations");
	else
		def.Set("animations", AnimationsToJson(model.animations));
	if (model.mechanic && !model.mechanic->type.empty()) {
		dataio::Json me = dataio::Json::MakeObject();
		me.Set("type", dataio::Json(model.mechanic->type));
		me.Set("animation", dataio::Json(model.mechanic->animation));
		me.Set("toggle", dataio::Json(model.mechanic->toggle));
		def.Set("mechanic", std::move(me));
	} else {
		RemoveKey(def, "mechanic");
	}
	return def;
}

std::string ObjToText(const Model &model) {
	std::vector<ModelQuad> quads;
	BuildQuads(model, {}, quads);
	std::string out = std::format("# Exporté par Voxelia Studio : {}\n", model.id.empty() ? "modèle" : model.id);
	size_t vbase = 1;
	int lastPart = -2;
	for (const ModelQuad &q : quads) {
		if (q.part != lastPart) {
			lastPart = q.part;
			out += std::format("o {}\n", q.part >= 0 && size_t(q.part) < model.parts.size() ? model.parts[size_t(q.part)].name : "mesh");
		}
		const int n = q.box >= 0 ? 4 : 3;
		for (int i = 0; i < n; ++i)
			out += std::format("v {:.5f} {:.5f} {:.5f}\n", q.p[i].x, q.p[i].y, q.p[i].z);
		for (int i = 0; i < n; ++i)
			out += std::format("vt {:.5f} {:.5f}\n", q.uv[i].x, 1.f - q.uv[i].y);
		out += "f";
		for (int i = 0; i < n; ++i)
			out += std::format(" {}/{}", vbase + size_t(i), vbase + size_t(i));
		out += "\n";
		vbase += size_t(n);
	}
	return out;
}

Model ToRig(const Model &model) {
	Model r = model;
	r.format = ModelFormat::Rig;
	r.boxUv = true;
	r.unitsPerBlock = 16.f;
	const float s = 16.f / model.unitsPerBlock;
	// Blocs centrés (y de -0,5 à 0,5) → rig posé sur ses pieds (y de 0 à 16).
	const bool centered = model.format != ModelFormat::Rig;
	const math::FVector3 shift = centered ? math::FVector3{0, 0.5f * model.unitsPerBlock, 0} : math::FVector3{};
	for (ModelPart &p : r.parts) {
		p.pivot = (p.pivot + shift) * s;
		for (ModelBox &b : p.boxes) {
			b.from = (b.from + shift) * s;
			b.size = b.size * s;
		}
		for (MeshVertex &v : p.vertices)
			v.position = (v.position + shift) * s;
	}
	for (ModelBox &b : r.collision) {
		b.from = (b.from + shift) * s;
		b.size = b.size * s;
	}
	if (model.format != ModelFormat::Rig) {
		r.textureWidth = std::max(r.textureWidth, 64);
		r.textureHeight = std::max(r.textureHeight, 32);
	}
	return r;
}

// ── Animation ────────────────────────────────────────────────────────────────

math::FVector3 SampleKeys(std::span<const Keyframe> keys, float time) noexcept {
	if (keys.empty())
		return {};
	if (time <= keys.front().time)
		return keys.front().value;
	if (time >= keys.back().time)
		return keys.back().value;
	for (size_t i = 1; i < keys.size(); ++i)
		if (time <= keys[i].time) {
			const Keyframe &a = keys[i - 1], &b = keys[i];
			const float t = b.time > a.time ? (time - a.time) / (b.time - a.time) : 1.f;
			return a.value + (b.value - a.value) * t;
		}
	return keys.back().value;
}

void SetKey(std::vector<Keyframe> &keys, float time, const math::FVector3 &value) {
	for (Keyframe &k : keys)
		if (std::fabs(k.time - time) < 1e-3f) {
			k.value = value;
			return;
		}
	keys.push_back({time, value});
	std::ranges::sort(keys, {}, &Keyframe::time);
}

bool RemoveKey(std::vector<Keyframe> &keys, float time) noexcept {
	const auto n = std::erase_if(keys, [&](const Keyframe &k) { return std::fabs(k.time - time) < 1e-3f; });
	return n > 0;
}

std::vector<PartOffset> SamplePose(const Model &model, const Animation *animation, float time, float motionTime) {
	std::vector<PartOffset> pose(model.parts.size());
	if (animation) {
		float t = time;
		if (animation->loop && animation->length > 0)
			t = std::fmod(std::max(0.f, t), animation->length);
		for (size_t i = 0; i < model.parts.size(); ++i)
			if (const AnimationTrack *tr = animation->Track(model.parts[i].name)) {
				pose[i].rotation = SampleKeys(tr->rotation, t);
				pose[i].position = SampleKeys(tr->position, t);
			}
	}
	if (motionTime >= 0) // mouvements procéduraux des rigs : walk / idle
		for (size_t i = 0; i < model.parts.size(); ++i) {
			const std::string &extra = model.parts[i].extra;
			if (extra.empty() || (extra.find("\"walk\"") == std::string::npos && extra.find("\"idle\"") == std::string::npos))
				continue;
			auto parsed = dataio::ParseJson(extra);
			if (!parsed)
				continue;
			for (const auto &[key, speed] : {std::pair<std::string_view, float>{"walk", 6.f}, {"idle", 1.6f}}) {
				const dataio::Json &m = parsed.Value()[key];
				if (!m.IsArray() || m.Size() < 3)
					continue;
				const std::string_view axis = m[0].AsString("x");
				const float angle = std::sin(motionTime * speed + m[1].AsFloat() * kDeg) * m[2].AsFloat();
				(axis == "y" ? pose[i].rotation.y : axis == "z" ? pose[i].rotation.z : pose[i].rotation.x) += angle;
			}
		}
	return pose;
}

// ── Géométrie ────────────────────────────────────────────────────────────────

std::array<UvRect, 6> BoxUvLayout(const ModelBox &box) noexcept {
	const float w = box.size.x, h = box.size.y, d = box.size.z;
	const float u = box.uv.x, v = box.uv.y;
	std::array<UvRect, 6> r{};
	r[size_t(BoxFace::NegX)] = {u, v + d, u + d, v + d + h};
	r[size_t(BoxFace::PosZ)] = {u + d, v + d, u + d + w, v + d + h};
	r[size_t(BoxFace::PosX)] = {u + d + w, v + d, u + 2 * d + w, v + d + h};
	r[size_t(BoxFace::NegZ)] = {u + 2 * d + w, v + d, u + 2 * d + 2 * w, v + d + h};
	r[size_t(BoxFace::PosY)] = {u + d, v, u + d + w, v + d};
	r[size_t(BoxFace::NegY)] = {u + d + w, v, u + d + 2 * w, v + d};
	if (box.mirror) {
		std::swap(r[size_t(BoxFace::NegX)], r[size_t(BoxFace::PosX)]);
		for (UvRect &f : r)
			std::swap(f.u0, f.u1);
	}
	return r;
}

void AutoBoxUv(Model &model, int margin) {
	if (!model.boxUv)
		return;
	struct Foot {
		int uw, uh;
		ModelBox *box;
	};
	std::vector<Foot> feet;
	for (ModelPart &p : model.parts)
		for (ModelBox &b : p.boxes) {
			const int w = std::max(1, int(std::ceil(b.size.x)));
			const int h = std::max(1, int(std::ceil(b.size.y)));
			const int d = std::max(1, int(std::ceil(b.size.z)));
			feet.push_back({2 * (w + d), d + h, &b});
		}
	if (feet.empty())
		return;
	const int gap = std::max(1, margin);
	// Largeur minimale : au moins la plus large des boîtes (et la largeur actuelle).
	int texW = std::max(16, model.textureWidth);
	for (const Foot &f : feet)
		texW = std::max(texW, f.uw + 2 * gap);
	// Emballage par étagères : boîtes rangées de gauche à droite, ligne par ligne.
	int cu = 0, cv = 0, rowH = 0;
	for (Foot &f : feet) {
		if (cu > 0 && cu + f.uw > texW) {
			cu = 0;
			cv += rowH + gap;
			rowH = 0;
		}
		f.box->uv = {float(cu + gap), float(cv + gap)};
		cu += f.uw + gap;
		rowH = std::max(rowH, f.uh);
	}
	model.textureWidth = texW;
	model.textureHeight = std::max(16, cv + rowH + gap);
}

math::FVector3 RotateEuler(const math::FVector3 &v, const math::FVector3 &deg) noexcept {
	math::FVector3 p = v;
	if (deg.x != 0) {
		const float c = std::cos(deg.x * kDeg), s = std::sin(deg.x * kDeg);
		p = {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
	}
	if (deg.y != 0) {
		const float c = std::cos(deg.y * kDeg), s = std::sin(deg.y * kDeg);
		p = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
	}
	if (deg.z != 0) {
		const float c = std::cos(deg.z * kDeg), s = std::sin(deg.z * kDeg);
		p = {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
	}
	return p;
}

void BuildModelMesh(const Model &model, std::span<const PartOffset> pose, std::vector<ModelVertex> &out) {
	out.clear();
	std::vector<ModelQuad> quads;
	BuildQuads(model, pose, quads);
	out.reserve(quads.size() * 6);
	for (const ModelQuad &q : quads) {
		// `BuildQuads` donne des coins dont l'enroulement peut être opposé au normal
		// déclaré (boîtes) : on le redresse pour l'élimination des faces arrière.
		const math::FVector3 wound = (q.p[1] - q.p[0]).Cross(q.p[2] - q.p[0]);
		const bool flip = wound.Dot(q.normal) < 0.f;
		auto emit = [&](int a) {
			out.push_back({q.p[a].x, q.p[a].y, q.p[a].z, q.normal.x, q.normal.y, q.normal.z, q.uv[a].x, q.uv[a].y});
		};
		auto tri = [&](int a, int b, int c) {
			emit(a);
			emit(b);
			emit(c);
		};
		if (q.p[3] == q.p[2]) { // triangle de maillage (p[3] == p[2])
			if (flip)
				tri(0, 2, 1);
			else
				tri(0, 1, 2);
		} else if (flip) {
			tri(0, 2, 1);
			tri(0, 3, 2);
		} else {
			tri(0, 1, 2);
			tri(0, 2, 3);
		}
	}
}

void BuildQuads(const Model &model, std::span<const PartOffset> pose, std::vector<ModelQuad> &out) {
	out.clear();
	const float inv = 1.f / model.unitsPerBlock;
	static constexpr math::FVector3 kNormals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (size_t pi = 0; pi < model.parts.size(); ++pi) {
		const ModelPart &part = model.parts[pi];
		if (!part.visible)
			continue;
		const PartOffset off = pi < pose.size() ? pose[pi] : PartOffset{};
		const math::FVector3 rot = part.rotation + off.rotation;
		auto xf = [&](const math::FVector3 &p) { return (RotateEuler(p - part.pivot, rot) + part.pivot + off.position) * inv; };
		for (size_t bi = 0; bi < part.boxes.size(); ++bi) {
			const ModelBox &b = part.boxes[bi];
			if (!b.visible)
				continue;
			const math::FVector3 g{b.inflate, b.inflate, b.inflate};
			const math::FVector3 lo = b.from - g, hi = b.To() + g;
			const float x0 = lo.x, x1 = hi.x, y0 = lo.y, y1 = hi.y, z0 = lo.z, z1 = hi.z;
			const math::FVector3 corners[6][4] = {
				{{x1, y1, z1}, {x1, y1, z0}, {x1, y0, z0}, {x1, y0, z1}}, // +X
				{{x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1}, {x0, y0, z0}}, // -X
				{{x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}}, // +Y
				{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}, // -Y
				{{x0, y1, z1}, {x1, y1, z1}, {x1, y0, z1}, {x0, y0, z1}}, // +Z
				{{x1, y1, z0}, {x0, y1, z0}, {x0, y0, z0}, {x1, y0, z0}}, // -Z
			};
			const std::array<UvRect, 6> layout = model.boxUv ? BoxUvLayout(b) : std::array<UvRect, 6>{};
			for (uint8_t f = 0; f < 6; ++f) {
				ModelQuad q;
				q.part = int(pi);
				q.box = int(bi);
				q.face = f;
				for (int k = 0; k < 4; ++k)
					q.p[k] = xf(corners[f][k]);
				if (model.boxUv) {
					const UvRect &r = layout[f];
					const float tw = float(model.textureWidth), th = float(model.textureHeight);
					q.uv[0] = {r.u0 / tw, r.v0 / th};
					q.uv[1] = {r.u1 / tw, r.v0 / th};
					q.uv[2] = {r.u1 / tw, r.v1 / th};
					q.uv[3] = {r.u0 / tw, r.v1 / th};
				} else { // projection par face dans la tuile du bloc (unités bloc centrées)
					for (int k = 0; k < 4; ++k) {
						const math::FVector3 c = corners[f][k] * inv;
						switch (BoxFace(f)) {
						case BoxFace::PosZ: q.uv[k] = {c.x + 0.5f, 0.5f - c.y}; break;
						case BoxFace::NegZ: q.uv[k] = {0.5f - c.x, 0.5f - c.y}; break;
						case BoxFace::PosX: q.uv[k] = {0.5f - c.z, 0.5f - c.y}; break;
						case BoxFace::NegX: q.uv[k] = {c.z + 0.5f, 0.5f - c.y}; break;
						case BoxFace::PosY: q.uv[k] = {c.x + 0.5f, c.z + 0.5f}; break;
						case BoxFace::NegY: q.uv[k] = {c.x + 0.5f, 0.5f - c.z}; break;
						}
					}
					// Boîtes hors du bloc : ramenées dans la tuile.
					const float du = std::floor(std::min({q.uv[0].x, q.uv[1].x, q.uv[2].x, q.uv[3].x}) + 1e-4f);
					const float dv = std::floor(std::min({q.uv[0].y, q.uv[1].y, q.uv[2].y, q.uv[3].y}) + 1e-4f);
					for (math::FVector2 &uv : q.uv)
						uv = {uv.x - du, uv.y - dv};
				}
				q.normal = RotateEuler(kNormals[f], rot);
				out.push_back(q);
			}
		}
		for (size_t t = 0; t + 2 < part.indices.size(); t += 3) {
			ModelQuad q;
			q.part = int(pi);
			for (int k = 0; k < 3; ++k) {
				const MeshVertex &v = part.vertices[part.indices[t + size_t(k)]];
				q.p[k] = xf(v.position);
				q.uv[k] = v.uv;
			}
			q.p[3] = q.p[2];
			q.uv[3] = q.uv[2];
			q.normal = (q.p[1] - q.p[0]).Cross(q.p[2] - q.p[0]).Normalized();
			out.push_back(q);
		}
	}
}

} // namespace voxel
