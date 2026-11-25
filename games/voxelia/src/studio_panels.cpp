// Voxelia Studio — espaces de travail, World Outliner, propriétés d'entité, mods.
#include "studio_panels.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include <sdl3/sdl3.hpp>

#include <core/robin_map.hpp>
#include <dataio/json.hpp>

namespace voxelia::studio {

namespace icons = ui::icons;
using Rect = sdl3::FRect;
using sdl3::Rgba;

// ══════════════════════════════════════════════════════════════════════════
// Panneaux et espaces de travail
// ══════════════════════════════════════════════════════════════════════════

const std::vector<ui::DockTab> &PanelCatalog() {
	static const std::vector<ui::DockTab> catalog = {
		{"explorer", "Explorateur", icons::Codepoint(icons::MaterialIcons::FOLDER)},
		{"outliner", "Outliner", icons::Codepoint(icons::MaterialIcons::LIST)},
		{"mods", "Mods", icons::Codepoint(icons::MaterialIcons::EXTENSION)},
		{"viewport", "Vue 3D", icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR)},
		{"script", "Script", icons::Codepoint(icons::MaterialIcons::CODE)},
		{"paint", "Peinture", icons::Codepoint(icons::MaterialIcons::BRUSH)},
		{"properties", "Propriétés", icons::Codepoint(icons::MaterialIcons::TUNE)},
		{"world", "Monde", icons::Codepoint(icons::MaterialIcons::PUBLIC)},
		{"texture", "Textures", icons::Codepoint(icons::MaterialIcons::PALETTE)},
		{"uv", "UV", icons::Codepoint(icons::MaterialIcons::GRID_ON)},
		{"console", "Console", icons::Codepoint(icons::MaterialIcons::TERMINAL)},
		{"nodal", "Mécanismes", icons::Codepoint(icons::MaterialIcons::BUILD)},
		{"perf", "Performances", icons::Codepoint(icons::MaterialIcons::SPEED)},
		// Phase 6 : génération du monde.
		{"worldgen", "Générateur", icons::Codepoint(icons::MaterialIcons::TERRAIN)},
		{"worldmap", "Carte", icons::Codepoint(icons::MaterialIcons::PUBLIC)},
		{"sandbox", "Banc d'essai", icons::Codepoint(icons::MaterialIcons::PLAY_ARROW)},
		// Phase 2 : atelier de modélisation.
		{"models", "Modèles", icons::Codepoint(icons::MaterialIcons::CATEGORY)},
		{"model", "Modèle 3D", icons::Codepoint(icons::MaterialIcons::TRANSFORM)},
		{"parts", "Arborescence", icons::Codepoint(icons::MaterialIcons::LAYERS)},
		{"element", "Élément", icons::Codepoint(icons::MaterialIcons::TUNE)},
		{"animation", "Animation", icons::Codepoint(icons::MaterialIcons::ANIMATION)},
	};
	return catalog;
}

namespace {

constexpr int kLayoutVersion = 4; // 2 : atelier de modélisation ; 3 : performances ; 4 : génération (phase 6)

ui::DockRegionState Region(std::vector<std::string> tabs, std::string active, float size, bool visible = true) {
	return {std::move(tabs), std::move(active), visible, size};
}

Workspace MakeWorkspace(std::string id, std::string title, char32_t icon, std::string tooltip, ui::DockState layout) {
	Workspace w{std::move(id), std::move(title), icon, std::move(tooltip), layout, layout};
	return w;
}

// Écriture / lecture d'une disposition en JSON.
dataio::NodePtr ToJson(const ui::DockState &state) {
	dataio::NodePtr regions = dataio::Node::MakeArray();
	for (const ui::DockRegionState &r : state.regions) {
		dataio::NodePtr tabs = dataio::Node::MakeArray();
		for (const std::string &t : r.tabs)
			tabs->Push(dataio::Node::MakeString(t));
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("tabs", std::move(tabs));
		o->Set("active", dataio::Node::MakeString(r.active));
		o->Set("visible", dataio::Node::MakeBool(r.visible));
		o->Set("size", dataio::Node::MakeFloat(double(r.size)));
		regions->Push(std::move(o));
	}
	return regions;
}

bool FromJson(const dataio::NodePtr &regions, ui::DockState &out) {
	if (!regions->IsArray() || regions->Size() != out.regions.size())
		return false;
	for (size_t i = 0; i < out.regions.size(); ++i) {
		const dataio::NodePtr &o = (*regions)[i];
		ui::DockRegionState r;
		for (const dataio::NodePtr &t : (*o)["tabs"]->AsArray())
			r.tabs.emplace_back(t->AsString());
		r.active = std::string((*o)["active"]->AsString());
		r.visible = (*o)["visible"]->AsBool(true);
		r.size = (*o)["size"]->AsFloat(280.f);
		out.regions[i] = std::move(r);
	}
	return true;
}

} // namespace

std::vector<Workspace> DefaultWorkspaces() {
	using R = ui::DockState;
	std::vector<Workspace> w;
	w.push_back(MakeWorkspace("world", "Monde en jeu", icons::Codepoint(icons::MaterialIcons::PUBLIC), "Le jeu dans la vue 3D : entités vivantes, propriétés, pause / pas à pas",
							  R{{Region({"outliner", "mods"}, "outliner", 270), Region({"viewport", "script"}, "viewport", 0),
								 Region({"properties", "world"}, "properties", 340), Region({"console", "nodal", "perf"}, "console", 180)}}));
	w.push_back(MakeWorkspace("modeling", "Modélisation", icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR),
							  "Formes 3D des mods (nodebox, rig, obj, b3d) : cubes, pivots, collision, animation",
							  R{{Region({"models", "parts", "explorer"}, "models", 270), Region({"model", "viewport", "script"}, "model", 0),
								 Region({"element", "uv", "parts"}, "element", 330), Region({"animation", "console"}, "animation", 190)}}));
	w.push_back(MakeWorkspace("painting", "Peinture 2D", icons::Codepoint(icons::MaterialIcons::BRUSH), "Pixel art : texture du modèle ou d'un bloc, palette, UV, peinture sur les faces",
							  R{{Region({"texture", "models"}, "texture", 270), Region({"paint", "model"}, "paint", 0), Region({"uv", "element"}, "uv", 320),
								 Region({"console"}, "console", 150, false)}}));
	w.push_back(MakeWorkspace("scripting", "Scripting", icons::Codepoint(icons::MaterialIcons::CODE), "Scripts des mods : édition, vérification, rechargement à chaud",
							  R{{Region({"explorer", "mods"}, "explorer", 260), Region({"script"}, "script", 0), Region({"perf", "world"}, "perf", 340),
								 Region({"console", "nodal"}, "console", 220)}}));
	w.push_back(MakeWorkspace("entities", "Propriétés d'entités", icons::Codepoint(icons::MaterialIcons::TUNE), "Sélection d'entités et édition de leurs composants",
							  R{{Region({"outliner"}, "outliner", 280), Region({"viewport"}, "viewport", 0), Region({"properties"}, "properties", 380),
								 Region({"nodal", "console"}, "nodal", 200)}}));
	w.push_back(MakeWorkspace("worldgen", "Génération", icons::Codepoint(icons::MaterialIcons::TERRAIN), "Relief, climat, biomes, structures ; carte ; banc d'essai",
							  R{{Region({"worldgen"}, "worldgen", 360), Region({"worldmap", "viewport"}, "worldmap", 0),
								 Region({"sandbox", "mods"}, "sandbox", 320), Region({"console", "perf"}, "console", 160)}}));
	return w;
}

