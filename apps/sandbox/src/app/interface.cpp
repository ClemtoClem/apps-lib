// interface.cpp — Interface retenue du sandbox : menus, barre d'outils, dock et
// barre d'état, construits une fois depuis la fabrique `ui::WidgetBuilder`
// (builder factory). Seule la structure change : le contenu des panneaux reste
// dessiné par le contexte immédiat, enveloppé dans des feuilles `ui::Custom`.
#include <algorithm>
#include <array>
#include <cctype>
#include <format>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

using modeling::ToolKind;

constexpr float kMenuH = 30.f, kToolbarH = 42.f, kStatusH = 28.f;

/// Item de menu (champs de `ui::MenuItem` : libellé, raccourci, séparateur,
/// coche, état, groupe radio, action).
ui::MenuItem Item(std::string label, std::string shortcut, std::function<void()> action, bool checkable = false,
				  bool checked = false, std::string group = {}) {
	return ui::MenuItem{std::move(label), std::move(shortcut), false, checkable, checked, std::move(group), std::move(action)};
}
const ui::MenuItem kSep{"", "", true, false, false, "", nullptr};

/// Les dix outils du menu « Outils » (radio), dans l'ordre de la barre.
constexpr std::array<ToolKind, 10> kTools = {ToolKind::SELECT,   ToolKind::ERASER,   ToolKind::PAINT,	  ToolKind::MOVE, ToolKind::ROTATE,
											 ToolKind::SCALE,	 ToolKind::PUSH_PULL, ToolKind::FOLLOW_ME, ToolKind::OFFSET, ToolKind::TAPE};

} // namespace

// ── Menus ────────────────────────────────────────────────────────────────────

