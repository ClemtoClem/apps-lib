#pragma once
/**
 * @file builder.hpp
 * @brief Fabrique déclarative d'interface (builder factory) + arbre retenu.
 *
 * L'application décrit son interface avec des builders fluents, chaque builder
 * étant une VALEUR qui accumule la configuration d'un nœud (identité, style,
 * enfants, callback de dessin personnalisé). Rien n'existe à l'écran tant que
 * l'arbre n'est pas monté dans un `ui::Ui` (voir retained.hpp) :
 *
 * @code{.cpp}
 * ui::Ui ui(canvas);
 * ui.Sheet(ui::StyleSheet::Load("assets/ui.css").Value());
 * ui.Root(ui::Column().Gap(8).Padding(16).Children({
 *     ui::Text("Options").FontSize(20).Bold(),
 *     ui::Row().Gap(8).Children({
 *         ui::Button("Quitter").Id("quit").Class("danger"),
 *         ui::Box().Grow(),                      // espace extensible
 *     }),
 *     ui::Custom([](sdl3::Canvas &c, const sdl3::FRect &content) {
 *         // dessin personnalisé : `content` = boîte de contenu du parent,
 *         // déjà borné par le clip rect.
 *         c.FillRect(content, sdl3::Rgba(60, 70, 100));
 *     }).Height(120.f),
 * }));
 * // … chaque image :
 * ui.Present(input, dt);   // style → mise en page → rendu automatiques
 * @endcode
 */
#include <functional>
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <sdl3/render/canvas.hpp>
#include <sdl3/input/input.hpp>
#include "css.hpp"
#include "code_editor.hpp"
#include "image.hpp"
#include "text_edit.hpp"
#include "style.hpp"

