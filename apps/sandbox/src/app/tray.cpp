// Plateau du sandbox (colonne de droite, à onglets) : infos sur l'entité, structure du modèle,
// matériaux, balises et scènes.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

sdl3::Color ToGfx(const modeling::Color &c) {
	return sdl3::Rgba(uint8_t(std::clamp(c.r, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.g, 0.f, 1.f) * 255.f), uint8_t(std::clamp(c.b, 0.f, 1.f) * 255.f));
}

modeling::Color ToModel(sdl3::Color c) { return {float(c.r) / 255.f, float(c.g) / 255.f, float(c.b) / 255.f, 1.f}; }

constexpr sdl3::Color kRed{226, 72, 72, 255}, kGreen{86, 196, 92, 255}, kBlue{76, 140, 236, 255}, kGray{150, 150, 160, 255};

struct AxisField {
	std::string_view caption;
	float *value;
	sdl3::Color accent; ///< repère de l'axe (rouge X, vert Z, bleu vertical)
};

/// Légendes puis champs numériques côte à côte (glisser pour ajuster, cliquer pour taper).
bool FieldRow(ui::Context &u, std::string_view id, std::initializer_list<AxisField> fields, float speed, std::string_view format,
			  float min = -1e6f, float max = 1e6f) {
	sdl3::Canvas &c = u.Canvas();
	const sdl3::FRect captions = u.Next(14), line = u.Next(26);
	const float gap = 6.f, w = (line.w - gap * float(fields.size() - 1)) / float(fields.size());
	bool changed = false;
	size_t i = 0;
	u.PushId(id);
	for (const AxisField &f : fields) {
		const float x = line.x + float(i) * (w + gap);
		c.Text(x + 2.f, captions.y, c.Ellipsize(f.caption, w, 11.f), u.Style().textDim, 11.f);
		changed |= u.DragNumber({x, line.y, w, line.h}, std::format("##f{}", i), *f.value, speed, format, f.accent, min, max);
		++i;
	}
	u.PopId();
	return changed;
}

/// Proportions liées : la cote modifiée entraîne les autres dans le même rapport.
math::FVector3 KeepRatio(const math::FVector3 &before, const math::FVector3 &after) {
	for (int i = 0; i < 3; ++i)
		if (std::abs(after[i] - before[i]) > 1e-7f && before[i] > 1e-6f) {
			const float k = after[i] / before[i];
			return {before.x * k, before.y * k, before.z * k};
		}
	return after;
}


} // namespace

void SandboxApp::ShowPanel(std::string_view id) {
	// Chaque onglet correspond à un index fixe de sa région (TabWidget retenu).
	// On met à jour à la fois la variable liée (`Bind`) et l'état du widget, car
	// la liaison est bidirectionnelle : la valeur repoussée vient de `selectedIndex`.
	const auto setTab = [this](std::string_view widget, int index) {
		if (m_ui)
			if (ui::Widget w = m_ui->Find(widget); w)
				w->selectedIndex = index;
	};
	if (id == "outliner")
		m_tabLeft = 0, setTab("tabs-left", 0), m_leftShown = true;
	else if (id == "scenes")
		m_tabLeft = 1, setTab("tabs-left", 1), m_leftShown = true;
	else if (id == "viewport")
		m_tabCenter = 0, setTab("tabs-center", 0), m_workspace = "viewport";
	else if (id == "overlay")
		m_tabCenter = 1, setTab("tabs-center", 1), m_workspace = "overlay";
	else if (id == "script")
		m_tabCenter = 2, setTab("tabs-center", 2), m_workspace = "script";
	else if (id == "entity")
		m_tabRight = 0, setTab("tabs-right", 0), m_rightShown = true;
	else if (id == "materials")
		m_tabRight = 1, setTab("tabs-right", 1), m_rightShown = true;
	else if (id == "tags")
		m_tabRight = 2, setTab("tabs-right", 2), m_rightShown = true;
	else if (id == "files")
		m_tabBottom = 0, setTab("tabs-bottom", 0), m_bottomShown = true;
	else if (id == "assets")
		m_tabBottom = 1, setTab("tabs-bottom", 1), m_bottomShown = true;
	else if (id == "commands")
		m_tabBottom = 2, setTab("tabs-bottom", 2), m_bottomShown = true;
	else if (id == "console")
		m_tabBottom = 3, setTab("tabs-bottom", 3), m_bottomShown = true;
}