ui::WidgetBuilder SandboxApp::MakeMenus() {
	std::vector<ui::MenuItem> file = {
		Item("Nouveau", ShortcutLabel("fichier.nouveau"), [this] { DispatchAction("fichier.nouveau"); }),
		Item("Ouvrir…", ShortcutLabel("fichier.ouvrir"), [this] { DispatchAction("fichier.ouvrir"); }),
		Item("Enregistrer", ShortcutLabel("fichier.enregistrer"), [this] { DispatchAction("fichier.enregistrer"); }),
		Item("Enregistrer sous…", ShortcutLabel("fichier.enregistrer_sous"), [this] { DispatchAction("fichier.enregistrer_sous"); }),
		kSep,
		Item("Accueil (projets)", "", [this] { m_home = true; }),
		Item("Exporter en OBJ…", "", [this] { ExportObj(); }),
		kSep,
		Item("Quitter", ShortcutLabel("fichier.quitter"), [this] { DispatchAction("fichier.quitter"); }),
	};
	// Aligner / répartir les groupes sélectionnés (sous-menus d'Édition).
	ui::MenuItem alignMenu("Aligner", "", false, false, false, "", nullptr);
	alignMenu.children = {
		Item("À gauche (X)", "", [this] { m_editor.AlignSelection(Align3D::Left); }),
		Item("Centrer (X)", "", [this] { m_editor.AlignSelection(Align3D::CenterX); }),
		Item("À droite (X)", "", [this] { m_editor.AlignSelection(Align3D::Right); }),
		kSep,
		Item("En bas (Y)", "", [this] { m_editor.AlignSelection(Align3D::Bottom); }),
		Item("Centrer (Y)", "", [this] { m_editor.AlignSelection(Align3D::CenterY); }),
		Item("En haut (Y)", "", [this] { m_editor.AlignSelection(Align3D::Top); }),
		kSep,
		Item("En arrière (Z)", "", [this] { m_editor.AlignSelection(Align3D::Back); }),
		Item("Centrer (Z)", "", [this] { m_editor.AlignSelection(Align3D::CenterZ); }),
		Item("En avant (Z)", "", [this] { m_editor.AlignSelection(Align3D::Front); }),
	};
	ui::MenuItem distributeMenu("Répartir", "", false, false, false, "", nullptr);
	distributeMenu.children = {
		Item("Sur X", "", [this] { m_editor.DistributeSelection(0); }),
		Item("Sur Y", "", [this] { m_editor.DistributeSelection(1); }),
		Item("Sur Z", "", [this] { m_editor.DistributeSelection(2); }),
	};
	std::vector<ui::MenuItem> edit = {
		Item("Annuler", ShortcutLabel("edition.annuler"), [this] { DispatchAction("edition.annuler"); }),
		Item("Rétablir", ShortcutLabel("edition.retablir"), [this] { DispatchAction("edition.retablir"); }),
		kSep,
		Item("Tout sélectionner", ShortcutLabel("edition.tout_selectionner"), [this] { DispatchAction("edition.tout_selectionner"); }),
		Item("Désélectionner", ShortcutLabel("edition.echap"), [this] { m_editor.ClearSelection(); }),
		Item("Effacer", ShortcutLabel("edition.effacer"), [this] { DispatchAction("edition.effacer"); }),
		kSep,
		Item("Grouper", ShortcutLabel("edition.grouper"), [this] { DispatchAction("edition.grouper"); }),
		Item("Éclater", "", [this] { (void)m_editor.ExplodeSelection(); }),
		Item("Dupliquer", ShortcutLabel("edition.dupliquer"), [this] { DispatchAction("edition.dupliquer"); }),
		alignMenu,
		distributeMenu,
		Item("Ouvrir le groupe", "", [this] {
			if (m_editor.SelectedEntities().size() == 1)
				(void)m_editor.OpenGroup(m_editor.SelectedEntities().front());
		}),
		Item("Fermer le groupe", "", [this] { m_editor.CloseGroup(); }),
		kSep,
		Item("Créer un composant", "", [this] { (void)m_editor.MakeComponent(); }),
		Item("Rendre unique", "", [this] { (void)m_editor.MakeUnique(); }),
	};
	std::vector<ui::MenuItem> view = {
		Item("Arêtes", "", [this] { m_display.edges = !m_display.edges; }, true, m_display.edges),
		Item("Axes", "", [this] { m_display.axes = !m_display.axes; }, true, m_display.axes),
		Item("Ombres", "", [this] { m_display.shadows = !m_display.shadows; }, true, m_display.shadows),
		Item("Ciel", "", [this] { m_display.sky = !m_display.sky; }, true, m_display.sky),
		Item("Rayons X", "", [this] { m_display.xray = !m_display.xray; }, true, m_display.xray),
		kSep,
		Item("Structure (gauche)", "", [this] { m_leftShown = !m_leftShown; }, true, m_leftShown),
		Item("Inspecteur (droite)", "", [this] { m_rightShown = !m_rightShown; }, true, m_rightShown),
		Item("Rangée du bas", "", [this] { m_bottomShown = !m_bottomShown; }, true, m_bottomShown),
		kSep,
		Item("Réinitialiser la disposition", "", [this] { ResetLayout(); }),
	};
	std::vector<ui::MenuItem> camera = {
		Item("Iso", ShortcutLabel("vue.iso"), [this] { DispatchAction("vue.iso"); }),
		Item("Dessus", ShortcutLabel("vue.dessus"), [this] { DispatchAction("vue.dessus"); }),
		Item("Face", ShortcutLabel("vue.face"), [this] { DispatchAction("vue.face"); }),
		Item("Droite", ShortcutLabel("vue.droite"), [this] { DispatchAction("vue.droite"); }),
		Item("Arrière", ShortcutLabel("vue.arriere"), [this] { DispatchAction("vue.arriere"); }),
		Item("Gauche", ShortcutLabel("vue.gauche"), [this] { DispatchAction("vue.gauche"); }),
		kSep,
		Item("Perspective", "", [this] { m_editor.camera.SetOrthographic(false); }, true, !m_editor.camera.Orthographic(), "projection"),
		Item("Projection parallèle", "", [this] { m_editor.camera.SetOrthographic(true); }, true, m_editor.camera.Orthographic(), "projection"),
		kSep,
		Item("Zoom étendu", ShortcutLabel("camera.zoom_etendu"), [this] { DispatchAction("camera.zoom_etendu"); }),
		Item("Ajouter une scène", "", [this] {
			m_editor.Checkpoint("Ajouter une scène");
			m_editor.Doc().Views().push_back(m_editor.camera.Save(String::Format("Scène {}", m_editor.Doc().Views().size() + 1)));
			ShowPanel("scenes");
		}),
	};
	std::vector<ui::MenuItem> draw = {
		Item("Ligne", ShortcutLabel(ToolActionId(ToolKind::LINE)), [this] { DispatchAction(ToolActionId(ToolKind::LINE)); }),
		Item("Rectangle", ShortcutLabel(ToolActionId(ToolKind::RECTANGLE)), [this] { DispatchAction(ToolActionId(ToolKind::RECTANGLE)); }),
		Item("Cercle", ShortcutLabel(ToolActionId(ToolKind::CIRCLE)), [this] { DispatchAction(ToolActionId(ToolKind::CIRCLE)); }),
		Item("Polygone", "", [this] { m_editor.SetTool(ToolKind::POLYGON); }),
	};
	std::vector<ui::MenuItem> tools;
	for (ToolKind k : kTools)
		tools.push_back(Item(modeling::ToolName(k), ShortcutLabel(ToolActionId(k)), [this, k] {
			DispatchAction(ToolActionId(k));
		}, true, m_editor.ToolKind() == k, "tool"));
	std::vector<ui::MenuItem> play = {
		Item("Jouer la visite", ShortcutLabel("jeu.jouer"), [this] { DispatchAction("jeu.jouer"); }),
		Item("Arrêter", "Échap", [this] { StopGame(); }),
		kSep,
		Item("Comportements et réglages…", "", [this] { ShowPanel("entity"); }),
	};
	std::vector<ui::MenuItem> help = {
		Item("Catalogue…", "", [this] { m_showCatalog = true; }),
		Item("Raccourcis clavier…", "", [this] { m_showShortcuts = true; }),
		Item("À propos du sandbox", "", [this] { Ui().Notify("Sandbox 2 — modeleur à la SketchUp sur apps-lib (sdl3, SDL_GPU)", Theme().info, 5.f); }),
	};
	// Réglages généraux : raccourcis clavier, fractionnement automatique des fichiers volumineux.
	std::vector<ui::MenuItem> settings = {
		Item("Raccourcis clavier…", "", [this] { m_showShortcuts = true; }),
		kSep,
		Item("Fractionner automatiquement les fichiers volumineux", "", [this] { m_splitFiles = !m_splitFiles; SaveSettings(); }, true, m_splitFiles),
	};
	ui::MenuItem sizeMenu("Taille maximale d'un fichier", "", false, false, false, "", nullptr);
	sizeMenu.children = {
		Item("50 Mo", "", [this] { m_splitMaxMb = 50; SaveSettings(); }, true, m_splitMaxMb == 50, "split-size"),
		Item("100 Mo", "", [this] { m_splitMaxMb = 100; SaveSettings(); }, true, m_splitMaxMb == 100, "split-size"),
		Item("250 Mo", "", [this] { m_splitMaxMb = 250; SaveSettings(); }, true, m_splitMaxMb == 250, "split-size"),
		Item("500 Mo", "", [this] { m_splitMaxMb = 500; SaveSettings(); }, true, m_splitMaxMb == 500, "split-size"),
	};
	settings.push_back(std::move(sizeMenu));

	return ui::MenuBar().Id("menu").Menus({
		{"Fichier", std::move(file)},
		{"Édition", std::move(edit)},
		{"Affichage", std::move(view)},
		{"Caméra", std::move(camera)},
		{"Dessin", std::move(draw)},
		{"Outils", std::move(tools)},
		{"Jeu", std::move(play)},
		{"Réglages", std::move(settings)},
		{"Aide", std::move(help)},
	}).Height(kMenuH).Background(Theme().title);
}