namespace ui {

using sdl3::Color;

class Context; // (immédiat) — utilisé par les feuilles `Custom`
struct Date;   // calendrier (Context::DatePicker)

// ============================================================================
// Types de widgets — source de vérité des noms de type (`Node::type`)
// ============================================================================
/// Noms de type des widgets retenus. `Node::type` est le sélecteur CSS de type
/// (« button », « text »…) : ces constantes sont LA source de vérité, partagées
/// entre la fabrique (`WidgetBuilder`) et le dispatch (`KindOf`), pour éviter
/// toute faute de frappe qui transformerait silencieusement un widget en `Box`.
namespace wt {
inline constexpr const char *box = "box";
inline constexpr const char *text = "text";
inline constexpr const char *label = "label";
inline constexpr const char *richtext = "richtext";
inline constexpr const char *image = "image";
inline constexpr const char *separator = "separator";
inline constexpr const char *button = "button";
inline constexpr const char *toggle = "toggle";
inline constexpr const char *radio = "radio";
inline constexpr const char *toolbutton = "toolbutton";
inline constexpr const char *checkbox = "checkbox";
inline constexpr const char *badge = "badge";
inline constexpr const char *lineedit = "lineedit";
inline constexpr const char *textarea = "textarea";
inline constexpr const char *codeeditor = "codeeditor";
inline constexpr const char *slider = "slider";
inline constexpr const char *spinbox = "spinbox";
inline constexpr const char *knob = "knob";
inline constexpr const char *progress = "progress";
inline constexpr const char *combobox = "combobox";
inline constexpr const char *panel = "panel";
inline constexpr const char *expander = "expander";
inline constexpr const char *scroll = "scroll";
inline constexpr const char *splitter = "splitter";
inline constexpr const char *modal = "modal";
inline constexpr const char *listview = "listview";
inline constexpr const char *treeview = "treeview";
inline constexpr const char *tableview = "tableview";
inline constexpr const char *tabbar = "tabbar";
inline constexpr const char *tabwidget = "tabwidget";
inline constexpr const char *menubar = "menubar";
inline constexpr const char *subwindow = "subwindow";
inline constexpr const char *popup = "popup";
inline constexpr const char *plot = "plot";
inline constexpr const char *pie = "pie";
inline constexpr const char *heatmap = "heatmap";
inline constexpr const char *selectable = "selectable"; ///< ligne d'une liste cliquable (sélection surlignée)
inline constexpr const char *switchbox = "switchbox"; ///< interrupteur à glissière (`Switch`) — « switch » est un mot réservé
inline constexpr const char *menubutton = "menubutton"; ///< grande entrée de menu (icône + titre + sous-titre)
inline constexpr const char *settingrow = "settingrow"; ///< ligne de réglage (titre + description + contrôle enfant)
inline constexpr const char *toolgroup = "toolgroup"; ///< groupe d'outils d'un ruban (outils en ligne, légende dessous, filet à droite)
} // namespace wt

// ============================================================================
// Node — nœud retenu (arbre de widgets)
// ============================================================================

struct Node;
using NodePtr = std::shared_ptr<Node>;

/// État de défilement d'un conteneur défilant (`Scroll`, `ListView`, `TreeView`,
/// `TableView`). Regroupé pour ne pas surcharger `Node` de champs plats.
struct ScrollState {
	float x = 0.f;        ///< décalage horizontal (contenu plus large que la zone)
	float y = 0.f;        ///< décalage vertical
	float contentW = 0.f; ///< largeur du contenu (barre horizontale si > zone)
	float contentH = 0.f; ///< hauteur du contenu (barre verticale si > zone)
	// Boutons fléchés (répétition au maintien) — même comportement que `Context::Scrollbar` :
	float arrowStep = 40.f;  ///< pas d'un clic de flèche (pixels de contenu)
	float arrowRepeat = 0.f; ///< temps restant avant la prochaine répétition
	int arrowDir = 0;        ///< flèche maintenue (-1 : début, +1 : fin) ; 0 : aucune
	int revealed = -1;       ///< onglets : onglet actif déjà amené dans la zone visible
};

/// État du fenêtrage (SubWindow / Popup flottants).
struct Floating {
	bool active = false;   ///< fenêtre flottante (hors du flux)
	float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
	bool resizable = false;
	bool dragging = false; ///< glissé de la barre de titre
	float dragOffsetX = 0.f, dragOffsetY = 0.f;
	int resizeEdge = -1;   ///< bord redimensionné en cours (-1 = aucun)
	float resizeStartX = 0.f, resizeStartY = 0.f, resizeStartW = 0.f, resizeStartH = 0.f;
};

/// État du glisser-déposer (source / cible).
struct DragDrop {
	bool draggable = false;
	std::string payload;
	bool dropTarget = false;
	std::function<void(std::string)> onDrop;
};

/// Modèle tabulaire (lignes × colonnes) piloté par l'application — pour
/// `ListView` et `TableView`. Les callbacks fournissent les données à la demande :
/// seules les lignes visibles sont interrogées (affichage virtuel).
struct TableModel {
	std::function<size_t()> rowCount = []() { return size_t(0); };
	std::function<size_t()> columnCount = []() { return size_t(1); };
	std::function<std::string(size_t row, size_t col)> text = [](size_t, size_t) { return std::string{}; };
	std::function<char32_t(size_t row)> icon = [](size_t) { return char32_t(0); };
};

/// Nœud d'une arborescence (`TreeView`) — construit par l'application.
struct TreeItem {
	std::string id;   ///< identifiant stable (chemin complet)
	std::string label;
	char32_t icon = 0; ///< icône (0 : dossier/fichier selon `isDir`)
	Color iconColor{0, 0, 0, 0};
	bool isDir = false;
	std::vector<TreeItem> children;
};

/// Item d'un menu (`MenuBar` / `ContextMenu`).
struct MenuItem {
	std::string label;
	std::string shortcut;   ///< affiché à droite (ex. « Ctrl+S »)
	bool separator = false; ///< ligne de séparation
	bool checkable = false; ///< case à cocher basculable
	bool checked = false;
	std::string radioGroup; ///< groupe radio (les items d'un même groupe s'excluent)
	std::function<void()> action;
	bool enabled = true;	///< faux : grisé, sans action
	/// Sous-menu (hiérarchie) : non vide → l'item ouvre un sous-menu (son `action`
	/// est ignoré, le clic/hover ouvre les enfants). Un seul niveau de profondeur.
	std::vector<MenuItem> children{}; // initialiseur : les agrégats partiels restent sans avertissement
};

/// Menu déroulant d'une `MenuBar` (titre + items).
struct Menu {
	std::string title;
	std::vector<MenuItem> items;
};

/// Type de rendu d'une série d'un graphique 2D.
enum class PlotKind : uint8_t { Line, Scatter, Bar, Area };

/// Série d'un graphique 2D (`Plot`).
struct PlotSeries {
	std::string name;                                   ///< légende
	Color color{74, 144, 255, 255};
	std::vector<std::pair<float, float>> points;        ///< points (x, y)
	PlotKind kind = PlotKind::Line;
	float width = 2.f;                                  ///< épaisseur de ligne / taille de marqueur
};

/// Données d'un graphique 2D cartésien (`Plot`).
struct PlotData {
	std::string title, xLabel, yLabel;
	std::vector<PlotSeries> series;
	bool autoFit = true;                                ///< ajuster les bornes aux données
	float xMin = 0.f, xMax = 1.f, yMin = 0.f, yMax = 1.f;
};

/// Tranche d'un diagramme circulaire (`PieChart`).
struct PieSlice {
	std::string label;
	float value = 0.f;
	Color color{74, 144, 255, 255};
};

/// État Modèle-Vue (ListView / TableView / TreeView) — regroupé pour ne pas
/// surcharger `Node` de champs plats liés aux vues tabulaires et arborescentes.
struct ModelView {
	TableModel model;
	std::vector<std::string> headers;
	std::vector<float> columnWidths;
	std::vector<TreeItem> tree;
	std::set<std::string> expanded;
	std::string selectedId;
	bool multiSelect = false;
	std::set<int> selectedRows;
	int sortColumn = -1;
	bool sortDescending = false;
	int resizeColumn = -1;
	float resizeStartX = 0.f, resizeStartWidth = 0.f;
	std::function<void(int)> onSelect;
	std::function<void(int)> onActivate;
	std::function<void(std::string)> onSelectId;
};

// ============================================================================
// Évènements normalisés livrés aux widgets
// ============================================================================

/// Évènement de pointeur (souris, tactile, stylet) livré à un widget.
struct PointerEvent {
	enum class Kind : uint8_t { Move, Enter, Leave, Press, Release, Click, Wheel, Rotate };
	Kind kind = Kind::Move;
	sdl3::MouseButton button = sdl3::MouseButton::Left; ///< bouton (Press/Release/Click)
	sdl3::FPoint position{}; ///< position fenêtre (logique)
	float wheelX = 0.f;      ///< molette horizontale (Wheel)
	float wheelY = 0.f;      ///< molette verticale (Wheel)
	float rotate = 0.f;      ///< rotation radians (Rotate)
	uint16_t mods = 0;       ///< modificateurs (Keymod)
	bool touch = false;      ///< provient d'un écran tactile / pavé (sinon souris)
};

/// Évènement clavier livré à un widget.
struct KeyEvent {
	enum class Kind : uint8_t { Down, Up, Repeat };
	Kind kind = Kind::Down;
	SDL_Keycode keycode = 0;
	SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
	uint16_t mods = 0;
};

/// Geste multi-doigts (pincement, rotation, balayage) livré à un widget.
struct GestureEvent {
	enum class Kind : uint8_t { None, Pinch, Rotate, Swipe, Pan };
	Kind kind = Kind::None;
	float pinch = 1.f;        ///< facteur d'échelle (Pinch)
	float rotate = 0.f;       ///< rotation radians (Rotate)
	float dx = 0.f, dy = 0.f; ///< déplacement (Swipe/Pan)
	sdl3::FPoint center{};    ///< centroïde (fenêtre, logique)
};

struct Node {
	std::string type;                 ///< type CSS ("box", "text", "button"…)
	std::string id;                   ///< identifiant unique (sélecteur #id)
	std::vector<std::string> classes; ///< classes CSS (sélecteur .classe)
	StyleProps style;                 ///< style inline (du builder)
	std::vector<StyleSheetPtr> localSheets; ///< feuilles propres à ce widget (Css()/Import())
	std::vector<NodePtr> children;
	Node *parent = nullptr;          ///< parent (renseigné au montage)
	std::string text; ///< contenu textuel (Text / Button)
	TextEdit textEdit; ///< curseur et sélection (LineEdit, TextArea, CodeEditor, texte sélectionné)
	std::shared_ptr<const SyntaxRules> syntax; ///< coloration d'un CodeEditor (nullptr : aucune)
	int errorLine = 0;						   ///< CodeEditor : ligne signalée en erreur (à partir de 1)
	/// Géométrie du texte au dernier rendu (placement du curseur à la souris).
	struct TextView {
		float x = 0, y = 0, size = 14.f, lineHeight = 16.f;
		sdl3::CanvasFont font = sdl3::CanvasFont::Regular;
		std::vector<float> lineX; ///< abscisse de chaque ligne (alignement centré, à droite)
	} textView;
	/// Dessin personnalisé du CONTENU : `content` = boîte de contenu (après
	/// marge/padding/bordure), le clip rect étant déjà posé par le rendu —
	/// tout ce qui déborde est rogné automatiquement.
	std::function<void(sdl3::Canvas &, const sdl3::FRect &content)> customPaint;
	/// Feuille dessinée avec le `ui::Context` immédiat existant (catalogue de
	/// widgets immédiat réutilisé) — le clip rect est posé avant l'appel.
	std::function<void(Context &)> customImmediate;
	bool hidden = false;
	bool disabled = false;
	/// Tooltip affichée au survol (sous-arbre construit par le builder ; par
	/// défaut un `Label`, mais n'importe quel widget est accepté).
	NodePtr tooltip;
	/// Callback de fermeture d'un modal (clic sur la croix).
	std::function<void()> onClose;