void SandboxApp::DrawEntityPanel() {
	if (m_workspace == "overlay") { // Vue 2D active : propriétés du calque
		DrawItem2DPanel();
		return;
	}
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Document &doc = ed.Doc();
	if (ed.SelectedEntities().size() == 1) {
		Entity *e = doc.Find(ed.SelectedEntities().front());
		if (!e)
			return;
		u.Section(icon::Entity, "Groupe");
		if (m_nameFor != e->id) {
			m_nameFor = e->id;
			m_nameEdit = e->name.c_str();
		}
		bool renamed = false;
		u.TextInput("##name", m_nameEdit, &renamed, "nom du groupe");
		if (renamed && !m_nameEdit.empty() && m_nameEdit != e->name.c_str()) {
			ed.Checkpoint("Renommer");
			e->name = String(m_nameEdit.c_str());
		}
		std::vector<std::string> tagNames;
		for (const Tag &t : doc.Tags())
			tagNames.push_back(t.name.c_str());
		std::vector<std::string_view> tagViews(tagNames.begin(), tagNames.end());
		int tag = int(std::min<size_t>(e->tag, tagNames.size() - 1));
		u.Label("Balise", th.textDim, 13.f);
		if (u.Choice(u.Next(30), "tag", tag, tagViews)) {
			ed.Checkpoint("Balise");
			e->tag = uint32_t(tag);
			doc.Touch();
		}
		bool visible = e->visible, locked = e->locked;
		u.Row({1.f, 1.f}, 28.f);
		if (u.Checkbox("Visible", visible)) {
			ed.Checkpoint(visible ? "Afficher" : "Masquer");
			e->visible = visible;
			doc.Touch();
		}
		if (u.Checkbox("Verrouillé", locked)) {
			ed.Checkpoint(locked ? "Verrouiller" : "Déverrouiller");
			e->locked = locked;
		}
		// Composant : instances qui partagent leur géométrie.
		if (const ComponentDef *def = doc.FindComponent(e->component)) {
			u.Wrapped(std::format("Composant « {} » · {} instance{}", def->name.c_str(), doc.InstanceCount(def->id), doc.InstanceCount(def->id) > 1 ? "s" : ""),
					  th.accent, 12.f);
			if (u.Button("Rendre unique")) {
				(void)ed.MakeUnique();
				return; // document modifié : panneau redessiné à l'image suivante
			}
		} else if (u.Button("Créer un composant")) {
			(void)ed.MakeComponent();
			return;
		}
		// Position, rotation et dimensions exactes : glisser pour ajuster, cliquer pour taper.
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TRANSFORM), "Position et dimensions");
		const Editor::Placement p = ed.EntityPlacement(e->id).Unwrap();
		Editor::Placement next = p;
		const bool moved = FieldRow(u, "position", {{"X", &next.position.x, kRed}, {"Z", &next.position.z, kGreen}, {"Hauteur", &next.position.y, kBlue}},
									0.01f, "{:.3f} m");
		const bool turned = FieldRow(u, "yaw", {{"Rotation autour de la verticale", &next.yawDegrees, kGray}}, 0.5f, "{:.1f}°", -360.f, 360.f);
		const bool sized = FieldRow(u, "size", {{"Largeur", &next.size.x, kRed}, {"Profondeur", &next.size.z, kGreen}, {"Hauteur", &next.size.y, kBlue}},
									0.01f, "{:.3f} m", 0.001f);
		if (sized && m_lockRatio)
			next.size = KeepRatio(p.size, next.size);
		u.Checkbox("Proportions liées", m_lockRatio);
		if (e->locked)
			u.Wrapped("Groupe verrouillé : déverrouillez-le pour le modifier.", th.warning, 12.f);
		else if (moved || turned || sized)
			(void)ed.SetEntityPlacement(e->id, next, moved ? "Déplacer le groupe" : turned ? "Tourner le groupe" : "Dimensions du groupe", u.MouseDown());
		u.Label(std::format("{} faces · {} arêtes", e->model.FaceCount(), e->model.EdgeCount()), th.textDim, 12.f);
		u.Space(6.f);
		u.Row({1.f, 1.f}, 30.f);
		if (u.Button("Ouvrir"))
			(void)ed.OpenGroup(e->id);
		if (u.Button("Éclater"))
			(void)ed.ExplodeSelection();
		DrawBehavior(); // comme l'inspecteur de Godot : comportement, script et paramètres du nœud
		return;
	}
	const modeling::Model &model = ed.ActiveModel();
	const modeling::Selection &sel = ed.Selection();
	if (!sel.IsEmpty()) {
		u.Section(icon::Entity, "Sélection");
		float area = 0.f, length = 0.f;
		for (modeling::FaceId f : sel.faces)
			area += model.FaceArea(f);
		for (modeling::EdgeId e : sel.edges)
			length += model.Position(model.Edges()[e].a).Distance(model.Position(model.Edges()[e].b));
		u.Label(std::format("{} face(s) · {} arête(s)", sel.faces.size(), sel.edges.size()), th.text, 14.f);
		if (!sel.faces.empty())
			u.Label(std::format("Aire : {:.2f} m²", area).c_str(), th.textDim, 13.f);
		if (!sel.edges.empty())
			u.Label("Longueur : " + std::string(modeling::FormatLength(length)), th.textDim, 13.f);
		// Dimensions de la sélection (boîte englobante) : mise à l'échelle depuis son coin bas.
		if (const Option<math::FAABB> box = ed.SelectionBounds()) {
			const math::FVector3 size = box->max - box->min;
			math::FVector3 next = size;
			u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::STRAIGHTEN), "Dimensions");
			if (FieldRow(u, "sel-size", {{"Largeur", &next.x, kRed}, {"Profondeur", &next.z, kGreen}, {"Hauteur", &next.y, kBlue}}, 0.01f, "{:.3f} m", 0.f)) {
				if (m_lockRatio)
					next = KeepRatio(size, next);
				(void)ed.ResizeSelection(next, "Dimensions", u.MouseDown());
			}
			u.Checkbox("Proportions liées", m_lockRatio);
		}
		if (sel.faces.size() == 1) {
			const uint32_t m = model.Faces()[sel.faces[0]].material;
			u.Label("Matériau : " + std::string(m < doc.Materials().size() ? doc.Materials()[m].name.c_str() : "par défaut"), th.textDim, 13.f);
		}
		u.Space(6.f);
		u.Row({1.f, 1.f}, 30.f);
		if (u.Button("Grouper", ed.Context() == kRootId))
			(void)ed.GroupSelection();
		if (u.DangerButton(u.Next(), "Effacer"))
			(void)ed.DeleteSelection();
		return;
	}
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::INFO), ed.Context() == kRootId ? "Modèle" : "Groupe ouvert");
	u.Label(std::format("{} faces · {} arêtes", doc.Root().FaceCount(), doc.Root().EdgeCount()), th.text, 14.f);
	u.Label(std::format("{} groupes · {} matériaux · {} scènes", doc.Entities().size(), doc.Materials().size(), doc.Views().size()), th.textDim, 13.f);
	const math::FAABB box = doc.Bounds();
	if (box.min.x <= box.max.x)
		u.Label(std::format("Emprise : {} × {} × {}", modeling::FormatLength(box.max.x - box.min.x).c_str(), modeling::FormatLength(box.max.z - box.min.z).c_str(),
							modeling::FormatLength(box.max.y - box.min.y).c_str()),
				th.textDim, 12.f);
	u.Space(8.f);
	u.Wrapped("Cliquez une face, une arête ou un groupe. Double-clic : ouvrir un groupe. Molette glissée : orbite, Maj ou clic droit : "
			  "panoramique. Pendant un outil, tapez une cote puis Entrée.",
			  th.textDim, 12.f);
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), "Inférences");
	u.Checkbox("Axes depuis le point précédent", ed.inference.inferAxes);
	u.Checkbox("Origine", ed.inference.inferOrigin);
	u.SliderFloat("Grille au sol", ed.inference.gridStep, 0.f, 1.f, "{:.2f} m");
	u.SliderFloat("Pas d'angle", ed.inference.angleStepDeg, 0.f, 90.f, "{:.0f}°");
	u.SliderFloat("Tolérance", ed.inference.pixelTolerance, 4.f, 24.f, "{:.0f} px");
	// Environnement de la scène (enregistré avec elle) : ambiance, soleil, brouillard.
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::WB_SUNNY), "Environnement");
	EnvironmentSettings env = doc.Environment();
	const auto presets = EnvironmentPresets();
	std::vector<std::string_view> names{"Ambiance…"};
	for (const auto &[name, preset] : presets)
		names.push_back(name);
	int preset = 0;
	if (u.Choice(u.Next(30), "env-preset", preset, names) && preset > 0)
		env = presets[size_t(preset - 1)].second;
	u.SliderFloat("Heure", env.sunHour, 5.f, 21.f, "{:.1f} h");
	u.SliderFloat("Soleil", env.sunIntensity, 0.f, 5.f, "{:.2f}");
	u.SliderFloat("Ciel", env.ambient, 0.f, 2.f, "{:.2f}");
	u.SliderFloat("Brouillard", env.fogDensity, 0.f, 0.2f, "{:.3f}");
	u.SliderFloat("Exposition", env.exposure, 0.2f, 3.f, "{:.2f}");
	if (env != doc.Environment()) {
		ed.Change("Environnement", preset == 0); // glissé : un pas ; ambiance choisie : un pas à elle
		ed.Doc().Environment() = env;
	}
	// Qualité du rendu (post-traitement : bloom, rayons…).
	int quality = int(m_display.quality);
	static constexpr std::array<std::string_view, 4> kQualities = {"Basse", "Moyenne", "Élevée", "Ultra"};
	if (u.Choice(u.Next(30), "render-quality", quality, kQualities))
		m_display.quality = RenderQuality(quality);
	u.Row({1.f, 1.f}, 28.f);
	u.Checkbox("Ombres", m_display.shadows);
	u.Checkbox("Lampes", m_display.lamps);
	DrawBehavior(); // sans sélection : réglages de la visite et script de la scène
}