// ── Barre d'outils et barre d'état (contenu immédiat, enveloppé) ──────────────

ui::WidgetBuilder SandboxApp::MakeToolbar() {
	return ui::Custom([this](ui::Context &u) { DrawToolbar(u.Remaining()); }).Height(kToolbarH).Background(Theme().title);
}

ui::WidgetBuilder SandboxApp::MakeStatusBar() {
	return ui::Custom([this](ui::Context &u) { DrawStatus(u.Remaining()); }).Height(kStatusH);
}

// ── Dock (Splitter + TabWidget, déclaratif) ──────────────────────────────────

ui::WidgetBuilder SandboxApp::MakePanel(std::string_view id) {
	if (id == "viewport") // vue 3D : rendu, navigation, surcouche d'inférence
		return ui::Custom([this](sdl3::Canvas &c, const sdl3::FRect &content) {
			m_view = content;
			m_viewShown = true;
			if (sdl3::Texture *tex = m_viewport.View())
				c.Image(*tex, m_view);
			HandleViewport(m_view);
			DrawViewportOverlay(m_view);
		}).Id("viewport").Ref(m_viewportSheet);
	if (id == "overlay") // calque 2D
		return ui::Custom([this](ui::Context &u) { DrawOverlayEditor(u.Remaining()); });
	if (id == "script") // éditeur de scripts pleine taille
		return ui::Custom([this](ui::Context &u) { DrawScriptWorkspace(u.Remaining()); });
	if (id == "files") // explorateur de fichiers (déjà défilant)
		return ui::Custom([this](ui::Context &u) { DrawExplorer(u.Remaining()); });

	// Panneaux de la colonne (défilement vertical) : inspecteur, structure…
	const std::string panel = std::string(id);
	return ui::Custom([this, panel](ui::Context &u) {
		m_inspecting = panel == "entity";
		u.BeginScroll("panel-" + panel, u.Remaining(), {.axes = ui::ScrollAxes::Vertical});
		if (panel == "entity")
			DrawEntityPanel();
		else if (panel == "outliner")
			DrawOutliner();
		else if (panel == "materials")
			DrawMaterials();
		else if (panel == "tags")
			DrawTags();
		else if (panel == "scenes")
			DrawScenes();
		else if (panel == "commands")
			DrawCommands();
		else if (panel == "assets")
			DrawAssets();
		else if (panel == "console")
			DrawConsole();
		m_inspecting = false;
		u.EndScroll();
	}).Background(Theme().panel);
}