	// ── Données des widgets ──────────────────────────────────────────────────
	std::string placeholder;   ///< texte grisé d'un champ vide (LineEdit/TextArea/ComboBox)
	bool password = false;     ///< masquage du texte (LineEdit)
	bool editable = true;      ///< saisie autorisée (LineEdit/TextArea/ComboBox)
	float step = 1.f;          ///< pas d'incrémentation (SpinBox)
	bool indeterminate = false; ///< barre de progression indéterminée
	bool wordWrap = false;      ///< retour à la ligne automatique d'un Text / Label
	bool ellipsis = false;      ///< troncature « … » d'un Text mono-ligne trop large
	std::vector<std::string> options; ///< items d'un ComboBox / options d'un ButtonGroup
	int selectedIndex = -1;    ///< item sélectionné (ComboBox)
	std::string group;         ///< groupe de RadioButton (exclusion mutuelle)
	char32_t icon = 0;         ///< icône (ImageView, ToolButton)
	bool tintIcon = false;     ///< icône image monochrome teintée à la couleur du texte (SVG blanc)
	Color swatch{0, 0, 0, 0};  ///< ToolButton : barre de couleur sous l'icône (couleur courante) ; transparente : aucune
	bool passThrough = false;  ///< transparent au pointeur : jamais survolé ni cliqué (calque de dessin)
	std::shared_ptr<Image> iconImage;   ///< icône image (dessinée au lieu du glyphe de police)
	std::shared_ptr<Image> cursorImage; ///< curseur de souris personnalisé (image)
	int cursorHotX = 0, cursorHotY = 0; ///< point chaud du curseur image (pixels)
	bool open = false;         ///< état déplié/ouvert (Expander, ComboBox)
	std::string url;           ///< lien d'un RichTextLabel cliquable
	std::vector<std::pair<sdl3::FRect, std::string>> links; ///< rectangles des liens (RichText, calculés au rendu)

	// ── Données complexes (Modèle-Vue) ────────────────────────────────────────
	ModelView modelView;

	// ── Navigation (onglets, menus) ──────────────────────────────────────────
	std::vector<std::string> tabs;        ///< libellés des onglets (TabBar / TabWidget)
	std::vector<Menu> menus;              ///< menus déroulants (MenuBar) / items du menu contextuel (menus[0])
	int openMenu = -1;                    ///< menu déroulant ouvert (MenuBar, -1 = aucun)
	int openSubMenu = -1;                 ///< item du menu ouvert dont le sous-menu est ouvert (-1 = aucun)
	bool contextMenu = false;             ///< `menus[0]` s'ouvre au clic droit sur le widget
	float menuX = 0.f, menuY = 0.f;       ///< point d'ouverture du menu contextuel

	// ── Fenêtrage (SubWindow / Popup) ────────────────────────────────────────
	Floating floating;

	// ── Comportements (animations) ────────────────────────────────────────────
	float animTime = -1.f;                ///< -1 = inactive ; 0..durée = animation en cours
	float animDuration = 0.3f;            ///< durée d'une animation (secondes)
	float animOffset = 16.f;              ///< décalage du glissé (pixels)
	Easing animEasing = Easing::OutCubic; ///< easing du glissé

	// ── Comportements (drag & drop) ───────────────────────────────────────────
	DragDrop dragDrop;

	// ── DataViz ───────────────────────────────────────────────────────────────
	PlotData plot;                        ///< graphique 2D cartésien (Plot)
	std::vector<PieSlice> pie;            ///< diagramme circulaire (PieChart)
	std::vector<float> heat;              ///< valeurs de la heatmap (Heatmap)
	size_t heatCols = 0;                  ///< nombre de colonnes de la heatmap
	float heatMin = 0.f, heatMax = 1.f;   ///< bornes de la palette

	// ── État calculé (par image) ─────────────────────────────────────────────
	State state = State::None;
	ComputedStyle computed;
	sdl3::FRect bounds{};   ///< boîte de bordure (border box)
	sdl3::FRect content{};  ///< boîte de contenu (content box)
	bool clicked = false; ///< cliqué cette image (relâchement sur le même widget)
	/// Panneau d'un modal (centré dans la zone de fond) ; vide sinon.
	sdl3::FRect panel{};
	/// Bouton de fermeture (croix) d'un modal ; vide sinon.
	sdl3::FRect closeRect{};

	// ── État des widgets interactifs ─────────────────────────────────────────
	float value = 0.f;                  ///< valeur (slider, barre de progression, ratio du Splitter)
	float valueMin = 0.f, valueMax = 1.f;
	bool checked = false;               ///< case à cocher / interrupteur
	ScrollState scroll;                 ///< défilement d'un conteneur défilant
	/// Liaisons vers les variables de l'application (synchronisées à chaque image).
	float *valueRef = nullptr;
	bool *checkedRef = nullptr;
	int *indexRef = nullptr;
	std::string *textRef = nullptr;
	/// Appelés quand l'utilisateur interagit.
	std::function<void()> onClick;
	std::function<void(float)> onChanged;
	std::function<void(int)> onIndexChanged; ///< item sélectionné (ComboBox, SpinBox)
	std::function<void()> onSubmit;          ///< validation d'un champ (Entrée)
	std::function<void()> onEdited;          ///< texte modifié (LineEdit/TextArea)
	std::function<void(std::string)> onLink; ///< lien cliqué (RichTextLabel)

