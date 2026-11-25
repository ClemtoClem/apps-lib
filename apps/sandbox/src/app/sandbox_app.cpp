#include "sandbox/app/sandbox_app.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <iterator>
#include <fstream>

#include <sdl3/io/dialog.hpp>
#include <sdl3/render/svg.hpp>

#include <math/rect.hpp>

#include "sandbox/modeling/io.hpp"

namespace sandbox {

namespace {

using modeling::ToolKind;

double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

struct ToolButton {
	ToolKind kind;
	char32_t glyph;
};

/// Barre d'outils : groupes séparés (sélection, dessin, modification, transformation, mesure).
constexpr std::array<std::array<ToolButton, 4>, 5> kToolGroups = {{
	{{{ToolKind::SELECT, icon::Select}, {ToolKind::ERASER, ui::icons::Codepoint(ui::icons::MaterialIcons::CLEANING_SERVICES)}, {ToolKind::PAINT, ui::icons::Codepoint(ui::icons::MaterialIcons::FORMAT_COLOR_FILL)}, {ToolKind::SELECT, 0}}},
	{{{ToolKind::LINE, ui::icons::Codepoint(ui::icons::MaterialIcons::EDIT)}, {ToolKind::RECTANGLE, icon::Rectangle}, {ToolKind::CIRCLE, icon::Circle}, {ToolKind::POLYGON, icon::Polygon}}},
	{{{ToolKind::PUSH_PULL, icon::PushPull}, {ToolKind::FOLLOW_ME, icon::FollowMe}, {ToolKind::OFFSET, icon::Offset}, {ToolKind::SELECT, 0}}},
	{{{ToolKind::MOVE, ui::icons::Codepoint(ui::icons::MaterialIcons::OPEN_WITH)}, {ToolKind::ROTATE, ui::icons::Codepoint(ui::icons::MaterialIcons::ROTATE_RIGHT)}, {ToolKind::SCALE, icon::Scale}, {ToolKind::SELECT, 0}}},
	{{{ToolKind::TAPE, ui::icons::Codepoint(ui::icons::MaterialIcons::STRAIGHTEN)}, {ToolKind::SELECT, 0}, {ToolKind::SELECT, 0}, {ToolKind::SELECT, 0}}},
}};

std::string ToolTip(ToolKind k) {
	const char key = modeling::ToolShortcut(k);
	const std::string name = modeling::ToolName(k);
	if (key == ' ')
		return name + " (Espace)";
	return key ? std::format("{} ({})", name, char(std::toupper(static_cast<unsigned char>(key)))) : name;
}

/// Nom du fichier d'icône SVG (SketchUp) d'un outil, sans l'extension.
const char *ToolIconName(ToolKind k) {
	switch (k) {
	case ToolKind::SELECT: return "select";
	case ToolKind::LINE: return "line";
	case ToolKind::RECTANGLE: return "rectangle";
	case ToolKind::CIRCLE: return "circle";
	case ToolKind::POLYGON: return "polygon";
	case ToolKind::PUSH_PULL: return "push_pull";
	case ToolKind::OFFSET: return "offset";
	case ToolKind::FOLLOW_ME: return "follow_me";
	case ToolKind::MOVE: return "move";
	case ToolKind::ROTATE: return "rotate";
	case ToolKind::SCALE: return "scale";
	case ToolKind::ERASER: return "eraser";
	case ToolKind::PAINT: return "paint";
	case ToolKind::TAPE: return "tape";
	}
	return "select";
}

/// Axes du gizmo de déplacement : X rouge, Y bleu (vertical), Z vert.
struct GizmoAxis {
	math::FVector3 dir;
	uint8_t r, g, b;
};
constexpr GizmoAxis kGizmoAxes[3] = {{{1.f, 0.f, 0.f}, 220, 50, 50}, {{0.f, 1.f, 0.f}, 50, 90, 230}, {{0.f, 0.f, 1.f}, 40, 170, 60}};

} // namespace

void RegisterIcons() {
	sdl3::Canvas::RegisterIcons({icon::Select, icon::Rectangle, icon::Circle, icon::Polygon, icon::PushPull, icon::FollowMe, icon::Offset, icon::Scale,
								icon::Orbit, icon::Pan, icon::Extents, icon::Group, icon::Explode, icon::NewFile, icon::XRay, icon::Axes, icon::Edges,
								icon::Scenes, icon::Tag, icon::Outline, icon::Entity, icon::Materials, icon::Play,
								ui::icons::Codepoint(ui::icons::MaterialIcons::STOP), ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW),
								// Onglets du dock et outils de l'onglet Script.
								ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR), ui::icons::Codepoint(ui::icons::MaterialIcons::CODE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::CHECK_CIRCLE), ui::icons::Codepoint(ui::icons::MaterialIcons::COMMENT),
								ui::icons::Codepoint(ui::icons::MaterialIcons::FORMAT_INDENT_DECREASE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::FORMAT_INDENT_INCREASE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::SELECT_ALL), ui::icons::Codepoint(ui::icons::MaterialIcons::TEXT_DECREASE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::TEXT_INCREASE),
								// Vue 2D : formes, ordre, effacement.
								ui::icons::Codepoint(ui::icons::MaterialIcons::DASHBOARD), ui::icons::Codepoint(ui::icons::MaterialIcons::CROP_SQUARE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::PANORAMA_FISH_EYE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::HORIZONTAL_RULE), ui::icons::Codepoint(ui::icons::MaterialIcons::TEXT_FIELDS),
								ui::icons::Codepoint(ui::icons::MaterialIcons::IMAGE), ui::icons::Codepoint(ui::icons::MaterialIcons::FLIP_TO_FRONT),
								ui::icons::Codepoint(ui::icons::MaterialIcons::FLIP_TO_BACK), ui::icons::Codepoint(ui::icons::MaterialIcons::DELETE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::TERMINAL), ui::icons::Codepoint(ui::icons::MaterialIcons::CHEVRON_RIGHT),
								ui::icons::Codepoint(ui::icons::MaterialIcons::EXPAND_MORE), ui::icons::Codepoint(ui::icons::MaterialIcons::CATEGORY),
								ui::icons::Codepoint(ui::icons::MaterialIcons::DESCRIPTION), ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER_OPEN),
								ui::icons::Codepoint(ui::icons::MaterialIcons::HISTORY), ui::icons::Codepoint(ui::icons::MaterialIcons::SCHOOL),
								ui::icons::Codepoint(ui::icons::MaterialIcons::CREATE_NEW_FOLDER),
								ui::icons::Codepoint(ui::icons::MaterialIcons::HELP_OUTLINE),
								ui::icons::Codepoint(ui::icons::MaterialIcons::ALIGN_HORIZONTAL_LEFT)});
}

// ── Cycle de vie ─────────────────────────────────────────────────────────────

bool SandboxApp::OnInit() {
	if (!Gpu() || !m_viewport.Init(*Gpu()))
		return false;
	// Interface retenue : l'arbre de widgets est construit une fois depuis la
	// fabrique `ui::WidgetBuilder`, puis affiché chaque image par `Present`.
	m_ui = std::make_unique<ui::Ui>(Canvas(), Events());
	m_ui->SetImmediateContext(&Ui()); // les panneaux restent en mode immédiat (`ui::Custom`)
	m_ui->SetTheme(Theme());
	if (auto sheet = ui::StyleSheet::Load("assets/ui.css"); sheet.IsOk())
		m_ui->Sheet(std::move(sheet.Value()));
	RegisterShortcuts(); // actions et défauts, avant de charger les réglages persistés
	PopulateCatalog(); // catalogue des outils et fonctionnalités (Aide → Catalogue)
	LoadSettings(); // réglages persistés (fractionnement, raccourcis…) avant de construire les menus
	BuildInterface();
	static constexpr std::array<std::string_view, 7> kTabs = {"entity", "outliner", "materials", "tags", "scenes", "entity", "assets"};
	ShowPanel(kTabs[size_t(std::clamp(m_launch.tab, 0, int(kTabs.size()) - 1))]);
	m_display.shadows = m_launch.shadows;
	LoadRecent();
	if (!m_launch.open.empty()) {
		m_launchPending = true; // options de lancement appliquées une fois le document chargé
		Load(m_launch.open);
	} else if (m_launch.sample) {
		m_launchPending = true;
		Load(std::string(kSampleProject));
	} else
		m_home = m_launch.home; // comme l'ancienne version : choisir un projet d'abord
	UpdateTitle();
	if (!m_launch.workspace.empty())
		ShowPanel(m_launch.workspace);
	return true;
}