ui::WidgetBuilder SandboxApp::MakeDock() {
	//   Left | (Center | Right)
	//   ───────────────────────
	//            Bottom
	return ui::Splitter().Id("dock-main").Vertical()
		.MinWidth(320.f).Width(ui::Length::Pct(100)).Children({
		ui::Splitter().Id("dock-top").Children({
			ui::TabWidget().Id("tabs-left").Tabs({"Structure", "Scènes"})
				.Bind(m_tabLeft).Margin(3.0f)
				.Children({
					MakePanel("outliner"), MakePanel("scenes"),
				}),
			ui::Splitter().Id("dock-center").Children({
				ui::TabWidget().Id("tabs-center").Tabs({"Vue 3D", "Vue 2D", "Script"})
					.Bind(m_tabCenter).Grow().Margin(3.0f)
					.Children({
						MakePanel("viewport"), MakePanel("overlay"), MakePanel("script"),
					}),
				ui::TabWidget().Id("tabs-right").Tabs({"Inspecteur", "Matériaux", "Balises"})
					.Bind(m_tabRight).Margin(3.0f)
					.Children({
						MakePanel("entity"), MakePanel("materials"), MakePanel("tags"),
					}),
			}),
		}),
		ui::TabWidget().Id("tabs-bottom").Tabs({"Fichiers", "Ressources", "Commandes", "Console"})
			.Bind(m_tabBottom).Margin(3.0f)
			.Children({
				MakePanel("files"), MakePanel("assets"), MakePanel("commands"), MakePanel("console"),
			}),
	});
}

// ── Montage ──────────────────────────────────────────────────────────────────

void SandboxApp::BuildInterface() {
	m_ui->Root(ui::Column().Children({
		MakeMenus(),
		MakeToolbar().Ref(m_toolbar),
		MakeDock().Grow().Ref(m_editorBody),
		// Corps alternatifs : partie à la première personne, et accueil.
		ui::Custom([this](sdl3::Canvas &c, const sdl3::FRect &content) {
			m_view = content;
			if (sdl3::Texture *tex = m_viewport.View())
				c.Image(*tex, m_view);
			DrawGameHud(m_view);
		}).Grow().Hidden().Ref(m_gameBody),
		ui::Custom([this](ui::Context &u) { DrawHome(u.Remaining()); }).Grow().Hidden().Ref(m_homeBody),
		MakeStatusBar().Ref(m_status),
	}));
	// Réglage initial des séparateurs (rapports, ajustables à la souris).
	ApplyLayout();
}