	// ── Évènements génériques (focus, souris, clavier, tactile, gestes) ───────
	std::function<void()> onGainFocus;   ///< widget devenu focus
	std::function<void()> onLoseFocus;   ///< widget ayant perdu le focus
	std::function<void()> onMouseEnter;  ///< souris entrée dans le widget
	std::function<void()> onMouseLeave;  ///< souris sortie du widget
	std::function<void()> onResize;      ///< taille du widget changée
	std::function<void(const KeyEvent &)> onKeyEvent; ///< touche enfoncée/relâchée (widget focus)
	std::function<void()> onKeyEnter;    ///< Entrée pressée (widget focus)
	std::function<void(const PointerEvent &)> onMouseEvent; ///< bouton/molette/rotation (souris)
	std::function<void(const PointerEvent &)> onTouchEvent; ///< idem, depuis un écran tactile / pavé
	std::function<void()> onButtonLeft;  ///< bouton gauche (clic)
	std::function<void()> onButtonRight; ///< bouton droit (clic)
	std::function<void()> onButtonMiddle;///< bouton du milieu (clic)
	std::function<void()> onButtonX1;    ///< bouton X1 (clic)
	std::function<void()> onButtonX2;    ///< bouton X2 (clic)
	std::function<void(float)> onWheelX; ///< molette horizontale (delta)
	std::function<void(float)> onWheelY; ///< molette verticale (delta)
	std::function<void(float)> onRotate; ///< rotation du geste (radians)
	std::function<void(const GestureEvent &)> onGesture; ///< geste multi-doigts brut
	std::function<void(int)> onSwipe;    ///< balayage horizontal : +1 suivant, −1 précédent
	std::function<void(float)> onZoom;   ///< pincement : facteur de zoom
	std::function<void()> onHidden;      ///< widget masqué (SetHidden(true))
	std::function<void()> onVisible;     ///< widget affiché (SetHidden(false))
	std::function<void()> onEnable;      ///< widget activé (SetEnabled(true))
	std::function<void()> onDisable;     ///< widget désactivé (SetEnabled(false))

	// ── Gestes & curseur ─────────────────────────────────────────────────────
	bool gestureZoom = false;     ///< pincement → zoom (OnGesture / OnZoom)
	bool gestureRotate = false;   ///< rotation → OnGesture / OnRotate
	bool gesturePrevNext = false; ///< balayage horizontal → OnSwipe (±1)

	// ── État de transition (interne à `Ui`) ───────────────────────────────────
	bool prevHidden = false, prevDisabled = false; ///< pour OnHidden/OnVisible/OnEnable/OnDisable
	sdl3::FRect prevBounds{}; ///< bornes de l'image précédente (OnResize)
	bool hasPrevBounds = false;

	// ── Traduction (ui::Ui::SetTranslator) ───────────────────────────────────
	/// Textes d'origine, mémorisés à la première traduction : rétablis quand une clé manque.
	struct I18nSource {
		bool captured = false;
		std::string text, placeholder, tooltip;
		std::vector<std::string> tabs, options, headers;
		std::vector<Menu> menus;
	};
	I18nSource i18n;

	[[nodiscard]] bool IsVisible() const noexcept { return !hidden && computed.display != Display::None; }
};

/// Poignée légère sur un nœud matérialisé (pour l'application).
class Widget {
public:
	Widget() = default;
	explicit Widget(NodePtr node) : m_node(std::move(node)) {}

	[[nodiscard]] explicit operator bool() const noexcept { return m_node != nullptr; }
	[[nodiscard]] Node *Get() const noexcept { return m_node.get(); }
	[[nodiscard]] Node *operator->() const noexcept { return m_node.get(); }
	[[nodiscard]] Node &operator*() const noexcept { return *m_node; }

	[[nodiscard]] const ComputedStyle &Computed() const noexcept { return m_node->computed; }
	[[nodiscard]] sdl3::FRect Bounds() const noexcept { return m_node ? m_node->bounds : sdl3::FRect{}; }
	[[nodiscard]] bool Hovered() const noexcept { return m_node && HasState(m_node->state, State::Hover); }
	[[nodiscard]] bool Active() const noexcept { return m_node && HasState(m_node->state, State::Active); }
	[[nodiscard]] bool Clicked() const noexcept { return m_node && m_node->clicked; }

	/// Met à jour le texte du nœud (un widget Text/Button).
	Widget &SetText(std::string text);
	/// Bascule une classe de style (déclenche un restyle à la prochaine image).
	Widget &AddClass(std::string name);
	/// Retire une classe de style.
	Widget &RemoveClass(std::string name);
	/// Met à jour la valeur d'un Slider / SpinBox / Knob / ProgressBar.
	Widget &SetValue(float value);
	/// Met à jour l'état d'une Checkbox / ToggleButton / RadioButton.
	Widget &SetChecked(bool on);
	/// Met à jour l'onglet / la ligne / le nœud sélectionné.
	Widget &SetSelectedIndex(int index);
	/// Affiche ou masque le widget.
	Widget &SetHidden(bool on);
	/// Active ou désactive le widget.
	Widget &SetEnabled(bool on);
	/// Curseur système affiché au survol du widget.
	Widget &SetCursor(sdl3::SystemCursor cursor);
	Widget &SetIcon(char32_t icon);
	/// Barre de couleur d'un ToolButton (transparente : aucune).
	Widget &SetSwatch(Color color);
	/// Remplace l'info-bulle (texte simple).
	Widget &SetTooltip(std::string text);
	/// Position absolue dans le parent (left, top, largeur, hauteur), modifiable à chaque image
	/// (calque au-dessus d'une fenêtre surgissante…).
	Widget &Place(const sdl3::FRect &r);

private:
	NodePtr m_node;
};

// ============================================================================
// WidgetBuilder — builder fluide
// ============================================================================

class WidgetBuilder {
public:
	explicit WidgetBuilder(std::string type = "box");

	// ── Identité ─────────────────────────────────────────────────────────────
	WidgetBuilder &Id(std::string id);
	WidgetBuilder &Class(std::string name);
	WidgetBuilder &Classes(std::initializer_list<std::string> names);
	WidgetBuilder &Type(std::string type);

	// ── CSS ──────────────────────────────────────────────────────────────────
	WidgetBuilder &Import(std::string path);
	WidgetBuilder &Css(std::string text);

	// ── Position / taille ─────────────────────────────────────────────────────
	WidgetBuilder &Width(Length v);
	WidgetBuilder &Height(Length v);
	WidgetBuilder &MinWidth(Length v);
	WidgetBuilder &MinHeight(Length v);
	WidgetBuilder &MaxWidth(Length v);
	WidgetBuilder &MaxHeight(Length v);
	WidgetBuilder &Position(Position p);
	WidgetBuilder &Anchor(Anchor a);
	WidgetBuilder &Top(Length v);
	WidgetBuilder &Right(Length v);
	WidgetBuilder &Bottom(Length v);
	WidgetBuilder &Left(Length v);