bool LoadWorkspaces(const fs::path &file, std::vector<Workspace> &workspaces, std::string &active) {
	auto json = dataio::LoadJsonFile(file.string());
	if (!json)
		return false;
	const dataio::NodePtr &root = json.Value();
	if ((*root)["version"]->AsInt(1) < kLayoutVersion) // dispositions d'une version antérieure : défauts
		return false;
	if (const std::string_view a = (*root)["active"]->AsString(); !a.empty())
		active = std::string(a);
	for (const dataio::NodePtr &w : (*root)["workspaces"]->AsArray()) {
		const std::string_view id = (*w)["id"]->AsString();
		for (Workspace &ws : workspaces)
			if (ws.id == id) {
				ui::DockState state;
				if (FromJson((*w)["regions"], state))
					ws.layout = std::move(state);
			}
	}
	return true;
}

bool SaveWorkspaces(const fs::path &file, const std::vector<Workspace> &workspaces, std::string_view active) {
	dataio::NodePtr list = dataio::Node::MakeArray();
	for (const Workspace &ws : workspaces) {
		dataio::NodePtr o = dataio::Node::MakeObject();
		o->Set("id", dataio::Node::MakeString(ws.id));
		o->Set("regions", ToJson(ws.layout));
		list->Push(std::move(o));
	}
	dataio::NodePtr root = dataio::Node::MakeObject();
	root->Set("version", dataio::Node::MakeInt(int64_t(kLayoutVersion)));
	root->Set("active", dataio::Node::MakeString(std::string(active)));
	root->Set("workspaces", std::move(list));
	std::error_code ec;
	fs::create_directories(file.parent_path(), ec);
	return dataio::SaveJsonFile(file.string(), root);
}

// ══════════════════════════════════════════════════════════════════════════
// World Outliner
// ══════════════════════════════════════════════════════════════════════════

namespace {

constexpr float kDistances[] = {0.f, 16.f, 32.f, 64.f, 128.f, 256.f};
constexpr std::string_view kDistanceLabels[] = {"partout", "16 m", "32 m", "64 m", "128 m", "256 m"};

char32_t CategoryIcon(voxel::EntityCategory c) {
	switch (c) {
	case voxel::EntityCategory::Player: return icons::Codepoint(icons::MaterialIcons::PERSON);
	case voxel::EntityCategory::Creature: return icons::Codepoint(icons::MaterialIcons::PETS);
	case voxel::EntityCategory::Item: return icons::Codepoint(icons::MaterialIcons::STORAGE);
	case voxel::EntityCategory::Projectile: return icons::Codepoint(icons::MaterialIcons::ARROW_FORWARD);
	default: return icons::Codepoint(icons::MaterialIcons::CIRCLE);
	}
}

sdl3::Color CategoryColor(const ui::Theme &th, voxel::EntityCategory c) {
	switch (c) {
	case voxel::EntityCategory::Player: return th.info;
	case voxel::EntityCategory::Creature: return th.warning;
	case voxel::EntityCategory::Item: return th.success;
	case voxel::EntityCategory::Projectile: return th.danger;
	default: return th.textDim;
	}
}

} // namespace

std::optional<OutlinerEvent> DrawOutliner(ui::Context &u, const Rect &r, OutlinerState &st, Game &game, ecs::Entity selected,
										  const math::FVector3 &from, uint64_t now) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	std::optional<OutlinerEvent> event;
	u.PushId("outliner");

	// ── Filtres ──
	const Rect search{r.x + 8, r.y + 8, r.w - 16, 28};
	if (u.TextInputAt(search, "##filter", st.text, nullptr, "Filtrer par identifiant…"))
		st.nextRefresh = 0;
	static constexpr std::string_view kCats[] = {"Toutes", "Joueurs", "Créatures", "Objets", "Projectiles", "Autres"};
	const float half = (r.w - 20) * 0.5f;
	if (u.Choice({r.x + 8, r.y + 42, half, 26}, "##cat", st.category, kCats))
		st.nextRefresh = 0;
	if (u.Choice({r.x + 12 + half, r.y + 42, half, 26}, "##dist", st.distance, kDistanceLabels))
		st.nextRefresh = 0;

	// ── Collecte (5 Hz) : lecture directe de l'ECS vivant ──
	if (now >= st.nextRefresh) {
		st.nextRefresh = now + 200;
		voxel::OutlinerFilter f;
		f.text = st.text;
		if (st.category > 0)
			f.category = voxel::EntityCategory(st.category - 1);
		f.maxDistance = kDistances[std::clamp(st.distance, 0, int(std::size(kDistances)) - 1)];
		st.cache = voxel::CollectEntities(game.Ecs(), game.Player(), f, from, 20000);
	}
	const voxel::OutlinerResult &res = st.cache;
	size_t shown = 0;
	for (size_t n : res.perCategory)
		shown += n;
	const std::string count = std::format("{} / {}", shown, res.total);
	c.Text(r.Right() - 10 - c.MeasureText(count, 12), r.y + 76, count, th.textDim, 12);
	c.Text(r.x + 10, r.y + 76, "ENTITÉS DU MONDE", th.textDim, 12, sdl3::CanvasFont::Bold);

	// ── Lignes : en-têtes de catégorie + entités (virtualisées) ──
	struct Line {
		int entry = -1; // -1 : en-tête de catégorie
		voxel::EntityCategory category;
	};
	std::vector<Line> lines;
	lines.reserve(res.entries.size() + 8);
	for (size_t i = 0; i < res.entries.size(); ++i) {
		if (i == 0 || res.entries[i].category != res.entries[i - 1].category)
			lines.push_back({-1, res.entries[i].category});
		lines.push_back({int(i), res.entries[i].category});
	}
	const ui::ScrollbarOptions bars{};
	const Rect area{r.x + 4, r.y + 96, r.w - 8, r.h - 100};
	const Rect view{area.x, area.y, area.w - bars.thickness, area.h};
	constexpr float kRowH = 24;
	const float contentH = float(lines.size()) * kRowH + 4;
	const float maxY = std::max(0.f, contentH - view.h);
	float dummy = 0;
	u.ScrollInput(view, dummy, st.scrollY, 0.f, maxY, kRowH * 3);
	u.ScrollbarV("##scroll", {view.Right(), view.y, bars.thickness, view.h}, st.scrollY, contentH, view.h, bars);
	st.scrollY = std::clamp(st.scrollY, 0.f, maxY);
	if (lines.empty())
		c.TextCentered({view.x, view.y, view.w, 60}, res.total ? "Aucune entité ne correspond au filtre." : "Aucune entité chargée.", th.textDim, 13);
	c.PushClip(view);
	const size_t first = size_t(st.scrollY / kRowH);
	for (size_t li = first; li < lines.size(); ++li) {
		const float y = view.y + float(li) * kRowH - st.scrollY;
		if (y > view.Bottom())
			break;
		const Line &line = lines[li];
		const Rect row{view.x, y, view.w, kRowH};
		if (line.entry < 0) { // en-tête de catégorie
			const std::string head = std::format("{} · {}", voxel::EntityCategoryLabel(line.category), res.perCategory[size_t(line.category)]);
			u.Icon({row.x + 2, row.y, 20, row.h}, CategoryIcon(line.category), CategoryColor(th, line.category), 15);
			c.Text(row.x + 26, row.y + (row.h - c.LineHeight(12, sdl3::CanvasFont::Bold)) * 0.5f, head, th.text, 12, sdl3::CanvasFont::Bold);
			continue;
		}
		const voxel::OutlinerEntry &e = res.entries[size_t(line.entry)];
		u.PushId(e.entity.Bits());
		const auto it = u.Interact(u.MakeId("##row"), row);
		const bool isSel = e.entity == selected;
		if (isSel)
			c.FillRoundRect(row, 4, Rgba(36, 72, 40, 230));
		else if (it.hovered)
			c.FillRoundRect(row, 4, th.widgetHover);
		u.Icon({row.x + 14, row.y, 20, row.h}, CategoryIcon(e.category), CategoryColor(th, e.category), 14);
		const std::string dist = std::format("{:.1f} m", e.distance);
		const float dw = c.MeasureText(dist, 12);
		u.TextIn({row.x + 38, row.y, row.w - 46 - dw, row.h}, e.kind, {isSel ? th.text : sdl3::Color{205, 210, 220, 255}, 13});
		c.Text(row.Right() - 6 - dw, row.y + (row.h - c.LineHeight(12)) * 0.5f, dist, th.textDim, 12);
		if (it.leftClicked) {
			const bool dbl = st.lastClickBits == e.entity.Bits() && now - st.lastClickTicks < 400;
			event = OutlinerEvent{e.entity, dbl};
			st.lastClickBits = e.entity.Bits();
			st.lastClickTicks = now;
		}
		if (it.hovered)
			u.Tooltip(std::format("{} · {:.1f} {:.1f} {:.1f} · double-clic : cadrer", e.kind, e.position.x, e.position.y, e.position.z));
		u.PopId();
	}
	c.PopClip();
	u.PopId();
	return event;
}