void SandboxApp::SyncMenuChecks() {
	if (!m_ui)
		return;
	SyncMenuShortcuts();
	ui::Widget menu = m_ui->Find("menu");
	if (!menu || menu->menus.size() < 6)
		return;
	if (menu->menus.size() > 2 && menu->menus[2].items.size() > 8) {
		menu->menus[2].items[0].checked = m_display.edges;
		menu->menus[2].items[1].checked = m_display.axes;
		menu->menus[2].items[2].checked = m_display.shadows;
		menu->menus[2].items[3].checked = m_display.sky;
		menu->menus[2].items[4].checked = m_display.xray;
		menu->menus[2].items[6].checked = m_leftShown;
		menu->menus[2].items[7].checked = m_rightShown;
		menu->menus[2].items[8].checked = m_bottomShown;
	}
	if (menu->menus.size() > 3 && menu->menus[3].items.size() > 8) {
		menu->menus[3].items[7].checked = !m_editor.camera.Orthographic();
		menu->menus[3].items[8].checked = m_editor.camera.Orthographic();
	}
	if (menu->menus.size() > 5)
		for (size_t i = 0; i < kTools.size() && i < menu->menus[5].items.size(); ++i)
			menu->menus[5].items[i].checked = m_editor.ToolKind() == kTools[i];
	// Régions visibles (Affichage) : gauche (Structure), droite (Inspecteur), bas.
	if (ui::Widget left = m_ui->Find("tabs-left"); left)
		left->hidden = !m_leftShown;
	if (ui::Widget right = m_ui->Find("tabs-right"); right)
		right->hidden = !m_rightShown;
	if (ui::Widget bottom = m_ui->Find("tabs-bottom"); bottom)
		bottom->hidden = !m_bottomShown;
}

void SandboxApp::SyncMenuShortcuts() {
	if (!m_ui)
		return;
	ui::Widget menu = m_ui->Find("menu");
	if (!menu || menu->menus.size() < 6)
		return;
	const auto set = [&](size_t mi, size_t ii, std::string_view action) {
		if (mi < menu->menus.size() && ii < menu->menus[mi].items.size())
			menu->menus[mi].items[ii].shortcut = ShortcutLabel(action);
	};
	set(0, 0, "fichier.nouveau");
	set(0, 1, "fichier.ouvrir");
	set(0, 2, "fichier.enregistrer");
	set(0, 3, "fichier.enregistrer_sous");
	set(0, 8, "fichier.quitter");
	set(1, 0, "edition.annuler");
	set(1, 1, "edition.retablir");
	set(1, 3, "edition.tout_selectionner");
	set(1, 4, "edition.echap");
	set(1, 5, "edition.effacer");
	set(1, 7, "edition.grouper");
	set(1, 9, "edition.dupliquer");
	set(3, 0, "vue.iso");
	set(3, 1, "vue.dessus");
	set(3, 2, "vue.face");
	set(3, 3, "vue.droite");
	set(3, 4, "vue.arriere");
	set(3, 5, "vue.gauche");
	set(3, 10, "camera.zoom_etendu");
	set(4, 0, ToolActionId(ToolKind::LINE));
	set(4, 1, ToolActionId(ToolKind::RECTANGLE));
	set(4, 2, ToolActionId(ToolKind::CIRCLE));
	if (menu->menus.size() > 5)
		for (size_t i = 0; i < kTools.size() && i < menu->menus[5].items.size(); ++i)
			menu->menus[5].items[i].shortcut = ShortcutLabel(ToolActionId(kTools[i]));
	set(6, 0, "jeu.jouer");
}

void SandboxApp::SyncVisibility() {
	if (m_editorBody)
		m_editorBody->hidden = m_game || m_home;
	if (m_gameBody)
		m_gameBody->hidden = !m_game;
	if (m_homeBody)
		m_homeBody->hidden = !m_home;
	if (m_toolbar)
		m_toolbar->hidden = m_home;
	if (m_status)
		m_status->hidden = m_home;
}

// ── Disposition du dock (persistée) ──────────────────────────────────────────

void SandboxApp::CaptureLayout() {
	if (!m_ui)
		return;
	if (ui::Widget s = m_ui->Find("dock-main"); s)
		m_splitMain = s->value;
	if (ui::Widget s = m_ui->Find("dock-top"); s)
		m_splitTop = s->value;
	if (ui::Widget s = m_ui->Find("dock-center"); s)
		m_splitCenter = s->value;
}

void SandboxApp::ApplyLayout() {
	if (!m_ui)
		return;
	if (ui::Widget s = m_ui->Find("dock-main"); s)
		s->value = m_splitMain;
	if (ui::Widget s = m_ui->Find("dock-top"); s)
		s->value = m_splitTop;
	if (ui::Widget s = m_ui->Find("dock-center"); s)
		s->value = m_splitCenter;
}

void SandboxApp::ResetLayout() {
	m_splitMain = 0.72f;
	m_splitTop = 0.22f;
	m_splitCenter = 0.72f;
	m_leftShown = m_rightShown = m_bottomShown = true;
	ApplyLayout();
	SaveSettings();
}

} // namespace sandbox