void SandboxApp::OnUpdate(double) {
	PollDialogs();
	PollLoad(); // résultat d'une ouverture / création en arrière-plan
	if (m_showShortcuts) { // modal « Raccourcis clavier » : capture, éditeur gelé
		HandleShortcutCapture();
		return;
	}
	if (m_showCatalog)
		return; // catalogue : éditeur gelé (la recherche est gérée par le champ du modal)
	if (m_home)
		return; // accueil : ni raccourcis d'édition ni vue 3D
	if (m_game) {
		for (game::GameMessage &m : m_game->TakeJournal())
			Log(std::move(m));
		HandleGameKeys();
		m_sound.Update();
		if (m_game && m_game->QuitRequested()) // `this.quit()` d'une scène
			StopGame();
		else if (m_game && !m_game->RequestedScene().empty()) { // `this.load(nom)` : autre scène, même état
			const std::string name = m_game->RequestedScene();
			script::Value state = m_game->State();
			StopGame();
			std::optional<std::filesystem::path> file;
			if (m_project)
				for (const String &s : m_project->scenes)
					if (std::filesystem::path(s.c_str()).stem() == name)
						file = m_project->Resolve(s);
			if (!file && !m_path.empty()) // sans index : à côté de la scène courante
				if (const auto near = std::filesystem::path(m_path).parent_path() / (name + ".scene"); std::filesystem::exists(near))
					file = near;
			if (file && LoadScene(file->string())) {
				StartGame();
				if (m_game)
					m_game->SetState(std::move(state));
			} else {
				Ui().Notify(std::format("Scène « {} » introuvable", name), Theme().danger, 5.f);
			}
		}
		return;
	}
	HandleKeyboard();
	// Fichier déposé sur la fenêtre : ouvert.
	for (const std::string &file : Events().DroppedFiles())
		if (file.ends_with(".project") || file.ends_with(".scene") || file.ends_with(".sandbox"))
			Load(file);
	UpdateTitle();
}

void SandboxApp::OnOffscreen() {
	if (m_view.w < 8.f || (!m_viewShown && !m_game))
		return; // vue masquée par un autre onglet : rien à rendre
	const float scale = Canvas().Scale();
	m_viewport.Render(m_editor, Renderer(), int(m_view.w * scale), int(m_view.h * scale), m_display, m_game.get());
}

bool SandboxApp::OnCloseRequested() {
	if (m_editor.Modified())
		Ui().Notify("Modifications non enregistrées : fermez à nouveau pour quitter sans enregistrer", Theme().warning, 4.f);
	const bool second = !m_editor.Modified();
	if (second)
		SaveSettings(); // persiste la disposition (ratios, régions) avant de quitter
	m_editor.MarkSaved(); // un second clic ferme
	return second;
}

void SandboxApp::UpdateTitle() {
	// Projet : son nom, puis la scène si elle s'appelle autrement (« donjon · Égouts »).
	const std::string scene = std::filesystem::path(m_path).stem().string();
	std::string name = m_path.empty() ? std::string("Sans titre") : scene;
	if (m_project)
		name = scene == std::string_view(m_project->name) || m_path.empty() ? std::string(m_project->name) : std::format("{} · {}", m_project->name.c_str(), scene);
	const std::string title = std::format("Sandbox — {}{}", name, m_editor.Modified() ? " *" : "");
	if (title != m_titleShown) {
		m_titleShown = title;
		SetTitle(title);
	}
}

void SandboxApp::OnRender(const sdl3::FRect &area, double) {
	m_viewShown = false; // la vue 3D n'est rendue que si son onglet est affiché
	// Onglet de travail : l'onglet actif du centre pilote la barre d'outils.
	if (!m_game && !m_home) {
		static constexpr std::array<std::string_view, 3> kCenter = {"viewport", "overlay", "script"};
		m_workspace = kCenter[size_t(std::clamp(m_tabCenter, 0, int(kCenter.size()) - 1))];
	}
	SyncMenuChecks();
	SyncVisibility();
	UpdateToolCursor(); // curseur (crayon/flèche) posé avant le rendu de l'interface
	m_ui->SetBlocked(m_showShortcuts || m_showCatalog); // l'interface retenue est gelée derrière le modal
	m_ui->Present(FrameSeconds(), area);
	m_ui->SetBlocked(false);
	if (m_loading.load())
		DrawLoadingOverlay(area); // spinner pendant l'ouverture / la création d'un projet
	if (m_showShortcuts)
		DrawShortcutsModal(area);
	if (m_showCatalog)
		DrawCatalogModal(area);
}

// ── Barre d'outils ───────────────────────────────────────────────────────────