// ══════════════════════════════════════════════════════════════════════════
// Propriétés d'entité
// ══════════════════════════════════════════════════════════════════════════

namespace {

std::string EntityLabel(ecs::World &w, ecs::Entity e) {
	const auto *k = w.Get<voxel::Kind>(e);
	return std::format("{} #{}", k ? k->id : std::string("entité"), e.index);
}

/// Réglages d'un joint (partagés par les propriétés et l'éditeur de mécanismes).
/// Retourne faux si le joint doit être supprimé.
bool JointEditor(ui::Context &u, voxel::Joint &j, std::string_view target) {
	const ui::Theme &th = u.Style();
	static constexpr std::string_view kTypes[] = {"Attache", "Charnière", "Glissière", "Poulie", "Corde"};
	u.Row({3, 1}, 26);
	int type = int(j.type);
	if (u.Choice(u.Next(), "##type", type, kTypes)) {
		j.type = voxel::JointType(type);
		j.initialized = false; // nouvel état de référence
		j.position = j.velocity = 0;
	}
	const bool keep = !u.Button("Supprimer##joint");
	u.Label(std::format("cible : {} · position {:.2f}{}", target, j.position, j.type == voxel::JointType::Hinge ? "°" : " m"), th.textDim, 12);
	float anchor[3] = {j.anchor.x, j.anchor.y, j.anchor.z};
	if (u.DragFloats("Ancre (repère du porteur)", anchor, 0.02f, "{:.2f}")) {
		j.anchor = {anchor[0], anchor[1], anchor[2]};
		j.initialized = false;
	}
	if (j.type == voxel::JointType::Slider) {
		float axis[3] = {j.axis.x, j.axis.y, j.axis.z};
		if (u.DragFloats("Axe de glissement", axis, 0.02f, "{:.2f}"))
			j.axis = {axis[0], axis[1], axis[2]};
	}
	if (j.type == voxel::JointType::Pulley) {
		float rl[2] = {j.ratio, j.length};
		if (u.DragFloats("Rapport · longueur du câble", rl, 0.01f, "{:.2f}")) {
			j.ratio = std::max(0.05f, rl[0]);
			j.length = rl[1];
		}
	}
	if (j.type == voxel::JointType::Rope) {
		float len[1] = {j.length};
		if (u.DragFloats("Longueur de la corde (m)", len, 0.02f, "{:.2f}", 0.f, 256.f))
			j.length = len[0];
	}
	if (j.type != voxel::JointType::Attach && j.type != voxel::JointType::Rope) {
		float lim[2] = {j.minLimit, j.maxLimit};
		if (u.DragFloats(j.type == voxel::JointType::Hinge ? "Limites (°, min < max)" : j.type == voxel::JointType::Pulley ? "Limites de hauteur (y)" : "Limites (m)",
						 lim, j.type == voxel::JointType::Hinge ? 1.f : 0.05f, "{:.2f}")) {
			j.minLimit = lim[0];
			j.maxLimit = lim[1];
		}
		u.Checkbox("Moteur", j.motor);
		if (j.motor) {
			float mv[2] = {j.motorSpeed, j.motorForce};
			if (u.DragFloats(j.type == voxel::JointType::Hinge ? "Vitesse (°/s) · force (N)" : "Vitesse (m/s) · force (N)", mv, 0.05f, "{:.2f}")) {
				j.motorSpeed = mv[0];
				j.motorForce = std::max(0.f, mv[1]);
			}
			u.Row({1, 1, 1}, 26);
			if (u.Button("Inverser"))
				j.motorSpeed = -j.motorSpeed;
			if (u.Button("Arrêt"))
				j.motorSpeed = 0;
			if (u.Button("Relancer"))
				j.motorSpeed = j.motorSpeed == 0 ? 1.f : j.motorSpeed;
		}
	}
	return keep;
}

void MechanicsSections(ui::Context &u, Game &game, ecs::Entity e, const std::vector<std::string> &sounds) {
	const ui::Theme &th = u.Style();
	ecs::World &w = game.Ecs();
	// ── Éclairage ──
	u.Section(icons::Codepoint(icons::MaterialIcons::LIGHTBULB), "Éclairage");
	bool emits = w.Has<voxel::LightEmitter>(e);
	if (u.Checkbox("Émettre de la lumière", emits)) {
		if (emits)
			w.Add(e, voxel::LightEmitter{});
		else
			w.Remove<voxel::LightEmitter>(e);
	}
	if (auto *l = w.Get<voxel::LightEmitter>(e)) {
		u.SliderFloat("Intensité", l->intensity, 0.f, 4.f, "{:.2f}");
		u.SliderFloat("Portée (blocs)", l->range, 1.f, 32.f, "{:.1f}");
		float col[3] = {l->color.x * 255.f, l->color.y * 255.f, l->color.z * 255.f};
		if (u.DragFloats("Couleur (R, V, B)", col, 1.f, "{:.0f}", 0.f, 255.f))
			l->color = {col[0] / 255.f, col[1] / 255.f, col[2] / 255.f};
		float off[3] = {l->offset.x, l->offset.y, l->offset.z};
		if (u.DragFloats("Position de la source", off, 0.02f, "{:.2f}"))
			l->offset = {off[0], off[1], off[2]};
		u.Checkbox("Allumée", l->enabled);
	}
	// ── Son ──
	u.Section(icons::Codepoint(icons::MaterialIcons::VOLUME_UP), "Son");
	bool sounding = w.Has<voxel::SoundEmitter>(e);
	if (u.Checkbox("Émettre un son", sounding)) {
		if (sounding)
			w.Add(e, voxel::SoundEmitter{sounds.empty() ? std::string() : sounds.front(), 1.f, 16.f, true, true});
		else
			w.Remove<voxel::SoundEmitter>(e);
	}
	if (auto *snd = w.Get<voxel::SoundEmitter>(e)) {
		std::vector<std::string_view> ids(sounds.begin(), sounds.end());
		int index = 0;
		for (size_t i = 0; i < sounds.size(); ++i)
			if (sounds[i] == snd->sound)
				index = int(i);
		if (!ids.empty() && u.Choice(u.Next(26), "##sound", index, ids))
			snd->sound = sounds[size_t(index)];
		u.SliderFloat("Volume", snd->volume, 0.f, 2.f, "{:.2f}");
		u.SliderFloat("Portée (blocs)##sndrange", snd->range, 2.f, 64.f, "{:.0f}");
		u.Checkbox("En boucle", snd->loop);
		u.Checkbox("Actif##snd", snd->enabled);
	}
	// ── Masse ──
	u.Section(icons::Codepoint(icons::MaterialIcons::SPEED), "Masse");
	float kg[1] = {w.Has<voxel::Mass>(e) ? w.Get<voxel::Mass>(e)->kg : 70.f};
	if (u.DragFloats("Masse (kg) — poulies, contrepoids", kg, 1.f, "{:.0f}", 0.1f, 100000.f)) {
		if (auto *m = w.Get<voxel::Mass>(e))
			m->kg = kg[0];
		else
			w.Add(e, voxel::Mass{kg[0]});
	}
	// ── Mécanique : joints portés et reçus ──
	u.Section(icons::Codepoint(icons::MaterialIcons::BUILD), "Mécanique (joints)");
	if (auto *js = w.Get<voxel::Joints>(e)) {
		for (size_t i = 0; i < js->list.size(); ++i) {
			voxel::Joint &j = js->list[i];
			u.PushId(uint64_t(i) + 70000);
			const ecs::Entity target = voxel::FindByUid(w, j.target);
			const std::string tl = target ? EntityLabel(w, target) : std::string("(cible non chargée)");
			u.Label(std::format("{} {} → {}", voxel::JointTypeLabel(j.type), i + 1, tl), th.text, 13);
			const bool keep = JointEditor(u, j, tl);
			u.PopId();
			if (!keep) {
				js->list.erase(js->list.begin() + std::ptrdiff_t(i));
				break;
			}
		}
	}
	// Joints d'autres entités qui visent celle-ci.
	if (const auto *uid = w.Get<voxel::Uid>(e))
		w.MakeQuery<const voxel::Joints>().Each([&](ecs::Entity owner, const voxel::Joints &js) {
			for (const voxel::Joint &j : js.list)
				if (j.target == uid->value)
					u.Label(std::format("reçu : {} depuis {}", voxel::JointTypeLabel(j.type), EntityLabel(w, owner)), th.textDim, 12);
		});
	// Nouveau joint vers une entité proche.
	static int pick = 0, type = 0;
	std::vector<ecs::Entity> near;
	std::vector<std::string> labels;
	if (const auto *t = w.Get<voxel::Transform>(e)) {
		std::vector<std::pair<float, ecs::Entity>> cand;
		w.MakeQuery<const voxel::Transform>().Each([&](ecs::Entity o, const voxel::Transform &ot) {
			if (o != e && o != game.Player())
				cand.push_back({(ot.position - t->position).LengthSq(), o});
		});
		std::ranges::sort(cand, {}, &std::pair<float, ecs::Entity>::first);
		for (size_t i = 0; i < cand.size() && i < 24; ++i) {
			near.push_back(cand[i].second);
			labels.push_back(std::format("{} ({:.1f} m)", EntityLabel(w, cand[i].second), std::sqrt(cand[i].first)));
		}
	}
	if (!near.empty()) {
		std::vector<std::string_view> lv(labels.begin(), labels.end());
		static constexpr std::string_view kTypes[] = {"Attache", "Charnière", "Glissière", "Poulie", "Corde"};
		pick = std::clamp(pick, 0, int(near.size()) - 1);
		u.Choice(u.Next(26), "##newtarget", pick, lv);
		u.Row({1, 1}, 26);
		u.Choice(u.Next(), "##newtype", type, kTypes);
		if (u.Button("Ajouter le joint")) {
			voxel::Joint j;
			j.type = voxel::JointType(type);
			j.target = voxel::EnsureUid(w, near[size_t(pick)]);
			voxel::EnsureUid(w, e);
			if (auto *js = w.Get<voxel::Joints>(e))
				js->list.push_back(j);
			else
				w.Add(e, voxel::Joints{{j}});
		}
	} else {
		u.Label("aucune autre entité à proximité", th.textDim, 12);
	}
}

} // namespace