	// ── Boîte ────────────────────────────────────────────────────────────────
	WidgetBuilder &Margin(float all);
	WidgetBuilder &Margin(const math::Sides &s);
	WidgetBuilder &Padding(float all);
	WidgetBuilder &Padding(const math::Sides &s);
	WidgetBuilder &Border(const math::Sides &widths, Color color);
	WidgetBuilder &BorderRadius(const math::Corners &c);
	WidgetBuilder &Background(Color c);
	WidgetBuilder &Opacity(float a);
	WidgetBuilder &ZIndex(int z);

	// ── Flex / grid ───────────────────────────────────────────────────────────
	WidgetBuilder &Display(Display d);
	WidgetBuilder &FlexDirection(FlexDirection d);
	WidgetBuilder &Wrap();
	WidgetBuilder &Justify(Justify j);
	WidgetBuilder &AlignItems(Align a);
	WidgetBuilder &AlignSelf(Align a);
	WidgetBuilder &Grow(float g = 1.f);
	WidgetBuilder &Shrink(float s);
	WidgetBuilder &Gap(float g);
	WidgetBuilder &GridColumns(int n);
	WidgetBuilder &OverflowX(Overflow o);
	WidgetBuilder &OverflowY(Overflow o);

	// ── Typographie ───────────────────────────────────────────────────────────
	WidgetBuilder &Color(Color c);
	WidgetBuilder &FontSize(float px);
	WidgetBuilder &FontWeight(int w);
	WidgetBuilder &Bold();
	WidgetBuilder &Italic();
	WidgetBuilder &Underline();
	WidgetBuilder &TextAlign(TextAlign a);
	/// Retour à la ligne automatique d'un `Text` / `Label` à la largeur du widget.
	WidgetBuilder &WordWrap(bool on = true);
	/// Troncature « … » d'un `Text` mono-ligne dont le texte dépasse la largeur.
	WidgetBuilder &Ellipsis(bool on = true);

	// ── État initial ──────────────────────────────────────────────────────────
	WidgetBuilder &Hidden(bool on = true);
	WidgetBuilder &Disabled(bool on = true);

	// ── Enfants ───────────────────────────────────────────────────────────────
	WidgetBuilder &Children(std::initializer_list<WidgetBuilder> children);
	WidgetBuilder &Child(WidgetBuilder child);

	/// Reçoit la poignée du widget une fois monté.
	WidgetBuilder &Ref(Widget &out);

	// ── Liaisons et évènements ───────────────────────────────────────────────
	/// Lie la valeur d'un Slider/SpinBox/Knob à `v` (lue puis écrite à chaque image).
	WidgetBuilder &Bind(float &v);
	/// Lie l'état d'une Checkbox/ToggleButton/RadioButton à `v`.
	WidgetBuilder &Bind(bool &v);
	/// Lie l'index sélectionné d'un ComboBox à `v`.
	WidgetBuilder &Bind(int &v);
	/// Lie le texte d'un LineEdit/TextArea à `v`.
	WidgetBuilder &Bind(std::string &v);
	/// Appelé quand l'utilisateur clique (Button) — ne pas cumuler avec Bind.
	WidgetBuilder &OnClick(std::function<void()> fn);
	/// Appelé quand l'utilisateur change la valeur (Slider, SpinBox, Knob).
	WidgetBuilder &OnChange(std::function<void(float)> fn);
	/// Appelé quand l'utilisateur choisit un item (ComboBox) — index sélectionné.
	WidgetBuilder &OnIndexChanged(std::function<void(int)> fn);
	/// Appelé quand l'utilisateur valide un champ (Entrée dans LineEdit).
	WidgetBuilder &OnSubmit(std::function<void()> fn);
	/// Appelé quand le texte d'un champ est modifié.
	WidgetBuilder &OnEdited(std::function<void()> fn);
	/// Appelé quand un lien d'un RichTextLabel est cliqué (URL en argument).
	WidgetBuilder &OnLink(std::function<void(std::string)> fn);

	// ── Évènements génériques (focus, souris, clavier, tactile, gestes) ───────
	WidgetBuilder &OnGainFocus(std::function<void()> fn);
	WidgetBuilder &OnLoseFocus(std::function<void()> fn);
	WidgetBuilder &OnMouseEnter(std::function<void()> fn);
	WidgetBuilder &OnMouseLeave(std::function<void()> fn);
	WidgetBuilder &OnResize(std::function<void()> fn);
	/// Touche enfoncée / relâchée (le widget doit avoir le focus). `fn` reçoit
	/// l'évènement normalisé (touche, modificateurs, Down/Up/Repeat).
	WidgetBuilder &OnKeyEvent(std::function<void(const KeyEvent &)> fn);
	/// Raccourci : `OnKeyEvent` filtré sur la touche Entrée.
	WidgetBuilder &OnKeyEnter(std::function<void()> fn);
	/// Bouton de souris / molette / rotation sur le widget (la souris doit être
	/// dessus). L'évènement normalisé précise le type et le bouton.
	WidgetBuilder &OnMouseEvent(std::function<void(const PointerEvent &)> fn);
	/// Évènement de pointeur provenant d'un écran tactile / pavé (au lieu d'une souris).
	WidgetBuilder &OnTouchEvent(std::function<void(const PointerEvent &)> fn);
	/// Raccourcis par bouton de souris (clic sur le widget).
	WidgetBuilder &OnButtonLeft(std::function<void()> fn);
	WidgetBuilder &OnButtonRight(std::function<void()> fn);
	WidgetBuilder &OnButtonMiddle(std::function<void()> fn);
	WidgetBuilder &OnButtonX1(std::function<void()> fn);
	WidgetBuilder &OnButtonX2(std::function<void()> fn);
	/// Molette horizontale (delta) sur le widget.
	WidgetBuilder &OnWheelX(std::function<void(float)> fn);
	/// Molette verticale (delta) sur le widget.
	WidgetBuilder &OnWheelY(std::function<void(float)> fn);
	/// Rotation du geste multi-doigts (radians) — nécessite `.GestureRotate()`.
	WidgetBuilder &OnRotate(std::function<void(float)> fn);
	/// Geste multi-doigts brut (pincement, rotation, balayage) sur le widget.
	WidgetBuilder &OnGesture(std::function<void(const GestureEvent &)> fn);
	/// Balayage horizontal (deux doigts / molette X) : `+1` suivant, `−1` précédent.
	WidgetBuilder &OnSwipe(std::function<void(int)> fn);
	/// Pincement : facteur de zoom (nécessite `.GestureZoom()`).
	WidgetBuilder &OnZoom(std::function<void(float)> fn);
	WidgetBuilder &OnHidden(std::function<void()> fn);
	WidgetBuilder &OnVisible(std::function<void()> fn);
	WidgetBuilder &OnEnable(std::function<void()> fn);
	WidgetBuilder &OnDisable(std::function<void()> fn);