void SandboxApp::DrawOutliner() {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Document &doc = ed.Doc();
	u.Section(icon::Outline, "Structure");
	u.Wrapped("Glisser un nœud sur un autre : il en devient l'enfant (sur « Modèle » : racine).", th.textDim, 11.f);
	// Recherche par nom : n'affiche que les nœuds correspondants (et leurs ancêtres).
	u.TextInput("outliner-filter", m_outlinerFilter, nullptr, "filtrer par nom…");
	const bool filtering = !m_outlinerFilter.empty();
	const auto lower = [](std::string_view s) {
		std::string out(s);
		std::ranges::transform(out, out.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
		return out;
	};
	const std::string needle = filtering ? lower(m_outlinerFilter) : std::string();
	const auto nameMatches = [&](EntityId id) -> bool {
		const Entity *e = doc.Find(id);
		return e && lower(e->name.c_str()).find(needle) != std::string::npos;
	};
	const auto subtreeMatches = [&](auto &&self, EntityId id) -> bool {
		if (nameMatches(id))
			return true;
		for (EntityId child : doc.Children(id))
			if (self(self, child))
				return true;
		return false;
	};
	const bool dragging = m_dragNode != 0 && u.MouseDown() && std::abs(u.MouseY() - m_dragFromY) > 4.f;
	if (!dragging)
		m_dropTarget = ~EntityId(0);
	const auto row = [&](uint64_t key, float indent, std::string_view name, std::string_view detail, bool selected, bool active, bool highlight = false) {
		const sdl3::FRect r = u.Next(38);
		u.PushId(key);
		const auto it = u.Interact(u.MakeId("row"), r);
		u.PopId();
		c.FillRoundRect(r, 6.f, selected ? sdl3::WithAlpha(th.accent, 90) : it.hovered ? th.widgetHover : th.widget);
		if (active)
			c.StrokeRoundRect(r, 6.f, th.accent);
		c.Text(r.x + 34.f + indent, r.y + 3.f, c.Ellipsize(name, r.w - 100.f - indent, 14.f), highlight ? th.accent : th.text, 14.f);
		c.Text(r.x + 34.f + indent, r.y + 20.f, c.Ellipsize(detail, r.w - 100.f - indent, 11.f), th.textDim, 11.f);
		return std::pair{it, r};
	};
	const auto dropZone = [&](const sdl3::FRect &r, EntityId target) { // survol pendant un glissé : cible
		if (dragging && r.Contains(u.MouseX(), u.MouseY()) && target != m_dragNode && !doc.IsAncestor(m_dragNode, target)) {
			m_dropTarget = target;
			c.StrokeRoundRect(r, 6.f, th.success, 2.f);
		}
	};
	{
		const auto [it, r] = row(0, 0.f, "Modèle (géométrie libre)", std::format("{} faces", doc.Root().FaceCount()), false, ed.Context() == kRootId);
		c.Icon({r.x + 6.f, r.y + 7.f, 24.f, 24.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::CATEGORY), th.textDim, 18.f);
		dropZone(r, 0);
		if (it.leftClicked) {
			ed.CloseAllGroups();
			ed.ClearSelection();
		}
	}
	// Nœuds, en arbre (profondeur d'abord) ; un nœud replié cache sa descendance.
	const auto drawNode = [&](auto &&self, EntityId id, int depth) -> void {
		Entity *e = doc.Find(id);
		if (!e)
			return;
		const std::vector<EntityId> children = doc.Children(id);
		// Filtre : on n'affiche que les nœuds correspondants et leurs ancêtres.
		const bool match = filtering && nameMatches(id);
		if (filtering && !match) {
			bool any = false;
			for (EntityId child : children)
				if (subtreeMatches(subtreeMatches, child)) {
					any = true;
					break;
				}
			if (!any)
				return;
		}
		const float indent = float(depth) * 16.f;
		const bool selected = std::ranges::find(ed.SelectedEntities(), id) != ed.SelectedEntities().end();
		const std::string tag = e->tag < doc.Tags().size() ? std::string(doc.Tags()[e->tag].name.c_str()) : std::string();
		std::string detail = std::format("{} · {} faces", e->type.c_str(), e->model.FaceCount());
		if (!children.empty())
			detail += std::format(" · {} enfant{}", children.size(), children.size() > 1 ? "s" : "");
		if (e->component)
			detail += std::format(" · composant ×{}", doc.InstanceCount(e->component));
		if (!tag.empty() && e->tag != 0)
			detail += " · " + tag;
		const auto [it, r] = row(id, indent, e->name.c_str(), detail, selected, ed.Context() == id, match);
		const bool collapsed = m_collapsed.contains(id) && !filtering; // filtre : tout déplié
		if (!children.empty() && u.IconButton({r.x + 2.f + indent - 14.f + 12.f, r.y + 9.f, 20.f, 20.f},
											  ui::icons::Codepoint(collapsed ? ui::icons::MaterialIcons::CHEVRON_RIGHT : ui::icons::MaterialIcons::EXPAND_MORE),
											  collapsed ? "Déplier" : "Replier")) {
			if (collapsed)
				m_collapsed.erase(id);
			else
				m_collapsed.insert(id);
		} else if (children.empty()) {
			c.Icon({r.x + 6.f + indent, r.y + 7.f, 24.f, 24.f}, icon::Group, doc.IsVisible(*e) ? th.accent : th.textDim, 18.f);
		}
		dropZone(r, id);
		if (it.held && m_dragNode == 0 && u.MousePressed()) {
			m_dragNode = id;
			m_dragFromY = u.MouseY();
		}
		if (u.IconButton({r.Right() - 58.f, r.y + 6.f, 26.f, 26.f},
						 e->visible ? ui::icons::Codepoint(ui::icons::MaterialIcons::VISIBILITY) : ui::icons::Codepoint(ui::icons::MaterialIcons::VISIBILITY_OFF),
						 e->visible ? "Masquer" : "Afficher")) {
			ed.Checkpoint(e->visible ? "Masquer" : "Afficher");
			e = doc.Find(id);
			e->visible = !e->visible;
			doc.Touch();
		} else if (u.IconButton({r.Right() - 30.f, r.y + 6.f, 26.f, 26.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::LOCK),
								e->locked ? "Déverrouiller" : "Verrouiller", true, e->locked ? th.warning : th.textDim)) {
			ed.Checkpoint(e->locked ? "Déverrouiller" : "Verrouiller");
			e = doc.Find(id);
			e->locked = !e->locked;
		} else if (it.leftClicked) {
			if (u.Shift() || u.Ctrl())
				ed.SelectEntity(id, true);
			else
				ed.Reveal(id); // ses ancêtres s'ouvrent : éditable à son niveau
		}
		if (!collapsed)
			for (const EntityId child : children)
				self(self, child, depth + 1);
	};
	for (const EntityId id : doc.Children(0))
		drawNode(drawNode, id, 0);
	if (filtering && !std::ranges::any_of(doc.Entities(), [&](const Entity &e) { return nameMatches(e.id); }))
		u.Wrapped(std::format("Aucun groupe ne correspond à « {} ».", m_outlinerFilter), th.textDim, 12.f);
	else if (doc.Entities().empty())
		u.Wrapped("Aucun groupe : sélectionnez de la géométrie puis Ctrl+G (des groupes choisis : un groupe parent).", th.textDim, 12.f);
	// Dépôt : changement de parent (un pas d'annulation).
	if (m_dragNode != 0 && !u.MouseDown()) {
		if (m_dropTarget != ~EntityId(0) && ed.Reparent(m_dragNode, m_dropTarget))
			m_collapsed.erase(m_dropTarget);
		m_dragNode = 0;
		m_dropTarget = ~EntityId(0);
	} else if (dragging) {
		if (const Entity *moving = doc.Find(m_dragNode)) // étiquette sous le curseur
			c.Text(u.MouseX() + 14.f, u.MouseY() + 6.f, moving->name.c_str(), th.accent, 13.f);
	}
}

void SandboxApp::DrawMaterials() {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Document &doc = ed.Doc();
	u.Section(icon::Materials, "Matériaux");
	u.Wrapped("Cliquez une teinte : elle devient celle du pot de peinture (B) et peint la sélection.", th.textDim, 12.f);
	const auto &mats = doc.Materials();
	constexpr float kCell = 60.f, kGap = 8.f;
	const sdl3::FRect head = u.Next(0);
	const int perRow = std::max(1, int((head.w + kGap) / (kCell + kGap)));
	for (size_t i = 0; i < mats.size(); i += size_t(perRow)) {
		const sdl3::FRect line = u.Next(kCell + 18.f);
		for (size_t k = i; k < std::min(mats.size(), i + size_t(perRow)); ++k) {
			const sdl3::FRect cell{line.x + float(k - i) * (kCell + kGap), line.y, kCell, kCell};
			u.PushId(uint64_t(k) + 1);
			const auto it = u.Interact(u.MakeId("swatch"), cell);
			u.PopId();
			c.FillRoundRect(cell, 8.f, ToGfx(mats[k].color));
			c.StrokeRoundRect(cell, 8.f, ed.material == k ? th.accent : it.hovered ? th.text : th.panelBorder, ed.material == k ? 3.f : 1.f);
			c.Text(cell.x, cell.Bottom() + 2.f, c.Ellipsize(mats[k].name.c_str(), kCell + kGap - 2.f, 11.f), th.textDim, 11.f);
			if (it.leftClicked) {
				ed.PaintSelection(uint32_t(k));
				m_pickColor = ToGfx(mats[k].color);
				if (ed.Selection().IsEmpty() && ed.SelectedEntities().empty())
					ed.SetTool(modeling::ToolKind::PAINT);
			}
		}
	}
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE), "Couleur");
	u.ColorPicker("material-color", m_pickColor, 150.f);
	u.Row({1.f, 1.f}, 30.f);
	if (u.Button("Appliquer")) { // couleur → matériau courant
		ed.Checkpoint("Couleur du matériau");
		doc.SetMaterialColor(ed.material, ToModel(m_pickColor));
	}
	if (u.Button("Nouveau")) { // nouveau matériau de cette couleur
		ed.Checkpoint("Nouveau matériau");
		ed.material = doc.AddMaterial(String::Format("Matériau {}", doc.Materials().size() + 1), ToModel(m_pickColor));
	}
	// Rendu du matériau courant (plastique, métal, verre, émissif, uni).
	if (ed.material < doc.Materials().size()) {
		modeling::Material next = doc.Materials()[ed.material];
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), std::format("Rendu de « {} »", next.name.c_str()));
		static constexpr std::array<std::string_view, 5> kKinds = {"Plastique", "Métal", "Verre", "Émissif", "Uni (sans éclairage)"};
		int kind = int(next.kind);
		if (u.Choice(u.Next(30), "material-kind", kind, kKinds))
			next.kind = modeling::MaterialKind(kind);
		if (next.kind == modeling::MaterialKind::Plastic || next.kind == modeling::MaterialKind::Metal)
			u.SliderFloat("Rugosité", next.roughness, 0.02f, 1.f);
		if (next.kind == modeling::MaterialKind::Emissive)
			u.SliderFloat("Intensité", next.strength, 0.f, 20.f, "{:.1f}");
		u.SliderFloat("Opacité", next.color.a, 0.05f, 1.f);
		if (!(next.kind == doc.Materials()[ed.material].kind && next.roughness == doc.Materials()[ed.material].roughness &&
			  next.strength == doc.Materials()[ed.material].strength && next.color == doc.Materials()[ed.material].color)) {
			ed.Change("Rendu du matériau", u.MouseDown());
			ed.Doc().SetMaterial(ed.material, next);
		}
	}
}