PropertiesActions DrawProperties(ui::Context &u, const Rect &r, Game &game, ecs::Entity e, bool following,
								  const std::vector<std::string> &sounds) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	PropertiesActions act;
	ecs::World &w = game.Ecs();
	if (!e || !w.IsAlive(e)) {
		u.BeginScroll("props-empty", r.Inset(10, 10));
		u.Wrapped("Sélectionnez une entité dans l'Outliner ou d'un clic dans la vue 3D : ses composants s'affichent ici et se modifient "
				  "à la volée.",
				  th.textDim, 13);
		u.EndScroll();
		return act;
	}
	u.PushId("props");
	u.BeginScroll(std::format("props-{}", e.Bits()), r.Inset(10, 8));
	const auto *kind = w.Get<voxel::Kind>(e);
	const bool isPlayer = e == game.Player();
	const voxel::EntityCategory cat = voxel::CategorizeEntity(kind ? kind->id : "", isPlayer, w.Has<voxel::Wander>(e));

	// ── En-tête : type, identifiant, actions ──
	{
		const Rect head = u.Next(40);
		u.Icon({head.x, head.y, 30, head.h}, CategoryIcon(cat), CategoryColor(th, cat), 22);
		c.Text(head.x + 36, head.y + 2, kind ? kind->id : "(sans type)", th.text, 16, sdl3::CanvasFont::Bold);
		const auto *t = w.Get<voxel::Transform>(e);
		const math::IVec3 chunk = t ? voxel::ChunkOfPosition(t->position) : math::IVec3{};
		c.Text(head.x + 36, head.y + 23, std::format("{} · entité #{} (gén. {}) · chunk {} {} {}", voxel::EntityCategoryLabel(cat), e.index, e.generation,
													 chunk.x, chunk.y, chunk.z),
			   th.textDim, 12);
		u.Row({1, 1, 1}, 28);
		act.focus = u.Button("Cadrer (F)");
		act.toggleFollow = u.Button(following ? "Ne plus suivre" : "Suivre");
		if (u.Button("Supprimer##despawn", !isPlayer)) {
			game.World().DespawnEntity(e);
			act.despawned = true;
			u.EndScroll();
			u.PopId();
			return act;
		}
	}

	// ── Transform ──
	if (auto *t = w.Get<voxel::Transform>(e)) {
		u.Section(icons::Codepoint(icons::MaterialIcons::OPEN_WITH), "Transform");
		float pos[3] = {t->position.x, t->position.y, t->position.z};
		if (u.DragFloats("Position", pos, 0.05f, "{:.2f}"))
			t->position = {pos[0], pos[1], pos[2]};
		float yaw[1] = {t->yaw * 180.f / std::numbers::pi_v<float>};
		if (u.DragFloats("Orientation (lacet, °)", yaw, 0.5f, "{:.1f}"))
			t->yaw = yaw[0] * std::numbers::pi_v<float> / 180.f;
	}
	if (auto *v = w.Get<voxel::Velocity>(e)) {
		float vel[3] = {v->value.x, v->value.y, v->value.z};
		if (u.DragFloats("Vitesse (m/s)", vel, 0.05f, "{:.2f}"))
			v->value = {vel[0], vel[1], vel[2]};
	}

	// ── Physique & collision (AABB) ──
	if (auto *b = w.Get<voxel::Body>(e)) {
		u.Section(icons::Codepoint(icons::MaterialIcons::VIEW_IN_AR), "Physique & collision");
		float size[3] = {b->halfExtents.x * 2, b->halfExtents.y * 2, b->halfExtents.z * 2};
		if (u.DragFloats("Boîte de collision (m)", size, 0.01f, "{:.2f}", 0.05f, 64.f))
			b->halfExtents = {size[0] * 0.5f, size[1] * 0.5f, size[2] * 0.5f};
		u.Checkbox("Soumis à la gravité", b->gravity);
		u.Checkbox("Collisions avec les blocs", b->collide);
		u.Label(b->onGround ? "Au sol" : "En l'air", b->onGround ? th.success : th.textDim, 12);
	}

	// ── Santé ──
	if (auto *h = w.Get<voxel::Health>(e)) {
		u.Section(icons::Codepoint(icons::MaterialIcons::BOLT), "Santé");
		u.SliderFloat("Points de vie", h->current, 0.f, std::max(1.f, h->max), "{:.1f}");
		float mx[1] = {h->max};
		if (u.DragFloats("Maximum", mx, 0.1f, "{:.1f}", 1.f, 10000.f)) {
			h->max = mx[0];
			h->current = std::min(h->current, h->max);
		}
	}

	// ── Comportement ──
	if (auto *wd = w.Get<voxel::Wander>(e)) {
		u.Section(icons::Codepoint(icons::MaterialIcons::PETS), "Comportement : errance");
		float sp[1] = {wd->speed};
		if (u.DragFloats("Vitesse de marche (m/s)", sp, 0.02f, "{:.2f}", 0.f, 20.f))
			wd->speed = sp[0];
		u.Label(std::format("prochain changement de cap dans {:.1f} s", wd->timer), th.textDim, 12);
	}

	// ── Apparence et données des mods ──
	if (const auto *sk = w.Get<voxel::Skin>(e)) {
		u.Section(icons::Codepoint(icons::MaterialIcons::FACE), "Apparence");
		u.Label(std::format("skin : {}", sk->id.empty() ? "(par défaut)" : sk->id), th.text, 13);
	}
	if (const auto *p = w.Get<voxel::Props>(e); p && !p->json.empty()) {
		u.Section(icons::Codepoint(icons::MaterialIcons::DESCRIPTION), "Propriétés des mods (JSON)");
		u.Wrapped(p->json, th.textDim, 12);
	}

	// ── Éclairage, son, masse, mécanique (phase 3) ──
	MechanicsSections(u, game, e, sounds);

	// ── Composants présents ──
	u.Section(icons::Codepoint(icons::MaterialIcons::LAYERS), "Composants ECS");
	{
		std::vector<std::string_view> names;
		if (w.Has<voxel::Transform>(e))
			names.push_back("Transform");
		if (w.Has<voxel::Velocity>(e))
			names.push_back("Velocity");
		if (w.Has<voxel::Body>(e))
			names.push_back("Body");
		if (w.Has<voxel::Health>(e))
			names.push_back("Health");
		if (w.Has<voxel::Kind>(e))
			names.push_back("Kind");
		if (w.Has<voxel::Skin>(e))
			names.push_back("Skin");
		if (w.Has<voxel::Props>(e))
			names.push_back("Props");
		if (w.Has<voxel::Wander>(e))
			names.push_back("Wander");
		if (w.Has<voxel::Transient>(e))
			names.push_back("Transient");
		if (w.Has<voxel::GridCell>(e))
			names.push_back("GridCell");
		if (w.Has<voxel::Uid>(e))
			names.push_back("Uid");
		if (w.Has<voxel::Mass>(e))
			names.push_back("Mass");
		if (w.Has<voxel::LightEmitter>(e))
			names.push_back("LightEmitter");
		if (w.Has<voxel::SoundEmitter>(e))
			names.push_back("SoundEmitter");
		if (w.Has<voxel::Joints>(e))
			names.push_back("Joints");
		// Badges en lignes successives.
		Rect line = u.Next(24);
		float x = line.x;
		for (std::string_view n : names) {
			const float bw = c.MeasureText(n, 12) + 18;
			if (x + bw > line.Right()) {
				line = u.Next(24);
				x = line.x;
			}
			u.Badge({x, line.y + 2, bw, 20}, n, th.info);
			x += bw + 6;
		}
	}
	u.EndScroll();
	u.PopId();
	return act;
}

