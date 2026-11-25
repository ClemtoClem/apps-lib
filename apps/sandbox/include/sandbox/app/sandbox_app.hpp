#pragma once
/**
 * @file sandbox_app.hpp
 * @brief Application sandbox : interface à la SketchUp autour de l'éditeur.
 *
 *   ┌ barre de titre ────────────────────────────────────────────────────────┐
 *   │ Fichier  Édition  Affichage  Caméra  Dessin  Outils  Aide              │ menus
 *   │ ↖ ⌫ ◩ │ ✎ ▢ ◯ △ │ ⇪ ∿ ▣ │ ✥ ↻ ⤢ │ 📏 │ ⟳ ✋ ⛶ │ Iso Dessus Face …     │ outils
 *   ├──┬[Vue 3D][Script]────────────────────────────────┬[Entité][Structure]…┤
 *   │  │                                                │                     │
 *   │  │      vue 3D  /  éditeur de scripts             │ panneaux            │ dock
 *   │  ├[ … ]───────────────────────────────────────────┤                     │
 *   ├──┴────────────────────────────────────────────────┴─────────────────────┤
 *
 * Dock à la Godot / Blender : quatre régions (structure, vue, inspecteur, bas) posées en
 * `ui::Splitter` + `ui::TabWidget` — l'interface est décrite une fois par la fabrique
 * `ui::WidgetBuilder` (voir interface.cpp), le contenu des panneaux restant en mode immédiat.
 * Le mode Jeu montre la vue seule, sur toute la surface.
 *   │ outil · consigne / message                             Mesures [ 1,5 ] │ état
 *   └────────────────────────────────────────────────────────────────────────┘
 */
#include <atomic>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>

#include <app/app.hpp>
#include <dataio/file.hpp>
#include <jobs/jobs.hpp>
#include <ui/catalog.hpp>
#include <ui/keymap.hpp>
#include <ui/retained.hpp>
#include <ui/script_ui.hpp>

#include "sandbox/app/viewport.hpp"
#include "sandbox/document/project.hpp"
#include "sandbox/editor/editor.hpp"
#include "sandbox/game/session.hpp"
#include "sandbox/game/sound.hpp"

