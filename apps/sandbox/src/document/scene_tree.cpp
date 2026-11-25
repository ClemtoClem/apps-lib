#include "sandbox/document/scene_tree.hpp"

#include <cmath>
#include <format>
#include <fstream>
#include <sstream>

#include "sandbox/modeling/io.hpp"

namespace sandbox {

namespace {

namespace fs = std::filesystem;
using modeling::Vec3;

constexpr int kMaxDepth = 32; ///< objets imbriqués (ObjectInstance) : garde contre les cycles

/// Valeur d'une propriété typée de l'ancien format : `{"type": …, "value": …}`.
dataio::NodePtr Prop(const dataio::NodePtr &props, std::string_view name) { return (*(*props)[name])["value"]; }

math::FVector3 Vec(const dataio::NodePtr &a, math::FVector3 fallback = {}) {
	return a->IsArray() && a->Size() >= 3 ? math::FVector3{(*a)[0]->AsFloat(), (*a)[1]->AsFloat(), (*a)[2]->AsFloat()} : fallback;
}

modeling::Color Color255(const dataio::NodePtr &a, modeling::Color fallback) {
	if (!a->IsArray() || a->Size() < 3)
		return fallback;
	return {(*a)[0]->AsFloat() / 255.f, (*a)[1]->AsFloat() / 255.f, (*a)[2]->AsFloat() / 255.f, a->Size() > 3 ? (*a)[3]->AsFloat(255.f) / 255.f : 1.f};
}

/// Transformation locale d'un nœud : translation · rotation (degrés, Y puis X puis Z) · échelle.
math::FMatrix4 LocalTransform(const dataio::NodePtr &t) {
	const math::FVector3 p = Vec((*t)["position"]), r = Vec((*t)["rotation"]), s = Vec((*t)["scale"], {1.f, 1.f, 1.f});
	return math::FMatrix4::Translation(p) * math::FMatrix4::Rotation({0.f, 1.f, 0.f}, r.y * math::DEG2RAD) *
		   math::FMatrix4::Rotation({1.f, 0.f, 0.f}, r.x * math::DEG2RAD) * math::FMatrix4::Rotation({0.f, 0.f, 1.f}, r.z * math::DEG2RAD) *
		   math::FMatrix4::Scale(s);
}

// ── Primitives (triangles centrés sur l'origine, dimensions totales `d`) ──────────

struct Mesh {
	std::vector<Vec3> positions;
	std::vector<uint32_t> indices;
	uint32_t Add(const Vec3 &p) {
		positions.push_back(p);
		return uint32_t(positions.size() - 1);
	}
	void Tri(uint32_t a, uint32_t b, uint32_t c) { indices.insert(indices.end(), {a, b, c}); }
};

Mesh Box(const math::FVector3 &d) {
	Mesh m;
	const math::FVector3 h = d * 0.5f;
	for (int i = 0; i < 8; ++i)
		m.Add({i & 1 ? h.x : -h.x, i & 2 ? h.y : -h.y, i & 4 ? h.z : -h.z});
	static constexpr uint32_t kFaces[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
	for (const auto &f : kFaces) {
		m.Tri(f[0], f[1], f[2]);
		m.Tri(f[0], f[2], f[3]);
	}
	return m;
}

/// Révolution autour de Y : profil (rayon relatif, hauteur relative) du bas vers le haut.
Mesh Lathe(const math::FVector3 &d, int segments, std::span<const std::pair<float, float>> profile) {
	Mesh m;
	segments = std::clamp(segments, 6, 96);
	std::vector<std::vector<uint32_t>> rings;
	for (const auto &[r, y] : profile) {
		std::vector<uint32_t> ring;
		if (r <= 1e-5f)
			ring.assign(size_t(segments), m.Add({0.f, (y - 0.5f) * d.y, 0.f})); // pôle : un seul sommet
		else
			for (int s = 0; s < segments; ++s) {
				const float a = float(s) / float(segments) * 2.f * math::PI;
				ring.push_back(m.Add({std::cos(a) * r * d.x * 0.5f, (y - 0.5f) * d.y, std::sin(a) * r * d.z * 0.5f}));
			}
		rings.push_back(std::move(ring));
	}
	for (size_t k = 0; k + 1 < rings.size(); ++k)
		for (int s = 0; s < segments; ++s) {
			const size_t n = size_t((s + 1) % segments);
			const uint32_t a = rings[k][size_t(s)], b = rings[k][n], c = rings[k + 1][n], e = rings[k + 1][size_t(s)];
			if (a != b)
				m.Tri(a, e, b);
			if (c != e)
				m.Tri(b, e, c);
		}
	return m;
}

Mesh Sphere(const math::FVector3 &d, int segments) {
	std::vector<std::pair<float, float>> profile;
	const int stacks = std::clamp(segments / 2, 4, 32);
	for (int i = 0; i <= stacks; ++i) {
		const float t = float(i) / float(stacks) * math::PI; // du pôle bas au pôle haut
		profile.emplace_back(std::sin(t), (1.f - std::cos(t)) * 0.5f);
	}
	return Lathe(d, segments, profile);
}

Mesh Torus(const math::FVector3 &d, int segments) {
	Mesh m;
	segments = std::clamp(segments, 8, 64);
	const int sides = std::max(6, segments / 2);
	const float minor = std::max(0.01f, d.y * 0.5f), major = std::max(minor * 1.2f, d.x * 0.5f - minor);
	for (int s = 0; s < segments; ++s)
		for (int k = 0; k < sides; ++k) {
			const float a = float(s) / float(segments) * 2.f * math::PI, b = float(k) / float(sides) * 2.f * math::PI;
			const float r = major + std::cos(b) * minor;
			m.Add({std::cos(a) * r, std::sin(b) * minor, std::sin(a) * r * (d.z / std::max(d.x, 1e-4f))});
		}
	for (int s = 0; s < segments; ++s)
		for (int k = 0; k < sides; ++k) {
			const auto at = [&](int i, int j) { return uint32_t(((i + segments) % segments) * sides + (j + sides) % sides); };
			m.Tri(at(s, k), at(s + 1, k), at(s + 1, k + 1));
			m.Tri(at(s, k), at(s + 1, k + 1), at(s, k + 1));
		}
	return m;
}

Mesh Icosahedron(const math::FVector3 &d) {
	Mesh m;
	const float t = (1.f + std::sqrt(5.f)) * 0.5f, n = std::sqrt(1.f + t * t);
	const float v[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
	for (const auto &p : v)
		m.Add({p[0] / n * d.x * 0.5f, p[1] / n * d.y * 0.5f, p[2] / n * d.z * 0.5f});
	static constexpr uint32_t kTris[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
											  {3, 9, 4},  {3, 4, 2}, {3, 2, 6}, {3, 6, 8},  {3, 8, 9},	 {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7},	{9, 8, 1}};
	for (const auto &f : kTris)
		m.Tri(f[0], f[1], f[2]);
	return m;
}

// ── Import ───────────────────────────────────────────────────────────────────

struct Importer {
	Document &doc;
	fs::path project; ///< dossier du projet (objets, scripts, modèles)
	fs::path scene;	  ///< dossier de la scène (chemins des scripts relatifs à lui)
	String error;
	bool spawned = false;

	std::optional<dataio::NodePtr> ReadJson(const fs::path &path) {
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return std::nullopt;
		std::stringstream text;
		text << in.rdbuf();
		auto parsed = dataio::ParseJson(text.str());
		if (!parsed)
			return std::nullopt;
		return std::move(parsed.Value());
	}

	/// Matériau de la palette pour une couleur (et un rendu métallique) : rapproché par nom.
	uint32_t MaterialFor(const modeling::Color &c, bool metal) {
		const auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
		const String name = String(std::format("{}#{:02x}{:02x}{:02x}", metal ? "métal " : "", byte(c.r), byte(c.g), byte(c.b)));
		const auto &mats = doc.Materials();
		if (const auto it = std::ranges::find(mats, name, &modeling::Material::name); it != mats.end())
			return uint32_t(it - mats.begin());
		const uint32_t index = doc.AddMaterial(name, c);
		if (metal) {
			modeling::Material m = doc.Materials()[index];
			m.kind = modeling::MaterialKind::Metal;
			m.roughness = 0.35f;
			doc.SetMaterial(index, m);
		}
		return index;
	}

	modeling::Model MeshModel(const dataio::NodePtr &p) {
		const std::string shape(Prop(p, "shape")->AsString());
		const math::FVector3 d = Vec(Prop(p, "dimensions"), {1.f, 1.f, 1.f});
		const int segments = int(Prop(p, "segments")->AsInt(24));
		modeling::ImportOptions options;
		options.material = MaterialFor(Color255(Prop(p, "base_color"), {0.8f, 0.8f, 0.8f, 1.f}), Prop(p, "metallic")->AsFloat() > 0.5f);
		if (shape == "mesh") { // modèle éditable : `models/<nom>.model` (les anciens `meshes/x.mesh` ont été convertis)
			const fs::path source(std::string(Prop(p, "source")->AsString()));
			for (const fs::path &candidate : {project / source, project / "models" / (source.stem().string() + ".model")})
				if (auto model = modeling::LoadModel(String(candidate.string())); model.IsOk()) {
					modeling::Model placed;
					placed.Materials() = doc.Materials();
					(void)placed.Append(model.Value());
					return placed;
				}
			doc.LoadWarnings().push_back(String::Format("modèle « {} » introuvable", source.string()));
			return {};
		}
		Mesh mesh;
		static constexpr std::pair<float, float> kCylinder[] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
		static constexpr std::pair<float, float> kCone[] = {{0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}};
		if (shape == "cylinder")
			mesh = Lathe(d, segments, kCylinder);
		else if (shape == "cone")
			mesh = Lathe(d, segments, kCone);
		else if (shape == "sphere")
			mesh = Sphere(d, segments);
		else if (shape == "torus")
			mesh = Torus(d, segments);
		else if (shape == "icosahedron")
			mesh = Icosahedron(d);
		else
			mesh = Box(d);
		modeling::Model model = modeling::FromTriangles(mesh.positions, mesh.indices, options);
		model.Materials() = doc.Materials();
		return model;
	}

	/// Un nœud et sa descendance ; `world` : transformation du monde de son parent.
	bool Node(const dataio::NodePtr &n, EntityId parent, const math::FMatrix4 &parentWorld, int depth) {
		if (depth > kMaxDepth) {
			error = String("arbre trop profond (objets imbriqués en boucle ?)");
			return false;
		}
		const math::FMatrix4 world = parentWorld * LocalTransform((*n)["transform"]);
		const std::string type((*n)["type"]->AsString().empty() ? std::string_view("Node") : std::string_view((*n)["type"]->AsString()));
		modeling::Model model;
		Behavior behavior;
		bool rigid = false, mesh = false;
		std::string object;
		for (const dataio::NodePtr &c : (*n)["components"]->AsArray()) {
			const std::string_view kind = (*c)["type"]->AsString();
			const dataio::NodePtr &p = (*c)["props"];
			if (kind == "MeshInstance") {
				model = MeshModel(p);
				mesh = true;
			} else if (kind == "RigidBody") {
				rigid = true;
				behavior.body = Prop(p, "body")->AsString() == "dynamic" ? BodyKind::Dynamic : BodyKind::Solid;
				behavior.mass = Prop(p, "mass")->AsFloat(behavior.mass);
				behavior.friction = Prop(p, "friction")->AsFloat(behavior.friction);
				behavior.bounce = Prop(p, "restitution")->AsFloat(behavior.bounce);
			} else if (kind == "Light") {
				behavior.light = true;
				behavior.lightKind = Prop(p, "kind")->AsString() == "spot" ? LightKind::Spot : LightKind::Point;
				behavior.lightColor = Color255(Prop(p, "color"), behavior.lightColor);
				behavior.lightIntensity = Prop(p, "intensity")->AsFloat(2.f) * 4.f; // échelle de l'ancien moteur
				behavior.lightRange = Prop(p, "range")->AsFloat(behavior.lightRange);
				behavior.spotAngle = Prop(p, "spot_angle")->AsFloat(behavior.spotAngle);
				behavior.penumbra = Prop(p, "penumbra")->AsFloat(behavior.penumbra);
			} else if (kind == "Script") {
				const std::string name(Prop(p, "script")->AsString());
				if (!name.empty()) {
					std::error_code ec;
					const fs::path file = project / "scripts" / (name + ".script");
					behavior.scriptFile = String(fs::relative(file, scene, ec).generic_string());
				}
			} else if (kind == "ObjectInstance") {
				object = std::string(Prop(p, "source")->AsString());
			}
		}
		if (mesh && !rigid)
			behavior.body = BodyKind::Ghost; // l'ancien moteur ne faisait collisionner que les RigidBody
		// Propriétés du nœud (réglages lus par ses scripts : `this.prop(clé)`) → paramètres.
		if ((*n)["properties"]->IsObject())
			for (const auto &[key, prop] : (*n)["properties"]->AsObject()) {
				const dataio::NodePtr &v = (*prop)["value"];
				if (v->IsBool())
					behavior.params.push_back({String(key), bool(v->AsBool())});
				else if (v->IsNumber())
					behavior.params.push_back({String(key), v->AsNumber()});
				else if (v->IsString())
					behavior.params.push_back({String(key), String(v->AsString())});
			}
		Entity &e = doc.AddEntity(String((*n)["name"]->AsString()), std::move(model), world);
		e.type = String(type);
		e.behavior = behavior;
		e.parent = parent;
		const EntityId id = e.id;
		if (type == "SpawnPoint" && !spawned) { // premier point d'apparition : départ de la visite
			spawned = true;
			const math::FVector3 at = world.TransformPoint({0.f, 0.f, 0.f});
			const math::FVector3 forward = world.TransformDirection({0.f, 0.f, 1.f});
			doc.Views().insert(doc.Views().begin(), {"Départ", at + math::FVector3{0.f, 1.7f, 0.f}, at + math::FVector3{forward.x, 1.6f, forward.z}, 60.f, false, 10.f});
		}
		if (!object.empty()) { // instance d'objet : son arbre sous ce nœud
			if (const auto json = ReadJson(project / "objects" / (object + ".object"))) {
				if (!Node((*(*(*json))["tree"])["root"], id, world, depth + 1))
					return false;
			} else {
				doc.LoadWarnings().push_back(String::Format("objet « {} » introuvable (objects/{}.object)", object, object));
			}
		}
		for (const dataio::NodePtr &child : (*n)["children"]->AsArray())
			if (!Node(child, id, world, depth + 1))
				return false;
		return true;
	}
};

} // namespace

bool IsSceneTree(const dataio::NodePtr &json) noexcept { return (*json)["tree"]->IsObject() && (*(*json)["tree"])["root"]->IsObject(); }

Result<Document, String> ImportSceneTree(const dataio::NodePtr &j, const fs::path &file) {
	Document doc;
	const fs::path sceneDir = file.parent_path();
	const fs::path project = sceneDir.filename() == "scenes" || sceneDir.filename() == "objects" ? sceneDir.parent_path() : sceneDir;
	Importer im{doc, project, sceneDir, {}, false};
	if (!im.Node((*(*j)["tree"])["root"], 0, math::FMatrix4::Identity(), 0))
		return Err(String("scène (arbre de nœuds) : " + im.error));
	// Réglages de la scène.
	if (const std::string_view gameplay = (*j)["gameplay_script_file"]->AsString(); !gameplay.empty())
		doc.Game().scriptFile = String(gameplay);
	if (const dataio::NodePtr &env = (*j)["environment"]; env->IsObject()) {
		EnvironmentSettings &e = doc.Environment();
		e.sunIntensity = (*env)["sun_intensity"]->AsFloat(0.95f) * 2.4f;
		e.sunColor = Color255((*env)["sun_color"], e.sunColor);
		const modeling::Color ambient = Color255((*env)["ambient_color"], {0.12f, 0.13f, 0.17f, 1.f});
		const modeling::Color background = Color255((*env)["background_color"], {0.07f, 0.08f, 0.11f, 1.f});
		e.zenith = background;
		e.horizon = {(background.r + ambient.r) * 0.5f, (background.g + ambient.g) * 0.5f, (background.b + ambient.b) * 0.5f, 1.f};
		e.ground = ambient;
		e.ambient = std::clamp((ambient.r + ambient.g + ambient.b) / 3.f * 3.f, 0.15f, 1.f);
	}
	if (const dataio::NodePtr &cam = (*j)["camera"]; cam->IsObject()) {
		const math::FVector3 edit = Vec((*cam)["edit_position"], {0.f, 6.f, -14.f});
		// Vue d'ouverture : depuis la caméra d'édition, vers le centre de la scène.
		const math::FAABB box = doc.Bounds();
		const math::FVector3 target = box.min.x <= box.max.x ? box.Center() : edit + math::FVector3{0.f, -1.f, 1.f};
		doc.Views().insert(doc.Views().begin(), {"Édition", edit, target, 45.f, false, 10.f});
		if (!im.spawned) {
			const math::FVector3 play = Vec((*cam)["play_position"], {0.f, 1.7f, 0.f});
			// Ancien moteur : lacet nul = vers +Z (le nôtre regarde vers −Z).
			math::FVector3 look = math::DirectionFromYawPitch((*cam)["play_yaw"]->AsFloat(), (*cam)["play_pitch"]->AsFloat());
			look.z = -look.z;
			doc.Views().push_back({"Départ", play, play + look * 5.f, 60.f, false, 10.f});
		}
	}
	doc.SyncMaterials();
	return Ok(std::move(doc));
}

} // namespace sandbox