void SandboxApp::DrawTags() {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Document &doc = ed.Doc();
	u.Section(icon::Tag, "Balises");
	u.Wrapped("Une balise masque d'un coup tous les groupes qui la portent (onglet Entité pour l'attribuer).", th.textDim, 12.f);
	for (size_t i = 0; i < doc.Tags().size(); ++i) {
		Tag &t = doc.Tags()[i];
		bool visible = t.visible;
		u.PushId(uint64_t(i) + 1);
		size_t count = 0;
		for (const Entity &e : doc.Entities())
			count += e.tag == i ? 1 : 0;
		if (u.Checkbox(std::format("{} ({})", t.name.c_str(), count), visible)) {
			ed.Checkpoint(visible ? "Afficher la balise" : "Masquer la balise");
			t.visible = visible;
			doc.Touch();
		}
		u.PopId();
	}
	bool add = false;
	u.TextInput("##newtag", m_newTag, &add, "nouvelle balise…");
	if ((u.Button("Ajouter la balise", !m_newTag.empty()) || add) && !m_newTag.empty()) {
		ed.Checkpoint("Nouvelle balise");
		doc.Tags().push_back({String(m_newTag.c_str()), true});
		m_newTag.clear();
	}
}

void SandboxApp::DrawScenes() {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Document &doc = ed.Doc();
	u.Section(icon::Scenes, "Scènes");
	u.Wrapped("Points de vue enregistrés avec le document : un clic y ramène la caméra.", th.textDim, 12.f);
	for (size_t i = 0; i < doc.Views().size(); ++i) {
		const SceneView &v = doc.Views()[i];
		const sdl3::FRect r = u.Next(32);
		u.PushId(uint64_t(i) + 1);
		if (u.ButtonAt({r.x, r.y, r.w - 36.f, r.h}, std::format("{}{}", v.name.c_str(), v.orthographic ? " · parallèle" : "")))
			ed.camera.Restore(v);
		const bool remove = u.IconButton({r.Right() - 32.f, r.y + 2.f, 28.f, 28.f}, ui::icons::Codepoint(ui::icons::MaterialIcons::DELETE), "Supprimer la scène");
		u.PopId();
		if (remove) {
			ed.Checkpoint("Supprimer la scène");
			doc.Views().erase(doc.Views().begin() + std::ptrdiff_t(i));
			break;
		}
	}
	bool add = false;
	u.TextInput("##newview", m_newView, &add, "nom de la scène…");
	if (u.Button("Ajouter la vue actuelle") || add) {
		ed.Checkpoint("Ajouter une scène");
		doc.Views().push_back(ed.camera.Save(m_newView.empty() ? String::Format("Scène {}", doc.Views().size() + 1) : String(m_newView.c_str())));
		m_newView.clear();
	}
}