// ══════════════════════════════════════════════════════════════════════════
// Gestionnaire de mods
// ══════════════════════════════════════════════════════════════════════════

void DrawMods(ui::Context &u, const Rect &r, App &app, Game *game, std::string &status, const OpenFileFn &openFile) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	u.PushId("mods");
	u.BeginScroll("mods", r.Inset(8, 8));
	const auto &mods = app.content.mods.Mods();
	size_t enabled = 0;
	for (const voxel::ModInfo &m : mods)
		enabled += !app.settings.disabledMods.contains(m.id);
	u.Label(std::format("{} mod(s), {} actif(s) · {} blocs · {} textures", mods.size(), enabled, app.content.blocks.Count(), app.content.layerCount),
			th.textDim, 12);
	if (u.Button("Recharger les scripts des mods (à chaud)", game && game->Scripts())) {
		game->ReloadScripts();
		status = "scripts des mods rechargés : l'état du monde est conservé";
	}
	u.Checkbox("Rechargement à chaud (scripts, définitions, textures)", app.hotReload.enabled);
	if (!app.reloadLog.empty())
		u.TextIn(u.Next(18), app.reloadLog.back(), {Rgba(120, 180, 255), 12, sdl3::CanvasFont::Regular, ui::TextOverflow::MarqueeOnHover});
	if (app.contentDirty)
		u.Wrapped("Changements en attente : le contenu sera rechargé au prochain lancement d'un monde.", th.warning, 12);
	for (const voxel::ModInfo &m : mods) {
		u.PushId(m.id);
		const bool on = !app.settings.disabledMods.contains(m.id);
		u.Space(4);
		const float descH = m.description.empty() ? 0.f : 34.f;
		const Rect card = u.Next(92 + descH);
		u.Card(card, u.MouseIn(card), false);
		const Rect in = card.Inset(10, 8);
		u.Icon({in.x, in.y, 22, 22}, icons::Codepoint(icons::MaterialIcons::EXTENSION), on ? th.accent : th.textDim, 20);
		const std::string title = m.name.empty() ? m.id : m.name;
		c.Text(in.x + 28, in.y + 2, title, th.text, 15, sdl3::CanvasFont::Bold);
		const float tw = c.MeasureText(title, 15, sdl3::CanvasFont::Bold);
		c.Text(in.x + 34 + tw, in.y + 4, std::format("{} · v{}", m.id, m.version.empty() ? "?" : m.version), th.textDim, 12);
		// Activation (impossible pendant une partie : le contenu est figé).
		bool value = on;
		const Rect toggle{in.Right() - 46, in.y, 46, 22};
		if (game) {
			c.FillRoundRect(toggle, 11, sdl3::WithAlpha(on ? th.accent : th.widget, 120));
			if (u.MouseIn(toggle))
				u.Tooltip("Quittez la partie pour activer ou désactiver un mod");
		} else if (u.Toggle(toggle, "##on", value) && m.id != "core") {
			if (value)
				app.settings.disabledMods.erase(m.id);
			else
				app.settings.disabledMods.insert(m.id);
			app.contentDirty = true;
			app.settings.Save(app.settingsFile);
		}
		float y = in.y + 28;
		if (!m.description.empty()) {
			u.TextIn({in.x, y, in.w, 16}, m.description, {th.textDim, 12, sdl3::CanvasFont::Regular, ui::TextOverflow::MarqueeOnHover});
			y += 18;
		}
		std::string deps;
		for (const std::string &d : m.dependencies)
			deps += (deps.empty() ? "" : ", ") + d;
		const std::string info = std::format("{} bloc(s) · {} script(s) · {} paramètre(s){}{}", m.blockCount, m.scripts.size(), m.settings.size(),
											 deps.empty() ? "" : " · dépend de ", deps);
		u.TextIn({in.x, y, in.w, 16}, info, {th.text, 12, sdl3::CanvasFont::Regular, ui::TextOverflow::MarqueeOnHover});
		y += 22;
		float bx = in.x;
		auto fileButton = [&](std::string_view label, const fs::path &file) {
			const float bw = c.MeasureText(label, 12) + 18;
			const bool exists = fs::exists(file);
			if (u.ButtonAt({bx, y, bw, 24}, std::format("{}##{}", label, file.filename().string()), exists) && openFile)
				openFile(file, m.id);
			bx += bw + 6;
		};
		fileButton("mod.json", m.directory / "mod.json");
		if (m.hasConfig)
			fileButton("config.json", m.directory / "config.json");
		if (!m.scripts.empty())
			fileButton(std::format("{} (script)", m.scripts.front().filename().string()), m.scripts.front());
		u.PopId();
	}
	u.EndScroll();
	u.PopId();
}

