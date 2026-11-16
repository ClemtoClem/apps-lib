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
#include "css.hpp"
#include "code_editor.hpp"
#include "text_edit.hpp"
#include "style.hpp"

namespace ui {

using sdl3::Color;

class Context; // (immédiat) — utilisé par les feuilles `Custom`

// ============================================================================
// Node — nœud retenu (arbre de widgets)
// ============================================================================

struct Node;
using NodePtr = std::shared_ptr<Node>;

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
	std::vector<std::string> options; ///< items d'un ComboBox / options d'un ButtonGroup
	int selectedIndex = -1;    ///< item sélectionné (ComboBox)
	std::string group;         ///< groupe de RadioButton (exclusion mutuelle)
	char32_t icon = 0;         ///< icône (ImageView, ToolButton)
	bool open = false;         ///< état déplié/ouvert (Expander, ComboBox)
	std::string url;           ///< lien d'un RichTextLabel cliquable
	std::vector<std::pair<sdl3::FRect, std::string>> links; ///< rectangles des liens (RichText, calculés au rendu)

	// ── Données complexes (Modèle-Vue) ────────────────────────────────────────
	TableModel model;                     ///< modèle tabulaire (ListView / TableView)
	std::vector<std::string> headers;     ///< en-têtes de colonnes (TableView)
	std::vector<float> columnWidths;      ///< largeurs de colonnes (TableView, ajustables)
	std::vector<TreeItem> tree;           ///< racines de l'arborescence (TreeView)
	std::set<std::string> expanded;       ///< nœuds dépliés (TreeView, état interne)
	std::string selectedId;               ///< nœud sélectionné (TreeView)
	bool multiSelect = false;             ///< sélection multiple (ListView / TableView)
	std::set<int> selectedRows;           ///< lignes sélectionnées (multi-sélection)
	int sortColumn = -1;                  ///< colonne triée (TableView, -1 = aucune)
	bool sortDescending = false;          ///< ordre de tri décroissant
	int resizeColumn = -1;                ///< colonne redimensionnée en cours (TableView)
	float resizeStartX = 0.f, resizeStartWidth = 0.f;
	std::function<void(int)> onSelect;    ///< ligne/nœud sélectionné
	std::function<void(int)> onActivate;  ///< double-clic / Entrée sur une ligne
	std::function<void(std::string)> onSelectId; ///< nœud sélectionné (TreeView, id)

	// ── Navigation (onglets, menus) ──────────────────────────────────────────
	std::vector<std::string> tabs;        ///< libellés des onglets (TabBar / TabWidget)
	std::vector<Menu> menus;              ///< menus déroulants (MenuBar) / items du menu contextuel (menus[0])
	int openMenu = -1;                    ///< menu déroulant ouvert (MenuBar, -1 = aucun)
	bool contextMenu = false;             ///< `menus[0]` s'ouvre au clic droit sur le widget
	float menuX = 0.f, menuY = 0.f;       ///< point d'ouverture du menu contextuel

	// ── Fenêtrage (SubWindow / Popup) ────────────────────────────────────────
	bool floating = false;                ///< fenêtre flottante (position absolue, hors du flux)
	float floatX = 0.f, floatY = 0.f;     ///< position (top-left) d'une fenêtre flottante
	float floatW = 0.f, floatH = 0.f;     ///< taille d'une fenêtre flottante
	bool resizable = false;               ///< redimensionnable (SubWindow)
	bool dragging = false;                ///< glissé de la barre de titre en cours
	float dragOffsetX = 0.f, dragOffsetY = 0.f;
	int winResizeEdge = -1;               ///< bord redimensionné en cours (-1 = aucun)
	float winResizeStartX = 0.f, winResizeStartY = 0.f, winResizeStartW = 0.f, winResizeStartH = 0.f;

	// ── Comportements (animations) ────────────────────────────────────────────
	float animTime = -1.f;                ///< -1 = inactive ; 0..durée = animation en cours
	float animDuration = 0.3f;            ///< durée d'une animation (secondes)
	float animOffset = 16.f;              ///< décalage du glissé (pixels)
	Easing animEasing = Easing::OutCubic; ///< easing du glissé

	// ── Comportements (drag & drop) ───────────────────────────────────────────
	bool draggable = false;               ///< source de glisser-déposer
	std::string dragPayload;              ///< donnée transportée (Draggable)
	bool dropTarget = false;              ///< cible de dépôt
	std::function<void(std::string)> onDrop; ///< appelé au dépôt (DropTarget)

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
	float value = 0.f;                  ///< valeur (slider, barre de progression)
	float valueMin = 0.f, valueMax = 1.f;
	bool checked = false;               ///< case à cocher / interrupteur
	float scrollX = 0.f, scrollY = 0.f; ///< décalage d'un conteneur défilant
	float scrollContentH = 0.f;         ///< hauteur du contenu (scroll vertical)
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
	/// Icône d'un ToolButton / ImageView (point de code de la police d'icônes).
	WidgetBuilder &Icon(char32_t code);
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

// ── Texte & affichage ────────────────────────────────────────────────────────
/// Filet de séparation horizontal (ou vertical selon `.Vertical()`).
[[nodiscard]] WidgetBuilder Separator();
/// Image / icône centrée (point de code de la police d'icônes), libellé optionnel.
[[nodiscard]] WidgetBuilder ImageView(char32_t icon = 0);
/// Texte enrichi (Markdown léger : `**gras**`, `*italique*`, `_souligné_`,
/// `` `code` ``, `[lien](url)`) — les liens sont cliquables.
[[nodiscard]] WidgetBuilder RichTextLabel(std::string_view markdown);

// ── Boutons ──────────────────────────────────────────────────────────────────
/// Bouton basculant (état coché) — lier avec `.Bind(bool)`.
[[nodiscard]] WidgetBuilder ToggleButton(std::string_view label);
/// Bouton radio — les radios d'un même `.Group(...)` s'excluent mutuellement.
[[nodiscard]] WidgetBuilder RadioButton(std::string_view label);
/// Bouton d'icône seule (avec tooltip).
[[nodiscard]] WidgetBuilder ToolButton(char32_t icon);

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