void SandboxApp::DrawToolbar(const sdl3::FRect &bar) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	c.Line(bar.x, bar.Bottom() - 0.5f, bar.Right(), bar.Bottom() - 0.5f, th.panelBorder);
	const float size = bar.h - 8.f;
	float x = bar.x + 8.f;
	const float y = bar.y + 4.f;
	const auto separator = [&] {
		x += 5.f;
		c.Line(x, bar.y + 9.f, x, bar.Bottom() - 9.f, th.panelBorder);
		x += 6.f;
	};
	const auto button = [&](char32_t glyph, const std::string &tip, bool active, bool enabled = true) {
		const sdl3::FRect r{x, y, size, size};
		x += size + 2.f;
		if (active)
			c.FillRoundRect(r, th.radius, sdl3::WithAlpha(th.accent, 70));
		return u.IconButton(r, glyph, tip, enabled, active ? th.accent : sdl3::Color{0, 0, 0, 0});
	};
	// Bouton à icône SVG (SketchUp) : texture blanche teintée, survol et info-bulle.
	const auto iconButton = [&](std::string_view icon, const std::string &tip, bool active, bool enabled = true) {
		const sdl3::FRect r{x, y, size, size};
		x += size + 2.f;
		if (active)
			c.FillRoundRect(r, th.radius, sdl3::WithAlpha(th.accent, 70));
		u.PushId(icon);
		const auto it = enabled ? u.Interact(u.MakeId("tool"), r) : ui::Context::Interaction{};
		u.PopId();
		if (it.hovered || it.held)
			c.FillRoundRect(r, th.radius, it.held ? th.widgetActive : th.widgetHover);
		const sdl3::Color tint = !enabled ? sdl3::WithAlpha(th.textDim, 90) : (active ? th.accent : (it.hovered ? th.text : th.textDim));
		if (sdl3::Texture *tex = ToolIcon(icon)) {
			const float pad = 5.f;
			c.Image(*tex, {r.x + pad, r.y + pad, r.w - 2.f * pad, r.h - 2.f * pad}, tint);
		}
		if (it.hovered && !tip.empty())
			u.Tooltip(tip);
		return it.leftClicked;
	};
	if (button(icon::NewFile, "Nouveau (Ctrl+N)", false))
		NewDocument();
	if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::FOLDER), "Ouvrir (Ctrl+O)", false))
		OpenDocument();
	if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::SAVE), "Enregistrer (Ctrl+S)", false))
		SaveDocument(false);
	separator();
	if (m_workspace == "script" && !m_game) { // outils de l'onglet actif
		DrawScriptTools(x, y, size);
		separator();
		if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW), "Jouer la visite (F5)", false))
			StartGame();
		separator();
	} else if (m_workspace == "overlay" && !m_game) {
		DrawOverlayTools(x, y, size);
		separator();
		if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW), "Jouer la visite (F5)", false))
			StartGame();
		separator();
	} else {
	for (const auto &group : kToolGroups) {
		for (const ToolButton &b : group)
			if (b.glyph && iconButton(ToolIconName(b.kind), ToolTip(b.kind), m_editor.ToolKind() == b.kind && m_navTool == Nav::None)) {
				m_editor.SetTool(b.kind);
				m_navTool = Nav::None;
			}
		separator();
	}
	// Visite : joue le modèle à la première personne (F5), Échap revient à l'édition.
	if (button(m_game ? ui::icons::Codepoint(ui::icons::MaterialIcons::STOP) : ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW),
			   m_game ? "Arrêter la visite (Échap)" : "Jouer la visite (F5)", m_game != nullptr))
		m_game ? StopGame() : StartGame();
	separator();
	if (iconButton("orbit", "Orbite (O) — ou glisser avec la molette", m_navTool == Nav::Orbit))
		m_navTool = m_navTool == Nav::Orbit ? Nav::None : Nav::Orbit;
	if (iconButton("pan", "Panoramique (H) — ou Maj + molette glissée", m_navTool == Nav::Pan))
		m_navTool = m_navTool == Nav::Pan ? Nav::None : Nav::Pan;
	if (iconButton("zoom_extents", "Zoom étendu (Maj+Z)", false))
		m_editor.camera.ZoomExtents(m_editor.Doc().Bounds(), m_viewport.Aspect());
	separator();
	// Vues normalisées et projection.
	static constexpr std::array<std::pair<std::string_view, StandardView>, 4> kViews = {
		{{"Iso", StandardView::Iso}, {"Dessus", StandardView::Top}, {"Face", StandardView::Front}, {"Droite", StandardView::Right}}};
	for (const auto &[label, v] : kViews) {
		const float w = c.MeasureText(label, 13.f) + 18.f;
		if (u.ButtonAt({x, y + 2.f, w, size - 4.f}, label))
			SetView(v);
		x += w + 3.f;
	}
	const bool ortho = m_editor.camera.Orthographic();
	const std::string_view projection = ortho ? "Parallèle" : "Perspective";
	const float pw = c.MeasureText(projection, th.fontSize) + 22.f;
	if (u.ButtonAt({x, y + 2.f, pw, size - 4.f}, projection, true, ortho))
		m_editor.camera.SetOrthographic(!ortho);
	x += pw + 6.f;
	separator();
	if (button(icon::Group, "Grouper (Ctrl+G)", false, !m_editor.Selection().IsEmpty() && m_editor.Context() == kRootId))
		(void)m_editor.GroupSelection();
	if (button(icon::Explode, "Éclater le groupe", false, !m_editor.SelectedEntities().empty()))
		(void)m_editor.ExplodeSelection();
	separator();
	if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::WB_SUNNY), "Ombres", m_display.shadows))
		m_display.shadows = !m_display.shadows;
	if (button(icon::XRay, "Rayons X", m_display.xray))
		m_display.xray = !m_display.xray;
	separator();
	}
	if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::UNDO), m_editor.Undos().CanUndo() ? "Annuler " + std::string(m_editor.Undos().UndoLabel()) + " (Ctrl+Z)" : "Annuler",
			   false, m_editor.Undos().CanUndo()))
		(void)m_editor.Undo();
	if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::REDO), "Rétablir (Ctrl+Y)", false, m_editor.Undos().CanRedo()))
		(void)m_editor.Redo();
	separator();
	if (button(ui::icons::Codepoint(ui::icons::MaterialIcons::HELP_OUTLINE), "Catalogue des outils et fonctionnalités", false))
		m_showCatalog = true;
}

// ── Icônes SVG des outils (SketchUp) et curseur ──────────────────────────────

sdl3::Texture *SandboxApp::ToolIcon(std::string_view name) {
	auto [it, inserted] = m_toolIcons.try_emplace(std::string(name));
	if (inserted) {
		const std::string path = std::string("assets/icons/") + std::string(name) + ".svg";
		if (auto parsed = sdl3::Svg::Load(String(path)); parsed.IsOk()) {
			if (auto surf = parsed->Rasterize(48, 48); surf.IsOk()) {
				if (auto tex = sdl3::Texture::CreateFromSurface(Renderer(), surf.Value()); tex.IsOk())
					it->second.emplace(std::move(tex.Value()));
			}
		}
	}
	return it->second ? &*it->second : nullptr;
}

void SandboxApp::UpdateToolCursor() {
	// Curseurs SketchUp chargés une fois (SVG blanc → noir pour les curseurs).
	if (!m_cursorArrow)
		if (auto img = ui::Image::Load("assets/icons/cursor_arrow.svg"); img.IsOk())
			m_cursorArrow = std::make_shared<ui::Image>(std::move(img.Value()));
	if (!m_cursorPencil)
		if (auto img = ui::Image::Load("assets/icons/cursor_pencil.svg"); img.IsOk())
			m_cursorPencil = std::make_shared<ui::Image>(std::move(img.Value()));
	if (!m_viewportSheet)
		return;
	// Vue 3D active, en édition, sans outil de navigation : crayon pour les outils,
	// flèche pour la sélection ; sinon le curseur système de l'interface.
	const bool viewport = m_workspace == "viewport" && !m_game && !m_home && !m_showShortcuts && !m_showCatalog;
	if (viewport && m_navTool == Nav::None && m_editor.ToolKind() != ToolKind::SELECT) {
		m_viewportSheet->cursorImage = m_cursorPencil;
		m_viewportSheet->cursorHotX = 7;
		m_viewportSheet->cursorHotY = 17;
	} else if (viewport && m_navTool == Nav::None) {
		m_viewportSheet->cursorImage = m_cursorArrow;
		m_viewportSheet->cursorHotX = 4;
		m_viewportSheet->cursorHotY = 3;
	} else {
		m_viewportSheet->cursorImage = nullptr;
	}
}

// ── Barre d'état ─────────────────────────────────────────────────────────────

void SandboxApp::DrawStatus(const sdl3::FRect &bar) {
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = Theme();
	c.FillRect(bar, th.title);
	c.Line(bar.x, bar.y + 0.5f, bar.Right(), bar.y + 0.5f, th.panelBorder);
	const modeling::ToolPreview p = m_editor.Preview();
	// Mesures (VCB) : saisie en cours, sinon valeur courante de l'outil.
	const sdl3::FRect box{bar.Right() - 220.f, bar.y + 3.f, 212.f, bar.h - 6.f};
	const bool typing = !m_editor.MeasureInput().IsEmpty();
	c.Text(box.x - 70.f, bar.y + 6.f, "Mesures", th.textDim, 13.f);
	c.FillRoundRect(box, 4.f, th.widget);
	c.StrokeRoundRect(box, 4.f, typing ? th.accent : th.panelBorder);
	std::string value = typing ? std::string(m_editor.MeasureInput()) + "▏" : std::string(p.measure);
	const bool prompt = value.empty() && m_editor.ToolKind() != ToolKind::SELECT && m_navTool == Nav::None;
	if (prompt) // l'outil attend une cote : on peut la taper directement, sans cliquer ici
		value = m_editor.ToolKind() == ToolKind::RECTANGLE ? "tapez largeur;hauteur…" : "tapez une cote…";
	c.Text(box.x + 8.f, box.y + 3.f, c.Ellipsize(value, box.w - 16.f, 13.f), typing ? th.text : prompt ? sdl3::WithAlpha(th.textDim, 140) : th.textDim, 13.f);
	// Outil et consigne (ou dernier message).
	if (m_game) { // visite : état de la partie à la place de l'outil
		c.Icon({bar.x + 6.f, bar.y + 2.f, 22.f, bar.h - 4.f}, icon::Play, th.accent, 16.f);
		c.Text(bar.x + 30.f, bar.y + 6.f, "Visite", th.text, 13.f, sdl3::CanvasFont::Bold);
		const game::GameSession &g = *m_game;
		const math::FVector3 feet = g.PlayerFeet();
		c.Text(bar.x + 92.f, bar.y + 6.f,
			   std::format("{:.0f} s · position {:.1f} ; {:.1f} ; {:.1f} · {} script(s) · Échap : retour à l'édition (le document est intact)", g.Time(),
						   feet.x + 0.f, std::abs(feet.y) < 0.05f ? 0.f : feet.y, feet.z + 0.f, g.ScriptCount()),
			   th.textDim, 13.f);
		return;
	}
	const std::string tool = m_navTool == Nav::Orbit ? "Orbite" : m_navTool == Nav::Pan ? "Panoramique" : modeling::ToolName(m_editor.ToolKind());
	const char *iconName = m_navTool == Nav::Orbit ? "orbit" : m_navTool == Nav::Pan ? "pan" : ToolIconName(m_editor.ToolKind());
	if (sdl3::Texture *tex = ToolIcon(iconName))
		c.Image(*tex, {bar.x + 8.f, bar.y + 5.f, 18.f, 18.f}, th.accent);
	c.Text(bar.x + 30.f, bar.y + 6.f, tool, th.text, 13.f, sdl3::CanvasFont::Bold);
	const float tx = bar.x + 40.f + c.MeasureText(tool, 13.f, sdl3::CanvasFont::Bold);
	const bool error = m_editor.StatusIsError();
	const std::string message = !m_editor.Status().IsEmpty() ? std::string(m_editor.Status()) : std::string(p.hint);
	c.Text(tx, bar.y + 6.f, c.Ellipsize(message, box.x - 90.f - tx, 13.f), error ? th.danger : th.textDim, 13.f);
}