// ══════════════════════════════════════════════════════════════════════════
// Mécanismes : graphe des entités et de leurs joints
// ══════════════════════════════════════════════════════════════════════════

namespace {

sdl3::Color JointColor(voxel::JointType t) {
	switch (t) {
	case voxel::JointType::Attach: return Rgba(160, 168, 186);
	case voxel::JointType::Hinge: return Rgba(236, 160, 60);
	case voxel::JointType::Slider: return Rgba(86, 200, 220);
	case voxel::JointType::Pulley: return Rgba(198, 120, 221);
	case voxel::JointType::Rope: return Rgba(236, 210, 90);
	}
	return Rgba(200, 200, 200);
}

/// Courbe de Bézier (sortie → entrée) dessinée en segments.
void Wire(sdl3::Canvas &c, float x0, float y0, float x1, float y1, sdl3::Color col, float t) {
	const float dx = std::max(40.f, std::fabs(x1 - x0) * 0.5f);
	float pts[2 * 25];
	for (int i = 0; i <= 24; ++i) {
		const float k = float(i) / 24.f, a = 1 - k;
		const float bx = a * a * a * x0 + 3 * a * a * k * (x0 + dx) + 3 * a * k * k * (x1 - dx) + k * k * k * x1;
		const float by = a * a * a * y0 + 3 * a * a * k * y0 + 3 * a * k * k * y1 + k * k * k * y1;
		pts[i * 2] = bx;
		pts[i * 2 + 1] = by;
	}
	c.Polyline(pts, 25, col, t);
}

math::FVector2 *NodePos(MechanismState &st, uint64_t uid) {
	for (auto &[u, p] : st.nodes)
		if (u == uid)
			return &p;
	return nullptr;
}

} // namespace