	// ── Gestes & curseur ─────────────────────────────────────────────────────
	/// Autorise le pincement (zoom) sur le widget — délivré via `OnZoom`/`OnGesture`.
	WidgetBuilder &GestureZoom(bool on = true);
	/// Autorise la rotation sur le widget — délivrée via `OnRotate`/`OnGesture`.
	WidgetBuilder &GestureRotate(bool on = true);
	/// Lie un balayage horizontal (deux doigts) à `OnSwipe(±1)` (précédent/suivant).
	WidgetBuilder &GesturePrevNext(bool on = true);
	/// Curseur système affiché au survol du widget (`SystemCursor::TEXT`, `MOVE`, …).
	WidgetBuilder &Cursor(sdl3::SystemCursor cursor);
	/// Curseur de souris personnalisé : l'image est affichée comme curseur au
	/// survol du widget (`hotX`/`hotY` = point chaud en pixels).
	WidgetBuilder &Cursor(Image image, int hotX = 0, int hotY = 0);
	/// Charge un fichier image (SVG/PNG/JPG/BMP…) et l'utilise comme curseur.
	WidgetBuilder &Cursor(std::string path, int hotX = 0, int hotY = 0);

	// ── Titre / tooltip / fermeture ──────────────────────────────────────────
	/// Titre d'un Modal, d'un Panel ou d'un Expander : texte affiché dans l'en-tête.
	WidgetBuilder &Title(std::string text);
	/// Tooltip affichée au survol du widget (un `Label` multi-lignes par défaut).
	WidgetBuilder &Tooltip(std::string text);
	/// Tooltip dont le contenu est un widget arbitraire (affiché au premier plan
	/// à la position de la souris).
	WidgetBuilder &Tooltip(WidgetBuilder content);
	/// Appelé quand un Modal est fermé (bouton croix).
	WidgetBuilder &OnClose(std::function<void()> fn);

	// ── Données des widgets ──────────────────────────────────────────────────
	/// Texte grisé d'un champ vide (LineEdit/TextArea/ComboBox).
	WidgetBuilder &Placeholder(std::string text);
	/// Règles de coloration d'un CodeEditor.
	WidgetBuilder &Syntax(std::shared_ptr<const SyntaxRules> rules);
	/// Ligne signalée en erreur d'un CodeEditor (0 : aucune).
	WidgetBuilder &ErrorLine(int line);
	/// Masquage du texte (LineEdit, mode mot de passe).
	WidgetBuilder &Password(bool on = true);
	/// Saisie autorisée (LineEdit/TextArea/ComboBox).
	WidgetBuilder &Editable(bool on = true);
	/// Pas d'incrémentation d'un SpinBox.
	WidgetBuilder &Step(float s);
	/// Barre de progression indéterminée (animation).
	WidgetBuilder &Indeterminate(bool on = true);
	/// Items d'un ComboBox.
	WidgetBuilder &Options(std::initializer_list<std::string_view> items);
	WidgetBuilder &Options(std::vector<std::string> items);
	/// Groupe d'un RadioButton (les radios d'un même groupe s'excluent).
	WidgetBuilder &Group(std::string name);
	/// Icône d'un ToolButton / ImageView / Selectable / MenuButton (point de code de la police d'icônes).
	WidgetBuilder &Icon(char32_t code);
	/// Icône image d'un bouton / outil / entrée / ImageView (SVG, PNG, JPG, BMP…).
	WidgetBuilder &Icon(Image image);
	/// Charge un fichier image et l'utilise comme icône.
	WidgetBuilder &Icon(std::string path);
	/// État sélectionné initial d'un Selectable / Switch (équivalent de `.Bind(bool)`
	/// sans liaison). Voir aussi `Widget::SetChecked`.
	WidgetBuilder &Selected(bool on = true);
	/// Icône image monochrome (SVG blanc…) teintée à la couleur du texte du widget.
	WidgetBuilder &TintIcon(bool on = true);
	/// ToolButton : barre de couleur sous l'icône (couleur du texte, du surlignage…).
	WidgetBuilder &Swatch(sdl3::Color color);
	/// Transparent au pointeur (`pointer-events: none`) : ni survol ni clic, les widgets
	/// dessous les reçoivent ; ses enfants restent interactifs.
	WidgetBuilder &PassThrough(bool on = true);
	/// Sous-titre d'un MenuButton (texte grisé sous le titre).
	WidgetBuilder &Subtitle(std::string text);
	/// Lien d'un RichTextLabel cliquable.
	WidgetBuilder &Url(std::string url);
	/// État initial déplié d'un Expander / ouvert d'un ComboBox.
	WidgetBuilder &Open(bool on = true);
	/// Passe un Separator / Splitter en orientation verticale.
	WidgetBuilder &Vertical(bool on = true);

	// ── Données complexes (Modèle-Vue) ────────────────────────────────────────
	/// Modèle tabulaire d'un ListView / TableView.
	WidgetBuilder &Model(TableModel model);
	/// En-têtes de colonnes d'un TableView.
	WidgetBuilder &Headers(std::initializer_list<std::string_view> headers);
	/// Sélection multiple d'un ListView / TableView.
	WidgetBuilder &MultiSelect(bool on = true);
	/// Racines d'une arborescence (TreeView).
	WidgetBuilder &Nodes(std::vector<TreeItem> roots);
	/// Appelé quand une ligne / un nœud est sélectionné (index, ou id pour TreeView).
	WidgetBuilder &OnSelect(std::function<void(int)> fn);
	WidgetBuilder &OnSelectId(std::function<void(std::string)> fn);
	/// Appelé quand une ligne est activée (double-clic / Entrée).
	WidgetBuilder &OnActivate(std::function<void(int)> fn);

	// ── Navigation (onglets, menus) ──────────────────────────────────────────
	/// Libellés des onglets d'un TabBar / TabWidget (avec `.Bind(int)`).
	WidgetBuilder &Tabs(std::initializer_list<std::string_view> tabs);
	/// Libellés calculés (traduits…).
	WidgetBuilder &Tabs(std::vector<std::string> tabs);
	/// Menus déroulants d'une MenuBar (titre + items, avec raccourcis et actions).
	WidgetBuilder &Menus(std::vector<Menu> menus);
	/// Menu contextuel : s'ouvre au clic droit sur le widget (ou sur un descendant
	/// qui n'a pas le sien), au point du clic ; mêmes items qu'une MenuBar.
	WidgetBuilder &ContextMenu(std::vector<MenuItem> items);