// ── Commandes (historique annuler/rétablir, comme le Command Stack de Fyrox) ──

void SandboxApp::DrawCommands() {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	const History &h = ed.Undos();
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::HISTORY), "Commandes");
	u.Wrapped("Historique annuler/rétablir : un clic revient à l'étape choisie.", th.textDim, 11.f);
	u.Row({1.f, 1.f, 1.f}, 28.f);
	if (u.Button("Annuler", h.CanUndo()))
		(void)ed.Undo();
	if (u.Button("Rétablir", h.CanRedo()))
		(void)ed.Redo();
	if (u.Button("Effacer", h.CanUndo() || h.CanRedo()))
		ed.ClearHistory();
	// Listes (la plus récente d'abord) ; cliquer une étape y ramène en une série d'annulations.
	const std::vector<String> undo = h.UndoLabels();
	const std::vector<String> redo = h.RedoLabels();
	const auto list = [&](std::string_view id, std::string_view title, const std::vector<String> &labels, bool isRedo) {
		if (labels.empty())
			return;
		u.Label(std::format("{} — {} étape{}", title, labels.size(), labels.size() > 1 ? "s" : ""), th.textDim, 11.f);
		for (size_t i = 0; i < labels.size(); ++i) {
			const sdl3::FRect r = u.Next(24.f);
			u.PushId(uint64_t(i) + (isRedo ? 8000 : 7000));
			const auto it = u.Interact(u.MakeId(id), r);
			u.PopId();
			if (it.hovered)
				c.FillRect(r, th.widgetHover);
			c.Text(r.x + 4.f, r.y + 4.f, c.Ellipsize(labels[i].c_str(), r.w - 8.f, 12.5f), isRedo ? th.textDim : th.text, 12.5f);
			if (it.leftClicked)
				for (size_t k = 0; k <= i; ++k)
					(void)(isRedo ? ed.Redo() : ed.Undo());
		}
	};
	list("cmd-undo", "Annuler", undo, false);
	list("cmd-redo", "Rétablir", redo, true);
	if (undo.empty() && redo.empty())
		u.Wrapped("Aucune commande : les modifications apparaissent ici à mesure que vous modélisez.", th.textDim, 12.f);
}