std::optional<MechanismEvent> DrawMechanisms(ui::Context &u, const Rect &r, MechanismState &st, Game &game, ecs::Entity selected) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	ecs::World &w = game.Ecs();
	std::optional<MechanismEvent> event;
	u.PushId("mechanisms");
	// ── Barre d'outils ──
	const float barH = 34;
	c.FillRect({r.x, r.y, r.w, barH}, Rgba(18, 21, 27));
	float bx = r.x + 6;
	auto button = [&](std::string_view label, bool enabled = true) {
		const float bw = c.MeasureText(label, th.fontSize) + 26;
		const bool clicked = u.ButtonAt({bx, r.y + 4, bw, 26}, label, enabled);
		bx += bw + 6;
		return clicked;
	};
	const bool hasSel = selected && w.IsAlive(selected);
	if (button("Ajouter la sélection", hasSel)) {
		const uint64_t uid = voxel::EnsureUid(w, selected);
		if (!NodePos(st, uid))
			st.nodes.push_back({uid, {40.f + float(st.nodes.size() % 4) * 190.f - st.panX, 30.f + float(st.nodes.size() / 4) * 90.f - st.panY}});
	}
	const auto *pt = w.Get<voxel::Transform>(game.Player());
	const math::FVector3 front = pt ? pt->position + math::FVector3{std::sin(pt->yaw) * 6.f, 0, -std::cos(pt->yaw) * 6.f} : math::FVector3{};
	if (button("Ascenseur de démonstration", pt != nullptr)) {
		const ecs::Entity cabin = BuildDemoElevator(game, front, {});
		event = MechanismEvent{cabin, true};
	}
	if (button("Porte de démonstration", pt != nullptr)) {
		const ecs::Entity door = BuildDemoDoor(game, front);
		event = MechanismEvent{door, true};
	}
	const std::string stats = std::format("{} joint(s) · {} actif(s)", game.Mechanics().joints, game.Mechanics().active);
	c.Text(r.Right() - c.MeasureText(stats, 12) - 10, r.y + 10, stats, th.textDim, 12);

	// ── Nœuds : entités à joints (ajoutées automatiquement) + entités épinglées ──
	core::RobinMap<uint64_t, ecs::Entity> byUid;
	w.MakeQuery<const voxel::Uid>().Each([&](ecs::Entity e, const voxel::Uid &id) { byUid.InsertOrAssign(id.value, e); });
	// Disposition automatique : porteurs en colonne, leurs cibles en éventail à droite.
	float nextOwnerY = 20.f;
	for (const auto &[uid, p] : st.nodes)
		nextOwnerY = std::max(nextOwnerY, p.y + 70.f);
	w.MakeQuery<const voxel::Joints, const voxel::Uid>().Each([&](const voxel::Joints &js, const voxel::Uid &id) {
		if (!NodePos(st, id.value)) {
			st.nodes.push_back({id.value, {30.f, nextOwnerY}});
			nextOwnerY += 70.f;
		}
		const math::FVector2 owner = *NodePos(st, id.value);
		int k = 0;
		for (const voxel::Joint &j : js.list)
			if (byUid.Find(j.target) && !NodePos(st, j.target)) {
				st.nodes.push_back({j.target, {owner.x + 300.f, owner.y + float(k++) * 66.f}});
				nextOwnerY = std::max(nextOwnerY, owner.y + float(k) * 66.f);
			}
	});
	std::erase_if(st.nodes, [&](const auto &n) { return !byUid.Find(n.first); }); // entités disparues

	const Rect canvas{r.x, r.y + barH, r.w, r.h - barH};
	c.FillRect(canvas, Rgba(14, 16, 21));
	c.PushClip(canvas);
	for (float gx = std::fmod(st.panX, 24.f); gx < canvas.w; gx += 24.f) // trame
		c.Line(canvas.x + gx, canvas.y, canvas.x + gx, canvas.Bottom(), Rgba(255, 255, 255, 8));
	for (float gy = std::fmod(st.panY, 24.f); gy < canvas.h; gy += 24.f)
		c.Line(canvas.x, canvas.y + gy, canvas.Right(), canvas.y + gy, Rgba(255, 255, 255, 8));
	constexpr float kW = 170, kH = 54;
	auto nodeRect = [&](const math::FVector2 &p) { return Rect{canvas.x + st.panX + p.x, canvas.y + st.panY + p.y, kW, kH}; };

	// Liens (joints).
	struct LinkHit {
		uint64_t owner;
		int joint;
		float x, y;
	};
	std::vector<LinkHit> linkHits;
	w.MakeQuery<voxel::Joints, const voxel::Uid>().Each([&](voxel::Joints &js, const voxel::Uid &id) {
		const math::FVector2 *pa = NodePos(st, id.value);
		if (!pa)
			return;
		for (size_t i = 0; i < js.list.size(); ++i) {
			const voxel::Joint &j = js.list[i];
			const math::FVector2 *pb = NodePos(st, j.target);
			if (!pb)
				continue;
			const Rect ra = nodeRect(*pa), rb = nodeRect(*pb);
			const float x0 = ra.Right(), y0 = ra.y + kH * 0.5f, x1 = rb.x, y1 = rb.y + kH * 0.5f;
			const bool sel = st.selOwner == id.value && st.selJoint == int(i);
			Wire(c, x0, y0, x1, y1, sel ? Rgba(255, 230, 120) : JointColor(j.type), sel ? 3.f : 2.f);
			const float mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f;
			const std::string label = std::string(voxel::JointTypeLabel(j.type)) + (j.motor ? " · moteur" : "");
			const float lw = c.MeasureText(label, 11) + 12;
			c.FillRoundRect({mx - lw * 0.5f, my - 9, lw, 18}, 9, Rgba(24, 27, 35, 235));
			c.StrokeRoundRect({mx - lw * 0.5f, my - 9, lw, 18}, 9, JointColor(j.type));
			c.Text(mx - lw * 0.5f + 6, my - 8, label, th.text, 11);
			linkHits.push_back({id.value, int(i), mx, my});
		}
	});

	// Nœuds.
	uint64_t hoverNode = 0;
	for (auto &[uid, pos] : st.nodes) {
		const ecs::Entity *ep = byUid.Find(uid);
		if (!ep)
			continue;
		const Rect nr = nodeRect(pos);
		const bool isSel = *ep == selected;
		const bool hover = u.MouseIn(nr);
		if (hover)
			hoverNode = uid;
		c.FillRoundRect(nr, 7, hover ? th.widgetHover : th.widget);
		c.StrokeRoundRect(nr, 7, isSel ? th.accent : th.panelBorder, isSel ? 2.f : 1.f);
		const auto *k = w.Get<voxel::Kind>(*ep);
		c.Text(nr.x + 10, nr.y + 7, c.Ellipsize(k ? k->id : "entité", kW - 20, 13), th.text, 13, sdl3::CanvasFont::Bold);
		std::string sub = std::format("#{}", ep->index);
		if (const auto *m = w.Get<voxel::Mass>(*ep))
			sub += std::format(" · {:.0f} kg", m->kg);
		if (w.Has<voxel::LightEmitter>(*ep))
			sub += " · lumière";
		if (w.Has<voxel::SoundEmitter>(*ep))
			sub += " · son";
		c.Text(nr.x + 10, nr.y + 29, c.Ellipsize(sub, kW - 20, 11), th.textDim, 11);
		c.FillCircle(nr.x, nr.y + kH * 0.5f, 5, th.info);			// entrée (cible)
		c.FillCircle(nr.Right(), nr.y + kH * 0.5f, 5, th.success); // sortie (porteur)
		const bool onOut = std::hypot(u.MouseX() - nr.Right(), u.MouseY() - (nr.y + kH * 0.5f)) < 9.f;
		if (u.MousePressed(sdl3::MouseButton::Left) && u.MouseIn(canvas)) {
			if (onOut) {
				st.linkFrom = uid;
			} else if (hover) {
				st.dragNode = uid;
				st.dragOffset = {u.MouseX() - nr.x, u.MouseY() - nr.y};
				event = MechanismEvent{*ep, false};
			}
		}
	}
	// Glisser un nœud.
	if (st.dragNode) {
		if (u.MouseDown(sdl3::MouseButton::Left)) {
			if (math::FVector2 *p = NodePos(st, st.dragNode))
				*p = {u.MouseX() - st.dragOffset.x - canvas.x - st.panX, u.MouseY() - st.dragOffset.y - canvas.y - st.panY};
		} else {
			st.dragNode = 0;
		}
	}
	// Créer un lien : glisser d'une sortie vers un autre nœud (attache par défaut).
	if (st.linkFrom) {
		if (const math::FVector2 *pa = NodePos(st, st.linkFrom)) {
			const Rect ra = nodeRect(*pa);
			Wire(c, ra.Right(), ra.y + kH * 0.5f, u.MouseX(), u.MouseY(), Rgba(255, 230, 120), 2.f);
		}
		if (!u.MouseDown(sdl3::MouseButton::Left)) {
			if (hoverNode && hoverNode != st.linkFrom)
				if (const ecs::Entity *owner = byUid.Find(st.linkFrom)) {
					voxel::Joint j;
					j.target = hoverNode;
					if (auto *js = w.Get<voxel::Joints>(*owner))
						js->list.push_back(j);
					else
						w.Add(*owner, voxel::Joints{{j}});
					st.selOwner = st.linkFrom;
					st.selJoint = int(w.Get<voxel::Joints>(*owner)->list.size()) - 1;
				}
			st.linkFrom = 0;
		}
	}
	// Sélection d'un lien (clic sur son étiquette).
	if (u.MousePressed(sdl3::MouseButton::Left) && !st.dragNode && !st.linkFrom && u.MouseIn(canvas))
		for (const LinkHit &h : linkHits)
			if (std::fabs(u.MouseX() - h.x) < 40 && std::fabs(u.MouseY() - h.y) < 10) {
				st.selOwner = h.owner;
				st.selJoint = h.joint;
			}
	// Déplacer la vue (milieu ou clic droit).
	if ((u.MousePressed(sdl3::MouseButton::Right) || u.MousePressed(sdl3::MouseButton::Middle)) && u.MouseIn(canvas)) {
		st.panning = true;
		st.lastX = u.MouseX();
		st.lastY = u.MouseY();
	}
	if (st.panning) {
		st.panX += u.MouseX() - st.lastX;
		st.panY += u.MouseY() - st.lastY;
		st.lastX = u.MouseX();
		st.lastY = u.MouseY();
		if (!u.MouseDown(sdl3::MouseButton::Right) && !u.MouseDown(sdl3::MouseButton::Middle))
			st.panning = false;
	}
	if (st.nodes.empty())
		c.TextCentered(canvas, "Aucun mécanisme : « Ajouter la sélection », puis glisser d'une sortie (●) vers une autre entité, ou créer une démonstration.",
					   th.textDim, 13);
	c.PopClip();

	// ── Réglages du lien sélectionné ──
	if (const ecs::Entity *owner = byUid.Find(st.selOwner))
		if (auto *js = w.Get<voxel::Joints>(*owner); js && st.selJoint >= 0 && size_t(st.selJoint) < js->list.size()) {
			const Rect panel{canvas.Right() - 300, canvas.y + 8, 290, std::min(canvas.h - 16, 330.f)};
			c.FillRoundRect(panel, 8, Rgba(20, 23, 30, 245));
			c.StrokeRoundRect(panel, 8, th.panelBorder);
			voxel::Joint &j = js->list[size_t(st.selJoint)];
			const ecs::Entity *target = byUid.Find(j.target);
			u.BeginScroll("joint", panel.Inset(10, 8));
			u.Label(std::format("{} : {} → {}", voxel::JointTypeLabel(j.type), EntityLabel(w, *owner), target ? EntityLabel(w, *target) : "?"),
					th.text, 13);
			const bool keep = JointEditor(u, j, target ? EntityLabel(w, *target) : "?");
			u.EndScroll();
			if (!keep) {
				js->list.erase(js->list.begin() + st.selJoint);
				st.selJoint = -1;
			}
		}
	u.PopId();
	return event;
}

// ══════════════════════════════════════════════════════════════════════════
// Démonstrations
// ══════════════════════════════════════════════════════════════════════════

namespace {

ecs::Entity SpawnPart(Game &game, std::string kind, const math::FVector3 &pos, const math::FVector3 &half, float kg) {
	voxel::EntityRecord rec;
	rec.kind = std::move(kind);
	rec.position = pos;
	rec.halfExtents = half;
	rec.health = rec.maxHealth = 100.f;
	const ecs::Entity e = game.World().SpawnEntity(rec);
	ecs::World &w = game.Ecs();
	w.Add(e, voxel::Mass{kg});
	if (auto *b = w.Get<voxel::Body>(e)) {
		b->gravity = false; // posées par leurs joints
		b->collide = false;
	}
	voxel::EnsureUid(w, e);
	return e;
}

} // namespace