// ── Vue 3D : souris ──────────────────────────────────────────────────────────

Option<math::FVector2> SandboxApp::Project(const math::FVector3 &world, const sdl3::FRect &view) const {
	const math::FVector4 h = m_editor.camera.Camera().ViewProjection(view.w / std::max(view.h, 1.f)) * math::FVector4(world, 1.f);
	if (h.w <= 1e-6f)
		return NONE;
	return Some(math::FVector2{view.x + (h.x / h.w + 1.f) * 0.5f * view.w, view.y + (1.f - h.y / h.w) * 0.5f * view.h});
}

Pointer SandboxApp::MakePointer(const sdl3::FRect &view) const {
	const ui::Context &u = const_cast<SandboxApp *>(this)->Ui();
	Pointer p;
	p.cursor = {u.MouseX(), u.MouseY()};
	p.ray = m_editor.camera.Camera().ScreenRay((p.cursor.x - view.x) / view.w, (p.cursor.y - view.y) / view.h, view.w / std::max(view.h, 1.f));
	p.project = [this, view](const math::FVector3 &w) { return Project(w, view); };
	p.shift = u.Shift();
	p.ctrl = u.Ctrl();
	return p;
}

void SandboxApp::HandleViewport(const sdl3::FRect &view) {
	ui::Context &u = Ui();
	const float mx = u.MouseX(), my = u.MouseY();
	const bool menuOpen = u.MenuOpen() || (m_ui->Find("menu") && m_ui->Find("menu")->openMenu >= 0);
	const bool over = u.MouseOver(view) && !menuOpen;
	const bool middle = u.MouseDown(sdl3::MouseButton::Middle), right = u.MouseDown(sdl3::MouseButton::Right), left = u.MouseDown();
	UpdateGizmo(); // centre et longueur du gizmo de déplacement
	// Navigation : molette (ou outil Orbite) = orbite, Maj ou clic droit = panoramique.
	if (m_nav == Nav::None && over) {
		if (u.MousePressed(sdl3::MouseButton::Middle))
			m_nav = u.Shift() ? Nav::Pan : Nav::Orbit;
		else if (u.MousePressed(sdl3::MouseButton::Right))
			m_nav = Nav::Pan;
		else if (m_navTool != Nav::None && u.MousePressed())
			m_nav = m_navTool;
		m_lastX = mx, m_lastY = my;
	}
	if (m_nav != Nav::None) {
		if (!middle && !right && !left)
			m_nav = Nav::None;
		else {
			if (m_nav == Nav::Orbit)
				m_editor.camera.Orbit(mx - m_lastX, my - m_lastY);
			else
				m_editor.camera.Pan(mx - m_lastX, my - m_lastY, view.h);
			m_lastX = mx, m_lastY = my;
			return;
		}
	}
	// Gizmo de transformation : glissé contraint (se poursuit hors de la vue).
	if (m_gizmoDragging) {
		if (left) {
			const Pointer pointer = MakePointer(view);
			const modeling::ToolKind kind = m_editor.ToolKind();
			if (kind == modeling::ToolKind::MOVE) {
				const math::FVector3 current = modeling::ClosestPointOnLine(m_gizmoCenter, kGizmoAxes[m_gizmoAxis].dir, pointer.ray);
				const math::FVector3 delta = current - m_gizmoGrab;
				if (delta.LengthSq() > 1e-12f) {
					m_editor.MoveSelected(delta);
					m_gizmoGrab = current;
				}
			} else if (kind == modeling::ToolKind::ROTATE) {
				if (const auto hit = modeling::IntersectPlane(pointer.ray, m_gizmoCenter, kGizmoAxes[m_gizmoAxis].dir)) {
					const math::FVector3 current = (*hit - m_gizmoCenter).Normalized();
					const float angle = std::atan2(kGizmoAxes[m_gizmoAxis].dir.Dot(m_gizmoGrabDir.Cross(current)), m_gizmoGrabDir.Dot(current));
					if (std::abs(angle) > 1e-6f) {
						m_editor.RotateSelected(m_gizmoCenter, kGizmoAxes[m_gizmoAxis].dir, angle);
						m_gizmoGrabDir = current;
					}
				}
			} else if (kind == modeling::ToolKind::SCALE) {
				const auto px = Project(m_gizmoCenter, view);
				const float dx = pointer.cursor.x - px->x, dy = pointer.cursor.y - px->y;
				const float dist = std::sqrt(dx * dx + dy * dy);
				if (m_gizmoGrabDist > 1e-3f && dist > 1e-3f) {
					m_editor.ScaleSelected(m_gizmoCenter, dist / m_gizmoGrabDist);
					m_gizmoGrabDist = dist;
				}
			}
			return;
		}
		m_gizmoDragging = false;
		m_gizmoAxis = -1;
		return;
	}
	// Sélection par cadre : mise à jour / fin (le tracé se poursuit hors de la vue).
	if (m_marquee) {
		if (left) {
			m_marqueeCurrent = {mx, my};
			return;
		}
		MarqueeSelect(view);
		m_marquee = false;
		return;
	}
	if (!over)
		return;
	const Pointer pointer = MakePointer(view);
	m_editor.Hover(pointer);
	// Survol du gizmo de transformation (mise en évidence de la poignée).
	m_gizmoAxis = m_gizmoLength > 0.f ? PickGizmo(view, pointer) : -1;
	if (u.WheelY() != 0.f) {
		// Zoom vers le point visé : inférence, sinon le plan horizontal de la cible.
		const modeling::Inference &at = m_editor.Hovered();
		math::FVector3 focus = m_editor.camera.Target();
		if (at.IsValid())
			focus = m_editor.ActiveTransform().TransformPoint(at.point);
		else if (std::abs(pointer.ray.dir.y) > 1e-4f) {
			const float t = (focus.y - pointer.ray.origin.y) / pointer.ray.dir.y;
			if (t > 0.f)
				focus = pointer.ray.At(t);
		}
		m_editor.camera.ZoomAt(u.WheelY(), focus);
	}
	if (m_navTool == Nav::None && u.MousePressed()) {
		if (HandleCornerGizmo(view))
			return; // clic dans le repère d'axes (orientation / projection)
		// Saisir le gizmo de transformation (déplacer / rotation / échelle).
		if (m_gizmoLength > 0.f) {
			if (const int axis = PickGizmo(view, pointer); axis >= 0) {
				m_gizmoAxis = axis;
				m_gizmoDragging = true;
				const modeling::ToolKind kind = m_editor.ToolKind();
				if (kind == modeling::ToolKind::MOVE) {
					m_gizmoGrab = modeling::ClosestPointOnLine(m_gizmoCenter, kGizmoAxes[axis].dir, pointer.ray);
					m_editor.Checkpoint("Déplacer le groupe");
				} else if (kind == modeling::ToolKind::ROTATE) {
					if (const auto hit = modeling::IntersectPlane(pointer.ray, m_gizmoCenter, kGizmoAxes[axis].dir))
						m_gizmoGrabDir = (*hit - m_gizmoCenter).Normalized();
					m_editor.Checkpoint("Pivoter le groupe");
				} else if (kind == modeling::ToolKind::SCALE) {
					if (const auto px = Project(m_gizmoCenter, view)) {
						const float dx = pointer.cursor.x - px->x, dy = pointer.cursor.y - px->y;
						m_gizmoGrabDist = std::sqrt(dx * dx + dy * dy);
					}
					m_editor.Checkpoint("Mettre à l'échelle le groupe");
				}
				return;
			}
		}
		const double now = Now();
		const bool doubleClick = now - m_lastClick < 0.35 && std::abs(mx - m_clickX) + std::abs(my - m_clickY) < 6.f;
		m_lastClick = doubleClick ? -1.0 : now;
		m_clickX = mx, m_clickY = my;
		if (doubleClick && m_editor.ToolKind() == ToolKind::SELECT && !m_editor.ToolBusy()) {
			// Double-clic : ouvre le groupe visé, ou sort du groupe ouvert si l'on vise dehors.
			if (const EntityId id = m_editor.PickEntity(pointer.ray))
				(void)m_editor.OpenGroup(id);
			else if (m_editor.Context() != kRootId && !m_editor.Hovered().IsValid())
				m_editor.CloseGroup();
			return;
		}
		// Sélection par cadre : outil Sélection sur du vide (à la racine).
		if (m_editor.ToolKind() == ToolKind::SELECT && !m_editor.ToolBusy() && m_editor.EmptyAt(pointer.ray)) {
			m_marquee = true;
			m_marqueeStart = m_marqueeCurrent = {mx, my};
			return;
		}
		m_editor.Click(pointer);
	}
}