	// ── Fenêtrage ─────────────────────────────────────────────────────────────
	/// Position (top-left) d'une fenêtre flottante (SubWindow / Popup).
	WidgetBuilder &At(float x, float y);
	/// Taille d'une fenêtre flottante.
	WidgetBuilder &Size(float w, float h);
	/// Fenêtre redimensionnable (SubWindow) : bords et coins déplaçables.
	WidgetBuilder &Resizable(bool on = true);

	// ── Comportements (animations) ────────────────────────────────────────────
	/// Anime le widget à son apparition : glissé depuis `offset` pixels (bas) avec
	/// l'easing choisi, sur `duration` secondes.
	WidgetBuilder &SlideIn(float duration = 0.3f, float offset = 16.f, Easing easing = Easing::OutCubic);

	// ── Comportements (drag & drop) ───────────────────────────────────────────
	/// Source de glisser-déposer : transporte `payload` vers une cible.
	WidgetBuilder &Draggable(std::string payload);
	/// Cible de dépôt : `fn` reçoit la donnée déposée.
	WidgetBuilder &DropTarget(std::function<void(std::string)> fn);

	// ── DataViz ───────────────────────────────────────────────────────────────
	/// Données d'un graphique 2D cartésien (Plot).
	WidgetBuilder &Plot(PlotData data);
	/// Tranches d'un diagramme circulaire (PieChart).
	WidgetBuilder &Pie(std::vector<PieSlice> slices);
	/// Matrice d'une heatmap (valeurs `rows`×`cols`, bornes de la palette).
	WidgetBuilder &Heatmap(std::vector<float> values, size_t cols, float min, float max);

	/// Matérialise le widget (et sa descendance). `parent` vide = racine.
	[[nodiscard]] NodePtr Build() const;