namespace sandbox {

/// Icônes Material supplémentaires (enregistrées par `RegisterIcons`).
namespace icon {
using ui::icons::MaterialIcons;
using ui::icons::Codepoint;
inline constexpr char32_t Select = Codepoint(MaterialIcons::NEAR_ME), Rectangle = Codepoint(MaterialIcons::CROP_SQUARE),
						  Circle = Codepoint(MaterialIcons::RADIO_BUTTON_UNCHECKED), Polygon = Codepoint(MaterialIcons::CHANGE_HISTORY),
						  PushPull = Codepoint(MaterialIcons::PUBLISH), FollowMe = Codepoint(MaterialIcons::GESTURE),
						  Offset = Codepoint(MaterialIcons::FILTER_NONE), Scale = Codepoint(MaterialIcons::ZOOM_OUT_MAP),
						  Orbit = Codepoint(MaterialIcons::THREED_ROTATION), Pan = Codepoint(MaterialIcons::PAN_TOOL),
						  Extents = Codepoint(MaterialIcons::FULLSCREEN), Group = Codepoint(MaterialIcons::GROUP_WORK),
						  Explode = Codepoint(MaterialIcons::CALL_SPLIT), NewFile = Codepoint(MaterialIcons::NOTE_ADD),
						  XRay = Codepoint(MaterialIcons::BLUR_ON), Axes = Codepoint(MaterialIcons::CONTROL_CAMERA),
						  Edges = Codepoint(MaterialIcons::BORDER_STYLE), Scenes = Codepoint(MaterialIcons::PHOTO_LIBRARY),
						  Tag = Codepoint(MaterialIcons::LABEL), Outline = Codepoint(MaterialIcons::ACCOUNT_TREE),
						  Entity = Codepoint(MaterialIcons::INFO), Materials = Codepoint(MaterialIcons::PALETTE),
						  Play = Codepoint(MaterialIcons::SPORTS_ESPORTS);
} // namespace icon

void RegisterIcons();

/// Identifiant d'action stable d'un outil (raccourcis clavier configurables).
[[nodiscard]] const char *ToolActionId(modeling::ToolKind k) noexcept;

/// Projet d'exemple (relatif au dossier de lancement) : scène et scripts de la maison.
inline constexpr std::string_view kSampleProject = "apps/sandbox/projects/maison/maison.project";

struct LaunchOptions {
	std::string open;		   ///< document ou dossier de projet à ouvrir
	bool sample = false;	   ///< projet d'exemple au démarrage (`--sample`)
	bool home = true;		   ///< écran d'accueil (projets récents) sans fichier à ouvrir ; `--empty` : scène vide
	std::string tool;		   ///< outil initial (raccourci : l, r, p…)
	std::string view;		   ///< vue initiale (iso, top, front…)
	std::string openGroup;	   ///< groupe ouvert au démarrage
	std::string selectGroup;   ///< groupe sélectionné au démarrage
	int tab = 0;			   ///< panneau affiché (0 Entité … 5 Jeu)
	bool shadows = false;
	bool play = false; ///< visite lancée au démarrage
	std::string workspace;	///< onglet de travail au démarrage : viewport, overlay ou script
	std::string import;		///< modèle glTF importé au démarrage
};

/// Résultat d'une ouverture / création de projet effectuée en arrière-plan.
struct LoadOutcome {
	std::string path;				///< chemin de la scène (vide si erreur)
	std::string error;				///< erreur (vide : succès)
	Document doc;					///< document chargé ou construit
	std::optional<Project> project; ///< index du projet, s'il y en a un
};

class SandboxApp final : public app::Application {
public:
	explicit SandboxApp(LaunchOptions options) : m_launch(std::move(options)) {}

protected:
	bool OnInit() override;
	void OnUpdate(double frameSeconds) override;
	void OnFixedUpdate(double stepSeconds) override;
	void OnOffscreen() override;
	void OnRender(const sdl3::FRect &area, double alpha) override;
	bool OnCloseRequested() override;

private:
	// ── Interface (sandbox_app.cpp) ──────────────────────────────────────
	/// Construit (une fois) l'arbre de widgets retenu : menus, barre d'outils,
	/// dock, barre d'état — le tout depuis la fabrique `ui::WidgetBuilder`.
	void BuildInterface();
	/// Synchronise les coches / radios des menus avec l'état de l'application.
	void SyncMenuChecks();
	/// Met à jour les libellés de raccourcis des menus depuis le `KeyMap`.
	void SyncMenuShortcuts();
	/// Bascule la visibilité des corps (éditeur / jeu / accueil) et des barres.
	void SyncVisibility();
	/// Lit les ratios des séparateurs depuis l'arbre (avant de persister la disposition).
	void CaptureLayout();
	/// Applique les ratios et la visibilité des régions mémorisés à l'arbre.
	void ApplyLayout();
	/// Rétablit la disposition par défaut (ratios et régions), puis la persiste.
	void ResetLayout();
	[[nodiscard]] ui::WidgetBuilder MakeMenus();
	[[nodiscard]] ui::WidgetBuilder MakeToolbar();
	[[nodiscard]] ui::WidgetBuilder MakeStatusBar();
	[[nodiscard]] ui::WidgetBuilder MakeDock();
	/// Panneau (onglet) du dock, contenu immédiat enveloppé dans une feuille `Custom`.
	[[nodiscard]] ui::WidgetBuilder MakePanel(std::string_view id);
	void DrawToolbar(const sdl3::FRect &bar);
	/// Icône SVG d'un outil/forme (SketchUp), rastérisée puis mise en cache. nullptr si absente.
	sdl3::Texture *ToolIcon(std::string_view name);
	/// Curseur de la souris selon l'outil actif (crayon pour les outils, flèche pour la sélection).
	void UpdateToolCursor();
	void DrawStatus(const sdl3::FRect &bar);
	void DrawViewportOverlay(const sdl3::FRect &view);
	void HandleViewport(const sdl3::FRect &view);
	/// Gizmo de coin (repère d'axes X rouge / Y bleu / Z vert) : oriente la caméra ou bascule la projection.
	void DrawCornerGizmo(const sdl3::FRect &view);
	/// Clic dans le gizmo de coin ; vrai si traité.
	bool HandleCornerGizmo(const sdl3::FRect &view);
	/// Sélectionne les groupes dont la boîte projetée coupe le cadre (Maj/Ctrl : ajoute).
	void MarqueeSelect(const sdl3::FRect &view);
	/// Gizmos de transformation : flèches (Déplacer), anneaux (Rotation), cube (Échelle).
	void UpdateGizmo();
	void DrawGizmo(const sdl3::FRect &view);
	[[nodiscard]] int PickGizmo(const sdl3::FRect &view, const Pointer &pointer) const;
	void DrawMoveGizmo(const sdl3::FRect &view);
	void DrawRotateGizmo(const sdl3::FRect &view);
	void DrawScaleGizmo(const sdl3::FRect &view);
	[[nodiscard]] int PickGizmoAxis(const sdl3::FRect &view, const Pointer &pointer) const;
	[[nodiscard]] int PickRotateAxis(const sdl3::FRect &view, const Pointer &pointer) const;
	[[nodiscard]] bool PickScaleGizmo(const sdl3::FRect &view, const Pointer &pointer) const;
	void HandleKeyboard();
	[[nodiscard]] Pointer MakePointer(const sdl3::FRect &view) const;
	[[nodiscard]] Option<math::FVector2> Project(const math::FVector3 &world, const sdl3::FRect &view) const;
	void SetView(StandardView v);
	void UpdateTitle();
	// ── Raccourcis clavier (raccourcis.hpp / raccourcis.cpp) ─────────────
	/// Déclare toutes les actions avec leurs raccourcis par défaut (appelé avant le chargement des réglages).
	void RegisterShortcuts();
	/// Exécute l'action nommée (toutes les actions du `KeyMap`).
	void DispatchAction(std::string_view action);
	/// Raccourci d'une lettre tapée (outil, navigation, vue) : consulté après la boîte de mesures.
	void DispatchLetter(char c);
	[[nodiscard]] std::string ShortcutLabel(std::string_view action) const;
	/// Modal « Réglages → Raccourcis clavier… » (liste, capture, conflit, réinitialisation).
	void DrawShortcutsModal(const sdl3::FRect &area);
	void HandleShortcutCapture();
	// ── Catalogue (catalog.cpp) ──────────────────────────────────────────
	/// Peuple le catalogue des outils, panneaux, notions et API de script (appelé une fois).
	void PopulateCatalog();
	/// Panneau « Aide → Catalogue » (recherche + description, raccourcis liés au KeyMap).
	void DrawCatalogModal(const sdl3::FRect &area);
	// ── Fichiers ─────────────────────────────────────────────────────────
	void NewDocument();
	void OpenDocument();
	void SaveDocument(bool saveAs);
	void ExportObj();
	// ── Réglages généraux (settings.cpp) ────────────────────────────────
	/// Charge (une fois) les réglages persistés (fractionnement des fichiers…).
	void LoadSettings();
	/// Persiste les réglages (fractionnement) dans le dossier de préférences.
	void SaveSettings();
	/// Options de fractionnement courantes (passées à `Document::Save`).
	[[nodiscard]] dataio::FileOptions SplitOptions() const noexcept;
	/// Ouvre un projet (`.project` : sa scène active), le `.project` d'un dossier, ou une scène
	/// (`.scene`, `.sandbox` ; son projet est retrouvé dans son dossier ou celui au-dessus).
	/// La lecture et l'analyse se font en arrière-plan ; un spinner s'affiche pendant ce temps.
	void Load(const std::string &path);
	/// Charge la scène `path` dans l'éditeur ; faux (et message) si elle est illisible.
	bool LoadScene(const std::string &path);
	/// Source d'un module de script du projet (`import "nom"`).
	[[nodiscard]] game::GameSession::ModuleLoader ModuleLoader() const;
	// ── Chargement en arrière-plan ───────────────────────────────────────
	/// Résout `request` (dossier / projet / scène) et charge le document, sans toucher l'éditeur.
	[[nodiscard]] static LoadOutcome ResolveAndLoad(const std::string &request);
	/// Construit puis enregistre un nouveau projet (scène + index) ; rend le document prêt.
	[[nodiscard]] static LoadOutcome CreateProjectOutcome(const std::string &folder, const std::string &name);
	/// Applique un résultat de chargement à l'éditeur (thread principal).
	void ApplyOutcome(LoadOutcome &&outcome);
	/// Récupère le résultat d'un chargement en arrière-plan, s'il est prêt.
	void PollLoad();
	/// Applique les options de lancement (--select, --open-group, --view, --tool, --import, --play) au document chargé.
	void ApplyLaunchOptions();
	/// Spinner animé affiché pendant une ouverture / création de projet.
	void DrawLoadingOverlay(const sdl3::FRect &area);
	void PollDialogs();
	// ── Accueil (home.cpp) : projets récents, exemples, nouveau projet ───────
	void DrawHome(const sdl3::FRect &area);
	void LoadRecent();
	void AddRecent(const std::string &path);
	/// Crée un projet `folder/name/` (index, scène vide, script de scène) puis l'ouvre.
	void CreateProject(const std::string &folder, const std::string &name);
	// ── Explorateur de fichiers (explorer.cpp), région du bas ────────────
	void DrawExplorer(const sdl3::FRect &area);
	/// Ouvre un fichier texte (script, JSON…) dans l'éditeur du centre (onglet Script).
	void OpenFileInEditor(const std::string &path);
	/// Enregistre le fichier ouvert dans l'éditeur ; les groupes dont c'est le script le relisent.
	bool SaveOpenFile();
	[[nodiscard]] bool FileActive() const noexcept { return m_activeFile >= 0 && size_t(m_activeFile) < m_files.size(); }
	/// Dossier racine de l'explorateur (projet, sinon dossier de la scène, sinon les projets).
	[[nodiscard]] std::filesystem::path ExplorerRoot() const;
	// ── Dock et panneaux (tray.cpp) ──────────────────────────────────────
	/// Affiche l'onglet `id` (région dépliée, onglet actif), où qu'il ait été glissé.
	void ShowPanel(std::string_view id);
	void DrawEntityPanel();
	void DrawOutliner();
	void DrawMaterials();
	void DrawTags();
	void DrawScenes();
	/// Historique annuler/rétablir (panneau « Commandes », comme le Command Stack de Fyrox).
	void DrawCommands();
	/// Ressources : modèles glTF (projet, assets/models) à importer, sons et scripts à attribuer.
	void DrawAssets();
	void ScanAssets();
	// ── Mode Jeu (game_mode.cpp) ─────────────────────────────────────────
	void StartGame();
	void StopGame();
	void HandleGameKeys();
	void DrawGameHud(const sdl3::FRect &view);
	void DrawBehavior();
	// ── Scripts (game_mode.cpp) : éditeur de code du groupe sélectionné ou de la scène ──
	/// Script édité (source, fichier) ; `label` : nom affiché. Nul si aucun.
	[[nodiscard]] std::pair<String *, String *> EditedScript(std::string *label = nullptr);
	void DrawScriptSection();
	/// Paramètres du script (`export let …`) du nœud sélectionné ou de la scène, réglables.
	void DrawScriptParams();
	void DrawScriptEditor(const sdl3::FRect &r);
	void CheckScript();
	// ── Calque 2D (overlay_view.cpp) ─────────────────────────────────────
	/// Dessine le calque (et des éléments éphémères) dans `view` (écran de référence ajusté).
	void DrawLayer2D(const Layer2D &layer, std::span<const Item2D> extra, const sdl3::FRect &view);
	void DrawItem2D(const Item2D &item, const OverlayView &v);
	void DrawTilemap(const TilemapLayer &tm, const OverlayView &v);
	/// Image d'un élément (relative au document), chargée une fois ; nullptr si illisible.
	[[nodiscard]] sdl3::Texture *OverlayImage(const String &file);
	/// Onglet Vue 2D : la scène 3D en fond, l'écran de référence, sélection, déplacement, poignée.
	void DrawOverlayEditor(const sdl3::FRect &area);
	void DrawOverlayTools(float &x, float y, float size);
	/// Onglet Entité quand la Vue 2D est l'onglet de travail : propriétés de l'élément choisi.
	void DrawItem2DPanel();
	/// Aligne les éléments 2D multi-sélectionnés (axe 0 X, 1 Y ; mode 0 bord bas, 1 centre, 2 bord haut).
	void AlignItems2D(int axis, int mode);
	void HandleOverlayKeys();
	// ── Console (console.cpp) : messages des scripts, erreurs cliquables ──
	void Log(game::GameMessage message);
	void DrawConsole();
	/// Ouvre le script d'un groupe (0 et `scene` : celui de la scène) à la ligne `line`.
	void OpenScriptAt(EntityId source, int line, const std::string &message);
	/// Onglet Script : liste des scripts du projet, barre d'outils, éditeur pleine taille.
	void DrawScriptWorkspace(const sdl3::FRect &area);
	/// Barre d'outils de l'onglet Script (vérifier, commenter, indenter, taille du texte).
	void DrawScriptTools(float &x, float y, float size);
	/// Applique `op` au script édité (état d'édition de son éditeur) ; un pas d'annulation.
	void ApplyToScript(const std::function<bool(ui::TextEdit &, std::string &)> &op);