void SandboxApp::MarqueeSelect(const sdl3::FRect &view) {
	const math::FRect box = math::FRect::FromPoints(m_marqueeStart.x, m_marqueeStart.y, m_marqueeCurrent.x, m_marqueeCurrent.y);
	const bool add = Ui().Shift() || Ui().Ctrl();
	if (!add)
		m_editor.ClearSelection(); // la sélection par cadre remplace la sélection (géométrie comprise)
	if (box.w < 4.f && box.h < 4.f)
		return; // simple clic dans le vide : désélectionne
	for (const Entity &e : m_editor.Doc().Entities()) {
		if (!m_editor.Doc().IsVisible(e))
			continue;
		const math::FAABB wb = m_editor.Doc().WorldBounds(e);
		math::FRect sb;
		bool any = false;
		for (int i = 0; i < 8; ++i) {
			const math::FVector3 corner{(i & 1) ? wb.max.x : wb.min.x, (i & 2) ? wb.max.y : wb.min.y, (i & 4) ? wb.max.z : wb.min.z};
			if (const auto p = Project(corner, view)) {
				if (!any) {
					sb = {p->x, p->y, 0.f, 0.f};
					any = true;
				} else {
					const float x0 = std::min(sb.x, p->x), y0 = std::min(sb.y, p->y);
					const float x1 = std::max(sb.x + sb.w, p->x), y1 = std::max(sb.y + sb.h, p->y);
					sb = {x0, y0, x1 - x0, y1 - y0};
				}
			}
		}
		if (any && box.Intersects(sb))
			m_editor.SelectEntity(e.id, true);
	}
}

// ── Gizmo de déplacement (flèches par axe) ──────────────────────────────────

void SandboxApp::UpdateGizmo() {
	if (m_gizmoDragging)
		return; // le centre reste figé pendant le glissé
	const modeling::ToolKind kind = m_editor.ToolKind();
	const bool transform = kind == modeling::ToolKind::MOVE || kind == modeling::ToolKind::ROTATE || kind == modeling::ToolKind::SCALE;
	if (!transform || m_editor.SelectedEntities().empty()) {
		m_gizmoLength = 0.f;
		m_gizmoAxis = -1;
		return;
	}
	m_gizmoCenter = {};
	int count = 0;
	for (EntityId id : m_editor.SelectedEntities())
		if (const Entity *e = m_editor.Doc().Find(id)) {
			m_gizmoCenter += m_editor.Doc().WorldBounds(*e).Center();
			++count;
		}
	if (count == 0) {
		m_gizmoLength = 0.f;
		return;
	}
	m_gizmoCenter /= float(count);
	// Longueur constante à l'écran (~12 % de la distance à la caméra).
	const math::FVector3 eye = m_editor.camera.Camera().position;
	m_gizmoLength = std::max(0.5f, (eye - m_gizmoCenter).Length() * 0.12f);
}

int SandboxApp::PickGizmoAxis(const sdl3::FRect &view, const Pointer &pointer) const {
	if (m_gizmoLength <= 0.f)
		return -1;
	int best = -1;
	float bestDist = 1e30f;
	for (int i = 0; i < 3; ++i) {
		const math::FVector3 p = modeling::ClosestPointOnLine(m_gizmoCenter, kGizmoAxes[i].dir, pointer.ray);
		const float t = (p - m_gizmoCenter).Dot(kGizmoAxes[i].dir);
		if (t < -0.1f * m_gizmoLength || t > 1.1f * m_gizmoLength)
			continue; // hors de la flèche
		if (const auto px = Project(p, view)) {
			const float dx = px->x - pointer.cursor.x, dy = px->y - pointer.cursor.y;
			const float d = dx * dx + dy * dy;
			if (d < bestDist) {
				bestDist = d;
				best = i;
			}
		}
	}
	return bestDist < 14.f * 14.f ? best : -1; // seuil de 14 px
}

void SandboxApp::DrawMoveGizmo(const sdl3::FRect &view) {
	if (m_gizmoLength <= 0.f)
		return;
	sdl3::Canvas &c = Canvas();
	for (int i = 0; i < 3; ++i) {
		const math::FVector3 tip = m_gizmoCenter + kGizmoAxes[i].dir * m_gizmoLength;
		const auto a = Project(m_gizmoCenter, view), b = Project(tip, view);
		if (!a || !b)
			continue;
		const bool hovered = i == m_gizmoAxis;
		const sdl3::Color col = hovered ? sdl3::Rgba(255, 220, 80) : sdl3::Rgba(kGizmoAxes[i].r, kGizmoAxes[i].g, kGizmoAxes[i].b);
		const float thick = hovered ? 3.5f : 2.5f;
		c.Line(a->x, a->y, b->x, b->y, col, thick);
		// Pointe de flèche (deux traits en « V »).
		const math::FVector2 dir = (math::FVector2{b->x, b->y} - math::FVector2{a->x, a->y}).Normalized();
		const math::FVector2 perp{-dir.y, dir.x};
		const float ah = hovered ? 13.f : 10.f;
		c.Line(b->x, b->y, b->x - dir.x * ah + perp.x * ah * 0.6f, b->y - dir.y * ah + perp.y * ah * 0.6f, col, thick);
		c.Line(b->x, b->y, b->x - dir.x * ah - perp.x * ah * 0.6f, b->y - dir.y * ah - perp.y * ah * 0.6f, col, thick);
	}
}

void SandboxApp::DrawGizmo(const sdl3::FRect &view) {
	const modeling::ToolKind kind = m_editor.ToolKind();
	if (kind == modeling::ToolKind::MOVE)
		DrawMoveGizmo(view);
	else if (kind == modeling::ToolKind::ROTATE)
		DrawRotateGizmo(view);
	else if (kind == modeling::ToolKind::SCALE)
		DrawScaleGizmo(view);
}

int SandboxApp::PickGizmo(const sdl3::FRect &view, const Pointer &pointer) const {
	const modeling::ToolKind kind = m_editor.ToolKind();
	if (kind == modeling::ToolKind::MOVE)
		return PickGizmoAxis(view, pointer);
	if (kind == modeling::ToolKind::ROTATE)
		return PickRotateAxis(view, pointer);
	if (kind == modeling::ToolKind::SCALE)
		return PickScaleGizmo(view, pointer) ? 3 : -1;
	return -1;
}

