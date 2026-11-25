// Voxelia Studio — atelier de modélisation (phase 2). Cf. studio_model.hpp.
#include "studio_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <fstream>
#include <iterator>
#include <numbers>

#include <sdl3/sdl3.hpp>

#include <core/log.hpp>
#include <core/string.hpp>
#include <sdl3/io/dialog.hpp>
#include <sdl3/render/image.hpp>

namespace voxelia::studio {

namespace icons = ui::icons;
using Rect = sdl3::FRect;
using sdl3::Rgba;
using math::FVector2;
using math::FVector3;

namespace {

constexpr size_t kUndoDepth = 64;
constexpr const char *kConnectFaces[] = {"north", "south", "east", "west", "up", "down"};

// ── Gabarits de modèles génériques (rigs) ──────────────────────────────────
constexpr const char *kPresetLabels[] = {"Cube", "Porte", "Dalle", "Torche"};

// ── Mécaniques génériques ──────────────────────────────────────────────────
constexpr std::string_view kMechanicTypes[] = {"", "door", "drawer", "switch"};
constexpr std::string_view kMechanicLabels[] = {"Aucune", "Porte", "Tiroir", "Interrupteur"};

/// Construit un rig générique prêt à animer depuis un gabarit (0 cube, 1 porte,
/// 2 dalle, 3 torche). La porte illustre la « mécanique » : panneau pivoté sur sa
/// charnière + animation d'ouverture, réutilisable par plusieurs objets.
voxel::Model MakeRigPreset(int preset) {
	voxel::Model m;
	m.format = voxel::ModelFormat::Rig;
	m.unitsPerBlock = 16.f;
	m.boxUv = true;
	m.textureWidth = 64;
	m.textureHeight = 64;
	switch (preset) {
	case 0: { // Cube : une partie, une boîte.
		voxel::ModelPart p;
		p.name = "corps";
		voxel::ModelBox b;
		b.from = {-4, 0, -4};
		b.size = {8, 8, 8};
		p.boxes.push_back(std::move(b));
		m.parts.push_back(std::move(p));
		break;
	}
	case 1: { // Porte : panneau + poignée, charnière à gauche, animation d'ouverture.
		voxel::ModelPart panel;
		panel.name = "panneau";
		panel.pivot = {-8, 16, 0};
		voxel::ModelBox pb;
		pb.from = {-8, 0, -1};
		pb.size = {16, 32, 2};
		panel.boxes.push_back(std::move(pb));
		m.parts.push_back(std::move(panel));
		voxel::ModelPart handle;
		handle.name = "poignee";
		handle.pivot = {-8, 16, 0};
		voxel::ModelBox hb;
		hb.from = {11, 14, 1};
		hb.size = {1, 4, 1};
		handle.boxes.push_back(std::move(hb));
		m.parts.push_back(std::move(handle));
		voxel::Animation open;
		open.name = "ouvrir";
		open.length = 0.5f;
		open.loop = false;
		voxel::AnimationTrack t;
		t.part = "panneau";
		t.rotation.push_back({0.f, math::FVector3{0, 0, 0}});
		t.rotation.push_back({0.5f, math::FVector3{0, -90, 0}});
		open.tracks.push_back(std::move(t));
		m.animations.push_back(std::move(open));
		m.mechanic = voxel::ModelMechanic{"door", "ouvrir", true};
		break;
	}
	case 2: { // Dalle : moitié basse d'un bloc.
		voxel::ModelPart p;
		p.name = "dalle";
		voxel::ModelBox b;
		b.from = {-8, 0, -8};
		b.size = {16, 8, 16};
		p.boxes.push_back(std::move(b));
		m.parts.push_back(std::move(p));
		break;
	}
	default: { // Torche : bâton + flamme.
		voxel::ModelPart p;
		p.name = "torche";
		voxel::ModelBox stick;
		stick.from = {-1, 0, -1};
		stick.size = {2, 8, 2};
		p.boxes.push_back(std::move(stick));
		voxel::ModelBox flame;
		flame.from = {-2, 8, -2};
		flame.size = {4, 6, 4};
		p.boxes.push_back(std::move(flame));
		m.parts.push_back(std::move(p));
		break;
	}
	}
	return m;
}

/// Aplatit les boîtes d'un modèle (pose de repos) en une liste nodebox **centrée**
/// (-0.5..0.5) : rig x/z déjà centrés, y (pieds) décalé de -0.5.
dataio::NodePtr ModelToNodeboxList(const voxel::Model &m) {
	dataio::NodePtr list = dataio::Node::MakeArray();
	const float inv = 1.f / m.unitsPerBlock;
	auto round = [](float v) { return dataio::Node::MakeFloat(std::round(double(v) * 10000.0) / 10000.0); };
	for (const voxel::ModelPart &p : m.parts)
		if (p.visible)
			for (const voxel::ModelBox &b : p.boxes) {
				if (!b.visible)
					continue;
				dataio::NodePtr e = dataio::Node::MakeArray();
				const float x0 = b.from.x * inv, y0 = b.from.y * inv - 0.5f, z0 = b.from.z * inv;
				const float x1 = (b.from.x + b.size.x) * inv, y1 = (b.from.y + b.size.y) * inv - 0.5f, z1 = (b.from.z + b.size.z) * inv;
				for (float v : {x0, y0, z0, x1, y1, z1})
					e->Push(round(v));
				list->Push(std::move(e));
			}
	return list;
}

/// Construit l'atlas 3×2 des six textures d'un bloc et remplit l'origine (0..1) de chaque
/// face — un bloc nodebox à 6 faces apparaît ainsi comme **une** texture dans l'atelier.
sdl3::Surface BuildFaceAtlas(App &app, const std::array<std::string, 6> &textures, std::array<math::FVector2, 6> &cells) {
	constexpr int cell = 64;
	sdl3::Surface atlas = sdl3::Surface::Solid(cell * 3, cell * 2, 0, 0, 0, 0).UnwrapOr(sdl3::Surface{});
	// Face → cellule (colonne, rangée) : +X est, -X ouest, +Y haut, -Y bas, +Z sud, -Z nord.
	static constexpr int kCol[6] = {0, 1, 2, 0, 1, 2};
	static constexpr int kRow[6] = {0, 0, 0, 1, 1, 1};
	for (int f = 0; f < 6; ++f) {
		cells[size_t(f)] = {float(kCol[f]) / 3.f, float(kRow[f]) / 2.f};
		std::optional<sdl3::Surface> img;
		const std::string &name = textures[size_t(f)];
		if (!name.empty())
			if (auto src = app.content.mods.ResolveTexture(name); src && !src->path.empty())
				if (auto r = sdl3::LoadImageRgba(src->path))
					img = std::move(r.Value());
		const sdl3::Surface thumb = img ? sdl3::SquareThumbnail(*img, cell).UnwrapOr(sdl3::Surface{})
									   : sdl3::Surface::Solid(cell, cell, 42, 42, 46, 255).UnwrapOr(sdl3::Surface{});
		for (int y = 0; y < cell; ++y)
			for (int x = 0; x < cell; ++x) {
				const size_t di = (size_t(kRow[f] * cell + y) * size_t(cell * 3) + size_t(kCol[f] * cell + x)) * 4;
				const size_t si = size_t(y * cell + x) * 4;
				std::memcpy(&atlas.Pixels()[di], &thumb.Pixels()[si], 4);
			}
	}
	return atlas;
}

std::string ReadText(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool WriteText(const fs::path &p, std::string_view text) {
	std::ofstream f(p, std::ios::binary | std::ios::trunc);
	f.write(text.data(), std::streamsize(text.size()));
	return bool(f);
}

/// JSON d'un fichier entier, indenté par tabulations (style des mods).
std::string WriteRootJson(const dataio::NodePtr &v) {
	const std::string raw = dataio::WriteJson(v, 1);
	std::string out;
	bool lineStart = true;
	for (char c : raw) {
		if (lineStart && c == ' ') {
			out.push_back('\t');
			continue;
		}
		lineStart = c == '\n';
		out.push_back(c);
	}
	out.push_back('\n');
	return out;
}

std::string QualifyName(std::string_view name, std::string_view mod) {
	if (name.empty() || name.find(':') != std::string_view::npos)
		return std::string(name);
	return std::format("{}:{}", mod, name);
}

const char *FormatLabel(voxel::ModelFormat f) { return voxel::ModelFormatLabel(f); }

sdl3::Color FormatColor(voxel::ModelFormat f) {
	switch (f) {
	case voxel::ModelFormat::Rig: return Rgba(198, 120, 221);
	case voxel::ModelFormat::Obj:
	case voxel::ModelFormat::B3d: return Rgba(86, 182, 194);
	case voxel::ModelFormat::Boxes: return Rgba(236, 180, 72);
	case voxel::ModelFormat::Cross: return Rgba(96, 200, 120);
	default: return Rgba(86, 156, 255);
	}
}

/// Caméra orbitale projetée sur le canvas.
struct ViewCam {
	FVector3 eye, right, up, fwd;
	float focal = 1, cx = 0, cy = 0;
	static ViewCam Make(const FVector3 &target, float yaw, float pitch, float distance, const Rect &r) {
		ViewCam c;
		const FVector3 dir{std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
		c.eye = target + dir * distance;
		c.fwd = (target - c.eye).Normalized();
		c.right = c.fwd.Cross({0, 1, 0}).Normalized();
		c.up = c.right.Cross(c.fwd);
		c.focal = r.h * 0.5f / std::tan(0.5f * 40.f * std::numbers::pi_v<float> / 180.f);
		c.cx = r.x + r.w * 0.5f;
		c.cy = r.y + r.h * 0.5f;
		return c;
	}
	bool Project(const FVector3 &p, float &sx, float &sy, float &depth) const {
		const FVector3 d = p - eye;
		depth = d.Dot(fwd);
		if (depth < 0.02f)
			return false;
		sx = cx + d.Dot(right) / depth * focal;
		sy = cy - d.Dot(up) / depth * focal;
		return true;
	}
	/// Segment 3D découpé au plan proche ; faux s'il est entièrement derrière.
	bool Segment(FVector3 a, FVector3 b, float &x0, float &y0, float &x1, float &y1) const {
		const float da = (a - eye).Dot(fwd), db = (b - eye).Dot(fwd);
		constexpr float kNear = 0.05f;
		if (da < kNear && db < kNear)
			return false;
		if (da < kNear)
			a = a + (b - a) * ((kNear - da) / (db - da));
		else if (db < kNear)
			b = b + (a - b) * ((kNear - db) / (da - db));
		float d;
		return Project(a, x0, y0, d) && Project(b, x1, y1, d);
	}
};

bool PointInTriangle(float px, float py, float ax, float ay, float bx, float by, float cx, float cy, float &w0, float &w1, float &w2) {
	const float den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
	if (std::fabs(den) < 1e-6f)
		return false;
	w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
	w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
	w2 = 1.f - w0 - w1;
	return w0 >= -1e-4f && w1 >= -1e-4f && w2 >= -1e-4f;
}

float DistToSegment(float px, float py, float ax, float ay, float bx, float by) {
	const float vx = bx - ax, vy = by - ay;
	const float l2 = vx * vx + vy * vy;
	const float t = l2 > 0 ? std::clamp(((px - ax) * vx + (py - ay) * vy) / l2, 0.f, 1.f) : 0.f;
	const float dx = ax + vx * t - px, dy = ay + vy * t - py;
	return std::sqrt(dx * dx + dy * dy);
}

float &Axis(FVector3 &v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }
float AxisOf(const FVector3 &v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

constexpr sdl3::Color kAxisColors[3] = {sdl3::Color{226, 72, 72, 255}, sdl3::Color{86, 196, 92, 255}, sdl3::Color{76, 140, 236, 255}};

std::array<uint8_t, 3> BiomeTintColor(std::string_view kind) {
	if (kind == "foliage")
		return {96, 166, 64};
	if (kind == "water")
		return {63, 118, 228};
	return {125, 185, 85};
}

constexpr std::array<std::array<uint8_t, 4>, 16> kDefaultPalette = {{
	{20, 20, 24, 255},	  {255, 255, 255, 255}, {128, 128, 136, 255}, {72, 72, 80, 255}, {200, 60, 56, 255},   {236, 140, 52, 255},
	{240, 210, 80, 255}, {88, 168, 72, 255},	  {44, 112, 52, 255},  {72, 152, 220, 255}, {48, 72, 160, 255},	 {150, 90, 200, 255},
	{230, 150, 170, 255}, {140, 96, 60, 255},	  {96, 64, 40, 255},   {0, 0, 0, 0},
}};

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Catalogue
// ══════════════════════════════════════════════════════════════════════════

ModelCatalog ScanModels(const std::vector<std::pair<std::string, fs::path>> &mods) {
	ModelCatalog cat;
	for (const auto &[modId, dir] : mods) {
		std::error_code ec;
		std::vector<fs::path> files;
		for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
			if (!it->is_regular_file() || it->path().extension() != ".json")
				continue;
			const std::string rel = fs::relative(it->path(), dir, ec).generic_string();
			if (rel.starts_with("sounds/") || rel == "textures/assets.json" || rel.starts_with("recipes/"))
				continue;
			files.push_back(it->path());
		}
		std::ranges::sort(files);
		for (const fs::path &file : files) {
			auto parsed = dataio::ParseJson(ReadText(file));
			if (!parsed)
				continue;
			++cat.files;
			const dataio::NodePtr &root = parsed.Value();
			auto visit = [&](const dataio::NodePtr &d, size_t index) {
				if (!d->IsObject())
					return;
				const std::string_view type = (*d)["type"]->AsString();
				const std::string id = QualifyName((*d)["id"]->AsString(), modId);
				ModelEntry e;
				e.id = id;
				e.label = std::string((*d)["displayName"]->AsString((*d)["id"]->AsString()));
				e.mod = modId;
				e.file = file;
				e.index = index;
				const dataio::NodePtr &g = (*d)["graphics"];
				e.texture = std::string((*g)["texture"]->AsString());
				if (e.texture.empty() && (*g)["textures"]->IsObject())
					for (const char *k : {"side", "top", "all", "front"})
						if (const std::string_view t = (*(*g)["textures"])[k]->AsString(); !t.empty()) {
							e.texture = std::string(t);
							break;
						}
				if (!e.texture.empty())
					e.texture = QualifyName(e.texture, modId);
				// Textures par face (ordre Face : +X est, -X ouest, +Y haut, -Y bas, +Z sud, -Z nord)
				// — permet de construire une seule texture UV (atlas 3×2) pour les blocs nodebox.
				e.textures.fill(e.texture);
				if ((*g)["textures"]->IsObject()) {
					const std::string side = QualifyName((*(*g)["textures"])["side"]->AsString(), modId);
					const std::string all = QualifyName((*(*g)["textures"])["all"]->AsString(), modId);
					auto pick = [&](const char *k) {
						const std::string v = QualifyName((*(*g)["textures"])[k]->AsString(), modId);
						return !v.empty() ? v : (!all.empty() ? all : (!side.empty() ? side : e.texture));
					};
					e.textures[size_t(voxel::Face::PosX)] = pick("east");
					e.textures[size_t(voxel::Face::NegX)] = pick("west");
					e.textures[size_t(voxel::Face::PosY)] = pick("top");
					e.textures[size_t(voxel::Face::NegY)] = pick("bottom");
					e.textures[size_t(voxel::Face::PosZ)] = pick("south");
					e.textures[size_t(voxel::Face::NegZ)] = pick("north");
				}
				if (const dataio::NodePtr &rig = (*d)["rig"]; rig->IsObject()) { // créature : skins de son modèle
					std::vector<std::string> skins;
					for (const dataio::NodePtr &s : (*rig)["skins"]->AsArray())
						skins.emplace_back(s->AsString());
					cat.skins.emplace_back(QualifyName((*rig)["model"]->AsString(), modId), std::move(skins));
				}
				if (type == "model") {
					const std::string_view fmt = (*d)["format"]->AsString("json");
					e.kind = "Modèle";
					e.format = fmt == "rig"	  ? voxel::ModelFormat::Rig
							   : fmt == "obj" ? voxel::ModelFormat::Obj
							   : fmt == "b3d" ? voxel::ModelFormat::B3d
											  : voxel::ModelFormat::Boxes;
					if (const std::string_view p = (*d)["path"]->AsString(); !p.empty())
						e.mesh = dir / p;
					cat.entries.push_back(std::move(e));
					return;
				}
				e.kind = "Bloc";
				if (d->Get("nodebox")) {
					e.format = voxel::ModelFormat::Nodebox;
				} else if ((type == "obj" || type == "b3d") && !(*d)["path"]->AsString().empty()) {
					e.format = type == "obj" ? voxel::ModelFormat::Obj : voxel::ModelFormat::B3d;
					e.mesh = dir / std::string((*d)["path"]->AsString());
				} else if (const dataio::NodePtr &shape = (*d)["shape"]; shape->IsObject() && !(*shape)["model"]->AsString().empty()) {
					e.format = voxel::ModelFormat::Boxes;
					e.refModel = QualifyName((*shape)["model"]->AsString(), modId);
				} else if ((*d)["shape"]->AsString() == "cross") {
					e.format = voxel::ModelFormat::Cross;
				} else {
					return; // cube plein (ou entrée sans forme) : pas listé
				}
				cat.entries.push_back(std::move(e));
			};
			if (root->IsArray()) {
				for (size_t i = 0; i < root->Size(); ++i)
					visit((*root)[i], i);
			} else {
				visit(root, SIZE_MAX);
			}
		}
	}
	// Avatar du joueur : rig éditable généré (forme + animations), sans fichier source.
	{
		ModelEntry e;
		e.id = "core:player_avatar";
		e.label = "Avatar du joueur";
		e.mod = "core";
		e.kind = "Modèle";
		e.format = voxel::ModelFormat::Rig;
		e.index = SIZE_MAX;
		cat.entries.insert(cat.entries.begin(), std::move(e));
	}
	return cat;
}

// ══════════════════════════════════════════════════════════════════════════
// État de l'atelier
// ══════════════════════════════════════════════════════════════════════════

struct ModelWorkshop::Impl {
	App &app;
	jobs::AsyncValue<ModelCatalog> catalog;
	bool catalogRequested = false;
	std::string search;
	int formatFilter = 0;
	std::string confirmId; // ouverture qui abandonnerait des modifications
	uint64_t confirmTicks = 0;

	struct Doc {
		ModelEntry entry;
		voxel::Model model;
		dataio::NodePtr original; // définition source
		bool dirty = false;
		std::string connectTo; // connectivité : blocs visés (« #fence, #solid »), parties « connect_<face> »
	};
	std::optional<Doc> doc;
	std::vector<voxel::Model> undo, redo;
	voxel::Model shadow; // état avant l'édition en cours
	bool editOpen = false;

	// Sélection.
	int part = -1, box = -1, collision = -1;
	bool showGrid = true, showBounds = true, showCollision = true;
	bool doubleSided = false; // voir aussi les faces arrière (édition des maillages)
	bool extruding = false;	  // clic sur une face du cube sélectionné : extrude un nouveau cube

	// Sous-éléments (édition type Sketchup/Blockbench) : sommets, arêtes, faces.
	enum class SelMode : uint8_t { Object, Vertex, Edge, Face };
	SelMode selMode = SelMode::Object;
	struct SubRef { int box = 0; uint8_t sub = 0; bool operator==(const SubRef &) const = default; }; // sub : coin (0-7) / arête (0-11) / face (0-5)
	std::vector<SubRef> selVerts, selEdges, selFaces; // multi-sélection (identité stable)
	// Push/pull (Sketchup) : tire une face (extrusion) ou pousse (creusement).
	int ppFace = -1;			   // face en cours (BoxFace) ; -1 : inactif
	int ppBox = 0;				   // cube source du push/pull
	math::FVector3 ppNormal{};	   // normale monde de la face
	math::FVector3 ppCenter{};	   // centre de la face (monde) au début du geste
	math::FVector2 ppScreenDir{};  // direction écran de la normale (normalisée)
	float ppScreenLen = 0.f;	   // px écran pour 0,1 bloc le long de la normale
	voxel::Model ppStart;		   // modèle au début du geste

	// Image (texture du document, ou texture de bloc seule).
	struct Image {
		fs::path path;
		std::string textureName; // texture de bloc (rechargement à chaud)
		sdl3::Surface pixels;
		std::optional<sdl3::Texture> tex;
		bool stale = true, dirty = false;
		std::vector<sdl3::Surface> undo, redo;
	};
	std::optional<Image> image;
	std::optional<sdl3::Texture> white;
	bool texturesChanged = false;

	// Atlas des faces d'un bloc nodebox : les 6 textures du bloc réunies en une seule
	// texture UV (3×2), avec l'origine (u,v) de chaque face pour remapper la projection.
	struct FaceAtlas {
		std::array<math::FVector2, 6> cells; // origine (0..1) de chaque face dans l'atlas
		math::FVector2 cellSize{1.f / 3.f, 0.5f};
	};
	std::optional<FaceAtlas> blockAtlas;

	// Vue.
	float yaw = 0.75f, pitch = 0.42f, distance = 3.f;
	FVector3 target{};
	enum class Mode : uint8_t { Edit, Paint, Animate } mode = Mode::Edit;
	enum class Tool : uint8_t { Move, Resize, Rotate } tool = Tool::Move;
	int dragAxis = -1;
	float dragX = 0, dragY = 0;
	FVector2 dragScreen{}; // déplacement écran par bloc le long de l'axe glissé
	voxel::Model dragStart;
	voxel::PartOffset dragOffset; // pose au début d'un glissé (mode Animation)
	bool pressed = false, orbiting = false, panning = false;
	float pressX = 0, pressY = 0, lastX = 0, lastY = 0;
	int pressQuad = -1; // quad survolé au clic (pour distinguer sélection / push-pull)
	std::vector<voxel::ModelQuad> quads;
	struct Proj {
		float x[4], y[4], z[4];
		float depth;
		size_t quad;
	};
	std::vector<Proj> proj; // triés du plus loin au plus proche
	// Maillage sous-élément de la partie sélectionnée (unités bloc, transformé par la pose).
	struct MeshVert { math::FVector3 p; int box = 0; uint8_t corner = 0; };
	std::vector<MeshVert> meshVerts;
	std::vector<std::array<int, 2>> meshEdges;
	std::vector<uint8_t> meshEdgeSub;					  // index de l'arête dans sa boîte (0-11)
	std::vector<math::FVector2> meshVerts2d;			  // projetés (pour le survol)
	std::vector<std::array<math::FVector2, 2>> meshEdges2d; // projetés

	// Peinture.
	enum class Brush : uint8_t { Pencil, Eraser, Picker, Fill } brush = Brush::Pencil;
	std::array<uint8_t, 4> color{88, 168, 72, 255};
	int brushSize = 1;
	bool stroke = false;
	float paintZoom = 0, paintPanX = 0, paintPanY = 0;
	float uvZoom = 0;

	// Animation.
	int anim = -1;
	float time = 0;
	bool playing = false, motion = false;
	float motionTime = 0;
	bool scrubbing = false;

	// Import de texture : chemin choisi par le dialogue natif (traité dans Update).
	std::optional<fs::path> pendingImport;

	// Variante de bloc : nom et texture du bloc généré depuis le modèle.
	std::string blockName, blockTexture;

	std::string status;

	explicit Impl(App &a) : app(a) {}

	voxel::Animation *Anim() {
		if (!doc || anim < 0 || size_t(anim) >= doc->model.animations.size())
			return nullptr;
		return &doc->model.animations[size_t(anim)];
	}
	voxel::ModelPart *Part() {
		if (!doc || part < 0 || size_t(part) >= doc->model.parts.size())
			return nullptr;
		return &doc->model.parts[size_t(part)];
	}
	voxel::ModelBox *Box() {
		voxel::ModelPart *p = Part();
		if (!p || box < 0 || size_t(box) >= p->boxes.size())
			return nullptr;
		return &p->boxes[size_t(box)];
	}
	/// Une modification du modèle commence (une étape d'annulation par geste).
	void Changed() {
		if (!doc)
			return;
		if (!editOpen) {
			undo.push_back(shadow);
			if (undo.size() > kUndoDepth)
				undo.erase(undo.begin());
			redo.clear();
			editOpen = true;
		}
		doc->dirty = true;
	}
	void ClampSelection() {
		if (!doc) {
			part = box = collision = -1;
			return;
		}
		if (part >= int(doc->model.parts.size()))
			part = int(doc->model.parts.size()) - 1;
		if (voxel::ModelPart *p = Part(); !p || box >= int(p->boxes.size()))
			box = p ? int(p->boxes.size()) - 1 : -1;
		if (collision >= int(doc->model.collision.size()))
			collision = int(doc->model.collision.size()) - 1;
		if (anim >= int(doc->model.animations.size()))
			anim = int(doc->model.animations.size()) - 1;
	}
	float Snap() const {
		if (!doc)
			return 1;
		return doc->model.boxUv ? 1.f : doc->model.unitsPerBlock / 16.f;
	}
	void Frame() {
		if (!doc)
			return;
		const voxel::ModelBounds b = doc->model.Bounds();
		target = b.Center();
		const FVector3 s = b.Size();
		distance = std::max({s.x, s.y, s.z, 0.25f}) * 2.4f + 0.4f;
	}

	// ── Image ──
	void ImageChanged() {
		if (image) {
			image->stale = true;
			image->dirty = true;
		}
	}
	void ImageSnapshot() {
		if (!image)
			return;
		if (auto d = image->pixels.Duplicate())
			image->undo.push_back(std::move(d.Value()));
		if (image->undo.size() > kUndoDepth)
			image->undo.erase(image->undo.begin());
		image->redo.clear();
	}
	sdl3::Texture *ImageTexture(sdl3::Renderer &ren) {
		if (!image || !image->pixels)
			return nullptr;
		if (!image->tex || int(image->tex->GetWidth()) != image->pixels.GetWidth() || int(image->tex->GetHeight()) != image->pixels.GetHeight()) {
			auto t = sdl3::Texture::Create(ren, image->pixels.GetWidth(), image->pixels.GetHeight(), sdl3::PixelFormat::RGBA32);
			if (!t)
				return nullptr;
			image->tex.emplace(std::move(t.Value()));
			image->tex->SetScaleMode(sdl3::ScaleMode::NEAREST);
			image->stale = true;
		}
		if (image->stale) {
			image->tex->Update(image->pixels.Pixels(), image->pixels.GetWidth() * 4);
			image->stale = false;
		}
		return &*image->tex;
	}
	sdl3::Texture *WhiteTexture(sdl3::Renderer &ren) {
		if (!white) {
			auto t = sdl3::Texture::Create(ren, 1, 1, sdl3::PixelFormat::RGBA32);
			if (!t)
				return nullptr;
			const uint8_t px[4] = {255, 255, 255, 255};
			t.Value().Update(px, 4);
			white.emplace(std::move(t.Value()));
		}
		return &*white;
	}
	/// Facteur appliqué aux V normalisés : première vignette d'une bande d'animation
	/// (nodebox), ou image plus haute que la `textureSize` du rig (skin 64×64 pour un
	/// rig 64×32 : les texels gardent leur taille, l'échelle suit la largeur).
	float FrameScale() const {
		if (!image || !doc || image->pixels.GetWidth() <= 0 || image->pixels.GetHeight() <= 0)
			return 1.f;
		const float w = float(image->pixels.GetWidth()), h = float(image->pixels.GetHeight());
		if (doc->model.boxUv)
			return float(doc->model.textureHeight) * w / (float(doc->model.textureWidth) * h);
		return h > w && int(h) % int(w) == 0 ? w / h : 1.f;
	}
	/// Pixels d'image par texel du dépliage « box UV ».
	float TexelScale() const {
		if (!image || !doc || !doc->model.boxUv || doc->model.textureWidth <= 0)
			return 1.f;
		return float(image->pixels.GetWidth()) / float(doc->model.textureWidth);
	}
	void Plot(int x, int y, const std::array<uint8_t, 4> &c) {
		if (!image)
			return;
		sdl3::Surface &img = image->pixels;
		for (int dy = 0; dy < brushSize; ++dy)
			for (int dx = 0; dx < brushSize; ++dx) {
				const int px = x + dx - brushSize / 2, py = y + dy - brushSize / 2;
				if (px < 0 || py < 0 || px >= img.GetWidth() || py >= img.GetHeight())
					continue;
				std::memcpy(&img.Pixels()[size_t(py * img.GetWidth() + px) * 4], c.data(), 4);
			}
		ImageChanged();
	}
	void FloodFill(int x, int y, const std::array<uint8_t, 4> &c) {
		sdl3::Surface &img = image->pixels;
		if (x < 0 || y < 0 || x >= img.GetWidth() || y >= img.GetHeight())
			return;
		std::array<uint8_t, 4> from;
		std::memcpy(from.data(), &img.Pixels()[size_t(y * img.GetWidth() + x) * 4], 4);
		if (from == c)
			return;
		std::deque<std::pair<int, int>> q{{x, y}};
		while (!q.empty()) {
			auto [px, py] = q.front();
			q.pop_front();
			if (px < 0 || py < 0 || px >= img.GetWidth() || py >= img.GetHeight())
				continue;
			uint8_t *p = &img.Pixels()[size_t(py * img.GetWidth() + px) * 4];
			if (std::memcmp(p, from.data(), 4) != 0)
				continue;
			std::memcpy(p, c.data(), 4);
			q.push_back({px + 1, py});
			q.push_back({px - 1, py});
			q.push_back({px, py + 1});
			q.push_back({px, py - 1});
		}
		ImageChanged();
	}
	/// Applique l'outil courant au texel (x, y) ; `begin` : début du trait.
	void Paint(int x, int y, bool begin) {
		if (!image)
			return;
		sdl3::Surface &img = image->pixels;
		x = std::clamp(x, 0, img.GetWidth() - 1);
		y = std::clamp(y, 0, img.GetHeight() - 1);
		if (brush == Brush::Picker) {
			std::memcpy(color.data(), &img.Pixels()[size_t(y * img.GetWidth() + x) * 4], 4);
			return;
		}
		if (begin)
			ImageSnapshot();
		if (brush == Brush::Fill) {
			if (begin)
				FloodFill(x, y, color);
			return;
		}
		Plot(x, y, brush == Brush::Eraser ? std::array<uint8_t, 4>{0, 0, 0, 0} : color);
	}
	bool SaveImage() {
		if (!image || image->path.empty())
			return false;
		const sdl3::Surface &img = image->pixels;
		auto surf = sdl3::Surface::CreateFromPixels(img.GetWidth(), img.GetHeight(), sdl3::PixelFormat::RGBA32, img.Pixels(), img.GetWidth() * 4);
		if (!surf) {
			status = "échec de création de l'image";
			return false;
		}
		if (!sdl3::ImgSavePng(surf.Value(), String(image->path.string()))) {
			status = std::format("échec d'écriture : {}", image->path.string());
			return false;
		}
		image->dirty = false;
		ReloadBlockTexture();
		return true;
	}
	/// Rechargement à chaud : couches 16×16 de la texture dans la partie.
	void ReloadBlockTexture() {
		if (!image || image->textureName.empty())
			return;
		size_t updated = 0;
		for (const auto &e : app.content.layerOf) {
			const std::string &key = e.key;
			const size_t bar = key.find('|');
			if (std::string_view(key).substr(0, bar) != image->textureName)
				continue;
			if (auto thumbR = sdl3::SquareThumbnail(image->pixels, voxel::kTextureSize)) {
				sdl3::Surface thumb = std::move(thumbR.Value());
				if (bar != std::string::npos) {
					const auto t = BiomeTintColor(std::string_view(key).substr(bar + 1));
					sdl3::Tint(thumb, t[0], t[1], t[2]);
				}
				const size_t bytes = size_t(voxel::kTextureSize) * voxel::kTextureSize * 4;
				const size_t offset = size_t(e.value) * bytes;
				if (offset + bytes <= app.content.textureLayers.size() && size_t(thumb.GetWidth()) * size_t(thumb.GetHeight()) * 4 == bytes) {
					std::memcpy(app.content.textureLayers.data() + offset, thumb.Pixels(), bytes);
					++updated;
				}
			}
		}
		if (updated && app.renderer) {
			app.renderer->SetBlockTextures(app.content.textureLayers, app.content.layerCount);
			texturesChanged = true;
		}
	}

	// ── Ouverture ──
	std::optional<fs::path> ResolveTexturePath(std::string_view name) const {
		if (name.empty())
			return std::nullopt;
		const std::string base(name.substr(0, name.find('^')));
		if (auto src = app.content.mods.ResolveTexture(base); src && !src->path.empty() && fs::exists(src->path))
			return src->path;
		const size_t colon = base.find(':');
		if (colon != std::string::npos)
			if (const voxel::ModInfo *m = app.content.mods.Find(std::string_view(base).substr(0, colon))) {
				fs::path p = m->directory / "textures" / (base.substr(colon + 1) + ".png");
				if (fs::exists(p))
					return p;
			}
		return std::nullopt;
	}

	bool OpenEntry(const ModelEntry &entry, const ModelCatalog *cat) {
		// Avatar du joueur : rig éditable (forme + animations), enregistré dans
		// `<core>/models/player_avatar.json` (chargé par le jeu).
		if (entry.id == "core:player_avatar") {
			ModelEntry e = entry;
			if (const voxel::ModInfo *core = app.content.mods.Find("core")) {
				e.file = core->directory / "models" / "player_avatar.json";
				e.mod = "core";
			}
			voxel::Model model = voxel::BuildAvatarRig();
			dataio::NodePtr original = dataio::Node::MakeObject();
			if (!e.file.empty() && fs::exists(e.file))
				if (auto parsed = dataio::ParseJson(ReadText(e.file))) {
					original = parsed.Value();
					if (auto loaded = voxel::ParseRig(original))
						model = std::move(loaded.Value());
				}
			Doc d{e, std::move(model), std::move(original), false, {}};
			doc = std::move(d);
			undo.clear();
			redo.clear();
			editOpen = false;
			part = doc->model.parts.empty() ? -1 : 0;
			box = -1;
			collision = -1;
			anim = -1;
			time = 0;
			playing = false;
			image.reset();
			if (const SkinDef *skin = app.content.FindSkin(app.settings.playerSkin))
				if (auto p = app.content.SkinTexturePath(skin->texture))
					if (auto img = sdl3::LoadImageRgba(*p))
						image = Image{*p, {}, std::move(*img), {}, true, false, {}, {}};
			status = "avatar du joueur : forme et animations modifiables";
			return true;
		}
		if (!entry.refModel.empty() && cat) // bloc dont la forme est un modèle : on édite le modèle
			for (const ModelEntry &e : cat->entries)
				if (e.id == entry.refModel && e.kind == "Modèle") {
					ModelEntry m = e;
					if (m.texture.empty())
						m.texture = entry.texture;
					return OpenEntry(m, cat);
				}
		auto parsed = dataio::ParseJson(ReadText(entry.file));
		if (!parsed) {
			status = std::format("{} : {}", entry.file.string(), parsed.Error().Format());
			return false;
		}
		const dataio::NodePtr def = entry.index == SIZE_MAX ? parsed.Value() : (*parsed.Value())[entry.index];
		Result<voxel::Model, std::string> model = Err(std::string("format inconnu"));
		switch (entry.format) {
		case voxel::ModelFormat::Rig: model = voxel::ParseRig(def); break;
		case voxel::ModelFormat::Boxes: model = voxel::ParseBoxesModel(def); break;
		case voxel::ModelFormat::Nodebox: model = voxel::ParseNodebox((*def)["nodebox"]); break;
		case voxel::ModelFormat::Cross: model = Ok(voxel::MakeCrossModel()); break;
		case voxel::ModelFormat::Cube: model = Ok(voxel::MakeCubeModel()); break;
		case voxel::ModelFormat::Obj: model = voxel::ParseObj(ReadText(entry.mesh)); break;
		case voxel::ModelFormat::B3d: {
			const std::string bytes = ReadText(entry.mesh);
			model = voxel::ParseB3d(std::span(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()));
			break;
		}
		}
		if (!model) {
			status = std::format("{} : {}", entry.id, model.Error());
			return false;
		}
		Doc d{entry, std::move(model.Value()), def, false, {}};
		d.model.id = entry.id;
		// Connectivité : une partie par face connectée (éditée comme les autres).
		if (const dataio::NodePtr con = def->Get("connects"); con && con->IsObject() && entry.format == voxel::ModelFormat::Nodebox) {
			for (const dataio::NodePtr &t : (*con)["to"]->AsArray())
				d.connectTo += (d.connectTo.empty() ? "" : ", ") + std::string(t->AsString());
			for (const char *face : kConnectFaces)
				if (const dataio::NodePtr boxes = con->Get(face)) {
					voxel::ModelPart connectPart;
					connectPart.name = std::string("connect_") + face;
					connectPart.boxes = voxel::ParseBoxList(boxes);
					d.model.parts.push_back(std::move(connectPart));
				}
		}
		if (entry.kind == "Bloc")
			d.model.collision = voxel::ParseCollision(def, d.model);
		doc = std::move(d);
		undo.clear();
		redo.clear();
		editOpen = false;
		part = doc->model.parts.empty() ? -1 : 0;
		box = -1;
		collision = -1;
		anim = doc->model.animations.empty() ? -1 : 0;
		time = 0;
		playing = false;
		// Texture : skin de créature (rig), image voisine du maillage, texture du bloc.
		std::optional<fs::path> tex;
		if (entry.format == voxel::ModelFormat::Rig && cat) {
			for (const auto &[modelId, skins] : cat->skins)
				if (modelId == entry.id && !skins.empty())
					if ((tex = ResolveTexturePath(skins.front())))
						break;
			if (!tex)
				if (const SkinDef *skin = app.content.FindSkin(app.settings.playerSkin))
					tex = app.content.SkinTexturePath(skin->texture);
		}
		if (!tex && !entry.mesh.empty()) {
			fs::path p = entry.mesh;
			p.replace_extension(".png");
			if (fs::exists(p))
				tex = p;
		}
		if (!tex)
			tex = ResolveTexturePath(entry.texture);
		image.reset();
		blockAtlas.reset();
		// Bloc nodebox à plusieurs textures de faces : on réunit les six faces en **une**
		// texture UV (atlas 3×2) pour uniformiser l'édition (peinture + UV).
		bool varied = false;
		for (int f = 1; f < 6 && !varied; ++f)
			varied = !entry.textures[size_t(f)].empty() && entry.textures[size_t(f)] != entry.textures[0];
		if (entry.kind == "Bloc" && entry.format == voxel::ModelFormat::Nodebox && varied) {
			FaceAtlas fa;
			image = Image{{}, {}, BuildFaceAtlas(app, entry.textures, fa.cells), {}, true, false, {}, {}};
			blockAtlas = fa;
		} else if (tex) {
			if (auto img = sdl3::LoadImageRgba(*tex)) // skin 64×64 d'un rig 64×32 : cf. FrameScale()
				image = Image{*tex, entry.format == voxel::ModelFormat::Rig ? std::string() : entry.texture, std::move(*img), {}, true, false, {}, {}};
		}
		Frame();
		status = std::format("ouvert : {} ({}, {} partie(s), {} cube(s), {} triangle(s))", entry.id, FormatLabel(doc->model.format), doc->model.parts.size(),
							 doc->model.BoxCount(), doc->model.TriangleCount());
		return true;
	}

	// ── Enregistrement ──
	bool SaveDoc() {
		if (!doc)
			return false;
		Doc &d = *doc;
		const voxel::ModelFormat f = d.model.format;
		std::string text = ReadText(d.entry.file);
		dataio::NodePtr def = d.original;
		bool writeJson = true;
		if (f == voxel::ModelFormat::Rig) {
			def = voxel::RigToJson(d.model, d.original);
		} else if (f == voxel::ModelFormat::Boxes) {
			if (dataio::NodePtr data = def->Get("data"); data && data->IsObject())
				data->Set("boxes", voxel::BoxListToJson(d.model));
			else
				def->Set("boxes", voxel::BoxListToJson(d.model));
			if (!d.model.animations.empty())
				def->Set("animations", voxel::AnimationsToJson(d.model.animations));
		} else if (f == voxel::ModelFormat::Nodebox || f == voxel::ModelFormat::Cube) {
			// Forme de base (parties hors « connect_* ») et connectivité par face.
			voxel::Model base = d.model;
			std::erase_if(base.parts, [](const voxel::ModelPart &p) { return p.name.starts_with("connect_"); });
			dataio::NodePtr list = voxel::BoxListToJson(base);
			const bool hasConnect = !d.connectTo.empty() || base.parts.size() != d.model.parts.size();
			if (hasConnect) {
				dataio::NodePtr con = dataio::Node::MakeObject();
				dataio::NodePtr to = dataio::Node::MakeArray();
				for (const auto &t : String(d.connectTo).Split(','))
					if (const std::string v(String(t).Trim()); !v.empty())
						to->Push(dataio::Node::MakeString(v));
				con->Set("to", std::move(to));
				for (const voxel::ModelPart &p : d.model.parts)
					if (p.name.starts_with("connect_")) {
						std::vector<voxel::ModelBox> boxes = p.boxes;
						con->Set(p.name.substr(8), voxel::BoxListToJson(boxes));
					}
				def->Set("connects", std::move(con));
			}
			if (dataio::NodePtr nb = def->Get("nodebox"); nb && nb->IsObject())
				nb->Set("fixed", std::move(list));
			else
				def->Set("nodebox", std::move(list));
			if (f == voxel::ModelFormat::Cube)
				def->Set("type", dataio::Node::MakeString("nodebox"));
		} else if (f == voxel::ModelFormat::Obj) {
			if (!WriteText(d.entry.mesh, voxel::ObjToText(d.model))) {
				status = std::format("échec d'écriture : {}", d.entry.mesh.string());
				return false;
			}
		} else if (f == voxel::ModelFormat::B3d || f == voxel::ModelFormat::Cross) {
			writeJson = d.entry.kind == "Bloc"; // la forme elle-même n'est pas modifiable dans ce format
		}
		// Collision d'un bloc (AABB).
		if (d.entry.kind == "Bloc" && def->IsObject()) {
			const dataio::NodePtr &col = (*(*def)["physics"])["collision"];
			// « FROM_NODEBOX » reste tel quel tant que les boîtes suivent la forme ; une
			// collision modifiée devient « FIXED ».
			bool followsShape = col->IsObject() && (*col)["shape"]->AsString() == "FROM_NODEBOX";
			if (followsShape) {
				const std::vector<voxel::ModelBox> shape = voxel::ParseCollision(def, d.model);
				followsShape = shape.size() == d.model.collision.size() &&
							   std::ranges::equal(shape, d.model.collision, [](const voxel::ModelBox &a, const voxel::ModelBox &b) {
								   return (a.from - b.from).LengthSq() < 1e-8f && (a.size - b.size).LengthSq() < 1e-8f;
							   });
			}
			if (!d.model.collision.empty() && !followsShape) {
				dataio::NodePtr physics = (*def)["physics"]->IsObject() ? (*def)["physics"] : dataio::Node::MakeObject();
				dataio::NodePtr c = dataio::Node::MakeObject();
				c->Set("shape", dataio::Node::MakeString("FIXED"));
				c->Set("fixed", voxel::BoxListToJson(d.model.collision));
				physics->Set("collision", std::move(c));
				def->Set("physics", std::move(physics));
			}
		}
		if (writeJson) {
			std::optional<std::string> out;
			if (d.entry.index == SIZE_MAX)
				out = WriteRootJson(def);
			else
				out = dataio::ReplaceJsonArrayElement(text, d.entry.index, def);
			if (!out || !WriteText(d.entry.file, *out)) {
				status = std::format("échec d'écriture : {}", d.entry.file.string());
				return false;
			}
			d.original = def;
		}
		d.dirty = false;
		status = std::format("enregistré : {} → {}", d.entry.id,
							 f == voxel::ModelFormat::Obj ? d.entry.mesh.filename().string() : d.entry.file.filename().string());
		if (f == voxel::ModelFormat::B3d)
			status += " (forme .b3d en lecture seule : « Exporter .obj » pour la modifier)";
		return true;
	}

	/// Copie animable du modèle (rig) ajoutée à `<mod>/models/studio_rigs.json`.
	bool SaveAsRig() {
		if (!doc)
			return false;
		const voxel::ModInfo *mod = app.content.mods.Find(doc->entry.mod);
		if (!mod)
			return false;
		voxel::Model rig = voxel::ToRig(doc->model);
		std::string name = doc->entry.id.substr(doc->entry.id.find(':') + 1) + "_rig";
		rig.id = name;
		const fs::path file = mod->directory / "models" / "studio_rigs.json";
		std::error_code ec;
		fs::create_directories(file.parent_path(), ec);
		std::string text = fs::exists(file) ? ReadText(file) : std::string("// Rigs créés par Voxelia Studio (conversion de formes de blocs et de maillages).\n[\n]\n");
		dataio::NodePtr def = voxel::RigToJson(rig, dataio::Node::MakeObject());
		def->Set("id", dataio::Node::MakeString(name));
		// Remplace un rig du même nom, sinon ajoute.
		std::optional<std::string> out;
		if (auto parsed = dataio::ParseJson(text); parsed && parsed.Value()->IsArray())
			for (size_t i = 0; i < parsed.Value()->Size(); ++i)
				if ((*(*parsed.Value())[i])["id"]->AsString() == name)
					out = dataio::ReplaceJsonArrayElement(text, i, def);
		if (!out)
			out = dataio::AppendJsonArrayElement(text, def);
		if (!out || !WriteText(file, *out)) {
			status = "échec d'écriture de " + file.string();
			return false;
		}
		// Le document devient ce rig (animable).
		ModelEntry e = doc->entry;
		e.id = QualifyName(name, doc->entry.mod);
		e.kind = "Modèle";
		e.format = voxel::ModelFormat::Rig;
		e.file = file;
		e.mesh.clear();
		e.refModel.clear();
		if (auto parsed = dataio::ParseJson(*out))
			for (size_t i = 0; i < parsed.Value()->Size(); ++i)
				if ((*(*parsed.Value())[i])["id"]->AsString() == name)
					e.index = i;
		rig.id = e.id;
		doc = Doc{e, std::move(rig), def, false, {}};
		catalogRequested = false; // le catalogue relira le nouveau fichier
		status = std::format("rig créé : {} ({})", e.id, file.string());
		return true;
	}
};

// ══════════════════════════════════════════════════════════════════════════
// Cycle de vie
// ══════════════════════════════════════════════════════════════════════════

ModelWorkshop::ModelWorkshop(App &app) : m_app(app), m_impl(std::make_unique<Impl>(app)) {}
ModelWorkshop::~ModelWorkshop() = default;

bool ModelWorkshop::HasDocument() const noexcept { return m_impl->doc.has_value(); }
void ModelWorkshop::ReloadCatalog() { m_impl->catalogRequested = false; }
bool ModelWorkshop::HasImage() const noexcept { return m_impl->image.has_value(); }
std::string ModelWorkshop::TakeStatus() { return std::exchange(m_impl->status, {}); }
void ModelWorkshop::Save() {
	Impl &s = *m_impl;
	if (s.doc && s.doc->dirty)
		s.SaveDoc();
	if (s.image && s.image->dirty && s.SaveImage() && s.status.empty())
		s.status = "texture enregistrée : " + s.image->path.string();
	else if (s.image && !s.image->dirty && s.doc && !s.doc->dirty && s.status.empty())
		s.status = "rien à enregistrer";
}
void ModelWorkshop::Undo() {
	Impl &s = *m_impl;
	if (s.mode == Impl::Mode::Paint && s.image && !s.image->undo.empty()) {
		s.image->redo.push_back(std::move(s.image->pixels));
		s.image->pixels = std::move(s.image->undo.back());
		s.image->undo.pop_back();
		s.ImageChanged();
		return;
	}
	if (s.doc && !s.undo.empty()) {
		s.redo.push_back(std::move(s.doc->model));
		s.doc->model = std::move(s.undo.back());
		s.undo.pop_back();
		s.doc->dirty = true;
		s.ClampSelection();
	}
}
void ModelWorkshop::Redo() {
	Impl &s = *m_impl;
	if (s.mode == Impl::Mode::Paint && s.image && !s.image->redo.empty()) {
		s.image->undo.push_back(std::move(s.image->pixels));
		s.image->pixels = std::move(s.image->redo.back());
		s.image->redo.pop_back();
		s.ImageChanged();
		return;
	}
	if (s.doc && !s.redo.empty()) {
		s.undo.push_back(std::move(s.doc->model));
		s.doc->model = std::move(s.redo.back());
		s.redo.pop_back();
		s.doc->dirty = true;
		s.ClampSelection();
	}
}

bool ModelWorkshop::OpenImage(const fs::path &png, std::string textureName) {
	auto img = sdl3::LoadImageRgba(png);
	if (!img)
		return false;
	m_impl->image = Impl::Image{png, std::move(textureName), std::move(*img), {}, true, false, {}, {}};
	m_impl->paintZoom = 0;
	m_impl->mode = Impl::Mode::Paint;
	return true;
}

bool ModelWorkshop::Open(std::string_view id) {
	Impl &s = *m_impl;
	const ModelCatalog *cat = s.catalog.Get();
	if (!cat)
		return false;
	for (const ModelEntry &e : cat->entries)
		if (e.id == id)
			return s.OpenEntry(e, cat);
	return false;
}

void ModelWorkshop::NewModel(int preset) {
	Impl &s = *m_impl;
	const voxel::ModInfo *mod = m_app.content.mods.Find("core");
	if (!mod) {
		s.status = "mod « core » introuvable";
		return;
	}
	const int p = std::clamp(preset, 0, 3);
	const fs::path file = mod->directory / "models" / "studio_rigs.json";
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::string text = fs::exists(file) ? ReadText(file) : std::string("[\n]\n");
	// Nom unique dérivé du gabarit.
	std::string name;
	for (int i = 1;; ++i) {
		name = i == 1 ? std::string(kPresetLabels[p]) : std::format("{}{}", kPresetLabels[p], i);
		bool taken = false;
		if (auto parsed = dataio::ParseJson(text); parsed && parsed.Value()->IsArray())
			for (size_t k = 0; k < parsed.Value()->Size(); ++k)
				if ((*(*parsed.Value())[k])["id"]->AsString() == name)
					taken = true;
		if (!taken)
			break;
	}
	voxel::Model rig = MakeRigPreset(p);
	rig.id = QualifyName(name, "core");
	dataio::NodePtr def = voxel::RigToJson(rig, dataio::Node::MakeObject());
	def->Set("id", dataio::Node::MakeString(name));
	std::optional<std::string> out;
	if (auto parsed = dataio::ParseJson(text); parsed && parsed.Value()->IsArray())
		for (size_t i = 0; i < parsed.Value()->Size(); ++i)
			if ((*(*parsed.Value())[i])["id"]->AsString() == name)
				out = dataio::ReplaceJsonArrayElement(text, i, def);
	if (!out)
		out = dataio::AppendJsonArrayElement(text, def);
	if (!out || !WriteText(file, *out)) {
		s.status = "échec d'écriture de " + file.string();
		return;
	}
	ModelEntry e;
	e.id = rig.id;
	e.mod = "core";
	e.kind = "Modèle";
	e.format = voxel::ModelFormat::Rig;
	e.file = file;
	if (auto parsed = dataio::ParseJson(*out))
		for (size_t i = 0; i < parsed.Value()->Size(); ++i)
			if ((*(*parsed.Value())[i])["id"]->AsString() == name)
				e.index = i;
	s.doc = Impl::Doc{e, std::move(rig), def, false, {}};
	s.undo.clear();
	s.redo.clear();
	s.editOpen = false;
	s.part = s.doc->model.parts.empty() ? -1 : 0;
	s.box = s.part >= 0 && !s.doc->model.parts[size_t(s.part)].boxes.empty() ? 0 : -1;
	s.collision = -1;
	s.anim = s.doc->model.animations.empty() ? -1 : 0;
	s.time = 0;
	s.playing = false;
	s.mode = Impl::Mode::Edit;
	s.image.reset();
	s.Frame();
	s.catalogRequested = false;
	s.status = std::format("modèle générique créé : {} ({} partie(s), {} cube(s), {} animation(s))", e.id, s.doc->model.parts.size(),
						  s.doc->model.BoxCount(), s.doc->model.animations.size());
}

bool ModelWorkshop::ImportTexture(const fs::path &png) {
	Impl &s = *m_impl;
	auto img = sdl3::LoadImageRgba(png);
	if (!img) {
		s.status = "échec de lecture de " + png.string();
		return false;
	}
	s.image = Impl::Image{png, {}, std::move(*img), {}, true, false, {}, {}};
	s.paintZoom = 0;
	if (s.doc) {
		s.Changed();
		voxel::Model &m = s.doc->model;
		m.boxUv = true;
		m.textureWidth = s.image->pixels.GetWidth();
		m.textureHeight = s.image->pixels.GetHeight();
		voxel::AutoBoxUv(m, 2); // cadrillage UV calculé automatiquement
		s.status = std::format("texture associée au modèle ({}×{}) et cadrillage UV automatique", s.image->pixels.GetWidth(), s.image->pixels.GetHeight());
	} else {
		s.status = "texture importée : " + png.filename().string();
	}
	s.mode = Impl::Mode::Paint;
	return true;
}

bool ModelWorkshop::CreateBlock() {
	Impl &s = *m_impl;
	if (!s.doc) {
		s.status = "ouvrez d'abord un modèle pour en faire un bloc";
		return false;
	}
	const voxel::ModInfo *mod = m_app.content.mods.Find(s.doc->entry.mod);
	if (!mod) {
		s.status = "mod introuvable : " + s.doc->entry.mod;
		return false;
	}
	// Nom et texture (par défaut : dérivés du modèle).
	const std::string stem = String(s.doc->entry.id.substr(s.doc->entry.id.find(':') + 1)).ToLower();
	const std::string name = s.blockName.empty() ? stem + "_block" : s.blockName;
	const std::string texture = s.blockTexture.empty() ? s.doc->entry.id : s.blockTexture;
	const fs::path file = mod->directory / "blocks" / "studio_blocks.json";
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	std::string text = fs::exists(file) ? ReadText(file)
										: std::string("// Blocs créés par Voxelia Studio (variantes de modèles génériques).\n[\n]\n");
	// Définition du bloc : nodebox aplatie + texture + mécanique (interact.toggle).
	dataio::NodePtr def = dataio::Node::MakeObject();
	def->Set("id", dataio::Node::MakeString(name));
	def->Set("displayName", dataio::Node::MakeString(s.doc->entry.id.substr(s.doc->entry.id.find(':') + 1)));
	def->Set("type", dataio::Node::MakeString("nodebox"));
	def->Set("nodebox", ModelToNodeboxList(s.doc->model));
	dataio::NodePtr gfx = dataio::Node::MakeObject();
	gfx->Set("texture", dataio::Node::MakeString(texture));
	def->Set("graphics", std::move(gfx));
	if (s.doc->model.mechanic && s.doc->model.mechanic->toggle) {
		dataio::NodePtr in = dataio::Node::MakeObject();
		in->Set("toggle", dataio::Node::MakeString(name + "_on"));
		def->Set("interact", std::move(in));
	}
	// Remplace une variante du même nom, sinon ajoute.
	std::optional<std::string> out;
	if (auto parsed = dataio::ParseJson(text); parsed && parsed.Value()->IsArray())
		for (size_t i = 0; i < parsed.Value()->Size(); ++i)
			if ((*(*parsed.Value())[i])["id"]->AsString() == name)
				out = dataio::ReplaceJsonArrayElement(text, i, def);
	if (!out)
		out = dataio::AppendJsonArrayElement(text, def);
	if (!out || !WriteText(file, *out)) {
		s.status = "échec d'écriture de " + file.string();
		return false;
	}
	s.status = std::format("variante de bloc créée : « {} » (texture « {} ») dans {}", name, texture, file.filename().string());
	return true;
}

namespace {
void KeyCurrent(ModelWorkshop::Impl &s, const voxel::PartOffset &off, bool rotation, bool position);
}

void ModelWorkshop::Update(jobs::ThreadPool &worker, ui::Context &u, bool keyboardBusy, float dt) {
	Impl &s = *m_impl;
	if (!s.catalogRequested) {
		s.catalogRequested = true;
		std::vector<std::pair<std::string, fs::path>> mods;
		for (const voxel::ModInfo &m : m_app.content.mods.Mods())
			mods.emplace_back(m.id, m.directory);
		s.catalog.Request(worker, [mods = std::move(mods)](std::stop_token) { return ScanModels(mods); });
	}
	s.catalog.Poll();
	if (s.pendingImport) {
		const fs::path png = std::move(*s.pendingImport);
		s.pendingImport.reset();
		ImportTexture(png);
	}
	if (s.playing)
		if (const voxel::Animation *a = s.Anim()) {
			s.time += dt;
			if (s.time > a->length)
				s.time = a->loop ? std::fmod(s.time, a->length) : a->length;
		}
	if (s.motion)
		s.motionTime += dt;
	// Un geste d'édition se termine au relâchement (et hors saisie au clavier) ; l'état
	// d'avant est photographié dès qu'une action peut commencer (clic, touche).
	if (s.editOpen && !u.MouseDown(sdl3::MouseButton::Left) && !keyboardBusy)
		s.editOpen = false;
	const bool action = u.MousePressed(sdl3::MouseButton::Left) || u.MouseReleased(sdl3::MouseButton::Left) || u.MousePressed(sdl3::MouseButton::Right) || !u.Events().PressedKeycodes().empty() || !u.TextInput().empty();
	if (s.doc && !s.editOpen && action)
		s.shadow = s.doc->model;
}

bool ModelWorkshop::TakeTexturesChanged() { return std::exchange(m_impl->texturesChanged, false); }

// ══════════════════════════════════════════════════════════════════════════
// Opérations d'édition
// ══════════════════════════════════════════════════════════════════════════

namespace {

using Impl = ModelWorkshop::Impl;

void AddCube(Impl &s) {
	if (!s.doc)
		return;
	voxel::Model &m = s.doc->model;
	s.Changed();
	if (m.parts.empty() || s.part < 0) {
		m.parts.push_back({});
		m.parts.back().name = std::format("groupe{}", m.parts.size());
		s.part = int(m.parts.size()) - 1;
	}
	voxel::ModelPart &p = m.parts[size_t(s.part)];
	voxel::ModelBox b;
	const float u = m.boxUv ? 4.f : m.unitsPerBlock * 0.25f;
	b.size = {u, u, u};
	b.from = m.boxUv ? FVector3{-u * 0.5f, 0, -u * 0.5f} : FVector3{-u * 0.5f, -u * 0.5f, -u * 0.5f};
	if (const voxel::ModelBox *sel = s.Box()) // à côté du cube sélectionné
		b.from = sel->from + FVector3{sel->size.x, 0, 0};
	p.boxes.push_back(b);
	s.box = int(p.boxes.size()) - 1;
	s.collision = -1;
}

/// Extrude la face `face` (ordre BoxFace) du cube sélectionné : ajoute un nouveau cube
/// adjacent (épaisseur = 1/4 de bloc), recouvrant toute la section de la face.
void ExtrudeFace(Impl &s, int face, float thickness = -1.f) {
	voxel::ModelBox *sel = s.Box();
	if (!s.doc || !sel)
		return;
	s.Changed();
	voxel::Model &m = s.doc->model;
	voxel::ModelBox b = *sel;
	const float t = thickness > 0 ? thickness : 4.f * s.Snap(); // épaisseur d'extrusion
	switch (face) {
	case 0: b.from.x = sel->from.x + sel->size.x; b.size.x = t; break; // +X
	case 1: b.from.x = sel->from.x - t;			b.size.x = t; break; // -X
	case 2: b.from.y = sel->from.y + sel->size.y; b.size.y = t; break; // +Y
	case 3: b.from.y = sel->from.y - t;			b.size.y = t; break; // -Y
	case 4: b.from.z = sel->from.z + sel->size.z; b.size.z = t; break; // +Z
	case 5: b.from.z = sel->from.z - t;			b.size.z = t; break; // -Z
	default: return;
	}
	m.parts[size_t(s.part)].boxes.push_back(b);
	s.box = int(m.parts[size_t(s.part)].boxes.size()) - 1;
}

/// Push/pull (Sketchup) sur la face `face` du cube sélectionné : `dist` > 0 tire
/// (extrusion d'un nouveau cube), `dist` < 0 pousse (rétrécit la boîte le long de la normale).
void PushPullFace(Impl &s, int face, float dist) {
	voxel::ModelBox *sel = s.Box();
	if (!s.doc || !sel || std::fabs(dist) < 1e-4f)
		return;
	if (dist > 0.f) {
		ExtrudeFace(s, face, std::max(s.Snap(), dist));
		return;
	}
	s.Changed();
	const float step = s.Snap();
	const int axis = face / 2;
	const bool positive = (face % 2) == 0;
	float &sz = axis == 0 ? sel->size.x : axis == 1 ? sel->size.y : sel->size.z;
	float &fr = axis == 0 ? sel->from.x : axis == 1 ? sel->from.y : sel->from.z;
	const float d = std::min(-dist, sz - step); // ne jamais inverser la boîte
	if (d <= 0.f)
		return;
	sz -= d;
	if (!positive)
		fr += d; // face min : on déplace aussi le coin min
}

/// Reconstruit le maillage sous-élément (sommets + arêtes) de la partie sélectionnée,
/// transformé par la pose, pour la sélection et le survol.
void BuildSubMesh(Impl &s, const voxel::Model &m, const std::vector<voxel::PartOffset> &pose) {
	s.meshVerts.clear();
	s.meshEdges.clear();
	s.meshEdgeSub.clear();
	if (!s.doc || s.part < 0 || size_t(s.part) >= m.parts.size())
		return;
	const voxel::ModelPart &part = m.parts[size_t(s.part)];
	const voxel::PartOffset off = size_t(s.part) < pose.size() ? pose[size_t(s.part)] : voxel::PartOffset{};
	const math::FVector3 rot = part.rotation + off.rotation;
	const float inv = 1.f / m.unitsPerBlock;
	auto xf = [&](const math::FVector3 &p) { return (voxel::RotateEuler(p - part.pivot, rot) + part.pivot + off.position) * inv; };
	static constexpr std::array<std::array<int, 2>, 12> kEdges = {{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
	for (size_t bi = 0; bi < part.boxes.size(); ++bi) {
		const voxel::ModelBox &b = part.boxes[bi];
		if (!b.visible)
			continue;
		const math::FVector3 lo = b.from, hi = b.from + b.size;
		const math::FVector3 corners[8] = {
			{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
			{lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
		};
		const int base = int(s.meshVerts.size());
		for (int c = 0; c < 8; ++c)
			s.meshVerts.push_back({xf(corners[c]), int(bi), uint8_t(c)});
		for (uint8_t e = 0; e < 12; ++e) {
			s.meshEdges.push_back({base + kEdges[e][0], base + kEdges[e][1]});
			s.meshEdgeSub.push_back(e);
		}
	}
}

/// Index du sommet projeté le plus proche du curseur (dans `meshVerts2d`), -1 si aucun.
int NearestVertex(const Impl &s, float mx, float my) {
	int best = -1;
	float bestD = 12.f * 12.f;
	for (size_t i = 0; i < s.meshVerts2d.size(); ++i) {
		const math::FVector2 &p = s.meshVerts2d[i];
		if (p.x > 1e8f)
			continue;
		const float d = (p.x - mx) * (p.x - mx) + (p.y - my) * (p.y - my);
		if (d < bestD) {
			bestD = d;
			best = int(i);
		}
	}
	return best;
}

/// Index de l'arête projetée la plus proche du curseur, -1 si aucune.
int NearestEdge(const Impl &s, float mx, float my) {
	int best = -1;
	float bestD = 8.f * 8.f;
	for (size_t i = 0; i < s.meshEdges2d.size(); ++i) {
		const math::FVector2 &a = s.meshEdges2d[i][0], &b = s.meshEdges2d[i][1];
		if (a.x > 1e8f || b.x > 1e8f)
			continue;
		const float d = DistToSegment(mx, my, a.x, a.y, b.x, b.y);
		if (d < bestD) {
			bestD = d;
			best = int(i);
		}
	}
	return best;
}

/// Bascule (ou ajoute) une référence de sous-élément dans la sélection.
void ToggleSel(std::vector<Impl::SubRef> &sel, const Impl::SubRef &ref, bool add) {
	const auto it = std::find(sel.begin(), sel.end(), ref);
	if (it != sel.end()) {
		if (!add)
			sel.erase(it); // Maj non tenu : re-clic → désélectionne
		return;
	}
	if (add)
		sel.push_back(ref);
	else
		sel = {ref}; // sélection unique
}

// Déclaration avancée : défini plus bas (après la projection).
bool HitTest(const Impl &s, float mx, float my, size_t &quad, FVector2 &uv);

/// Prépare un push/pull sur la face `q` (BoxFace) : mémorise la normale, le centre et
/// l'échelle écran pour convertir le glissement en distance monde.
void BeginPushPull(Impl &s, const voxel::ModelQuad &q, const ViewCam &cam) {
	s.ppFace = q.face;
	s.ppBox = q.box;
	s.ppNormal = q.normal;
	s.ppCenter = (q.p[0] + q.p[2]) * 0.5f;
	s.ppStart = s.doc->model;
	float x0, y0, x1, y1, d;
	if (cam.Project(s.ppCenter, x0, y0, d) && cam.Project(s.ppCenter + q.normal * 0.1f, x1, y1, d)) {
		const float dx = x1 - x0, dy = y1 - y0;
		const float len = std::sqrt(dx * dx + dy * dy);
		s.ppScreenDir = len > 1e-4f ? math::FVector2{dx / len, dy / len} : math::FVector2{};
		s.ppScreenLen = len;
	} else {
		s.ppScreenDir = {};
		s.ppScreenLen = 0.f;
	}
}

/// Applique le push/pull selon la position écran courante (glissement projeté sur la normale).
void ApplyPushPull(Impl &s, float mx, float my) {
	if (s.ppFace < 0 || s.ppScreenLen < 1e-4f)
		return;
	const float px = (mx - s.pressX) * s.ppScreenDir.x + (my - s.pressY) * s.ppScreenDir.y; // px le long de la normale
	const float world = px / s.ppScreenLen * 0.1f;											 // unités monde (blocs)
	// Restaure l'état initial puis applique la nouvelle distance (annule le geste précédent).
	if (s.doc) {
		s.doc->model = s.ppStart;
		s.box = s.ppBox; // l'extrusion précédente a pu déplacer la sélection
	}
	if (world > 0.f)
		ExtrudeFace(s, s.ppFace, std::max(s.Snap(), world));
	else
		PushPullFace(s, s.ppFace, world);
}

/// Sélection au clic : sommet / arête / face / objet, selon le mode de sélection.
void ClickSelect(Impl &s, float mx, float my, bool add) {
	if (s.selMode == Impl::SelMode::Vertex) {
		if (const int vi = NearestVertex(s, mx, my); vi >= 0) {
			const Impl::MeshVert &v = s.meshVerts[size_t(vi)];
			ToggleSel(s.selVerts, {v.box, v.corner}, add);
			s.box = v.box;
		} else if (!add) {
			s.selVerts.clear();
			s.box = -1;
		}
		return;
	}
	if (s.selMode == Impl::SelMode::Edge) {
		if (const int ei = NearestEdge(s, mx, my); ei >= 0) {
			const Impl::MeshVert &v = s.meshVerts[size_t(s.meshEdges[size_t(ei)][0])];
			ToggleSel(s.selEdges, {v.box, s.meshEdgeSub[size_t(ei)]}, add);
			s.box = v.box;
		} else if (!add) {
			s.selEdges.clear();
			s.box = -1;
		}
		return;
	}
	size_t hitQuad = 0;
	FVector2 hitUv{};
	if (HitTest(s, mx, my, hitQuad, hitUv)) {
		const voxel::ModelQuad &q = s.quads[hitQuad];
		if (s.extruding && q.box >= 0 && q.part == s.part) {
			ExtrudeFace(s, q.face);
			s.extruding = false;
		} else if (s.selMode == Impl::SelMode::Face && q.box >= 0 && q.part == s.part) {
			ToggleSel(s.selFaces, {q.box, q.face}, add);
			s.part = q.part;
			s.box = q.box;
			s.collision = -1;
		} else {
			s.part = q.part;
			s.box = q.box;
			s.collision = -1;
		}
	} else {
		s.box = -1;
		s.collision = -1;
		if (!add) {
			s.selVerts.clear();
			s.selEdges.clear();
			s.selFaces.clear();
		}
	}
}

void AddPart(Impl &s) {
	if (!s.doc)
		return;
	s.Changed();
	voxel::Model &m = s.doc->model;
	std::string name;
	for (int i = 1;; ++i) {
		name = std::format("groupe{}", i);
		if (m.FindPart(name) < 0)
			break;
	}
	voxel::ModelPart p;
	p.name = name;
	if (const voxel::ModelPart *sel = s.Part())
		p.pivot = sel->pivot;
	m.parts.push_back(std::move(p));
	s.part = int(m.parts.size()) - 1;
	s.box = -1;
}

void DuplicateSelection(Impl &s) {
	if (!s.doc)
		return;
	voxel::Model &m = s.doc->model;
	if (s.collision >= 0) {
		s.Changed();
		voxel::ModelBox b = m.collision[size_t(s.collision)];
		b.from.x += b.size.x;
		m.collision.push_back(b);
		s.collision = int(m.collision.size()) - 1;
	} else if (voxel::ModelBox *b = s.Box()) {
		s.Changed();
		voxel::ModelBox copy = *b;
		copy.from.x += copy.size.x;
		m.parts[size_t(s.part)].boxes.push_back(copy);
		s.box = int(m.parts[size_t(s.part)].boxes.size()) - 1;
	} else if (voxel::ModelPart *p = s.Part()) {
		s.Changed();
		voxel::ModelPart copy = *p;
		copy.name += "_copie";
		m.parts.push_back(std::move(copy));
		s.part = int(m.parts.size()) - 1;
	}
}

void DeleteSelection(Impl &s) {
	if (!s.doc)
		return;
	voxel::Model &m = s.doc->model;
	if (s.collision >= 0) {
		s.Changed();
		m.collision.erase(m.collision.begin() + s.collision);
		s.collision = -1;
	} else if (s.Box()) {
		s.Changed();
		auto &boxes = m.parts[size_t(s.part)].boxes;
		boxes.erase(boxes.begin() + s.box);
		s.box = std::min(s.box, int(boxes.size()) - 1);
	} else if (voxel::ModelPart *p = s.Part()) {
		s.Changed();
		for (voxel::Animation &a : m.animations)
			std::erase_if(a.tracks, [&](const voxel::AnimationTrack &t) { return t.part == p->name; });
		m.parts.erase(m.parts.begin() + s.part);
		s.part = std::min(s.part, int(m.parts.size()) - 1);
		s.box = -1;
	}
}

/// Pose courante (animation + mouvements) de la partie `i`.
voxel::PartOffset CurrentOffset(Impl &s, int i) {
	if (!s.doc || i < 0)
		return {};
	const auto pose = voxel::SamplePose(s.doc->model, s.Anim(), s.time, -1.f);
	return size_t(i) < pose.size() ? pose[size_t(i)] : voxel::PartOffset{};
}

/// Clé à l'instant courant pour la partie sélectionnée (crée l'animation au besoin).
void KeyCurrent(Impl &s, const voxel::PartOffset &off, bool rotation, bool position) {
	voxel::ModelPart *p = s.Part();
	if (!s.doc || !p)
		return;
	s.Changed();
	if (!s.Anim()) {
		s.doc->model.animations.push_back({"animation", 1.f, true, {}});
		s.anim = int(s.doc->model.animations.size()) - 1;
	}
	voxel::AnimationTrack *t = s.Anim()->Track(p->name, true);
	if (rotation)
		voxel::SetKey(t->rotation, s.time, off.rotation);
	if (position)
		voxel::SetKey(t->position, s.time, off.position);
}

void Hint(ui::Context &u, const Rect &r, std::string_view text) {
	u.BeginColumn(r.Inset(16, 20));
	u.Wrapped(text, u.Style().textDim, 13);
	u.EndColumn();
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// Catalogue
// ══════════════════════════════════════════════════════════════════════════

void ModelWorkshop::DrawCatalog(ui::Context &u, const Rect &r) {
	Impl &s = *m_impl;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	if (u.MouseIn(r) && u.MousePressed(sdl3::MouseButton::Left))
		m_focused = true;
	u.PushId("modelcat");
	u.TextInputAt({r.x + 8, r.y + 8, r.w - 16, 28}, "##search", s.search, nullptr, "Rechercher un modèle…");
	static constexpr std::string_view kFormats[] = {"Tous les formats", "nodebox", "rig", "obj", "b3d", "json", "croix"};
	u.Choice({r.x + 8, r.y + 42, r.w - 16, 26}, "##fmt", s.formatFilter, kFormats);
	// Création d'un modèle générique (gabarit) — enregistré dans core/models/studio_rigs.json.
	const Rect newRow{r.x + 8, r.y + 72, r.w - 16, 26};
	const float nw = (newRow.w - 3.f * 4.f) / 4.f;
	for (int i = 0; i < 4; ++i) {
		const Rect br{newRow.x + float(i) * (nw + 4.f), newRow.y, nw, newRow.h};
		if (u.ButtonAt(br, std::format("{}##preset{}", kPresetLabels[i], i), true, false))
			NewModel(i);
	}
	const ModelCatalog *cat = s.catalog.Get();
	const Rect list{r.x + 4, r.y + 106, r.w - 8, r.h - 110};
	if (!cat) {
		u.BeginColumn(list);
		u.Loader("Lecture des modèles des mods…");
		u.EndColumn();
		u.PopId();
		return;
	}
	const std::string needle = String(s.search).ToLower();
	std::vector<const ModelEntry *> shown;
	for (const ModelEntry &e : cat->entries) {
		if (s.formatFilter > 0) {
			static constexpr voxel::ModelFormat kF[] = {voxel::ModelFormat::Nodebox, voxel::ModelFormat::Rig, voxel::ModelFormat::Obj,
														voxel::ModelFormat::B3d, voxel::ModelFormat::Boxes, voxel::ModelFormat::Cross};
			if (e.format != kF[s.formatFilter - 1])
				continue;
		}
		if (!needle.empty() && String(e.id + " " + e.label).ToLower().find(needle) == std::string::npos)
			continue;
		shown.push_back(&e);
	}
	u.BeginScroll("list", list);
	u.Label(std::format("{} / {} formes · {} fichiers JSON lus", shown.size(), cat->entries.size(), cat->files), th.textDim, 12);
	std::string_view lastMod;
	for (const ModelEntry *e : shown) {
		if (e->mod != lastMod) {
			lastMod = e->mod;
			u.Space(4);
			const Rect h = u.Next(20);
			u.Icon({h.x, h.y, 18, h.h}, icons::Codepoint(icons::MaterialIcons::EXTENSION), th.accent, 14);
			c.Text(h.x + 22, h.y + (h.h - c.LineHeight(12, sdl3::CanvasFont::Bold)) * 0.5f, e->mod, th.text, 12, sdl3::CanvasFont::Bold);
		}
		u.PushId(e->id + "#" + e->kind);
		const Rect row = u.Next(24);
		const auto it = u.Interact(u.MakeId("##row"), row);
		const bool open = s.doc && s.doc->entry.id == e->id;
		if (open)
			c.FillRoundRect(row, 4, Rgba(36, 72, 40, 230));
		else if (it.hovered)
			c.FillRoundRect(row, 4, th.widgetHover);
		u.Icon({row.x + 6, row.y, 18, row.h}, e->kind == "Bloc" ? icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR) : icons::Codepoint(icons::MaterialIcons::CATEGORY), FormatColor(e->format), 15);
		const char *fmt = FormatLabel(e->format);
		const float fw = c.MeasureText(fmt, 11) + 10;
		u.TextIn({row.x + 28, row.y, row.w - 36 - fw, row.h}, e->label, {th.text, 13, sdl3::CanvasFont::Regular, ui::TextOverflow::MarqueeOnHover});
		c.Text(row.Right() - fw, row.y + (row.h - c.LineHeight(11)) * 0.5f, fmt, FormatColor(e->format), 11);
		if (it.hovered)
			u.Tooltip(std::format("{} · {} · {}", e->id, e->kind, e->file.filename().string()));
		if (it.leftClicked) {
			const uint64_t now = sdl3::GetTicksMS();
			if (s.doc && s.doc->dirty && !open && !(s.confirmId == e->id && now - s.confirmTicks < 4000)) {
				s.confirmId = e->id;
				s.confirmTicks = now;
				u.Notify("Modifications non enregistrées : cliquez à nouveau pour les abandonner", th.warning);
			} else if (!open) {
				s.OpenEntry(*e, cat);
			}
		}
		u.PopId();
	}
	u.EndScroll();
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Arborescence (parties, cubes, collision)
// ══════════════════════════════════════════════════════════════════════════

void ModelWorkshop::DrawParts(ui::Context &u, const Rect &r) {
	Impl &s = *m_impl;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	if (u.MouseIn(r) && u.MousePressed(sdl3::MouseButton::Left))
		m_focused = true;
	if (!s.doc)
		return Hint(u, r, "Ouvrez un modèle dans le catalogue « Modèles » : ses parties et ses cubes s'affichent ici.");
	voxel::Model &m = s.doc->model;
	u.PushId("parts");
	float bx = r.x + 6;
	auto tool = [&](char32_t icon, std::string_view tip, bool enabled) {
		const bool clicked = u.IconButton({bx, r.y + 5, 28, 28}, icon, tip, enabled);
		bx += 31;
		return clicked;
	};
	const bool boxes = !m.HasMeshes() || m.format == voxel::ModelFormat::Rig;
	if (tool(icons::Codepoint(icons::MaterialIcons::ADD_BOX), "Ajouter un cube", boxes))
		AddCube(s);
	if (tool(icons::Codepoint(icons::MaterialIcons::CREATE_NEW_FOLDER), "Ajouter un groupe (partie)", true))
		AddPart(s);
	if (tool(icons::Codepoint(icons::MaterialIcons::CONTENT_COPY), "Dupliquer la sélection (Ctrl+D)", s.part >= 0 || s.collision >= 0))
		DuplicateSelection(s);
	if (tool(icons::Codepoint(icons::MaterialIcons::DELETE_OUTLINE), "Supprimer la sélection (Suppr)", s.part >= 0 || s.collision >= 0))
		DeleteSelection(s);
	const std::string count = std::format("{} / {}", m.BoxCount(), m.parts.size());
	c.Text(r.Right() - 10 - c.MeasureText(count, 12), r.y + 12, count, th.textDim, 12);
	u.BeginScroll("tree", {r.x + 2, r.y + 38, r.w - 4, r.h - 40});
	for (size_t pi = 0; pi < m.parts.size(); ++pi) {
		voxel::ModelPart &p = m.parts[pi];
		u.PushId(uint64_t(pi) + 1);
		const Rect row = u.Next(24);
		const bool selPart = int(pi) == s.part && s.box < 0 && s.collision < 0;
		const auto it = u.Interact(u.MakeId("##part"), {row.x, row.y, row.w - 28, row.h});
		if (selPart)
			c.FillRoundRect(row, 4, Rgba(38, 92, 170, 200));
		else if (it.hovered)
			c.FillRoundRect(row, 4, th.widgetHover);
		u.Icon({row.x + 4, row.y, 18, row.h}, icons::Codepoint(icons::MaterialIcons::FOLDER), th.warning, 15);
		const std::string label = !p.indices.empty() ? std::format("{}  · {} tri.", p.name, p.indices.size() / 3)
													 : std::format("{}  · {}", p.name, p.boxes.size());
		u.TextIn({row.x + 26, row.y, row.w - 60, row.h}, label, {p.visible ? th.text : th.textDim, 13});
		if (u.IconButton({row.Right() - 26, row.y + 1, 22, 22}, p.visible ? icons::Codepoint(icons::MaterialIcons::VISIBILITY) : icons::Codepoint(icons::MaterialIcons::VISIBILITY_OFF),
						 p.visible ? "Masquer" : "Afficher", true, p.visible ? th.textDim : sdl3::WithAlpha(th.textDim, 120)))
			p.visible = !p.visible;
		if (it.leftClicked) {
			s.part = int(pi);
			s.box = -1;
			s.collision = -1;
		}
		for (size_t bi = 0; bi < p.boxes.size(); ++bi) {
			voxel::ModelBox &b = p.boxes[bi];
			u.PushId(uint64_t(bi) + 1000);
			const Rect br = u.Next(22);
			const bool sel = int(pi) == s.part && int(bi) == s.box;
			const auto bit = u.Interact(u.MakeId("##box"), {br.x, br.y, br.w - 28, br.h});
			if (sel)
				c.FillRoundRect(br, 4, Rgba(38, 92, 170, 200));
			else if (bit.hovered)
				c.FillRoundRect(br, 4, th.widgetHover);
			u.Icon({br.x + 22, br.y, 16, br.h}, icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), b.visible && p.visible ? Rgba(236, 180, 72) : th.textDim, 13);
			const std::string bl = std::format("cube {}  {:g}×{:g}×{:g}", bi + 1, b.size.x, b.size.y, b.size.z);
			u.TextIn({br.x + 42, br.y, br.w - 74, br.h}, bl, {b.visible ? sdl3::Color{205, 210, 220, 255} : th.textDim, 12});
			if (u.IconButton({br.Right() - 26, br.y, 22, 22}, b.visible ? icons::Codepoint(icons::MaterialIcons::VISIBILITY) : icons::Codepoint(icons::MaterialIcons::VISIBILITY_OFF), b.visible ? "Masquer" : "Afficher",
							 true, sdl3::WithAlpha(th.textDim, b.visible ? 255 : 120)))
				b.visible = !b.visible;
			if (bit.leftClicked) {
				s.part = int(pi);
				s.box = int(bi);
				s.collision = -1;
			}
			u.PopId();
		}
		u.PopId();
	}
	if (s.doc->entry.kind == "Bloc") {
		u.Space(6);
		const Rect h = u.Next(22);
		u.Icon({h.x + 2, h.y, 18, h.h}, icons::Codepoint(icons::MaterialIcons::CROP_FREE), Rgba(240, 210, 80), 15);
		c.Text(h.x + 24, h.y + (h.h - c.LineHeight(12, sdl3::CanvasFont::Bold)) * 0.5f, std::format("COLLISION (AABB) · {}", m.collision.size()), th.text, 12,
			   sdl3::CanvasFont::Bold);
		for (size_t i = 0; i < m.collision.size(); ++i) {
			u.PushId(uint64_t(i) + 5000);
			const Rect row = u.Next(22);
			const auto it = u.Interact(u.MakeId("##col"), row);
			if (int(i) == s.collision)
				c.FillRoundRect(row, 4, Rgba(120, 100, 30, 200));
			else if (it.hovered)
				c.FillRoundRect(row, 4, th.widgetHover);
			const voxel::ModelBox &b = m.collision[i];
			c.Text(row.x + 26, row.y + (row.h - c.LineHeight(12)) * 0.5f, std::format("boîte {}  {:g}×{:g}×{:g}", i + 1, b.size.x, b.size.y, b.size.z),
				   th.text, 12);
			if (it.leftClicked) {
				s.collision = int(i);
				s.box = -1;
			}
			u.PopId();
		}
		if (m.collision.empty())
			u.Label("pleine (cube) ou sans collision", th.textDim, 12);
	}
	u.EndScroll();
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Propriétés de l'élément
// ══════════════════════════════════════════════════════════════════════════

void ModelWorkshop::DrawElement(ui::Context &u, const Rect &r, const OpenFileFn &openFile) {
	Impl &s = *m_impl;
	const ui::Theme &th = u.Style();
	if (u.MouseIn(r) && u.MousePressed(sdl3::MouseButton::Left))
		m_focused = true;
	if (!s.doc)
		return Hint(u, r, "Les propriétés du modèle, de la partie et du cube sélectionnés s'affichent ici.");
	voxel::Model &m = s.doc->model;
	u.PushId("element");
	u.BeginScroll("element", r.Inset(10, 8));
	// ── Modèle ──
	u.Section(icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), "Modèle");
	u.Label(s.doc->entry.id + (s.doc->dirty ? "  (modifié)" : ""), th.text, 14);
	u.Label(std::format("{} · format {} · mod {}", s.doc->entry.kind, FormatLabel(m.format), s.doc->entry.mod), th.textDim, 12);
	if (s.image)
		u.Label(std::format("texture {}×{} · {}", s.image->pixels.GetWidth(), s.image->pixels.GetHeight(), s.image->path.filename().string()), th.textDim, 12);
	else
		u.Label("sans texture (couleurs par partie)", th.textDim, 12);
	u.Row({1, 1}, 28);
	if (u.Button("Enregistrer (Ctrl+S)", s.doc->dirty || (s.image && s.image->dirty)))
		Save();
	if (u.Button("Ouvrir le JSON") && openFile)
		openFile(m.format == voxel::ModelFormat::Obj && s.doc->entry.kind == "Modèle" ? s.doc->entry.mesh : s.doc->entry.file, s.doc->entry.mod);
	u.Row({1, 1}, 28);
	if (u.Button("Exporter .obj")) {
		fs::path out = !s.doc->entry.mesh.empty() ? s.doc->entry.mesh : s.doc->entry.file;
		const std::string stem = s.doc->entry.id.substr(s.doc->entry.id.find(':') + 1);
		out = out.parent_path() / (stem + (m.format == voxel::ModelFormat::Obj ? "_export.obj" : ".obj"));
		s.status = WriteText(out, voxel::ObjToText(m)) ? "exporté : " + out.string() : "échec d'écriture : " + out.string();
	}
	if (u.Button(m.format == voxel::ModelFormat::Rig ? "Rig (animable)##rig" : "Convertir en rig", m.format != voxel::ModelFormat::Rig))
		s.SaveAsRig();
	u.Row({1, 1}, 28);
	if (u.Button("Auto-UV##autouv", m.boxUv)) { // cadrillage UV calculé automatiquement
		s.Changed();
		voxel::AutoBoxUv(m, 2);
		s.status = std::format("cadrillage UV automatique : {} boîte(s) rangée(s) dans une texture {}×{}", m.BoxCount(), m.textureWidth, m.textureHeight);
	}
	if (u.Button("Importer une texture…##importtex")) {
		sdl3::dialog::ShowOpenFile(
			[this](const sdl3::DialogResult &res, int) {
				if (res.Ok())
					m_impl->pendingImport = fs::path(std::string(res.First()));
			},
			{sdl3::DialogFilter{String("Images PNG"), String("png")}}, String(""), false);
	}
	if (m.boxUv) {
		float ts[2] = {float(m.textureWidth), float(m.textureHeight)};
		if (u.DragFloats("Taille de texture (box UV)", ts, 0.2f, "{:.0f}", 1.f, 4096.f)) {
			s.Changed();
			m.textureWidth = int(std::round(ts[0]));
			m.textureHeight = int(std::round(ts[1]));
		}
	}
	const voxel::ModelBounds b = m.Bounds();
	u.Label(std::format("dimensions {:.2f} × {:.2f} × {:.2f} m", b.Size().x, b.Size().y, b.Size().z), th.textDim, 12);

	// ── Mécanique générique ──
	u.Section(icons::Codepoint(icons::MaterialIcons::BUILD), "Mécanique");
	int mechType = 0;
	if (m.mechanic)
		for (size_t i = 0; i < std::size(kMechanicTypes); ++i)
			if (m.mechanic->type == kMechanicTypes[i])
				mechType = int(i);
	std::vector<std::string_view> mechViews(kMechanicLabels, kMechanicLabels + std::size(kMechanicLabels));
	if (u.Choice(u.Next(28), "##mechtype", mechType, mechViews)) {
		s.Changed();
		if (mechType == 0) {
			m.mechanic.reset();
		} else {
			if (!m.mechanic)
				m.mechanic = voxel::ModelMechanic{};
			m.mechanic->type = std::string(kMechanicTypes[size_t(mechType)]);
		}
	}
	if (m.mechanic) {
		std::vector<std::string> animNames;
		for (const voxel::Animation &a : m.animations)
			animNames.push_back(a.name);
		std::vector<std::string_view> animViews(animNames.begin(), animNames.end());
		int animSel = -1;
		for (size_t i = 0; i < animNames.size(); ++i)
			if (animNames[i] == m.mechanic->animation)
				animSel = int(i);
		if (animViews.empty()) {
			u.Wrapped("Ajoutez une animation (timeline « Animation ») pour l'activation.", th.textDim, 12);
		} else if (u.Choice(u.Next(28), "##mechanim", animSel, animViews)) {
			s.Changed();
			m.mechanic->animation = animSel >= 0 ? animNames[size_t(animSel)] : std::string();
		}
		bool toggle = m.mechanic->toggle;
		if (u.Checkbox("Bascule (aller-retour)", toggle)) {
			s.Changed();
			m.mechanic->toggle = toggle;
		}
	}

	// ── Variante de bloc ──
	u.Section(icons::Codepoint(icons::MaterialIcons::ADD_BOX), "Variante de bloc");
	std::string bn = s.blockName, bt = s.blockTexture;
	u.TextInput("##blockname", bn, nullptr, "nom (ex. porte_chene)");
	u.TextInput("##blocktex", bt, nullptr, "texture (ex. core:oak_door)");
	s.blockName = bn;
	s.blockTexture = bt;
	if (u.Button("Créer un bloc…##createblock", m.BoxCount() > 0))
		CreateBlock();
	u.Wrapped("Aplatit le modèle en nodebox, y associe la texture et la mécanique (interact.toggle), puis l'enregistre dans <mod>/blocks/studio_blocks.json.",
			  th.textDim, 12);

	// ── Pose animée (mode Animation) ──
	voxel::ModelPart *p = s.Part();
	if (s.mode == Impl::Mode::Animate && p) {
		u.Section(icons::Codepoint(icons::MaterialIcons::ANIMATION), std::format("Pose de « {} » à {:.2f} s", p->name, s.time));
		voxel::PartOffset off = CurrentOffset(s, s.part);
		float rot[3] = {off.rotation.x, off.rotation.y, off.rotation.z};
		if (u.DragFloats("Rotation ajoutée (°)", rot, 0.5f, "{:.1f}")) {
			off.rotation = {rot[0], rot[1], rot[2]};
			KeyCurrent(s, off, true, false);
		}
		float pos[3] = {off.position.x, off.position.y, off.position.z};
		if (u.DragFloats("Déplacement", pos, 0.05f, "{:.2f}")) {
			off.position = {pos[0], pos[1], pos[2]};
			KeyCurrent(s, off, false, true);
		}
		u.Wrapped("Chaque modification pose une image clé à l'instant courant (timeline « Animation »).", th.textDim, 12);
	} else if (p) {
		// ── Partie ──
		u.Section(icons::Codepoint(icons::MaterialIcons::FOLDER), "Partie");
		std::string name = p->name;
		bool submitted = false;
		u.TextInput("##partname", name, &submitted, "nom de la partie");
		if (name != p->name && !name.empty() && m.FindPart(name) < 0) {
			s.Changed();
			for (voxel::Animation &a : m.animations)
				for (voxel::AnimationTrack &t : a.tracks)
					if (t.part == p->name)
						t.part = name;
			p->name = name;
		}
		float pivot[3] = {p->pivot.x, p->pivot.y, p->pivot.z};
		if (u.DragFloats("Point de pivot", pivot, 0.05f, "{:.3g}")) {
			s.Changed();
			p->pivot = {pivot[0], pivot[1], pivot[2]};
		}
		float rot[3] = {p->rotation.x, p->rotation.y, p->rotation.z};
		if (u.DragFloats("Rotation (°)", rot, 0.5f, "{:.1f}")) {
			s.Changed();
			p->rotation = {rot[0], rot[1], rot[2]};
		}
		if (m.format != voxel::ModelFormat::Rig && (p->rotation.x != 0 || p->rotation.y != 0 || p->rotation.z != 0))
			u.Wrapped("Les rotations ne s'enregistrent qu'au format rig (« Convertir en rig »).", th.warning, 12);
		if (!p->indices.empty())
			u.Label(std::format("maillage : {} sommets, {} triangles", p->vertices.size(), p->indices.size() / 3), th.textDim, 12);
		if (!p->extra.empty())
			u.Wrapped("mouvements : " + p->extra, th.textDim, 12);
	}
	// ── Cube ──
	if (voxel::ModelBox *bx = s.Box(); bx && s.mode != Impl::Mode::Animate) {
		u.Section(icons::Codepoint(icons::MaterialIcons::ADD_BOX), std::format("Cube {}", s.box + 1));
		const float speed = m.boxUv ? 0.1f : 0.005f;
		float from[3] = {bx->from.x, bx->from.y, bx->from.z};
		if (u.DragFloats("Position (origine)", from, speed, "{:.4g}")) {
			s.Changed();
			bx->from = {from[0], from[1], from[2]};
		}
		float size[3] = {bx->size.x, bx->size.y, bx->size.z};
		if (u.DragFloats("Taille", size, speed, "{:.4g}", 0.f, 1e6f)) {
			s.Changed();
			bx->size = {size[0], size[1], size[2]};
		}
		if (m.boxUv) {
			float uv[2] = {bx->uv.x, bx->uv.y};
			if (u.DragFloats("UV (origine du dépliage, texels)", uv, 0.2f, "{:.0f}", 0.f, 4096.f)) {
				s.Changed();
				bx->uv = {std::round(uv[0]), std::round(uv[1])};
			}
			float inflate[1] = {bx->inflate};
			if (u.DragFloats("Gonflement", inflate, 0.01f, "{:.2f}", -4.f, 4.f)) {
				s.Changed();
				bx->inflate = inflate[0];
			}
			bool mirror = bx->mirror;
			if (u.Checkbox("Dépliage miroir", mirror)) {
				s.Changed();
				bx->mirror = mirror;
			}
		}
	}
	// ── Connectivité automatique (nodebox) ──
	if (m.format == voxel::ModelFormat::Nodebox && s.mode != Impl::Mode::Animate) {
		u.Section(icons::Codepoint(icons::MaterialIcons::EXTENSION), "Connectivité");
		std::string to = s.doc->connectTo;
		u.TextInput("##connectto", to, nullptr, "se connecte à : core:fence, #fence, #solid");
		if (to != s.doc->connectTo) {
			s.Changed();
			s.doc->connectTo = to;
		}
		u.Wrapped("Une partie « connect_<face> » par côté : ses cubes s'ajoutent quand le voisin de ce côté est cité ci-dessus (identifiant, "
				  "#étiquette, #solid). L'œil de l'arborescence simule le voisin.",
				  th.textDim, 12);
		static constexpr const char *kLabels[] = {"Nord (-Z)", "Sud (+Z)", "Est (+X)", "Ouest (-X)", "Haut", "Bas"};
		for (size_t f = 0; f < 6; ++f) {
			const std::string name = std::string("connect_") + kConnectFaces[f];
			const int pi = m.FindPart(name);
			u.Row({2, 1}, 24);
			if (pi >= 0) {
				bool on = m.parts[size_t(pi)].visible;
				if (u.Checkbox(std::format("{} · {} cube(s)##cf{}", kLabels[f], m.parts[size_t(pi)].boxes.size(), f), on))
					m.parts[size_t(pi)].visible = on;
				if (u.Button(std::format("Éditer##ce{}", f))) {
					s.part = pi;
					s.box = m.parts[size_t(pi)].boxes.empty() ? -1 : 0;
				}
			} else {
				u.Label(kLabels[f], th.textDim, 12);
				if (u.Button(std::format("Créer##cc{}", f))) {
					s.Changed();
					voxel::ModelPart connectPart;
					connectPart.name = name;
					// Proposition : une traverse du centre vers ce côté.
					voxel::ModelBox rail;
					const float t = 0.0625f;
					switch (f) {
					case 0: rail.from = {-t, 0, -0.5f}; rail.size = {2 * t, 0.25f, 0.375f}; break;
					case 1: rail.from = {-t, 0, 0.125f}; rail.size = {2 * t, 0.25f, 0.375f}; break;
					case 2: rail.from = {0.125f, 0, -t}; rail.size = {0.375f, 0.25f, 2 * t}; break;
					case 3: rail.from = {-0.5f, 0, -t}; rail.size = {0.375f, 0.25f, 2 * t}; break;
					case 4: rail.from = {-t, 0.125f, -t}; rail.size = {2 * t, 0.375f, 2 * t}; break;
					default: rail.from = {-t, -0.5f, -t}; rail.size = {2 * t, 0.375f, 2 * t}; break;
					}
					connectPart.boxes.push_back(rail);
					m.parts.push_back(std::move(connectPart));
					s.part = int(m.parts.size()) - 1;
					s.box = 0;
					if (s.doc->connectTo.empty())
						s.doc->connectTo = "#solid";
				}
			}
		}
	}
	// ── Collision ──
	if (s.doc->entry.kind == "Bloc") {
		u.Section(icons::Codepoint(icons::MaterialIcons::CROP_FREE), "Collision (AABB)");
		if (s.collision >= 0 && size_t(s.collision) < m.collision.size()) {
			voxel::ModelBox &cb = m.collision[size_t(s.collision)];
			float from[3] = {cb.from.x, cb.from.y, cb.from.z};
			if (u.DragFloats(std::format("Boîte {} : origine##colfrom", s.collision + 1), from, 0.005f, "{:.4g}")) {
				s.Changed();
				cb.from = {from[0], from[1], from[2]};
			}
			float size[3] = {cb.size.x, cb.size.y, cb.size.z};
			if (u.DragFloats("Taille##colsize", size, 0.005f, "{:.4g}", 0.f, 64.f)) {
				s.Changed();
				cb.size = {size[0], size[1], size[2]};
			}
		}
		u.Row({1, 1}, 28);
		if (u.Button("Ajouter une boîte")) {
			s.Changed();
			voxel::ModelBox nb;
			nb.from = {-0.5f, -0.5f, -0.5f};
			m.collision.push_back(nb);
			s.collision = int(m.collision.size()) - 1;
			s.box = -1;
		}
		if (u.Button("Depuis la forme")) {
			s.Changed();
			m.collision.clear();
			const voxel::ModelBounds fb = m.Bounds();
			voxel::ModelBox nb;
			nb.from = fb.min;
			nb.size = fb.Size();
			m.collision.push_back(nb);
			s.collision = 0;
		}
	}
	u.EndScroll();
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Vue du modèle
// ══════════════════════════════════════════════════════════════════════════

namespace {

/// Quadrilatère sous la souris (le plus proche) et ses coordonnées de texture.
bool HitTest(const Impl &s, float mx, float my, size_t &quad, FVector2 &uv) {
	for (size_t k = s.proj.size(); k-- > 0;) {
		const Impl::Proj &pr = s.proj[k];
		for (int tri = 0; tri < 2; ++tri) {
			const int a = 0, b = tri == 0 ? 1 : 2, c = tri == 0 ? 2 : 3;
			float w0, w1, w2;
			if (!PointInTriangle(mx, my, pr.x[a], pr.y[a], pr.x[b], pr.y[b], pr.x[c], pr.y[c], w0, w1, w2))
				continue;
			// Correction de perspective (poids divisés par la profondeur).
			const float p0 = w0 / pr.z[a], p1 = w1 / pr.z[b], p2 = w2 / pr.z[c];
			const float sum = p0 + p1 + p2;
			const voxel::ModelQuad &q = s.quads[pr.quad];
			uv = (q.uv[a] * p0 + q.uv[b] * p1 + q.uv[c] * p2) * (1.f / sum);
			quad = pr.quad;
			return true;
		}
	}
	return false;
}

/// Repère de la sélection : centre (unités bloc) et axes (monde).
bool SelectionFrame(Impl &s, const std::vector<voxel::PartOffset> &pose, FVector3 &center, std::array<FVector3, 3> &axes) {
	if (!s.doc)
		return false;
	const voxel::Model &m = s.doc->model;
	const float inv = 1.f / m.unitsPerBlock;
	axes = {FVector3{1, 0, 0}, FVector3{0, 1, 0}, FVector3{0, 0, 1}};
	if (s.collision >= 0 && size_t(s.collision) < m.collision.size()) {
		const voxel::ModelBox &b = m.collision[size_t(s.collision)];
		center = (b.from + b.size * 0.5f) * inv;
		return true;
	}
	const voxel::ModelPart *p = s.Part();
	if (!p)
		return false;
	const voxel::PartOffset off = size_t(s.part) < pose.size() ? pose[size_t(s.part)] : voxel::PartOffset{};
	const FVector3 rot = p->rotation + off.rotation;
	for (int a = 0; a < 3; ++a)
		axes[size_t(a)] = voxel::RotateEuler(axes[size_t(a)], rot);
	FVector3 local = p->pivot;
	if (const voxel::ModelBox *b = s.Box())
		local = b->from + b->size * 0.5f;
	center = (voxel::RotateEuler(local - p->pivot, rot) + p->pivot + off.position) * inv;
	return true;
}

void ApplyDrag(Impl &s, float mx, float my, bool shift, bool ctrl) {
	voxel::Model &m = s.doc->model;
	const voxel::Model &st = s.dragStart;
	const int a = s.dragAxis;
	if (s.tool == Impl::Tool::Rotate || (s.mode == Impl::Mode::Animate && s.tool == Impl::Tool::Rotate)) {
		float deg = (mx - s.dragX) * 0.5f;
		const float snap = shift ? 1.f : 5.f;
		if (!ctrl)
			deg = std::round(deg / snap) * snap;
		if (s.part < 0 || size_t(s.part) >= m.parts.size())
			return;
		if (s.mode == Impl::Mode::Animate) {
			voxel::PartOffset off = s.dragOffset;
			Axis(off.rotation, a) += deg;
			KeyCurrent(s, off, true, false);
			return;
		}
		s.Changed();
		Axis(m.parts[size_t(s.part)].rotation, a) = AxisOf(st.parts[size_t(s.part)].rotation, a) + deg;
		return;
	}
	const FVector2 v = s.dragScreen;
	const float l2 = v.Dot(v);
	if (l2 < 1e-6f)
		return;
	float units = ((mx - s.dragX) * v.x + (my - s.dragY) * v.y) / l2 * m.unitsPerBlock;
	float step = s.Snap() * (shift ? 0.25f : 1.f);
	if (!ctrl)
		units = std::round(units / step) * step;
	if (s.mode == Impl::Mode::Animate) {
		voxel::PartOffset off = s.dragOffset;
		Axis(off.position, a) += units;
		KeyCurrent(s, off, false, true);
		return;
	}
	s.Changed();
	if (s.collision >= 0 && size_t(s.collision) < m.collision.size()) {
		const voxel::ModelBox &b0 = st.collision[size_t(s.collision)];
		voxel::ModelBox &b = m.collision[size_t(s.collision)];
		if (s.tool == Impl::Tool::Move)
			Axis(b.from, a) = AxisOf(b0.from, a) + units;
		else
			Axis(b.size, a) = std::max(step, AxisOf(b0.size, a) + units);
		return;
	}
	if (s.part < 0 || size_t(s.part) >= m.parts.size())
		return;
	voxel::ModelPart &p = m.parts[size_t(s.part)];
	const voxel::ModelPart &p0 = st.parts[size_t(s.part)];
	if (s.box >= 0 && size_t(s.box) < p.boxes.size()) {
		voxel::ModelBox &b = p.boxes[size_t(s.box)];
		const voxel::ModelBox &b0 = p0.boxes[size_t(s.box)];
		if (s.tool == Impl::Tool::Move)
			Axis(b.from, a) = AxisOf(b0.from, a) + units;
		else
			Axis(b.size, a) = std::max(step, AxisOf(b0.size, a) + units);
		return;
	}
	if (s.tool == Impl::Tool::Move) { // partie entière
		Axis(p.pivot, a) = AxisOf(p0.pivot, a) + units;
		for (size_t i = 0; i < p.boxes.size(); ++i)
			Axis(p.boxes[i].from, a) = AxisOf(p0.boxes[i].from, a) + units;
		for (size_t i = 0; i < p.vertices.size(); ++i)
			Axis(p.vertices[i].position, a) = AxisOf(p0.vertices[i].position, a) + units;
	}
}

} // namespace

void ModelWorkshop::DrawView(ui::Context &u, const Rect &area, sdl3::Renderer &ren) {
	Impl &s = *m_impl;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	if (u.MouseIn(area) && u.MousePressed(sdl3::MouseButton::Left))
		m_focused = true;
	// ── Barre d'outils : modes, gizmo, affichage ──
	const float barH = 34;
	const Rect bar{area.x, area.y, area.w, barH};
	c.FillRect(bar, Rgba(18, 21, 27));
	c.FillRect({bar.x, bar.Bottom() - 1, bar.w, 1}, th.panelBorder);
	u.PushId("modelview");
	float x = bar.x + 6;
	auto modeButton = [&](Impl::Mode mode, char32_t icon, std::string_view label) {
		const float w = c.MeasureText(label, 13) + 34;
		const Rect b{x, bar.y + 4, w, 26};
		const auto it = u.Interact(u.MakeId(label), b);
		const bool on = s.mode == mode;
		c.FillRoundRect(b, 5, on ? Rgba(38, 92, 170) : (it.hovered ? th.widgetHover : Rgba(18, 21, 27)));
		u.Icon({b.x + 4, b.y, 22, b.h}, icon, on ? th.text : th.textDim, 16);
		c.Text(b.x + 28, b.y + (b.h - c.LineHeight(13)) * 0.5f, label, on ? th.text : th.textDim, 13);
		if (it.leftClicked)
			s.mode = mode;
		x += w + 4;
	};
	modeButton(Impl::Mode::Edit, icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), "Édition");
	modeButton(Impl::Mode::Paint, icons::Codepoint(icons::MaterialIcons::BRUSH), "Peinture");
	modeButton(Impl::Mode::Animate, icons::Codepoint(icons::MaterialIcons::ANIMATION), "Animation");
	c.FillRect({x + 3, bar.y + 8, 1, barH - 16}, th.panelBorder);
	x += 10;
	auto toolButton = [&](char32_t icon, std::string_view tip, bool on, bool enabled = true) {
		const Rect b{x, bar.y + 3, 28, 28};
		if (on)
			c.FillRoundRect(b, 5, Rgba(38, 92, 170));
		x += 31;
		return u.IconButton(b, icon, tip, enabled, on ? th.text : sdl3::Color{0, 0, 0, 0});
	};
	if (toolButton(icons::Codepoint(icons::MaterialIcons::OPEN_WITH), "Déplacer (W)", s.tool == Impl::Tool::Move))
		s.tool = Impl::Tool::Move;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::ASPECT_RATIO), "Redimensionner (E)", s.tool == Impl::Tool::Resize, s.mode != Impl::Mode::Animate))
		s.tool = Impl::Tool::Resize;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::ROTATE_RIGHT), "Tourner la partie (R)", s.tool == Impl::Tool::Rotate))
		s.tool = Impl::Tool::Rotate;
	c.FillRect({x + 3, bar.y + 8, 1, barH - 16}, th.panelBorder);
	x += 10;
	// Mode de sélection : objet entier, sommets, arêtes ou faces (type Sketchup/Blockbench).
	auto selButton = [&](Impl::SelMode mode, char32_t icon, std::string_view tip) {
		const bool on = s.selMode == mode;
		if (toolButton(icon, tip, on, s.mode == Impl::Mode::Edit)) {
			s.selMode = on ? Impl::SelMode::Object : mode;
			s.selVerts.clear();
			s.selEdges.clear();
			s.selFaces.clear();
			s.ppFace = -1;
		}
	};
	selButton(Impl::SelMode::Vertex, icons::Codepoint(icons::MaterialIcons::FIBER_MANUAL_RECORD), "Sélection : sommet (clic ; Maj : multi)");
	selButton(Impl::SelMode::Edge, icons::Codepoint(icons::MaterialIcons::STRAIGHTEN), "Sélection : arête (clic ; Maj : multi)");
	selButton(Impl::SelMode::Face, icons::Codepoint(icons::MaterialIcons::CROP_FREE), "Sélection : face (clic ; Maj : multi ; glisser : push/pull)");
	c.FillRect({x + 3, bar.y + 8, 1, barH - 16}, th.panelBorder);
	x += 10;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::GRID_ON), "Grille", s.showGrid))
		s.showGrid = !s.showGrid;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::STRAIGHTEN), "Boîte englobante et cotes", s.showBounds))
		s.showBounds = !s.showBounds;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::CROP_FREE), "Boîtes de collision (AABB)", s.showCollision))
		s.showCollision = !s.showCollision;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::LAYERS), "Double-face (voir les faces arrière)", s.doubleSided))
		s.doubleSided = !s.doubleSided;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::ADD_BOX), "Extruder une face (X) : clic sur une face du cube sélectionné", s.extruding, s.mode == Impl::Mode::Edit && s.box >= 0))
		s.extruding = !s.extruding;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::CENTER_FOCUS_STRONG), "Cadrer (F)", false, s.doc.has_value()))
		s.Frame();
	c.FillRect({x + 3, bar.y + 8, 1, barH - 16}, th.panelBorder);
	x += 10;
	if (toolButton(icons::Codepoint(icons::MaterialIcons::UNDO), "Annuler (Ctrl+Z)", false, !s.undo.empty() || (s.image && !s.image->undo.empty())))
		Undo();
	if (toolButton(icons::Codepoint(icons::MaterialIcons::REDO), "Rétablir (Ctrl+Y)", false, !s.redo.empty() || (s.image && !s.image->redo.empty())))
		Redo();
	if (toolButton(icons::Codepoint(icons::MaterialIcons::SAVE), "Enregistrer (Ctrl+S)", false, (s.doc && s.doc->dirty) || (s.image && s.image->dirty)))
		Save();

	const Rect r{area.x, area.y + barH, area.w, area.h - barH - 24};
	c.FillGradientV(r, Rgba(46, 50, 60), Rgba(24, 27, 34));
	if (!s.doc) {
		c.TextCentered(r, "Choisissez une forme dans le catalogue « Modèles ».", th.textDim, 14);
		u.PopId();
		return;
	}
	voxel::Model &m = s.doc->model;
	const ViewCam cam = ViewCam::Make(s.target, s.yaw, s.pitch, s.distance, r);
	const float motionT = s.motion ? s.motionTime : -1.f;
	const std::vector<voxel::PartOffset> pose = voxel::SamplePose(m, s.mode == Impl::Mode::Animate || s.playing ? s.Anim() : nullptr, s.time, motionT);
	voxel::BuildQuads(m, pose, s.quads);
	// Bloc nodebox à plusieurs textures : remappe chaque face vers sa cellule de l'atlas.
	if (s.blockAtlas)
		for (voxel::ModelQuad &q : s.quads)
			if (q.box >= 0 && q.face < 6) {
				const math::FVector2 cell = s.blockAtlas->cells[q.face];
				for (int k = 0; k < 4; ++k)
					q.uv[k] = {cell.x + q.uv[k].x * s.blockAtlas->cellSize.x, cell.y + q.uv[k].y * s.blockAtlas->cellSize.y};
			}
	// Maillage sous-élément de la partie sélectionnée (sélection sommets/arêtes/faces).
	BuildSubMesh(s, m, pose);
	s.meshVerts2d.assign(s.meshVerts.size(), {1e9f, 1e9f});
	s.meshEdges2d.assign(s.meshEdges.size(), {});
	for (size_t i = 0; i < s.meshVerts.size(); ++i) {
		float sx, sy, sd;
		if (cam.Project(s.meshVerts[i].p, sx, sy, sd))
			s.meshVerts2d[i] = {sx, sy};
	}
	for (size_t i = 0; i < s.meshEdges.size(); ++i)
		s.meshEdges2d[i] = {s.meshVerts2d[size_t(s.meshEdges[i][0])], s.meshVerts2d[size_t(s.meshEdges[i][1])]};
	auto vertSelected = [&](int box, uint8_t corner) {
		for (const Impl::SubRef &ref : s.selVerts)
			if (ref.box == box && ref.sub == corner)
				return true;
		return false;
	};
	auto edgeSelected = [&](int box, uint8_t edge) {
		for (const Impl::SubRef &ref : s.selEdges)
			if (ref.box == box && ref.sub == edge)
				return true;
		return false;
	};
	auto faceSelected = [&](int box, uint8_t face) {
		for (const Impl::SubRef &ref : s.selFaces)
			if (ref.box == box && ref.sub == face)
				return true;
		return false;
	};
	const voxel::ModelBounds bounds = m.Bounds();
	c.PushClip(r);

	// ── Grille au sol, axes ──
	if (s.showGrid) {
		const float y0 = bounds.min.y;
		const float half = std::max(2.f, std::ceil(std::max(bounds.Size().x, bounds.Size().z)) + 1.f);
		auto line = [&](FVector3 a, FVector3 b, sdl3::Color col, float t = 1.f) {
			float x0, yy0, x1, yy1;
			if (cam.Segment(a, b, x0, yy0, x1, yy1))
				c.Line(x0, yy0, x1, yy1, col, t);
		};
		for (int i = -8; i <= 8; ++i) { // 1/16 de bloc dans le bloc d'origine
			const float f = float(i) / 16.f;
			line({f, y0, -0.5f}, {f, y0, 0.5f}, Rgba(255, 255, 255, 22));
			line({-0.5f, y0, f}, {0.5f, y0, f}, Rgba(255, 255, 255, 22));
		}
		for (float f = -half; f <= half + 1e-3f; f += 1.f) {
			line({f + 0.5f, y0, -half + 0.5f}, {f + 0.5f, y0, half + 0.5f}, Rgba(255, 255, 255, 40));
			line({-half + 0.5f, y0, f + 0.5f}, {half + 0.5f, y0, f + 0.5f}, Rgba(255, 255, 255, 40));
		}
		line({-half, y0, 0}, {half, y0, 0}, sdl3::WithAlpha(kAxisColors[0], 150), 1.5f);
		line({0, y0, -half}, {0, y0, half}, sdl3::WithAlpha(kAxisColors[2], 150), 1.5f);
		line({0, y0, 0}, {0, y0 + 1.f, 0}, sdl3::WithAlpha(kAxisColors[1], 150), 1.5f);
	}

	// ── Faces (tri du plus loin au plus proche) ──
	s.proj.clear();
	for (size_t qi = 0; qi < s.quads.size(); ++qi) {
		const voxel::ModelQuad &q = s.quads[qi];
		Impl::Proj pr{};
		bool ok = true;
		float depth = 0;
		for (int k = 0; k < 4 && ok; ++k) {
			ok = cam.Project(q.p[k], pr.x[k], pr.y[k], pr.z[k]);
			depth += pr.z[k];
		}
		if (!ok)
			continue;
		if (!s.doubleSided) { // élimination des faces arrière (cubes et maillages)
			const FVector3 centre = (q.p[0] + q.p[1] + q.p[2] + q.p[3]) * 0.25f;
			if (q.normal.Dot(cam.eye - centre) <= 0)
				continue;
		}
		pr.depth = depth * 0.25f;
		pr.quad = qi;
		s.proj.push_back(pr);
	}
	std::ranges::sort(s.proj, [](const Impl::Proj &a, const Impl::Proj &b) { return a.depth > b.depth; });
	// Face survolée en mode extrusion : surbrillance verte sur la face qui sera extrudée.
	size_t hoverQuad = SIZE_MAX;
	FVector2 hoverUv{};
	const bool hoverFace = s.extruding && u.MouseIn(r) && HitTest(s, u.MouseX(), u.MouseY(), hoverQuad, hoverUv);
	sdl3::Texture *tex = s.ImageTexture(ren);
	sdl3::Texture *white = s.WhiteTexture(ren);
	const float frameScale = s.FrameScale();
	const FVector3 light = FVector3{0.45f, 0.9f, 0.3f}.Normalized();
	for (const Impl::Proj &pr : s.proj) {
		const voxel::ModelQuad &q = s.quads[pr.quad];
		float xy[8], uv[8];
		for (int k = 0; k < 4; ++k) {
			xy[k * 2] = pr.x[k];
			xy[k * 2 + 1] = pr.y[k];
			uv[k * 2] = q.uv[k].x;
			uv[k * 2 + 1] = q.uv[k].y * frameScale;
		}
		const float shade = 0.62f + 0.38f * std::max(0.f, std::fabs(q.normal.Dot(light)));
		sdl3::Color tint{uint8_t(255 * shade), uint8_t(255 * shade), uint8_t(255 * shade), 255};
		if (!tex) { // couleur par partie
			const uint32_t h = uint32_t(q.part) * 2654435761u;
			const sdl3::Color base = Rgba(uint8_t(120 + (h & 0x5F)), uint8_t(130 + ((h >> 8) & 0x4F)), uint8_t(150 + ((h >> 16) & 0x5F)));
			tint = {uint8_t(base.r * shade), uint8_t(base.g * shade), uint8_t(base.b * shade), 255};
		}
		if (hoverFace && pr.quad == hoverQuad && q.box >= 0 && q.part == s.part)
			tint = sdl3::MixColor(tint, Rgba(120, 220, 120), 0.45f);
		if (s.selMode == Impl::SelMode::Face && q.part == s.part && q.box >= 0 && faceSelected(q.box, q.face))
			tint = sdl3::MixColor(tint, Rgba(120, 220, 120), 0.5f);
		const bool selected = s.mode != Impl::Mode::Paint && q.part == s.part && (s.box < 0 || q.box == s.box) && s.collision < 0;
		if (selected)
			tint = sdl3::MixColor(tint, Rgba(120, 170, 255), 0.28f);
		c.TexturedQuad(tex ? *tex : *white, xy, uv, tint);
	}

	// ── Surbrillance des sous-éléments sélectionnés (sommets, arêtes) ──
	if (s.selMode != Impl::SelMode::Object && s.mode != Impl::Mode::Paint) {
		for (size_t i = 0; i < s.meshEdges.size(); ++i) {
			const Impl::MeshVert &a = s.meshVerts[size_t(s.meshEdges[i][0])];
			if (!edgeSelected(a.box, s.meshEdgeSub[i]))
				continue;
			const math::FVector2 &pa = s.meshEdges2d[i][0], &pb = s.meshEdges2d[i][1];
			if (pa.x < 1e8f && pb.x < 1e8f)
				c.Line(pa.x, pa.y, pb.x, pb.y, Rgba(120, 220, 120), 3.f);
		}
		for (size_t i = 0; i < s.meshVerts.size(); ++i) {
			const Impl::MeshVert &v = s.meshVerts[i];
			if (!vertSelected(v.box, v.corner))
				continue;
			const math::FVector2 &p = s.meshVerts2d[i];
			if (p.x < 1e8f)
				c.FillCircle(p.x, p.y, 5, Rgba(120, 220, 120));
		}
	}

	// ── Contours de la sélection, boîte englobante, collision ──
	auto edge = [&](const FVector3 &a, const FVector3 &b, sdl3::Color col, float t) {
		float x0, y0, x1, y1;
		if (cam.Segment(a, b, x0, y0, x1, y1))
			c.Line(x0, y0, x1, y1, col, t);
	};
	auto wireBox = [&](const FVector3 &lo, const FVector3 &hi, sdl3::Color col, float t) {
		const FVector3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
							   {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
		for (int i = 0; i < 4; ++i) {
			edge(p[i], p[(i + 1) % 4], col, t);
			edge(p[i + 4], p[(i + 1) % 4 + 4], col, t);
			edge(p[i], p[i + 4], col, t);
		}
	};
	if (s.mode != Impl::Mode::Paint)
		for (const voxel::ModelQuad &q : s.quads)
			if (q.part == s.part && q.box >= 0 && q.box == s.box && s.collision < 0)
				for (int k = 0; k < 4; ++k)
					edge(q.p[k], q.p[(k + 1) % 4], Rgba(255, 170, 60), 2.f);
	if (s.showBounds) {
		wireBox(bounds.min, bounds.max, Rgba(90, 150, 255, 200), 1.f);
		const FVector3 sz = bounds.Size();
		auto label = [&](const FVector3 &p, float value) {
			float sx, sy, d;
			if (!cam.Project(p, sx, sy, d))
				return;
			const std::string t = std::format("{:.2f} m", value);
			const float w = c.MeasureText(t, 12) + 10;
			c.FillRoundRect({sx - w * 0.5f, sy - 9, w, 18}, 4, Rgba(10, 12, 16, 190));
			c.Text(sx - w * 0.5f + 5, sy - 8, t, Rgba(200, 220, 255), 12);
		};
		label({bounds.Center().x, bounds.min.y, bounds.max.z}, sz.x);
		label({bounds.max.x, bounds.min.y, bounds.Center().z}, sz.z);
		label({bounds.max.x, bounds.Center().y, bounds.max.z}, sz.y);
	}
	if (s.showCollision)
		for (size_t i = 0; i < m.collision.size(); ++i) {
			const voxel::ModelBox &b = m.collision[i];
			const float inv = 1.f / m.unitsPerBlock;
			wireBox(b.from * inv, b.To() * inv, int(i) == s.collision ? Rgba(255, 220, 80) : Rgba(240, 210, 80, 150), int(i) == s.collision ? 2.f : 1.f);
		}

	// ── Gizmo ──
	FVector3 gc;
	std::array<FVector3, 3> axes;
	int hoverAxis = -1;
	float gx = 0, gy = 0, gz = 1;
	const bool gizmo = s.mode != Impl::Mode::Paint && SelectionFrame(s, pose, gc, axes) && cam.Project(gc, gx, gy, gz) &&
					   !(s.tool == Impl::Tool::Resize && s.box < 0 && s.collision < 0);
	std::array<FVector2, 3> axisScreen{};
	if (gizmo) {
		const float L = 70.f * gz / cam.focal; // longueur monde ≈ 70 px
		for (int a = 0; a < 3; ++a) {
			const FVector3 tip = gc + axes[size_t(a)] * L;
			float tx, ty, td;
			if (!cam.Project(tip, tx, ty, td))
				continue;
			axisScreen[size_t(a)] = FVector2{tx - gx, ty - gy} * (1.f / L);
			const bool hot = s.dragAxis == a;
			if (s.tool == Impl::Tool::Rotate) { // anneaux
				std::vector<float> pts;
				const FVector3 u1 = axes[size_t((a + 1) % 3)], u2 = axes[size_t((a + 2) % 3)];
				float best = 1e9f;
				float px = 0, py = 0;
				for (int k = 0; k <= 48; ++k) {
					const float ang = float(k) / 48.f * 2.f * std::numbers::pi_v<float>;
					float sx, sy, sd;
					if (!cam.Project(gc + (u1 * std::cos(ang) + u2 * std::sin(ang)) * L * 0.8f, sx, sy, sd))
						continue;
					if (k > 0)
						best = std::min(best, DistToSegment(u.MouseX(), u.MouseY(), px, py, sx, sy));
					pts.push_back(sx);
					pts.push_back(sy);
					px = sx;
					py = sy;
				}
				if (best < 7.f && s.dragAxis < 0 && u.MouseIn(r))
					hoverAxis = a;
				const sdl3::Color col = hot || hoverAxis == a ? Rgba(255, 230, 120) : kAxisColors[a];
				if (pts.size() >= 4)
					c.Polyline(pts.data(), pts.size() / 2, col, hot || hoverAxis == a ? 3.f : 2.f);
			} else {
				if (DistToSegment(u.MouseX(), u.MouseY(), gx, gy, tx, ty) < 8.f && s.dragAxis < 0 && u.MouseIn(r))
					hoverAxis = a;
				const sdl3::Color col = hot || hoverAxis == a ? Rgba(255, 230, 120) : kAxisColors[a];
				c.Line(gx, gy, tx, ty, col, 3.f);
				if (s.tool == Impl::Tool::Move)
					c.FillCircle(tx, ty, 6, col);
				else
					c.FillRect({tx - 5, ty - 5, 10, 10}, col);
			}
		}
		c.FillCircle(gx, gy, 4, Rgba(240, 240, 240));
	}

	// ── Interaction ──
	const bool inside = u.MouseIn(r);
	size_t hitQuad = 0;
	FVector2 hitUv{};
	if (inside && u.MousePressed(sdl3::MouseButton::Left)) {
		s.pressX = s.lastX = u.MouseX();
		s.pressY = s.lastY = u.MouseY();
		if (hoverAxis >= 0) {
			s.dragAxis = hoverAxis;
			s.dragX = u.MouseX();
			s.dragY = u.MouseY();
			s.dragStart = m;
			s.dragScreen = axisScreen[size_t(hoverAxis)];
			s.dragOffset = CurrentOffset(s, s.part);
		} else if (s.mode == Impl::Mode::Paint && s.image && HitTest(s, u.MouseX(), u.MouseY(), hitQuad, hitUv)) {
			s.stroke = true;
			s.Paint(int(std::floor(hitUv.x * float(s.image->pixels.GetWidth()))), int(std::floor(hitUv.y * frameScale * float(s.image->pixels.GetHeight()))), true);
		} else {
			s.pressQuad = HitTest(s, u.MouseX(), u.MouseY(), hitQuad, hitUv) ? int(hitQuad) : -1;
			s.pressed = true;
		}
	}
	if ((u.MousePressed(sdl3::MouseButton::Right) || u.MousePressed(sdl3::MouseButton::Middle)) && inside) {
		s.lastX = u.MouseX();
		s.lastY = u.MouseY();
		s.orbiting = u.MousePressed(sdl3::MouseButton::Right) && !u.Shift();
		s.panning = u.MousePressed(sdl3::MouseButton::Middle) || (u.MousePressed(sdl3::MouseButton::Right) && u.Shift());
	}
	if (s.dragAxis >= 0) {
		if (u.MouseDown(sdl3::MouseButton::Left))
			ApplyDrag(s, u.MouseX(), u.MouseY(), u.Shift(), u.Ctrl());
		else
			s.dragAxis = -1;
	} else if (s.stroke) {
		if (u.MouseDown(sdl3::MouseButton::Left)) {
			if (s.brush != Impl::Brush::Fill && HitTest(s, u.MouseX(), u.MouseY(), hitQuad, hitUv))
				s.Paint(int(std::floor(hitUv.x * float(s.image->pixels.GetWidth()))), int(std::floor(hitUv.y * frameScale * float(s.image->pixels.GetHeight()))),
						false);
		} else {
			s.stroke = false;
		}
	} else if (s.pressed) {
		if (u.MouseDown(sdl3::MouseButton::Left)) {
			const bool moved = std::fabs(u.MouseX() - s.pressX) + std::fabs(u.MouseY() - s.pressY) > 3.f;
			if (s.ppFace >= 0) {
				ApplyPushPull(s, u.MouseX(), u.MouseY()); // push/pull en cours
			} else if (moved && s.selMode == Impl::SelMode::Face && s.pressQuad >= 0) {
				const voxel::ModelQuad &q = s.quads[size_t(s.pressQuad)];
				if (q.box >= 0 && q.part == s.part) { // glisser une face : push/pull
					s.part = q.part;
					s.box = q.box;
					s.collision = -1;
					BeginPushPull(s, q, cam);
					ApplyPushPull(s, u.MouseX(), u.MouseY());
				} else {
					s.orbiting = true;
				}
			} else if (moved) {
				s.orbiting = true; // glisser sur le vide : orbite
			}
		} else {
			if (!s.orbiting && s.ppFace < 0 && s.mode != Impl::Mode::Paint)
				ClickSelect(s, u.MouseX(), u.MouseY(), u.Shift());
			s.ppFace = -1;
			s.pressQuad = -1;
			s.pressed = false;
			s.orbiting = false;
		}
	}
	if (s.orbiting || s.panning) {
		const float dx = u.MouseX() - s.lastX, dy = u.MouseY() - s.lastY;
		if (s.orbiting) {
			s.yaw -= dx * 0.01f;
			s.pitch = std::clamp(s.pitch + dy * 0.01f, -1.5f, 1.5f);
		} else {
			s.target = s.target - cam.right * (dx * s.distance * 0.0016f) + cam.up * (dy * s.distance * 0.0016f);
		}
		if (!u.MouseDown(sdl3::MouseButton::Left) && !u.MouseDown(sdl3::MouseButton::Right) && !u.MouseDown(sdl3::MouseButton::Middle)) {
			s.orbiting = s.panning = false;
		}
	}
	s.lastX = u.MouseX();
	s.lastY = u.MouseY();
	if (inside && u.WheelY() != 0)
		s.distance = std::clamp(s.distance * std::pow(0.87f, u.WheelY()), 0.05f, 400.f);
	if (inside && !u.WantsKeyboard()) {
		if (u.KeycodePressed(SDLK_W))
			s.tool = Impl::Tool::Move;
		if (u.KeycodePressed(SDLK_E) && s.mode != Impl::Mode::Animate)
			s.tool = Impl::Tool::Resize;
		if (u.KeycodePressed(SDLK_R))
			s.tool = Impl::Tool::Rotate;
		if (u.KeycodePressed(SDLK_X) && s.mode == Impl::Mode::Edit && s.box >= 0)
			s.extruding = !s.extruding;
		if (u.KeycodePressed(SDLK_F))
			s.Frame();
		if (u.KeycodePressed(SDLK_DELETE) && s.mode == Impl::Mode::Edit)
			DeleteSelection(s);
		if (u.KeycodePressed(SDLK_D) && u.Ctrl() && s.mode == Impl::Mode::Edit)
			DuplicateSelection(s);
	}
	if (inside && s.mode == Impl::Mode::Paint && s.image && HitTest(s, u.MouseX(), u.MouseY(), hitQuad, hitUv))
		u.Tooltip(std::format("texel {} {}", int(hitUv.x * float(s.image->pixels.GetWidth())), int(hitUv.y * frameScale * float(s.image->pixels.GetHeight()))));
	c.PopClip();

	// ── Ligne d'information ──
	const Rect info{area.x, r.Bottom(), area.w, 24};
	c.FillRect(info, Rgba(18, 21, 27));
	std::string text = std::format("{} · {} · {} partie(s) · {} cube(s) · {} triangle(s) · {:.2f}×{:.2f}×{:.2f} m", s.doc->entry.id, FormatLabel(m.format),
								   m.parts.size(), m.BoxCount(), m.TriangleCount(), bounds.Size().x, bounds.Size().y, bounds.Size().z);
	if (s.doc->dirty)
		text += " · modifié";
	const std::string help = s.mode == Impl::Mode::Paint ? "clic : peindre · glisser le vide : orbiter · molette : zoom"
													   : "clic : sélectionner · glisser : orbiter · milieu : déplacer · W/E/R · F";
	c.Text(info.x + 8, info.y + 5, c.Ellipsize(text, info.w * 0.62f, 12), th.textDim, 12);
	const float hw = std::min(c.MeasureText(help, 12), info.w * 0.36f);
	c.Text(info.Right() - hw - 8, info.y + 5, c.Ellipsize(help, hw + 1, 12), Rgba(110, 120, 140), 12);
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Peinture 2D (pixel art)
// ══════════════════════════════════════════════════════════════════════════

namespace {

/// Rectangles UV (texels) des boîtes de la partie sélectionnée : (rect, cube sélectionné).
std::vector<std::pair<voxel::UvRect, bool>> SelectedUvRects(Impl &s) {
	std::vector<std::pair<voxel::UvRect, bool>> out;
	const voxel::ModelPart *p = s.Part();
	if (!s.doc || !s.doc->model.boxUv || !p)
		return out;
	for (size_t bi = 0; bi < p->boxes.size(); ++bi)
		for (const voxel::UvRect &f : voxel::BoxUvLayout(p->boxes[bi]))
			out.push_back({f, int(bi) == s.box});
	return out;
}

} // namespace

bool ModelWorkshop::DrawPaint(ui::Context &u, const Rect &r, sdl3::Renderer &ren) {
	Impl &s = *m_impl;
	if (!s.image)
		return false;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	if (u.MouseIn(r) && u.MousePressed(sdl3::MouseButton::Left)) {
		m_focused = true;
		s.mode = Impl::Mode::Paint; // Ctrl+Z annule les traits
	}
	u.PushId("paint");
	sdl3::Surface &img = s.image->pixels;
	// ── Outils ──
	const float barH = 34;
	c.FillRect({r.x, r.y, r.w, barH}, Rgba(18, 21, 27));
	float x = r.x + 6;
	auto tool = [&](Impl::Brush b, char32_t icon, std::string_view tip) {
		const Rect br{x, r.y + 3, 28, 28};
		const bool on = s.brush == b;
		if (on)
			c.FillRoundRect(br, 5, Rgba(38, 92, 170));
		if (u.IconButton(br, icon, tip, true, on ? th.text : sdl3::Color{0, 0, 0, 0}))
			s.brush = b;
		x += 31;
	};
	tool(Impl::Brush::Pencil, icons::Codepoint(icons::MaterialIcons::EDIT), "Crayon (B)");
	tool(Impl::Brush::Eraser, icons::Codepoint(icons::MaterialIcons::CLEANING_SERVICES), "Gomme (E)");
	tool(Impl::Brush::Picker, icons::Codepoint(icons::MaterialIcons::COLORIZE), "Pipette (I)");
	tool(Impl::Brush::Fill, icons::Codepoint(icons::MaterialIcons::FORMAT_COLOR_FILL), "Remplissage (G)");
	for (int sz = 1; sz <= 3; ++sz) {
		if (u.ButtonAt({x, r.y + 5, 24, 24}, std::format("{}##size{}", sz, sz), true, s.brushSize == sz))
			s.brushSize = sz;
		x += 27;
	}
	x += 6;
	// Couleur courante + palette.
	const Rect sw{x, r.y + 5, 24, 24};
	c.FillRect(sw, Rgba(60, 64, 72));
	c.FillRect({sw.x, sw.y, 12, 12}, Rgba(44, 48, 56));
	c.FillRect({sw.x + 12, sw.y + 12, 12, 12}, Rgba(44, 48, 56));
	c.FillRect(sw, sdl3::Color{s.color[0], s.color[1], s.color[2], s.color[3]});
	c.StrokeRect(sw, th.text);
	x += 30;
	for (size_t i = 0; i < kDefaultPalette.size() && x + 16 < r.Right() - 140; ++i) {
		const auto &pc = kDefaultPalette[i];
		const Rect pr{x, r.y + 9, 15, 15};
		c.FillRect(pr, pc[3] ? sdl3::Color{pc[0], pc[1], pc[2], 255} : Rgba(60, 64, 72));
		if (!pc[3])
			c.Line(pr.x, pr.Bottom(), pr.Right(), pr.y, th.danger);
		u.PushId(uint64_t(i) + 77);
		if (u.Interact(u.MakeId("##pal"), pr).leftClicked)
			s.color = pc;
		u.PopId();
		x += 17;
	}
	if (u.ButtonAt({r.Right() - 132, r.y + 5, 126, 24}, s.image->dirty ? "Enregistrer PNG##savepng" : "Enregistré##savepng", s.image->dirty))
		Save();
	// Couleur exacte (RGBA).
	const Rect colorRow{r.x + 6, r.y + barH + 2, r.w - 12, 26};
	float rgba[4] = {float(s.color[0]), float(s.color[1]), float(s.color[2]), float(s.color[3])};
	const float cw = (colorRow.w - 36) / 4.f;
	static constexpr sdl3::Color kChan[4] = {sdl3::Color{226, 72, 72, 255}, sdl3::Color{86, 196, 92, 255}, sdl3::Color{76, 140, 236, 255},
											sdl3::Color{180, 180, 190, 255}};
	c.Text(colorRow.x, colorRow.y + 5, "RGBA", th.textDim, 12);
	for (int k = 0; k < 4; ++k)
		if (u.DragNumber({colorRow.x + 36 + float(k) * cw, colorRow.y, cw - 4, colorRow.h}, std::format("##c{}", k), rgba[k], 0.5f, "{:.0f}", kChan[k], 0.f,
						 255.f))
			s.color[size_t(k)] = uint8_t(std::round(rgba[k]));

	// ── Canevas ──
	const Rect view{r.x, r.y + barH + 30, r.w, r.h - barH - 30 - 24};
	c.FillRect(view, Rgba(14, 16, 21));
	const float fit = std::max(1.f, std::floor(std::min(view.w / float(img.GetWidth()), view.h / float(img.GetHeight())) * 0.92f));
	if (s.paintZoom <= 0)
		s.paintZoom = fit;
	const bool hover = u.MouseIn(view);
	if (hover && u.WheelY() != 0) {
		const float old = s.paintZoom;
		s.paintZoom = std::clamp(std::round(s.paintZoom * (u.WheelY() > 0 ? 1.25f : 0.8f)), 1.f, 96.f);
		const float mx = u.MouseX() - (view.x + view.w * 0.5f), my = u.MouseY() - (view.y + view.h * 0.5f);
		s.paintPanX = mx - (mx - s.paintPanX) * s.paintZoom / old;
		s.paintPanY = my - (my - s.paintPanY) * s.paintZoom / old;
	}
	if (hover && (u.MouseDown(sdl3::MouseButton::Right) || u.MouseDown(sdl3::MouseButton::Middle)) && !u.MousePressed(sdl3::MouseButton::Right) && !u.MousePressed(sdl3::MouseButton::Middle)) {
		s.paintPanX += u.MouseX() - s.lastX;
		s.paintPanY += u.MouseY() - s.lastY;
	}
	s.lastX = u.MouseX();
	s.lastY = u.MouseY();
	const float iw = float(img.GetWidth()) * s.paintZoom, ih = float(img.GetHeight()) * s.paintZoom;
	const Rect pic{view.x + (view.w - iw) * 0.5f + s.paintPanX, view.y + (view.h - ih) * 0.5f + s.paintPanY, iw, ih};
	c.PushClip(view);
	const float cell = std::max(8.f, s.paintZoom * 2);
	for (float yy = 0; yy < ih; yy += cell) // damier
		for (float xx = 0; xx < iw; xx += cell)
			c.FillRect({pic.x + xx, pic.y + yy, std::min(cell, iw - xx), std::min(cell, ih - yy)},
					   (int(xx / cell) + int(yy / cell)) % 2 ? Rgba(60, 64, 72) : Rgba(44, 48, 56));
	if (sdl3::Texture *t = s.ImageTexture(ren))
		c.Image(*t, pic);
	if (s.paintZoom >= 6) {
		for (int k = 0; k <= img.GetWidth(); ++k)
			c.Line(pic.x + float(k) * s.paintZoom, pic.y, pic.x + float(k) * s.paintZoom, pic.Bottom(), Rgba(255, 255, 255, k % 8 ? 18 : 50));
		for (int k = 0; k <= img.GetHeight(); ++k)
			c.Line(pic.x, pic.y + float(k) * s.paintZoom, pic.Right(), pic.y + float(k) * s.paintZoom, Rgba(255, 255, 255, k % 8 ? 18 : 50));
	}
	const float tz = s.paintZoom * s.TexelScale();
	for (const auto &[f, sel] : SelectedUvRects(s)) // dépliage de la partie sélectionnée
		c.StrokeRect({pic.x + std::min(f.u0, f.u1) * tz, pic.y + f.v0 * tz, std::fabs(f.u1 - f.u0) * tz, (f.v1 - f.v0) * tz},
					 sel ? Rgba(255, 170, 60) : Rgba(120, 170, 255, 160), sel ? 2.f : 1.f);
	std::string info = std::format("{} · {}×{} · zoom ×{:.0f}", s.image->path.filename().string(), img.GetWidth(), img.GetHeight(), s.paintZoom);
	if (hover && pic.Contains(u.MouseX(), u.MouseY())) {
		const int px = std::clamp(int((u.MouseX() - pic.x) / s.paintZoom), 0, img.GetWidth() - 1);
		const int py = std::clamp(int((u.MouseY() - pic.y) / s.paintZoom), 0, img.GetHeight() - 1);
		const uint8_t *p = &img.Pixels()[size_t(py * img.GetWidth() + px) * 4];
		info += std::format(" · ({}, {}) #{:02X}{:02X}{:02X} opacité {}", px, py, p[0], p[1], p[2], p[3]);
		c.StrokeRect({pic.x + float(px) * s.paintZoom, pic.y + float(py) * s.paintZoom, s.paintZoom * float(s.brushSize),
					  s.paintZoom * float(s.brushSize)},
					 th.accent, 1.5f);
		if (u.MousePressed(sdl3::MouseButton::Left)) {
			s.stroke = true;
			s.Paint(px, py, true);
		} else if (s.stroke && u.MouseDown(sdl3::MouseButton::Left) && s.brush != Impl::Brush::Fill) {
			s.Paint(px, py, false);
		}
	}
	if (!u.MouseDown(sdl3::MouseButton::Left))
		s.stroke = false;
	if (hover && !u.WantsKeyboard()) {
		if (u.KeycodePressed(SDLK_B))
			s.brush = Impl::Brush::Pencil;
		if (u.KeycodePressed(SDLK_E))
			s.brush = Impl::Brush::Eraser;
		if (u.KeycodePressed(SDLK_I))
			s.brush = Impl::Brush::Picker;
		if (u.KeycodePressed(SDLK_G))
			s.brush = Impl::Brush::Fill;
	}
	c.PopClip();
	c.FillRect({r.x, r.Bottom() - 24, r.w, 24}, Rgba(18, 21, 27));
	if (s.image->dirty)
		info += " · modifiée";
	c.Text(r.x + 8, r.Bottom() - 19, c.Ellipsize(info, r.w - 110, 12), th.textDim, 12);
	if (u.ButtonAt({r.Right() - 96, r.Bottom() - 22, 88, 20}, "Ajuster##fit")) {
		s.paintZoom = fit;
		s.paintPanX = s.paintPanY = 0;
	}
	u.PopId();
	return true;
}

// ══════════════════════════════════════════════════════════════════════════
// Éditeur UV
// ══════════════════════════════════════════════════════════════════════════

bool ModelWorkshop::DrawUv(ui::Context &u, const Rect &r, sdl3::Renderer &ren) {
	Impl &s = *m_impl;
	if (!s.doc)
		return false;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	if (u.MouseIn(r) && u.MousePressed(sdl3::MouseButton::Left))
		m_focused = true;
	voxel::Model &m = s.doc->model;
	const float tw = s.image ? float(s.image->pixels.GetWidth()) : float(m.textureWidth);
	const float thh = s.image ? float(s.image->pixels.GetHeight()) : float(m.textureHeight);
	const float zoom = std::max(0.5f, std::min((r.w - 16) / tw, (r.h - 70) / thh));
	const Rect pic{r.x + (r.w - tw * zoom) * 0.5f, r.y + 30, tw * zoom, thh * zoom};
	c.Text(r.x + 8, r.y + 7, m.boxUv ? std::format("Box UV · texture {}×{} (modèle {}×{})", int(tw), int(thh), m.textureWidth, m.textureHeight)
									 : std::string("UV par projection (position dans le bloc)"),
		   th.textDim, 12);
	c.FillRect(pic, Rgba(36, 40, 48));
	if (sdl3::Texture *t = s.ImageTexture(ren))
		c.Image(*t, pic);
	c.StrokeRect(pic, th.panelBorder);
	// Box UV : un texel du modèle = TexelScale() pixels d'image (même échelle en X et Y).
	// `sy` est borné par `sx` : si l'image est plus courte que la texture (ex. avatar 64×64
	// avec une skin 64×32), les boîtes des parties basses ne débordent plus sous l'image.
	const float sx = m.boxUv ? pic.w / float(m.textureWidth) : pic.w;
	const float sy = m.boxUv ? std::min(sx, pic.h / float(m.textureHeight)) : pic.h;
	if (m.boxUv) {
		// Toutes les boîtes (atténuées), celles de la partie sélectionnée en évidence.
		for (size_t pi = 0; pi < m.parts.size(); ++pi)
			for (size_t bi = 0; bi < m.parts[pi].boxes.size(); ++bi) {
				const bool selPart = int(pi) == s.part, selBox = selPart && int(bi) == s.box;
				for (const voxel::UvRect &f : voxel::BoxUvLayout(m.parts[pi].boxes[bi])) {
					const Rect fr{pic.x + std::min(f.u0, f.u1) * sx, pic.y + f.v0 * sy, std::fabs(f.u1 - f.u0) * sx, (f.v1 - f.v0) * sy};
					if (selBox)
						c.FillRect(fr, Rgba(255, 170, 60, 50));
					c.StrokeRect(fr, selBox ? Rgba(255, 170, 60) : selPart ? Rgba(120, 170, 255, 200) : Rgba(255, 255, 255, 40), selBox ? 2.f : 1.f);
				}
			}
		// Glisser le dépliage du cube sélectionné (pas d'un texel).
		static bool dragging = false;
		static FVector2 startUv{};
		static float startX = 0, startY = 0;
		voxel::ModelBox *b = s.Box();
		const auto it = u.Interact(u.MakeId("##uvdrag"), pic);
		if (b && it.held && u.MousePressed(sdl3::MouseButton::Left)) {
			dragging = true;
			startUv = b->uv;
			startX = u.MouseX();
			startY = u.MouseY();
		}
		if (dragging && b && u.MouseDown(sdl3::MouseButton::Left)) {
			const FVector2 uv{std::round(startUv.x + (u.MouseX() - startX) / sx), std::round(startUv.y + (u.MouseY() - startY) / sy)};
			if (uv.x != b->uv.x || uv.y != b->uv.y) {
				s.Changed();
				b->uv = {std::max(0.f, uv.x), std::max(0.f, uv.y)};
			}
		}
		if (!u.MouseDown(sdl3::MouseButton::Left))
			dragging = false;
		c.Text(r.x + 8, pic.Bottom() + 6, b ? "glisser : déplacer le dépliage du cube sélectionné" : "sélectionnez un cube pour déplacer son dépliage",
			   th.textDim, 12);
	} else {
		// Projection : faces des cubes de la partie sélectionnée dans la tuile.
		std::vector<voxel::ModelQuad> quads;
		voxel::BuildQuads(m, {}, quads);
		if (s.blockAtlas)
			for (voxel::ModelQuad &q : quads)
				if (q.box >= 0 && q.face < 6) {
					const math::FVector2 cell = s.blockAtlas->cells[q.face];
					for (int k = 0; k < 4; ++k)
						q.uv[k] = {cell.x + q.uv[k].x * s.blockAtlas->cellSize.x, cell.y + q.uv[k].y * s.blockAtlas->cellSize.y};
				}
		const float fs = s.FrameScale();
		for (const voxel::ModelQuad &q : quads) {
			if (q.part != s.part || (s.box >= 0 && q.box != s.box) || (q.box < 0 && quads.size() > 4000))
				continue;
			float pts[10];
			for (int k = 0; k < 5; ++k) {
				pts[k * 2] = pic.x + q.uv[k % 4].x * pic.w;
				pts[k * 2 + 1] = pic.y + q.uv[k % 4].y * fs * pic.h;
			}
			c.Polyline(pts, 5, q.box == s.box && s.box >= 0 ? Rgba(255, 170, 60) : Rgba(120, 170, 255, 170), 1.f);
		}
	}
	return true;
}

// ══════════════════════════════════════════════════════════════════════════
// Timeline d'animation
// ══════════════════════════════════════════════════════════════════════════

void ModelWorkshop::DrawAnimation(ui::Context &u, const Rect &r) {
	Impl &s = *m_impl;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	if (u.MouseIn(r) && u.MousePressed(sdl3::MouseButton::Left))
		m_focused = true;
	if (!s.doc)
		return Hint(u, r, "Ouvrez un modèle : ses animations (images clés de rotation et de position par partie) se règlent ici.");
	voxel::Model &m = s.doc->model;
	u.PushId("anim");
	// ── Barre : animation, lecture, clés ──
	const float barH = 34;
	c.FillRect({r.x, r.y, r.w, barH}, Rgba(18, 21, 27));
	float x = r.x + 6;
	std::vector<std::string> names;
	for (const voxel::Animation &a : m.animations)
		names.push_back(a.name);
	names.emplace_back("(repos)");
	std::vector<std::string_view> views(names.begin(), names.end());
	int choice = s.anim < 0 ? int(m.animations.size()) : s.anim;
	if (u.Choice({x, r.y + 4, 170, 26}, "##anim", choice, views)) {
		s.anim = choice >= int(m.animations.size()) ? -1 : choice;
		s.time = 0;
	}
	x += 176;
	if (u.IconButton({x, r.y + 3, 28, 28}, icons::Codepoint(icons::MaterialIcons::ADD), "Nouvelle animation")) {
		s.Changed();
		std::string name;
		for (int i = 1;; ++i) {
			name = i == 1 ? std::string("animation") : std::format("animation{}", i);
			if (std::ranges::none_of(m.animations, [&](const voxel::Animation &a) { return a.name == name; }))
				break;
		}
		m.animations.push_back({name, 1.f, true, {}});
		s.anim = int(m.animations.size()) - 1;
		s.time = 0;
		s.mode = Impl::Mode::Animate;
	}
	x += 31;
	voxel::Animation *anim = s.Anim();
	if (u.IconButton({x, r.y + 3, 28, 28}, icons::Codepoint(icons::MaterialIcons::DELETE_OUTLINE), "Supprimer l'animation", anim != nullptr)) {
		s.Changed();
		m.animations.erase(m.animations.begin() + s.anim);
		s.anim = -1;
		anim = nullptr;
	}
	x += 34;
	if (u.IconButton({x, r.y + 3, 28, 28}, s.playing ? icons::Codepoint(icons::MaterialIcons::PAUSE) : icons::Codepoint(icons::MaterialIcons::PLAY_ARROW), s.playing ? "Pause" : "Lire", anim != nullptr))
		s.playing = !s.playing;
	x += 31;
	if (u.IconButton({x, r.y + 3, 28, 28}, icons::Codepoint(icons::MaterialIcons::REPLAY), "Revenir au début", anim != nullptr))
		s.time = 0;
	x += 34;
	if (anim) {
		float len = anim->length;
		c.Text(x, r.y + 10, "durée", th.textDim, 12);
		if (u.DragNumber({x + 40, r.y + 5, 64, 24}, "##len", len, 0.01f, "{:.2f} s", {}, 0.05f, 120.f)) {
			s.Changed();
			anim->length = len;
		}
		x += 110;
		bool loop = anim->loop;
		if (u.ButtonAt({x, r.y + 5, 70, 24}, loop ? "boucle##loop" : "une fois##loop", true, loop)) {
			s.Changed();
			anim->loop = !loop;
		}
		x += 76;
	}
	if (u.ButtonAt({x, r.y + 5, 110, 24}, "Mouvements##motion", true, s.motion))
		s.motion = !s.motion;
	if (u.MouseIn({x, r.y + 5, 110, 24}))
		u.Tooltip("Aperçu des mouvements procéduraux du rig (walk, idle)");
	x += 116;
	voxel::ModelPart *p = s.Part();
	if (u.ButtonAt({x, r.y + 5, 92, 24}, "+ Clé##key", p != nullptr)) {
		KeyCurrent(s, CurrentOffset(s, s.part), true, true);
		s.mode = Impl::Mode::Animate;
		anim = s.Anim();
	}
	x += 98;
	const std::string tl = anim ? std::format("{:.2f} / {:.2f} s", s.time, anim->length) : std::string("pose de repos");
	c.Text(r.Right() - c.MeasureText(tl, 13) - 10, r.y + 9, tl, th.text, 13);

	// ── Pistes ──
	const float nameW = 150, rulerH = 22, rowH = 22;
	const Rect tracks{r.x, r.y + barH, r.w, r.h - barH};
	const Rect lane{tracks.x + nameW, tracks.y, tracks.w - nameW - 12, tracks.h};
	c.FillRect(tracks, Rgba(14, 16, 21));
	const float length = anim ? anim->length : 1.f;
	auto timeToX = [&](float t) { return lane.x + 8 + (lane.w - 16) * (t / length); };
	auto xToTime = [&](float px) { return std::clamp((px - lane.x - 8) / (lane.w - 16) * length, 0.f, length); };
	// Règle.
	c.FillRect({lane.x, lane.y, lane.w, rulerH}, Rgba(22, 25, 32));
	const float step = length <= 1.5f ? 0.1f : length <= 4.f ? 0.25f : length <= 10.f ? 0.5f : 1.f;
	for (int k = 0; float(k) * step <= length + 1e-4f; ++k) {
		const float t = float(k) * step;
		const float tx = timeToX(t);
		const bool major = k % (step < 0.2f ? 5 : 2) == 0;
		c.Line(tx, lane.y + (major ? 6 : 13), tx, lane.Bottom(), Rgba(255, 255, 255, major ? 26 : 12));
		if (major)
			c.Text(tx + 3, lane.y + 3, std::format("{:g}", std::round(t * 100) / 100), th.textDim, 11);
	}
	const auto rulerIt = u.Interact(u.MakeId("##ruler"), {lane.x, lane.y, lane.w, rulerH});
	if (rulerIt.held && anim) {
		s.time = xToTime(u.MouseX());
		s.playing = false;
		s.mode = Impl::Mode::Animate;
	}
	// Lignes des parties et clés.
	for (size_t pi = 0; pi < m.parts.size(); ++pi) {
		const float y = tracks.y + rulerH + float(pi) * rowH;
		if (y + rowH > tracks.Bottom())
			break;
		const Rect row{tracks.x, y, tracks.w, rowH};
		const bool sel = int(pi) == s.part;
		if (sel)
			c.FillRect(row, Rgba(38, 92, 170, 70));
		else if (pi % 2)
			c.FillRect(row, Rgba(255, 255, 255, 6));
		u.PushId(uint64_t(pi) + 300);
		if (u.Interact(u.MakeId("##name"), {row.x, row.y, nameW, row.h}).leftClicked) {
			s.part = int(pi);
			s.box = -1;
			s.collision = -1;
		}
		u.TextIn({row.x + 8, row.y, nameW - 12, row.h}, m.parts[pi].name, {sel ? th.text : th.textDim, 12});
		if (anim)
			if (voxel::AnimationTrack *track = anim->Track(m.parts[pi].name, false)) {
				auto keys = [&](std::vector<voxel::Keyframe> &list, sdl3::Color col, float dy, bool rotation) {
					for (size_t k = 0; k < list.size(); ++k) {
						const float kx = timeToX(list[k].time), ky = row.y + row.h * 0.5f + dy;
						const Rect kr{kx - 5, ky - 5, 10, 10};
						u.PushId(uint64_t(k) * 2 + (rotation ? 1 : 0) + 9000);
						const auto kit = u.Interact(u.MakeId("##key"), kr);
						if (rotation)
							c.FillCircle(kx, ky, kit.hovered ? 5.5f : 4.5f, col);
						else
							c.FillRect({kx - 4, ky - 4, 8, 8}, col);
						if (kit.hovered) {
							u.Tooltip(std::format("{} à {:.2f} s : {:.1f} {:.1f} {:.1f} · clic droit : supprimer", rotation ? "rotation" : "position", list[k].time,
												  list[k].value.x, list[k].value.y, list[k].value.z));
							if (kit.rightClicked) {
								s.Changed();
								list.erase(list.begin() + std::ptrdiff_t(k));
								u.PopId();
								break;
							}
						}
						if (kit.leftClicked) {
							s.time = list[k].time;
							s.part = int(pi);
							s.playing = false;
							s.mode = Impl::Mode::Animate;
						}
						u.PopId();
					}
				};
				keys(track->rotation, Rgba(236, 160, 60), -3.f, true);
				keys(track->position, Rgba(86, 200, 220), 3.f, false);
			}
		u.PopId();
	}
	if (m.parts.empty())
		c.Text(lane.x + 10, tracks.y + rulerH + 6, "aucune partie", th.textDim, 12);
	// Tête de lecture.
	if (anim) {
		const float px = timeToX(s.time);
		c.Line(px, lane.y, px, tracks.Bottom(), th.accent, 2.f);
		c.FillRect({px - 5, lane.y, 10, 6}, th.accent);
	} else {
		c.Text(lane.x + 10, tracks.Bottom() - 22, "Créez une animation (+) puis posez des clés : « + Clé » ou glisser le gizmo en mode Animation.",
			   th.textDim, 12);
	}
	u.PopId();
}

} // namespace voxelia::studio