	LaunchOptions m_launch;
	Editor m_editor;
	Viewport m_viewport;
	DisplayOptions m_display;
	sdl3::FRect m_view{};
	std::string m_path;		  ///< fichier de la scène (vide : sans titre)
	bool m_home = false;	  ///< écran d'accueil affiché à la place de l'éditeur
	std::vector<std::string> m_recent; ///< projets (ou scènes) ouverts récemment, le plus récent d'abord
	std::string m_newName = "mon_projet", m_newFolder = "apps/sandbox/projects";
	std::optional<sandbox::Project> m_project; ///< projet de la scène (index `.project`)
	std::string m_titleShown; ///< dernier titre appliqué
	// Interface retenue (builder factory) : arbre construit une fois, affiché chaque image.
	std::unique_ptr<ui::Ui> m_ui;
	int m_tabLeft = 0, m_tabCenter = 0, m_tabRight = 0, m_tabBottom = 0; ///< onglet actif de chaque région
	bool m_leftShown = true, m_rightShown = true, m_bottomShown = true; ///< régions visibles (Affichage)
	float m_splitMain = 0.72f, m_splitTop = 0.22f, m_splitCenter = 0.72f; ///< ratios des séparateurs (persistés)
	ui::Widget m_toolbar, m_status, m_editorBody, m_gameBody, m_homeBody; ///< poignées des feuilles
	ui::Widget m_viewportSheet; ///< feuille de la vue 3D (curseur dynamique)
	bool m_viewShown = true; ///< onglet Vue 3D visible à l'image précédente (rendu hors écran utile)
	struct Asset {
		enum class Kind : uint8_t { Scene, Object, Model, Sound, Script, Image } kind = Kind::Model;
		std::string path;  ///< chemin à ouvrir
		std::string name;  ///< nom affiché
		std::string value; ///< valeur attribuée (fichier de son, script relatif au document)
	};
	std::vector<Asset> m_assets;
	std::string m_assetsFor = "\x01"; ///< document dont les ressources sont listées (rescan au changement)
	std::string m_assetFilter;
	// Fichiers ouverts dans l'éditeur du centre, explorateur.
	struct OpenFile {
		std::string path;
		String text, saved;
		[[nodiscard]] bool Dirty() const { return text != saved; }
	};
	std::vector<OpenFile> m_files;
	int m_activeFile = -1;	  ///< fichier affiché par l'onglet Script (-1 : script du nœud ou de la scène)
	String m_noFile;		  ///< « fichier du script » d'un fichier ouvert : sans objet
	bool m_inspecting = false; ///< inspecteur en cours : le script édité est celui de la sélection
	std::string m_explorerDir, m_explorerSel;
	std::set<std::string> m_explorerOpen; ///< dossiers dépliés
	// Arborescence (Structure).
	std::set<EntityId> m_collapsed; ///< nœuds repliés
	std::string m_outlinerFilter;	///< filtre par nom de la Structure (vide : tout afficher)
	EntityId m_dragNode = 0;		///< nœud glissé (changement de parent)
	float m_dragFromY = 0.f;
	EntityId m_dropTarget = ~EntityId(0); ///< cible du dépôt (0 : racine, ~0 : aucune)
	std::string m_pendingScene; ///< scène à ouvrir malgré des modifications (second clic)
	float m_importScale = 1.f; ///< échelle d'import des modèles (unités du fichier → mètres)
	// Réglages généraux (persistés dans le dossier de préférences).
	bool m_splitFiles = false;	   ///< fractionner automatiquement les fichiers volumineux
	uint64_t m_splitMaxMb = 100;   ///< taille maximale d'un fichier avant fractionnement (Mo)
	// Raccourcis clavier (persistés dans le dossier de préférences).
	ui::KeyMap m_keys;				///< registre des actions et de leurs combinaisons
	bool m_showShortcuts = false;	///< modal « Raccourcis clavier… » ouvert
	std::string m_shortcutFilter;	///< filtre du modal
	std::string m_shortcutCapture;	///< action en cours de re-liaison (vide = aucune)
	// Catalogue des outils et fonctionnalités.
	ui::Catalog m_catalog;			///< entrées du catalogue (outils, panneaux, notions, scripts)
	bool m_showCatalog = false;		///< panneau « Aide → Catalogue » ouvert
	std::string m_catalogFilter;	///< filtre du catalogue
	int m_catalogSelected = -1;		///< entrée sélectionnée (index dans `m_catalog.entries`)
	// Calque 2D en édition.
	enum class Drag2D : uint8_t { None, Move, Resize };
	uint32_t m_item2D = 0; ///< élément sélectionné (0 : aucun)
	std::set<uint32_t> m_items2D; ///< sélection multiple (Ctrl+clic, pour l'alignement)
	int8_t m_paintTerrain = 0;	  ///< terrain à peindre (0 = off) — outil « tuiles »
	Drag2D m_drag2D = Drag2D::None;
	math::FVector2 m_dragFrom; ///< point de départ du glissé (repère de référence)
	Item2D m_dragStart;		   ///< élément au début du glissé
	bool m_dragDirty = false;  ///< le glissé a modifié l'élément (pas d'annulation créé)
	float m_overlaySnap = 10.f; ///< pas d'aimantation (Maj) en pixels de référence
	float m_guide2DX = -1e30f, m_guide2DY = -1e30f; ///< guides d'alignement actifs (−1e30 : aucun)
	std::unordered_map<std::string, std::optional<sdl3::Texture>> m_images;
	// Icônes SVG des outils (SketchUp) et curseurs, chargés à la demande.
	std::unordered_map<std::string, std::optional<sdl3::Texture>> m_toolIcons;
	std::shared_ptr<ui::Image> m_cursorArrow, m_cursorPencil;
	std::deque<game::GameMessage> m_log; ///< console (les plus anciens retirés au-delà de 1000)
	bool m_logErrorsOnly = false;
	size_t m_gameErrors = 0; ///< erreurs de la partie en cours (console ouverte à l'arrêt)
	int m_gotoLine = 0;
	std::string m_paramsKey, m_paramsError; ///< paramètres lus pour ce script (clé : nom + empreinte du source)
	std::vector<std::pair<std::string, script::Value>> m_params;		 ///< ligne à montrer dans l'éditeur de script (0 : aucune)
	std::unordered_map<std::string, std::string> m_paramComments; ///< commentaire « # … » de chaque paramètre exporté
	std::string m_workspace = "viewport"; ///< onglet de travail actif (« viewport » ou « script ») : outils de la barre
	float m_codeFontSize = 0.f;			  ///< taille du texte des scripts (0 : celle du thème)
	// Navigation à la souris.
	enum class Nav : uint8_t { None, Orbit, Pan };
	Nav m_nav = Nav::None;
	Nav m_navTool = Nav::None; ///< outil Orbite / Panoramique de la barre (glissé gauche)
	float m_lastX = 0.f, m_lastY = 0.f;
	double m_lastClick = -1.0;
	float m_clickX = 0.f, m_clickY = 0.f;
	// Sélection par cadre (outil Sélection, espace vide à la racine).
	bool m_marquee = false;							 ///< cadre en cours de tracé
	math::FVector2 m_marqueeStart, m_marqueeCurrent; ///< coins du cadre (pixels écran)
	// Gizmo de déplacement (flèches par axe, outil Déplacer).
	int m_gizmoAxis = -1;		  ///< axe survolé / saisi (−1 aucun, 0 X, 1 Y, 2 Z, 3 uniforme)
	bool m_gizmoDragging = false; ///< glissé de gizmo en cours
	math::FVector3 m_gizmoCenter{}, m_gizmoGrab{};
	math::FVector3 m_gizmoGrabDir{}; ///< direction de référence (rotation)
	float m_gizmoGrabDist = 0.f;	 ///< distance de référence (échelle)
	float m_gizmoLength = 0.f;
	// Saisies du plateau.
	std::string m_nameEdit, m_newTag, m_newView;
	EntityId m_nameFor = 0;
	sdl3::Color m_pickColor = sdl3::Rgba(200, 160, 110);
	bool m_lockRatio = false; ///< plateau : dimensions modifiées dans le même rapport
	// Mode Jeu.
	game::SoundSystem m_sound;
	// Interfaces des scripts (module `ui`), détruites avant la partie (elles tiennent ses interpréteurs).
	std::unique_ptr<ui::Ui> m_gameUi;
	std::vector<std::unique_ptr<ui::ScriptUi>> m_scriptUis;
	std::unique_ptr<game::GameSession> m_game;
	std::vector<std::string> m_soundFiles; ///< fichiers de assets/sounds (onglet Jeu)
	float m_lookX = 0.f, m_lookY = 0.f;	   ///< souris accumulée depuis le dernier pas
	bool m_useQueued = false;				   ///< clic gauche : utiliser (relevé au pas suivant)
	int m_ignoreLook = 0;					   ///< images dont la souris est ignorée (passage en mode relatif)
	std::string m_scriptCheck;			   ///< résultat de « Vérifier » (onglet Jeu)
	std::string m_scriptCheckFor;		   ///< script vérifié (le résultat s'efface en changeant de script)
	int m_scriptErrorLine = 0;			   ///< ligne en erreur du dernier « Vérifier » (0 : aucune)
	// Résultats des boîtes de dialogue (rappel possiblement sur un autre fil).
	std::mutex m_dialogMutex;
	std::optional<std::pair<int, std::string>> m_dialogResult; ///< 0 ouvrir, 1 enregistrer, 2 exporter
	// Chargement en arrière-plan (ouverture / création de projet sans figer l'interface).
	std::mutex m_loadMutex;					  ///< protège m_loadResult
	std::optional<LoadOutcome> m_loadResult;  ///< résultat prêt à appliquer (thread principal)
	std::atomic<bool> m_loading{false};		  ///< une ouverture / création est en cours (spinner)
	std::string m_loadingLabel = "Ouverture du projet…"; ///< texte affiché sous le spinner
	bool m_launchPending = false;			  ///< options de lancement (--select, --play…) à appliquer après le premier chargement
};

} // namespace sandbox