void SandboxApp::DrawRotateGizmo(const sdl3::FRect &view) {
	if (m_gizmoLength <= 0.f)
		return;
	sdl3::Canvas &c = Canvas();
	static constexpr math::FVector3 kU[3] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
	static constexpr math::FVector3 kV[3] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}};
	for (int i = 0; i < 3; ++i) {
		const bool hovered = i == m_gizmoAxis;
		const sdl3::Color col = hovered ? sdl3::Rgba(255, 220, 80) : sdl3::Rgba(kGizmoAxes[i].r, kGizmoAxes[i].g, kGizmoAxes[i].b);
		std::array<float, 2 * 49> ring{};
		size_t n = 0;
		for (int k = 0; k <= 48; ++k) {
			const float a = float(k) / 48.f * 2.f * math::PI;
			const math::FVector3 p = m_gizmoCenter + (kU[i] * std::cos(a) + kV[i] * std::sin(a)) * m_gizmoLength;
			if (const auto px = Project(p, view)) {
				ring[2 * n] = px->x;
				ring[2 * n + 1] = px->y;
				++n;
			}
		}
		if (n >= 2)
			c.Polyline(ring.data(), n, col, hovered ? 3.f : 2.f);
	}
}

void SandboxApp::DrawScaleGizmo(const sdl3::FRect &view) {
	if (m_gizmoLength <= 0.f)
		return;
	sdl3::Canvas &c = Canvas();
	const auto px = Project(m_gizmoCenter, view);
	if (!px)
		return;
	const bool hovered = m_gizmoAxis == 3;
	const sdl3::Color col = hovered ? sdl3::Rgba(255, 220, 80) : sdl3::Rgba(60, 200, 220);
	const float half = hovered ? 10.f : 8.f;
	c.FillRect({px->x - half, px->y - half, 2.f * half, 2.f * half}, col);
	c.StrokeRect({px->x - half, px->y - half, 2.f * half, 2.f * half}, sdl3::Rgba(255, 255, 255, 200), 1.5f);
}

int SandboxApp::PickRotateAxis(const sdl3::FRect &view, const Pointer &pointer) const {
	if (m_gizmoLength <= 0.f)
		return -1;
	static constexpr math::FVector3 kU[3] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 0.f}};
	static constexpr math::FVector3 kV[3] = {{0.f, 0.f, 1.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}};
	int best = -1;
	float bestDist = 1e30f;
	for (int i = 0; i < 3; ++i)
		for (int k = 0; k < 48; ++k) {
			const float a = float(k) / 48.f * 2.f * math::PI;
			const math::FVector3 p = m_gizmoCenter + (kU[i] * std::cos(a) + kV[i] * std::sin(a)) * m_gizmoLength;
			if (const auto px = Project(p, view)) {
				const float dx = px->x - pointer.cursor.x, dy = px->y - pointer.cursor.y;
				const float d = dx * dx + dy * dy;
				if (d < bestDist) {
					bestDist = d;
					best = i;
				}
			}
		}
	return bestDist < 14.f * 14.f ? best : -1;
}

bool SandboxApp::PickScaleGizmo(const sdl3::FRect &view, const Pointer &pointer) const {
	const auto px = Project(m_gizmoCenter, view);
	if (!px)
		return false;
	const float dx = px->x - pointer.cursor.x, dy = px->y - pointer.cursor.y;
	return dx * dx + dy * dy < 12.f * 12.f;
}

void SandboxApp::DrawViewportOverlay(const sdl3::FRect &view) {
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = Theme();
	c.PushClip(view);
	// Inférence sous le curseur : pastille colorée et étiquette (« Extrémité », « Sur l'axe rouge »…).
	const modeling::Inference &at = m_editor.Hovered();
	const bool drawing = m_editor.ToolKind() != ToolKind::SELECT && m_navTool == Nav::None;
	if (drawing && at.IsValid() && at.kind != modeling::InferenceKind::GROUND && Ui().MouseOver(view)) {
		if (const auto px = Project(m_editor.ActiveTransform().TransformPoint(at.point), view)) {
			const modeling::Color col = modeling::InferenceColor(at.kind);
			const sdl3::Color color = sdl3::Rgba(uint8_t(col.r * 255.f), uint8_t(col.g * 255.f), uint8_t(col.b * 255.f));
			c.FillCircle(px->x, px->y, 6.f, sdl3::Rgba(255, 255, 255, 230));
			c.FillCircle(px->x, px->y, 4.5f, color);
			const std::string_view label = modeling::InferenceLabel(at.kind);
			const float w = c.MeasureText(label, 12.f) + 14.f;
			const sdl3::FRect tag{Ui().MouseX() + 16.f, Ui().MouseY() + 18.f, w, 20.f};
			c.FillRoundRect(tag, 4.f, sdl3::Rgba(255, 255, 238, 240));
			c.StrokeRoundRect(tag, 4.f, sdl3::Rgba(120, 120, 100));
			c.Text(tag.x + 7.f, tag.y + 3.f, label, sdl3::Rgba(30, 30, 30), 12.f);
		}
	}
	// Contexte : groupe ouvert.
	if (m_editor.Context() != kRootId)
		if (const Entity *e = m_editor.Doc().Find(m_editor.Context())) {
			const std::string text = std::format("Édition du groupe « {} »", e->name.c_str());
			const float w = c.MeasureText(text, 13.f) + 24.f;
			const sdl3::FRect pill{view.x + (view.w - w) * 0.5f, view.y + 10.f, w, 26.f};
			c.FillRoundRect(pill, 13.f, sdl3::Rgba(30, 34, 44, 210));
			c.Text(pill.x + 12.f, pill.y + 5.f, text, sdl3::Rgba(240, 242, 248), 13.f);
		}
	// Statistiques discrètes.
	const sdl3::RenderStats &s = m_viewport.Stats();
	const std::string stats = std::format("{} faces · {} groupes · {} triangles · {:.0f} i/s", m_editor.Doc().Root().FaceCount(), m_editor.Doc().Entities().size(),
										  s.triangles, Fps());
	c.Text(view.x + 10.f, view.Bottom() - 20.f, stats, sdl3::Rgba(70, 74, 84, 200), 11.f);
	// Sélection par cadre.
	if (m_marquee) {
		const math::FRect box = math::FRect::FromPoints(m_marqueeStart.x, m_marqueeStart.y, m_marqueeCurrent.x, m_marqueeCurrent.y);
		c.FillRect({box.x, box.y, box.w, box.h}, sdl3::Rgba(76, 196, 84, 28));
		c.StrokeRect({box.x, box.y, box.w, box.h}, sdl3::Rgba(76, 196, 84, 210), 1.f);
	}
	DrawGizmo(view); // gizmo de transformation (déplacer / rotation / échelle)
	DrawCornerGizmo(view); // repère d'axes en haut à droite
	(void)th;
	c.PopClip();
}

void SandboxApp::DrawCornerGizmo(const sdl3::FRect &view) {
	sdl3::Canvas &c = Canvas();
	const math::FVector2 center{view.Right() - 46.f, view.y + 46.f};
	constexpr float kRadius = 30.f;
	// Base de la caméra (repère écran) pour projeter les axes du monde.
	const sdl3::Camera3D cam = m_editor.camera.Camera();
	const math::FVector3 forward = (cam.target - cam.position).Normalized();
	const math::FVector3 right = forward.Cross(cam.up).Normalized();
	const math::FVector3 up = right.Cross(forward);
	static constexpr struct {
		math::FVector3 dir;
		sdl3::Color color;
	} kAxes[] = {{{1.f, 0.f, 0.f}, sdl3::Rgba(220, 50, 50)}, {{0.f, 1.f, 0.f}, sdl3::Rgba(50, 90, 230)}, {{0.f, 0.f, 1.f}, sdl3::Rgba(40, 170, 60)}};
	for (const auto &a : kAxes) {
		const float dx = a.dir.Dot(right), dy = a.dir.Dot(up);
		const math::FVector2 tip{center.x + dx * kRadius, center.y - dy * kRadius};
		c.Line(center.x, center.y, tip.x, tip.y, a.color, 2.f);
		c.Line(center.x, center.y, center.x - dx * kRadius, center.y + dy * kRadius, sdl3::WithAlpha(a.color, 110), 2.f);
		c.FillCircle(tip.x, tip.y, 5.f, a.color);
	}
	// Centre : bascule perspective / parallèle.
	c.FillCircle(center.x, center.y, 8.f, sdl3::Rgba(90, 96, 110));
	c.FillCircle(center.x, center.y, 5.f, sdl3::Rgba(235, 238, 245));
}