ecs::Entity BuildDemoElevator(Game &game, const math::FVector3 &origin, const std::vector<std::string> &) {
	ecs::World &w = game.Ecs();
	const float ground = float(game.World().SurfaceHeight(int(std::floor(origin.x)), int(std::floor(origin.z))));
	const math::FVector3 base{std::floor(origin.x) + 0.5f, ground + 1.1f, std::floor(origin.z) + 0.5f};
	// Cabine (A) sur poulie avec contrepoids (B) à côté ; porte coulissante sur la cabine.
	const ecs::Entity cabin = SpawnPart(game, "core:elevator_cabin", base, {0.8f, 1.1f, 0.8f}, 650.f);
	const ecs::Entity weight = SpawnPart(game, "core:elevator_counterweight", base + math::FVector3{2.2f, 6.f, 0}, {0.3f, 0.7f, 0.3f}, 600.f);
	const ecs::Entity door = SpawnPart(game, "core:elevator_door", base + math::FVector3{0, 0, 0.85f}, {0.75f, 1.0f, 0.06f}, 40.f);
	voxel::Joint pulley;
	pulley.type = voxel::JointType::Pulley;
	pulley.name = "câble";
	pulley.target = voxel::EnsureUid(w, weight);
	pulley.motor = true;
	pulley.motorSpeed = 1.2f;
	pulley.motorForce = 30000.f;
	pulley.minLimit = base.y;
	pulley.maxLimit = base.y + 8.f;
	voxel::Joint slide;
	slide.type = voxel::JointType::Slider;
	slide.name = "porte";
	slide.target = voxel::EnsureUid(w, door);
	slide.axis = {1, 0, 0};
	slide.motor = true;
	slide.motorSpeed = 0.6f;
	slide.maxLimit = 1.4f;
	w.Add(cabin, voxel::Joints{{pulley, slide}});
	w.Add(cabin, voxel::LightEmitter{{1.f, 0.85f, 0.6f}, 1.6f, 9.f, {0, 1.2f, 0}, true});
	w.Add(weight, voxel::SoundEmitter{"elevators:sounds/elevator_motor_loop.wav", 0.6f, 18.f, true, true});
	return cabin;
}

ecs::Entity BuildDemoDoor(Game &game, const math::FVector3 &origin) {
	ecs::World &w = game.Ecs();
	const float ground = float(game.World().SurfaceHeight(int(std::floor(origin.x)), int(std::floor(origin.z))));
	const math::FVector3 base{std::floor(origin.x) + 0.5f, ground + 1.1f, std::floor(origin.z) + 0.5f};
	const ecs::Entity frame = SpawnPart(game, "core:door_frame", base, {0.1f, 1.1f, 0.1f}, 200.f);
	const ecs::Entity door = SpawnPart(game, "core:door_panel", base + math::FVector3{0.55f, 0, 0}, {0.45f, 1.0f, 0.05f}, 30.f);
	voxel::Joint hinge;
	hinge.type = voxel::JointType::Hinge;
	hinge.name = "gonds";
	hinge.target = voxel::EnsureUid(w, door);
	hinge.motor = true;
	hinge.motorSpeed = 45.f;
	hinge.minLimit = 0.f;
	hinge.maxLimit = 100.f;
	w.Add(frame, voxel::Joints{{hinge}});
	w.Add(frame, voxel::SoundEmitter{"core:door", 0.8f, 12.f, false, true});
	return frame;
}

// ══════════════════════════════════════════════════════════════════════════
// Performances
// ══════════════════════════════════════════════════════════════════════════

void DrawPerformance(ui::Context &u, const Rect &r, PerfState &st, App &app, Game *game, float dt) {
	const ui::Theme &th = u.Style();
	st.frameMs += (dt * 1000.f - st.frameMs) * 0.1f;
	auto push = [&](const std::string &name, float v) {
		for (auto &[n, q] : st.series)
			if (n == name) {
				q.push_back(v);
				if (q.size() > 150)
					q.pop_front();
				return;
			}
		st.series.push_back({name, std::deque<float>{v}});
	};
	const uint64_t now = sdl3::GetTicksMS();
	if (!st.paused && now >= st.next) { // 10 échantillons par seconde
		st.next = now + 100;
		push("image (ms)", dt * 1000.f);
		if (game) {
			push("mise à jour (ms)", float(game->LastUpdateMs()));
			for (const auto &sys : game->Schedule().Stats())
				push("système " + sys.name + " (ms)", float(sys.lastMs));
			push("entités", float(game->Ecs().EntityCount()));
			push("chunks chargés", float(game->World().LoadedChunkCount()));
		}
		if (app.renderer) {
			push("appels de dessin", float(app.renderer->Stats().drawCalls));
			push("sommets dessinés (k)", float(app.renderer->Stats().verticesDrawn) / 1000.f);
		}
	}
	u.PushId("perf");
	u.BeginScroll("perf", r.Inset(8, 6));
	u.Row({3, 1}, 26);
	u.Label(std::format("{:.1f} img/s · image {:.2f} ms{}", st.frameMs > 0 ? 1000.f / st.frameMs : 0.f, st.frameMs,
						game ? std::format(" · tick {}", game->Tick()) : std::string()),
			th.text, 13);
	if (u.Button(st.paused ? "Reprendre##perfpause" : "Figer##perfpause"))
		st.paused = !st.paused;
	if (game) {
		const voxel::WorldStats ws = game->World().Stats();
		u.Label(std::format("{} entités · {} chunks ({:.1f} Mo) · {} chargements, {} maillages en attente", game->Ecs().EntityCount(),
							ws.loadedChunks, double(ws.memoryBytes) / 1048576.0, ws.pendingLoads, ws.pendingMeshes),
				th.textDim, 12);
		u.Label(std::format("mécanique : {} joint(s), {} actif(s){}", game->Mechanics().joints, game->Mechanics().active,
							game->Paused() ? " · simulation en pause" : ""),
				th.textDim, 12);
	}
	if (app.renderer) {
		const auto &rs = app.renderer->Stats();
		u.Label(std::format("rendu : {} / {} chunks, {} appels, {:.0f} k sommets, {:.1f} Mo GPU", rs.chunksDrawn, rs.chunksResident, rs.drawCalls,
							double(rs.verticesDrawn) / 1000.0, double(rs.gpuBytes) / 1048576.0),
				th.textDim, 12);
	}
	static constexpr sdl3::Color kColors[] = {sdl3::Color{76, 196, 84, 255}, sdl3::Color{86, 156, 255, 255}, sdl3::Color{236, 180, 72, 255},
											 sdl3::Color{198, 120, 221, 255}, sdl3::Color{86, 200, 220, 255}, sdl3::Color{226, 72, 72, 255}};
	size_t k = 0;
	for (const auto &[name, q] : st.series) {
		const std::vector<float> values(q.begin(), q.end());
		float mx = 0;
		for (float v : values)
			mx = std::max(mx, v);
		u.Plot(std::format("{} · {:.3g} (max {:.3g})", name, values.empty() ? 0.f : values.back(), mx), values, 0.f, mx > 0 ? mx * 1.15f : 1.f, 46,
			   kColors[k++ % std::size(kColors)]);
	}
	if (!app.reloadLog.empty()) {
		u.Section(icons::Codepoint(icons::MaterialIcons::REPLAY), "Rechargements à chaud");
		for (size_t i = app.reloadLog.size(); i-- > 0 && i + 6 >= app.reloadLog.size();)
			u.Wrapped(app.reloadLog[i], th.textDim, 12);
	}
	u.EndScroll();
	u.PopId();
}

} // namespace voxelia::studio