// ── Ressources ───────────────────────────────────────────────────────────────

void SandboxApp::ScanAssets() {
	namespace fs = std::filesystem;
	m_assets.clear();
	m_assetsFor = m_path;
	std::error_code ec;
	const auto lower = [](std::string s) {
		std::ranges::transform(s, s.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
		return s;
	};
	const auto scan = [&](const fs::path &root, bool project) {
		if (!fs::is_directory(root, ec))
			return;
		for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator();
			 it.increment(ec)) {
			if (ec || it.depth() > 3)
				break;
			if (!it->is_regular_file(ec))
				continue;
			const fs::path &p = it->path();
			const std::string ext = lower(p.extension().string());
			Asset a;
			a.path = p.string();
			a.name = fs::relative(p, root, ec).generic_string();
			if (ext == ".gltf" || ext == ".glb" || ext == ".model")
				a.kind = Asset::Kind::Model;
			else if (project && ext == ".scene")
				a.kind = Asset::Kind::Scene;
			else if (project && ext == ".object")
				a.kind = Asset::Kind::Object;
			else if (project && (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp"))
				a.kind = Asset::Kind::Image;
			else if (project && ext == ".script")
				a.kind = Asset::Kind::Script;
			else
				continue;
			if (a.kind == Asset::Kind::Image || a.kind == Asset::Kind::Script) // attribués : relatifs à la scène
				a.value = m_path.empty() ? a.name : fs::relative(p, fs::path(m_path).parent_path(), ec).generic_string();
			m_assets.push_back(std::move(a));
		}
	};
	if (m_project)
		scan(m_project->Folder(), true);
	else if (!m_path.empty())
		scan(fs::path(m_path).parent_path(), true);
	scan("assets/models", false);
	for (const std::string &file : m_sound.Files())
		m_assets.push_back({Asset::Kind::Sound, file, file, file});
	std::ranges::stable_sort(m_assets, {}, [](const Asset &a) { return int(a.kind); });
}

void SandboxApp::DrawAssets() {
	ui::Context &u = Ui();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	if (m_assetsFor != m_path)
		ScanAssets();
	u.Row({2.2f, 1.f}, 30.f);
	u.TextInput("asset-filter", m_assetFilter, nullptr, "filtrer…");
	if (u.Button("Actualiser"))
		ScanAssets();
	u.SliderFloat("Échelle", m_importScale, 0.01f, 10.f, "×{:.2f}");
	Entity *target = ed.SelectedEntities().size() == 1 ? ed.Doc().Find(ed.SelectedEntities().front()) : nullptr;
	static constexpr std::array<std::string_view, 6> kTitles = {"Scènes du projet", "Objets du projet", "Modèles (glTF, .model)", "Sons",
																"Scripts du projet", "Images (calque 2D)"};
	static constexpr std::array<ui::icons::MaterialIcons, 6> kIcons = {ui::icons::MaterialIcons::DASHBOARD, ui::icons::MaterialIcons::CATEGORY,
																	   ui::icons::MaterialIcons::VIEW_IN_AR, ui::icons::MaterialIcons::VOLUME_UP,
																	   ui::icons::MaterialIcons::CODE, ui::icons::MaterialIcons::IMAGE};
	// Projet : le groupe sélectionné devient un objet réutilisable (objects/<nom>.object).
	if (m_project) {
		u.Label(std::format("Projet « {} »", m_project->name.c_str()), th.textDim, 12.f);
		if (u.Button("Enregistrer le groupe comme objet", target != nullptr) && target) {
			const std::string fileName = std::string(target->name) + ".object";
			const std::filesystem::path file = m_project->Folder() / "objects" / fileName;
			if (auto error = ed.Doc().SaveObject(target->id, file)) {
				u.Notify(std::string(*error), th.danger, 5.f);
			} else {
				sandbox::Project::AddUnique(m_project->objects, m_project->Relative(file));
				if (auto error2 = m_project->Save())
					u.Notify(std::string(*error2), th.danger, 5.f);
				else
					u.Notify("Objet enregistré : objects/" + fileName, th.success);
				ScanAssets();
			}
		}
	}
	Item2D *image = ed.Doc().Overlay().Find(m_item2D);
	if (image && image->shape != Shape2D::Image)
		image = nullptr;
	int section = -1;
	for (size_t i = 0; i < m_assets.size(); ++i) {
		const Asset &a = m_assets[i];
		if (!m_assetFilter.empty() && a.name.find(m_assetFilter) == std::string::npos)
			continue;
		if (int(a.kind) != section) {
			section = int(a.kind);
			u.Section(ui::icons::Codepoint(kIcons[size_t(section)]), kTitles[size_t(section)]);
			if (a.kind == Asset::Kind::Scene || a.kind == Asset::Kind::Object || a.kind == Asset::Kind::Model)
				; // actions sans cible
			else if (a.kind == Asset::Kind::Image)
				u.Wrapped(image ? std::format("Attribuer à l'élément « {} »", image->name.c_str()) : std::string("Sélectionnez une image du calque 2D"),
						  th.textDim, 11.f);
			else if (a.kind != Asset::Kind::Model)
				u.Wrapped(target ? std::format("Attribuer au groupe « {} »", target->name.c_str()) : std::string("Sélectionnez un groupe pour attribuer"),
						  th.textDim, 11.f);
		}
		u.PushId(uint64_t(i) + 900);
		u.Row({2.2f, 1.f}, 28.f);
		u.Label(a.name, th.text, 13.f);
		if (a.kind == Asset::Kind::Scene) {
			if (u.Button("Ouvrir", a.path != m_path)) {
				if (ed.Modified() && m_pendingScene != a.path) { // comme la fermeture : un second clic confirme
					m_pendingScene = a.path;
					u.Notify("Scène modifiée non enregistrée : cliquez à nouveau pour l'abandonner (ou Ctrl+S)", th.warning, 4.f);
				} else {
					m_pendingScene.clear();
					(void)LoadScene(a.path);
					return; // liste des ressources relue
				}
			}
		} else if (a.kind == Asset::Kind::Object) {
			if (u.Button("Insérer")) {
				const math::FVector3 t = ed.camera.Target();
				ed.Checkpoint("Insérer un objet");
				if (auto r = ed.Doc().InsertObject(a.path, {t.x, 0.f, t.z}); r.IsError())
					u.Notify(std::string(r.Error()), th.danger, 5.f);
				else {
					ed.ClearSelection();
					ed.SelectEntity(r.Value());
					ShowPanel("viewport");
				}
			}
		} else if (a.kind == Asset::Kind::Model) {
			if (u.Button("Importer")) {
				const math::FVector3 t = ed.camera.Target();
				if (auto r = ed.ImportModel(String(a.path), {t.x, 0.f, t.z}, m_importScale); r.IsError())
					u.Notify(std::string(r.Error()), th.danger, 5.f);
				else
					ShowPanel("viewport");
			}
		} else if (a.kind == Asset::Kind::Image) {
			if (u.Button("Attribuer", image != nullptr) && image) {
				ed.Change("Image (2D)", false);
				ed.Doc().Overlay().Find(m_item2D)->image = String(a.value);
			}
		} else if (u.Button("Attribuer", target != nullptr) && target) {
			ed.Checkpoint(a.kind == Asset::Kind::Sound ? "Attribuer un son" : "Attribuer un script");
			target = ed.Doc().Find(ed.SelectedEntities().front());
			if (a.kind == Asset::Kind::Sound) {
				target->behavior.sound = String(a.value);
			} else {
				std::ifstream in(a.path, std::ios::binary);
				std::stringstream text;
				text << in.rdbuf();
				target->behavior.scriptFile = String(a.value);
				target->behavior.script = String(text.str());
			}
		}
		u.PopId();
	}
	if (m_assets.empty())
		u.Wrapped("Aucune ressource : placez des modèles glTF dans le dossier du projet ou dans assets/models.", th.textDim, 12.f);
}

} // namespace sandbox