bool SandboxApp::HandleCornerGizmo(const sdl3::FRect &view) {
	ui::Context &u = Ui();
	const math::FVector2 center{view.Right() - 46.f, view.y + 46.f};
	constexpr float kRadius = 30.f;
	const float dxm = u.MouseX() - center.x, dym = u.MouseY() - center.y;
	if (dxm * dxm + dym * dym > (kRadius + 12.f) * (kRadius + 12.f))
		return false; // hors du gizmo
	if (!u.MousePressed())
		return false;
	const sdl3::Camera3D cam = m_editor.camera.Camera();
	const math::FVector3 forward = (cam.target - cam.position).Normalized();
	const math::FVector3 right = forward.Cross(cam.up).Normalized();
	const math::FVector3 up = right.Cross(forward);
	// Centre : bascule de projection.
	if (dxm * dxm + dym * dym < 12.f * 12.f) {
		m_editor.camera.SetOrthographic(!m_editor.camera.Orthographic());
		return true;
	}
	// Axe le plus proche du clic (six directions), oriente la caméra.
	static constexpr math::FVector3 kDirs[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
	static constexpr StandardView kViews[6] = {StandardView::Right, StandardView::Left, StandardView::Top, StandardView::Bottom, StandardView::Front, StandardView::Back};
	int best = -1;
	float bestDist = 1e30f;
	for (int i = 0; i < 6; ++i) {
		const float dx = kDirs[i].Dot(right), dy = kDirs[i].Dot(up);
		const float tx = center.x + dx * kRadius, ty = center.y - dy * kRadius;
		const float dist = (u.MouseX() - tx) * (u.MouseX() - tx) + (u.MouseY() - ty) * (u.MouseY() - ty);
		if (dist < bestDist) {
			bestDist = dist;
			best = i;
		}
	}
	if (best >= 0 && bestDist < 15.f * 15.f) {
		SetView(kViews[best]);
		return true;
	}
	return false;
}

void SandboxApp::SetView(StandardView v) { m_editor.camera.SetView(v, m_editor.Doc().Bounds(), m_viewport.Aspect()); }

// ── Fichiers ─────────────────────────────────────────────────────────────────

void SandboxApp::NewDocument() {
	m_home = false;
	m_editor.Reset(Document{});
	m_path.clear();
	m_project.reset();
	m_editor.camera.SetView(StandardView::Iso, {{-5.f, 0.f, -5.f}, {5.f, 3.f, 5.f}}, m_viewport.Aspect());
}

LoadOutcome SandboxApp::ResolveAndLoad(const std::string &request) {
	namespace fs = std::filesystem;
	LoadOutcome out;
	fs::path path = request;
	std::error_code ec;
	const auto projectIn = [&](const fs::path &folder) -> std::optional<fs::path> {
		for (const auto &entry : fs::directory_iterator(folder, ec))
			if (entry.path().extension() == ".project")
				return entry.path();
		return std::nullopt;
	};
	if (fs::is_directory(path, ec)) { // dossier : son projet, sinon sa première scène
		if (const auto project = projectIn(path))
			path = *project;
		else
			for (const auto &entry : fs::directory_iterator(request, ec))
				if (entry.path().extension() == ".scene" || entry.path().extension() == ".sandbox") {
					path = entry.path();
					break;
				}
	}
	if (path.extension() == ".project") {
		auto project = sandbox::Project::Load(path);
		if (!project) {
			out.error = std::string(project.Error());
			return out;
		}
		const auto scene = project.Value().ActiveScene();
		if (!scene) {
			out.error = "Le projet n'a aucune scène";
			return out;
		}
		out.project = std::move(project.Value());
		path = *scene;
	} else { // scène seule : son projet, dans son dossier ou celui au-dessus (scenes/…), s'il la recense.
		for (const fs::path &folder : {path.parent_path(), path.parent_path().parent_path()})
			if (const auto file = folder.empty() ? std::nullopt : projectIn(folder))
				if (auto project = sandbox::Project::Load(*file); project && std::ranges::any_of(project.Value().scenes, [&](const String &s) {
														 return fs::equivalent(project.Value().Resolve(s), path, ec);
													 })) {
					out.project = std::move(project.Value());
					break;
				}
	}
	auto doc = Document::Load(String(path.string()));
	if (!doc) {
		out.error = std::string(doc.Error());
		out.project.reset();
		return out;
	}
	out.doc = std::move(doc.Value());
	out.path = path.string();
	return out;
}

LoadOutcome SandboxApp::CreateProjectOutcome(const std::string &folder, const std::string &name) {
	namespace fs = std::filesystem;
	LoadOutcome out;
	if (name.empty() || name.find_first_of("/\\:") != std::string::npos) {
		out.error = "Nom de projet invalide";
		return out;
	}
	const fs::path root = fs::path(folder) / name;
	std::error_code ec;
	if (fs::exists(root / (name + ".project"), ec))
		return ResolveAndLoad((root / (name + ".project")).string()); // déjà présent : on l'ouvre
	// Scène de départ : un sol, une vue, un script de scène (classe Scene) à compléter.
	Document scene;
	(void)scene.Root().AddRectangle({-5.f, 0.f, -5.f}, {10.f, 0.f, 0.f}, {0.f, 0.f, 10.f});
	scene.Views().push_back({"Départ", {0.f, 1.65f, 6.f}, {0.f, 1.4f, 0.f}, 35.f, false, 10.f});
	scene.Game().scriptFile = "../scripts/main.script";
	scene.Game().script = String("# Script de la scène « " + name + " ».\n\n"
								 "class Main extends Scene {\n"
								 "    export let vitesse = 4.0   # marche du joueur, réglable dans l'inspecteur\n\n"
								 "    fn OnStart() {\n"
								 "        this.set_player_speed(this.vitesse)\n"
								 "        this.say(\"Bienvenue dans " + name + " !\")\n"
								 "    }\n\n"
								 "    fn OnUpdate(dt) {}\n"
								 "}\n");
	const std::string scenePath = (root / "scenes" / "main.scene").string();
	if (auto error = scene.Save(String(scenePath))) {
		out.error = std::string(*error);
		return out;
	}
	sandbox::Project project = sandbox::Project::Create(root / (name + ".project"), String(name));
	project.activeScene = "main";
	project.scenes.push_back("scenes/main.scene");
	project.scripts.push_back({"main", "Script de la scène principale", "scripts/main.script"});
	if (auto error = project.Save()) {
		out.error = std::string(*error);
		return out;
	}
	out.doc = std::move(scene);
	out.path = scenePath;
	out.project = std::move(project);
	return out;
}

void SandboxApp::Load(const std::string &request) {
	if (m_loading.exchange(true))
		return; // un chargement est déjà en cours
	m_loadingLabel = "Ouverture du projet…";
	jobs::ThreadPool::Global().Submit([this, request] {
		LoadOutcome outcome = ResolveAndLoad(request);
		std::lock_guard lock(m_loadMutex);
		m_loadResult = std::move(outcome);
		m_loading = false;
	});
}

bool SandboxApp::LoadScene(const std::string &path) {
	LoadOutcome outcome = ResolveAndLoad(path);
	if (!outcome.error.empty()) {
		Ui().Notify(outcome.error, Theme().danger, 5.f);
		return false;
	}
	ApplyOutcome(std::move(outcome));
	return true;
}

void SandboxApp::ApplyOutcome(LoadOutcome &&outcome) {
	m_editor.Reset(std::move(outcome.doc));
	m_path = outcome.path;
	m_project = std::move(outcome.project);
	if (!m_editor.Doc().Views().empty())
		m_editor.camera.Restore(m_editor.Doc().Views().front());
	else
		m_editor.camera.ZoomExtents(m_editor.Doc().Bounds(), m_viewport.Aspect() > 0.f ? m_viewport.Aspect() : 1.6f);
	m_assetsFor = "\x01"; // ressources relistées
	m_home = false;
	AddRecent(m_project ? m_project->File().string() : outcome.path);
	if (std::vector<String> &warnings = m_editor.Doc().LoadWarnings(); !warnings.empty()) { // console + notification
		for (const String &w : warnings) {
			game::GameMessage m;
			m.text = std::format("Chargement : {}", w.c_str());
			m.error = true;
			Log(std::move(m));
		}
		Ui().Notify(std::format("{} avertissement(s) au chargement (voir la console)", warnings.size()), Theme().warning, 5.f);
		warnings.clear();
	}
	Ui().Notify("Ouvert : " + std::filesystem::path(outcome.path).filename().string(), Theme().success);
	if (m_launchPending) {
		m_launchPending = false;
		ApplyLaunchOptions();
	}
}

void SandboxApp::ApplyLaunchOptions() {
	// Groupes à sélectionner / ouvrir (le document vient d'être chargé).
	for (const Entity &e : m_editor.Doc().Entities()) {
		if (e.name == m_launch.selectGroup)
			m_editor.SelectEntity(e.id);
		if (e.name == m_launch.openGroup)
			(void)m_editor.OpenGroup(e.id);
	}
	static constexpr std::pair<std::string_view, StandardView> kViewNames[] = {{"iso", StandardView::Iso},	  {"top", StandardView::Top},
																			   {"front", StandardView::Front}, {"right", StandardView::Right},
																			   {"back", StandardView::Back},	  {"left", StandardView::Left}};
	for (const auto &[name, v] : kViewNames)
		if (m_launch.view == name)
			m_editor.camera.SetView(v, m_editor.Doc().Bounds(), 1.6f);
	if (!m_launch.tool.empty())
		if (auto k = modeling::ToolFromShortcut(m_launch.tool[0]))
			m_editor.SetTool(*k);
	if (!m_launch.import.empty()) {
		const math::FVector3 t = m_editor.camera.Target();
		if (auto r = m_editor.ImportModel(String(m_launch.import), {t.x, 0.f, t.z}); r.IsError())
			Ui().Notify(std::string(r.Error()), Theme().danger, 5.f);
	}
	if (m_launch.play)
		StartGame();
}

void SandboxApp::PollLoad() {
	std::optional<LoadOutcome> result;
	{
		std::lock_guard lock(m_loadMutex);
		result.swap(m_loadResult);
	}
	if (!result)
		return;
	if (!result->error.empty()) {
		Ui().Notify(result->error, Theme().danger, 5.f);
		return;
	}
	ApplyOutcome(std::move(*result));
}

void SandboxApp::DrawLoadingOverlay(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = Theme();
	c.FillRect(area, sdl3::Rgba(0, 0, 0, 120)); // assombrit l'arrière-plan pendant le chargement
	const float w = 280.f, h = 92.f;
	const sdl3::FRect box{area.x + (area.w - w) * 0.5f, area.y + (area.h - h) * 0.5f, w, h};
	c.FillRoundRect(box, 10.f, th.panel);
	c.StrokeRoundRect(box, 10.f, th.panelBorder);
	u.Spinner({box.x + 22.f, box.y + 16.f, 40.f, 40.f}, 40.f, th.accent);
	c.TextCentered({box.x + 70.f, box.y + 6.f, box.w - 78.f, box.h - 12.f}, m_loadingLabel, th.text, 14.f);
}

game::GameSession::ModuleLoader SandboxApp::ModuleLoader() const {
	std::optional<sandbox::Project> project = m_project;
	const std::filesystem::path scene = m_path;
	return [project, scene](std::string_view name) -> Result<std::string, std::string> {
		std::optional<std::filesystem::path> file;
		if (project)
			file = project->ScriptFile(name);
		if (!file && !scene.empty()) // sans projet : à côté de la scène
			if (std::filesystem::path near = scene.parent_path() / "scripts" / (std::string(name) + ".script"); std::filesystem::exists(near))
				file = near;
		if (!file)
			return Err(std::format("module « {} » introuvable dans le projet", name));
		std::ifstream in(*file, std::ios::binary);
		if (!in)
			return Err(std::format("impossible de lire {}", file->string()));
		return Ok(std::string(std::istreambuf_iterator<char>(in), {}));
	};
}

void SandboxApp::OpenDocument() {
	sdl3::dialog::ShowOpenFile(
		[this](const sdl3::DialogResult &r, int) {
			if (r.Ok()) {
				std::lock_guard lock(m_dialogMutex);
				m_dialogResult = {0, std::string(r.First())};
			}
		},
		Window(), {{"Projets et scènes", "project;scene;sandbox"}, {"Tous les fichiers", "*"}}, "apps/sandbox/projects");
}

void SandboxApp::SaveDocument(bool saveAs) {
	if (!saveAs && !m_path.empty()) {
		if (auto error = m_editor.Doc().Save(String(m_path), SplitOptions()))
			Ui().Notify(std::string(*error), Theme().danger, 5.f);
		else {
			if (m_project) { // scène recensée par l'index du projet
				const size_t before = m_project->scenes.size();
				sandbox::Project::AddUnique(m_project->scenes, m_project->Relative(m_path));
				if (m_project->scenes.size() != before)
					(void)m_project->Save();
			}
			m_editor.MarkSaved();
			Ui().Notify("Enregistré : " + std::filesystem::path(m_path).filename().string(), Theme().success);
		}
		return;
	}
	std::error_code ec;
	std::filesystem::create_directories("data/sandbox", ec);
	sdl3::dialog::ShowSaveFile(
		[this](const sdl3::DialogResult &r, int) {
			if (r.Ok()) {
				std::lock_guard lock(m_dialogMutex);
				m_dialogResult = {1, std::string(r.First())};
			}
		},
		Window(), {{"Documents sandbox", "sandbox"}}, "data/sandbox/modele.sandbox");
}

void SandboxApp::ExportObj() {
	sdl3::dialog::ShowSaveFile(
		[this](const sdl3::DialogResult &r, int) {
			if (r.Ok()) {
				std::lock_guard lock(m_dialogMutex);
				m_dialogResult = {2, std::string(r.First())};
			}
		},
		Window(), {{"Wavefront OBJ", "obj"}, {"STL", "stl"}, {"PLY", "ply"}}, "data/sandbox/modele.obj");
}

void SandboxApp::PollDialogs() {
	std::optional<std::pair<int, std::string>> result;
	{
		std::lock_guard lock(m_dialogMutex);
		result.swap(m_dialogResult);
	}
	if (!result)
		return;
	auto &[kind, path] = *result;
	if (kind == 0) {
		Load(path);
	} else if (kind == 1) {
		if (!path.ends_with(".scene") && !path.ends_with(".sandbox"))
			path += ".scene";
		m_path = path;
		SaveDocument(false);
	} else if (kind == 3) { // dossier d'un nouveau projet (accueil)
		m_newFolder = path;
	} else {
		// Export : géométrie libre et groupes visibles réunis dans un seul modèle triangulé.
		Document &doc = m_editor.Doc();
		doc.SyncMaterials();
		modeling::Model all = doc.Root();
		for (const Entity &e : doc.Entities())
			if (doc.IsVisible(e))
				(void)all.Append(e.model, e.transform);
		if (auto error = modeling::ExportModel(String(path), all))
			Ui().Notify(std::string(*error), Theme().danger, 5.f);
		else
			Ui().Notify("Exporté : " + std::filesystem::path(path).filename().string(), Theme().success);
	}
}

} // namespace sandbox