	[[nodiscard]] const Node &Spec() const noexcept { return *m_node; }
	[[nodiscard]] Node &Spec() noexcept { return *m_node; }

private:
	NodePtr m_node;
};

// ============================================================================
// Fabriques du catalogue
// ============================================================================

[[nodiscard]] WidgetBuilder Box();                              ///< conteneur générique
[[nodiscard]] WidgetBuilder Row();                              ///< flex en ligne
[[nodiscard]] WidgetBuilder Column();                           ///< flex en colonne
[[nodiscard]] WidgetBuilder Text(std::string_view text);        ///< libellé (une ligne)
/// Libellé multi-lignes (retours à la ligne `\n`) : couleurs, gras, italique,
/// souligné — l'équivalent d'un `<span>` HTML, utilisé par défaut pour les tooltips.
[[nodiscard]] WidgetBuilder Label(std::string_view text);
[[nodiscard]] WidgetBuilder Button(std::string_view label);     ///< bouton
[[nodiscard]] WidgetBuilder Spacer();                           ///< espace extensible (flex-grow)
/// Curseur (valeur bornée). Lier avec `.Bind(valeur)` et/ou `.OnChange(fn)`.
[[nodiscard]] WidgetBuilder Slider(float min, float max);
/// Case à cocher. Lier avec `.Bind(état)`.
[[nodiscard]] WidgetBuilder Checkbox(std::string_view label);
/// Barre de progression (fraction 0..1, à mettre à jour via `.Bind(v)`).
[[nodiscard]] WidgetBuilder ProgressBar(float fraction = 0.f);
/// Conteneur défilant : ses enfants défilent (molette) dans un clip.
[[nodiscard]] WidgetBuilder Scroll();
/// Panneau titré : un en-tête `title` puis les enfants dans le corps.
[[nodiscard]] WidgetBuilder Panel(std::string_view title);
/// Overlay modal : un fond sombre semi-transparent recouvre l'arrière-plan (qui
/// ignore alors les évènements), et un panneau centré affiche un titre, un
/// bouton croix (`.OnClose`) et les widgets de `.Children`. Selon sa position
/// dans l'arborescence, il masque les widgets placés avant lui (à la racine, il
/// masque tout).
[[nodiscard]] WidgetBuilder Modal();
/// Widget au CONTENU dessiné par l'application : `content` = boîte de contenu
/// du parent, clip rect déjà posé (tout dessin hors de `content` est rogné).
[[nodiscard]] WidgetBuilder Custom(std::function<void(sdl3::Canvas &, const sdl3::FRect &content)> paint);
/// Feuille dessinée avec le `ui::Context` immédiat (le catalogue de widgets
/// immédiat existant est réutilisé) — clip rect déjà posé avant l'appel.
[[nodiscard]] WidgetBuilder Custom(std::function<void(Context &)> draw);
/// Sélecteur de couleur (teinte, saturation, valeur et transparence) — enroule
/// `Context::ColorPicker` dans une feuille `Custom` ; `color` est lue/écrite.
[[nodiscard]] WidgetBuilder ColorPicker(Color &color, float height = 170.f);
/// Sélecteur de date — enroule `Context::DatePicker` dans une feuille `Custom`.
[[nodiscard]] WidgetBuilder DatePicker(Date &date);

// ── Texte & affichage ────────────────────────────────────────────────────────
/// Filet de séparation horizontal (ou vertical selon `.Vertical()`).
[[nodiscard]] WidgetBuilder Separator();
/// Image / icône centrée (point de code de la police d'icônes), libellé optionnel.
[[nodiscard]] WidgetBuilder ImageView(char32_t icon = 0);
/// Image centrée (fichier image : SVG, PNG, JPG, BMP…), libellé optionnel.
[[nodiscard]] WidgetBuilder ImageView(Image image);
[[nodiscard]] WidgetBuilder ImageView(std::string path);
/// Texte enrichi (Markdown léger : `**gras**`, `*italique*`, `_souligné_`,
/// `` `code` ``, `[lien](url)`) — les liens sont cliquables.
[[nodiscard]] WidgetBuilder RichTextLabel(std::string_view markdown);
/// Titre de section (classe `.heading` : police agrandie et grasse).
[[nodiscard]] WidgetBuilder Heading(std::string_view text);
/// Pilule de statut / étiquette colorée (`color` teinte le fond et le texte).
[[nodiscard]] WidgetBuilder Badge(std::string_view text, Color color);

// ── Boutons ──────────────────────────────────────────────────────────────────
/// Bouton basculant (état coché) — lier avec `.Bind(bool)`.
[[nodiscard]] WidgetBuilder ToggleButton(std::string_view label);
/// Interrupteur à glissière (commutateur on/off) — lier avec `.Bind(bool)`,
/// `.Selected(...)` pour l'état initial, `.OnClick` pour la notification.
[[nodiscard]] WidgetBuilder Switch(std::string_view label = {});
/// Bouton radio — les radios d'un même `.Group(...)` s'excluent mutuellement.
[[nodiscard]] WidgetBuilder RadioButton(std::string_view label);
/// Bouton d'icône seule (avec tooltip).
[[nodiscard]] WidgetBuilder ToolButton(char32_t icon);
/// Bouton d'outil à icône image (fichier SVG ou PNG), teintée si `tint`.
[[nodiscard]] WidgetBuilder ToolButton(std::string path, bool tint = true);
/// Groupe d'outils d'un ruban : enfants en ligne (centrés), légende `label` dessous, filet à
/// droite ; au moins aussi large que sa légende.
[[nodiscard]] WidgetBuilder ToolGroup(std::string_view label);
/// Bouton d'action principale (classe `.primary`).
[[nodiscard]] WidgetBuilder PrimaryButton(std::string_view label);
/// Bouton d'action destructive (classe `.danger`).
[[nodiscard]] WidgetBuilder DangerButton(std::string_view label);
/// Bouton d'action positive (classe `.success`).
[[nodiscard]] WidgetBuilder SuccessButton(std::string_view label);

// ── Listes & navigation ─────────────────────────────────────────────────────
/// Ligne d'une liste cliquable : sélection surlignée (`.Selected(...)` /
/// `.Bind(bool)`), `.OnClick` à la sélection, `.Icon(...)` optionnelle.
[[nodiscard]] WidgetBuilder Selectable(std::string_view label);
/// Grande entrée de menu (panneau de navigation) : icône + titre + sous-titre.
[[nodiscard]] WidgetBuilder MenuButton(std::string_view title, std::string_view subtitle = {}, char32_t icon = 0);
/// Ligne de réglage : titre (gras) + description (grisée), le contrôle est un
/// enfant (`.Children({ Slider(…) })`). Équivalent retenu de `Setting*` immédiat.
[[nodiscard]] WidgetBuilder SettingRow(std::string_view title, std::string_view description = {});

// ── Saisie de texte ──────────────────────────────────────────────────────────
/// Champ de saisie sur une ligne (masquage par `.Password()`, `.Placeholder(...)`).
[[nodiscard]] WidgetBuilder LineEdit();
/// Zone de saisie multi-lignes.
[[nodiscard]] WidgetBuilder TextArea();
/// Éditeur de code : numéros de ligne, coloration (`.Syntax`), indentation automatique,
/// Tab / Maj+Tab sur plusieurs lignes ; texte lié par `.Bind(chaîne)`.
[[nodiscard]] WidgetBuilder CodeEditor();

// ── Contrôle de valeurs ──────────────────────────────────────────────────────
/// Champ numérique avec flèches d'incrémentation (`.Step(...)`).
[[nodiscard]] WidgetBuilder SpinBox(float min, float max, float step = 1.f);
/// Cadran circulaire (glisser verticalement pour changer la valeur).
[[nodiscard]] WidgetBuilder Knob(float min, float max);

// ── Sélection ────────────────────────────────────────────────────────────────
/// Menu déroulant : `.Options({...})` + `.Bind(int)` / `.OnIndexChanged(...)`.
[[nodiscard]] WidgetBuilder ComboBox();

// ── Conteneurs ───────────────────────────────────────────────────────────────
/// Section dépliante : un en-tête cliquable (flèche + titre) et un corps.
[[nodiscard]] WidgetBuilder Expander(std::string_view title);
/// Séparateur redimensionnable entre ses enfants (horizontal par défaut,
/// `.Vertical()` pour un panneau vertical).
[[nodiscard]] WidgetBuilder Splitter();
/// Conteneur en ligne avec retour à la ligne automatique (flex-wrap).
[[nodiscard]] WidgetBuilder Flow();

// ── Données complexes (Modèle-Vue) ───────────────────────────────────────────
/// Liste virtuelle (seules les lignes visibles sont rendues). `.Model(...)` +
/// `.Bind(int)` / `.OnSelect(...)` / `.OnActivate(...)`.
[[nodiscard]] WidgetBuilder ListView();
/// Arborescence repliable (indentation, déplier/plier). `.Nodes(...)`.
[[nodiscard]] WidgetBuilder TreeView();
/// Tableau : en-têtes triables et redimensionnables, édition de cellules,
/// sélection simple ou multiple. `.Model(...)` + `.Headers(...)`.
[[nodiscard]] WidgetBuilder TableView();

// ── Navigation (onglets, menus) ─────────────────────────────────────────────
/// Barre d'onglets réutilisable : `.Tabs({...})` + `.Bind(int)`.
[[nodiscard]] WidgetBuilder TabBar();
/// Conteneur à onglets : la barre d'onglets affiche l'enfant actif parmi ses
/// enfants (un par onglet). `.Tabs({...})` + `.Bind(int)`.
[[nodiscard]] WidgetBuilder TabWidget();
/// Barre de menus d'application : `.Menus({...})` — clic sur un titre ouvre le
/// menu déroulant (items, séparateurs, cases, radios, raccourcis, actions).
[[nodiscard]] WidgetBuilder MenuBar();

// ── Fenêtrage ────────────────────────────────────────────────────────────────
/// Fenêtre interne flottante (type MDI) : barre de titre déplaçable, bouton
/// fermer (`.OnClose`), `.Resizable()`, amenée au premier plan au clic.
[[nodiscard]] WidgetBuilder SubWindow(std::string_view title);
/// Surface flottante transitoire : positionnée via `.At(x, y)`, fermée au clic
/// à l'extérieur.
[[nodiscard]] WidgetBuilder Popup();

// ── DataViz ───────────────────────────────────────────────────────────────────
/// Graphique 2D cartésien : axes, grille, graduations, et séries (ligne,
/// nuage de points, barres, aire). `.Plot(...)`.
[[nodiscard]] WidgetBuilder Plot();
/// Diagramme circulaire / anneau (donut). `.Pie({...})`.
[[nodiscard]] WidgetBuilder PieChart();
/// Heatmap : matrice de valeurs colorée par une palette. `.Heatmap(...)`.
[[nodiscard]] WidgetBuilder Heatmap();

} // namespace ui
