#pragma once
/**
 * @file ui.hpp
 * @brief Interface en mode immédiat sur `sdl3::Canvas`.
 *
 * - Aucun arbre de widgets persistant : chaque image décrit l'interface ; l'état
 *   (survol, élément actif, focus clavier, défilement) est indexé par des
 *   identifiants hachés (pile d'ID + libellé), stockés dans une `core::RobinMap`.
 * - Mise en page par colonnes : `BeginColumn(rect)` puis chaque widget prend la
 *   ligne suivante (`Row(poids…)` découpe une ligne en cellules).
 * - Widgets de paramètres (`Setting*`) : libellé, description, contrôle et bouton
 *   « valeur par défaut » (icône Material « undo », actif seulement si la valeur
 *   diffère du défaut).
 * - Défilement : toutes les barres passent par `ScrollbarV` / `ScrollbarH` (flèches aux
 *   extrémités, piste cliquable, curseur déplaçable) ; `BeginScroll` réserve leur place
 *   dans la mise en page (les widgets ne passent jamais dessous) et gère la molette
 *   verticale, la molette horizontale / pavé tactile (X et Y) et le glissé tactile.
 * - Icônes : police Material Icons (`sdl3::CanvasFont::Icons`), icônes typées dans
 *   `ui::icons` (cf. `ui/icons.hpp`).
 */
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/robin_map.hpp>
#include <sdl3/render/canvas.hpp>

#include "code_editor.hpp"
#include "text_edit.hpp"
#include <sdl3/core/events_manager.hpp>

#include "icons.hpp"

namespace ui {

using sdl3::Color;
using Rect = sdl3::FRect;

/// Icônes typées (CupertinoIcons / MaterialIcons) et résolution icône → codepoint /
/// police : voir `ui/icons.hpp`. `RegisterAll` enregistre auprès du canvas les icônes
/// Material utilisées par apps-lib (avant la première image).
namespace icons {
void RegisterAll();
} // namespace icons

struct Theme {
	Color background = sdl3::Rgba(13, 15, 20);
	Color panel = sdl3::Rgba(22, 25, 32, 238);
	Color panelBorder = sdl3::Rgba(54, 60, 74);
	Color title = sdl3::Rgba(30, 34, 43);
	Color text = sdl3::Rgba(228, 232, 240);
	Color textDim = sdl3::Rgba(146, 154, 170);
	Color accent = sdl3::Rgba(76, 196, 84);	
	Color accentHover = sdl3::Rgba(104, 216, 110);
	Color accentText = sdl3::Rgba(12, 22, 12);
	Color widget = sdl3::Rgba(36, 41, 52);
	Color widgetHover = sdl3::Rgba(46, 52, 66);
	Color widgetActive = sdl3::Rgba(58, 66, 84);
	Color row = sdl3::Rgba(30, 34, 43, 160);
	Color danger = sdl3::Rgba(222, 76, 82);
	Color success = sdl3::Rgba(96, 200, 120);
	Color warning = sdl3::Rgba(236, 180, 72);
	Color info = sdl3::Rgba(86, 156, 255);
	float fontSize = 15.f;
	float rowHeight = 30.f;
	float spacing = 6.f;
	float padding = 12.f;
	float radius = 6.f;
};

using Id = uint64_t;

/// Comportement d'un texte plus large que sa zone.
enum class TextOverflow : uint8_t {
	Clip,			// coupé net
	Ellipsis,		// « … » final (infobulle avec le texte complet au survol)
	Marquee,		// défilement automatique aller-retour pour lire la totalité
	MarqueeOnHover, // défilement seulement au survol, « … » sinon
};

/// Style d'un texte dans une zone bornée (valeurs nulles = thème).
struct TextStyle {
	Color color{0, 0, 0, 0};
	float size = 0;
	sdl3::CanvasFont font = sdl3::CanvasFont::Regular;
	TextOverflow overflow = TextOverflow::Ellipsis;
};

/// Bouton d'icône à droite d'un onglet (par défaut : fermer).
struct TabButton {
	char32_t icon = icons::Codepoint(icons::MaterialIcons::CLOSE);
	std::string tooltip = "Fermer";
};

/// Onglet de `Context::TabBar`.
struct TabItem {
	std::string id;	   // identifiant stable (défilement, glisser-déposer)
	std::string label; // texte de l'onglet
	char32_t icon = 0; // icône à gauche (0 : aucune)
	Color iconColor{0, 0, 0, 0};
	TextStyle labelStyle{{0, 0, 0, 0}, 0, sdl3::CanvasFont::Regular, TextOverflow::MarqueeOnHover}; // style et débordement du libellé
	bool modified = false;		 // non enregistré : icône « point » (si `TabBarOptions::modifiedDot`)
	std::vector<char32_t> statusIcons; // icônes d'état supplémentaires (à droite, avant les boutons)
	std::vector<TabButton> buttons = {TabButton{}}; // boutons d'icônes (défaut : fermer)
	std::string tooltip;		 // infobulle de l'onglet (défaut : libellé)
};

/// Apparence et comportement d'une barre de défilement.
struct ScrollbarOptions {
	float thickness = 12;  // épaisseur ; les boutons fléchés sont des carrés de ce côté
	bool arrows = true;	   // boutons fléchés aux extrémités (défilement pas à pas, répété si maintenu)
	float step = 40;	   // pas d'un clic de flèche (pixels de contenu)
	float minThumb = 22;   // longueur minimale du curseur
	bool alwaysVisible = false; // zone de défilement : barre affichée même sans débordement
};

enum class ScrollAxes : uint8_t { Vertical = 1, Horizontal = 2, Both = 3 };

/// Zone de défilement (`BeginScroll`).
/// Entrée d'un menu de `Context::MenuBar` (interface immédiate) ; libellé vide : séparateur.
struct MenuEntry {
	std::string_view label;
	std::string_view shortcut = {}; ///< affiché à droite (« Ctrl+Z »)
	bool enabled = true;
	bool checked = false; ///< coche devant le libellé
};

struct BarMenu {
	std::string_view title;
	std::span<const MenuEntry> items;
};

/// Entrée choisie dans une barre de menus (`menu` = -1 : aucune).
struct MenuChoice {
	int menu = -1, item = -1;
	[[nodiscard]] bool Is(int m, int i) const noexcept { return menu == m && item == i; }
};

struct ScrollOptions {
	ScrollAxes axes = ScrollAxes::Both;
	ScrollbarOptions bar{};
	float wheelStep = 48; // pixels par cran de molette
};

struct TabBarOptions {
	float tabHeight = 32;
	float minTabWidth = 80;
	float maxTabWidth = 220;
	ScrollbarOptions scrollbar{10, true, 60}; // barre horizontale sous les onglets (si débordement)
	bool modifiedDot = true;   // icône « point » pour un onglet modifié
	bool reorderable = true;   // glisser-déposer pour réordonner
};

struct TabBarResult {
	int activated = -1;			// onglet cliqué
	int buttonTab = -1;			// onglet dont un bouton a été cliqué…
	int button = -1;			// …et l'index de ce bouton (0 = fermer par défaut)
	int moveFrom = -1, moveTo = -1; // réordonnancement : déplacer `moveFrom` à l'index `moveTo`
	[[nodiscard]] bool Moved() const noexcept { return moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo; }
};

/// Déplace l'élément `from` à l'index `to` (application d'un `TabBarResult` à un vecteur parallèle).
template <typename T> void MoveItem(std::vector<T> &v, int from, int to) {
	if (from < 0 || to < 0 || from == to || size_t(from) >= v.size() || size_t(to) >= v.size())
		return;
	auto f = v.begin() + from, t = v.begin() + to;
	if (from < to)
		std::rotate(f, f + 1, t + 1);
	else
		std::rotate(t, f, f + 1);
}

/// Nœud d'une arborescence (`Context::Tree`) — construit par le producteur (fabrique),
/// affiché et mis à jour par l'interface.
struct TreeNode {
	std::string id;		// identifiant stable (chemin complet)
	std::string label;	// texte affiché (nom)
	char32_t icon = 0;	// icône (0 : dossier/fichier selon `isDir`)
	Color iconColor{0, 0, 0, 0};
	bool isDir = false;
	std::vector<TreeNode> children;
};

/// Interactions d'une image de `Context::Tree`.
struct TreeResult {
	std::string selected;		 // nœud sélectionné (clic droit → focus)
	std::string doubleClicked;	 // double-clic (répertoire : plier/déplier ; fichier : ouvrir)
	bool doubleIsDir = false;
	std::string menuAction;		 // « open », « rename », « duplicate », « delete »
	std::string menuTarget;		 // nœud visé par l'action du menu
	std::string renameTarget;	 // nœud à renommer (F2 sur le nœud sélectionné)
};

struct TreeOptions {
	float rowHeight = 22.f;
	float indent = 14.f; // décalage horizontal par niveau
};

/// Comportement d'un champ de saisie (`TextInput`).
struct TextInputOptions {
	std::vector<std::string> *history = nullptr; // navigation ↑/↓ (console, historique des commandes)
	int *historyIndex = nullptr;				 // position dans l'historique (persistée par l'appelant)
	bool multiline = false;						 // Tab insère une indentation ; Entrée ne soumet pas
	std::string indent = "\t";					 // indentation insérée par Tab
	bool selectOnFocus = false;					 // tout sélectionner au focus (console)
	bool tabAdvance = false;					 // Tab quitte le champ au lieu d'indenter (formulaires)
};

/// Comportement de `Context::Choice` à valeur texte.
struct ChoiceOptions {
	bool editable = true;	///< saisie au clavier d'une valeur libre
	bool filter = true;		///< liste restreinte aux suggestions contenant le texte tapé
	bool arrows = true;		///< flèches ‹ › : suggestion précédente / suivante
	std::string placeholder; ///< texte grisé d'un champ vide
};

/// Date du calendrier (`Context::DatePicker`) : année, mois 1..12, jour 1..31.
struct Date {
	int year = 2026, month = 1, day = 1;
	[[nodiscard]] constexpr bool operator==(const Date &) const noexcept = default;
};

class Context {
public:
	explicit Context(sdl3::Canvas &canvas, sdl3::EventsManager &events) : m_canvas(&canvas), m_events(&events) {}

	/// Construit l'état d'entrée depuis `sdl3::EventsManager` (déjà alimenté par
	/// `Poll()`), en coordonnées logiques (échelle lue sur le canvas), puis
	/// démarre l'image. C'est l'entrée recommandée : l'application n'itère plus
	/// elle-même la file d'évènements.
	void Begin(float dt = -1.f);
	void End();

	[[nodiscard]] sdl3::Canvas &Canvas() noexcept { return *m_canvas; }
	[[nodiscard]] sdl3::EventsManager &Events() const noexcept { return *m_events; }
	/// Échelle fenêtre → logique (coordonnées de la souris).
	[[nodiscard]] float Scale() const noexcept { return m_canvas->Scale() > 0.f ? m_canvas->Scale() : 1.f; }
	/// Position de la souris en coordonnées logiques.
	[[nodiscard]] float MouseX() const noexcept { return m_events->MouseX() / Scale(); }
	[[nodiscard]] float MouseY() const noexcept { return m_events->MouseY() / Scale(); }
	/// État d'un bouton de souris (défaut : bouton gauche).
	[[nodiscard]] bool MouseDown(sdl3::MouseButton button = sdl3::MouseButton::Left) const noexcept {
		return !m_touchScrolling && (*m_events)[button].IsPressed();
	}
	[[nodiscard]] bool MousePressed(sdl3::MouseButton button = sdl3::MouseButton::Left) const noexcept {
		return !m_touchScrolling && (*m_events)[button].IsJustPressed();
	}
	[[nodiscard]] bool MouseReleased(sdl3::MouseButton button = sdl3::MouseButton::Left) const noexcept {
		return !m_touchScrolling && (*m_events)[button].IsJustReleased();
	}
	/// Touche pressée cette image (répétition comprise).
	[[nodiscard]] bool KeyPressed(sdl3::keyboard::Key k) const noexcept;
	[[nodiscard]] bool KeycodePressed(SDL_Keycode kc) const noexcept { return m_events->KeycodePressed(kc); }
	[[nodiscard]] std::string_view TextInput() const noexcept { return m_events->TextInput(); }
	/// Souris au-dessus de `r`, interface non bloquée et aucune liste déroulante dessus
	/// (vues personnalisées : graphiques, scènes 3D).
	[[nodiscard]] bool MouseOver(const Rect &r) const noexcept { return !m_blocked && !m_popupHover && r.Contains(MouseX(), MouseY()); }
	[[nodiscard]] float WheelY() const noexcept { return m_wheelY; }
	[[nodiscard]] float WheelX() const noexcept { return m_wheelX; }
	// ── Gestes multi-doigts (pavé tactile / écran) ─────────────────────────
	/// Autorise le zoom par pincement (lu via `GesturePinch()`).
	void SetGestureZoom(bool on) noexcept { m_gestureZoom = on; }
	[[nodiscard]] bool GestureZoom() const noexcept { return m_gestureZoom; }
	/// Autorise la rotation par geste (lue via `GestureRotate()`).
	void SetGestureRotate(bool on) noexcept { m_gestureRotate = on; }
	[[nodiscard]] bool GestureRotateEnabled() const noexcept { return m_gestureRotate; }
	/// Deux doigts posés (geste en cours).
	[[nodiscard]] bool GestureActive() const noexcept { return m_gesture.active; }
	/// Facteur de zoom du pincement (1 = aucun) — 0 si désactivé.
	[[nodiscard]] float GesturePinch() const noexcept {
		return m_gestureZoom && m_gesture.active ? m_gesture.pinch : 1.f;
	}
	/// Rotation du geste (radians) — 0 si désactivée.
	[[nodiscard]] float GestureRotate() const noexcept {
		return m_gestureRotate && m_gesture.active ? m_gesture.rotate : 0.f;
	}
	/// Déplacement du centroïde (balayage) : X et Y normalisés 0..1.
	[[nodiscard]] float GestureSwipeX() const noexcept { return m_gesture.active ? m_gesture.dx : 0.f; }
	[[nodiscard]] float GestureSwipeY() const noexcept { return m_gesture.active ? m_gesture.dy : 0.f; }
	/// Centroïde du geste en coordonnées fenêtre (logiques).
	[[nodiscard]] float GestureCenterX() const noexcept { return m_gesture.center.x * m_canvas->Width(); }
	[[nodiscard]] float GestureCenterY() const noexcept { return m_gesture.center.y * m_canvas->Height(); }
	/// Curseur système à afficher (appelé à chaque image, appliqué si différent).
	void SetCursor(sdl3::SystemCursor cursor) noexcept;
	[[nodiscard]] bool Shift() const noexcept { return m_events->Shift(); }
	[[nodiscard]] bool Ctrl() const noexcept { return m_events->Ctrl(); }
	[[nodiscard]] bool Alt() const noexcept { return m_events->Alt(); }
	[[nodiscard]] Theme &Style() noexcept { return m_theme; }
	[[nodiscard]] bool WantsMouse() const noexcept { return m_wantsMouse; }
	/// Un champ a le focus, ou un texte statique est sélectionné (Ctrl+C, Ctrl+A lui reviennent).
	[[nodiscard]] bool WantsKeyboard() const noexcept { return m_focus != 0 || HasTextSelection(); }
	/// Un texte statique (libellé, paragraphe) est sélectionné.
	[[nodiscard]] bool HasTextSelection() const noexcept { return m_staticSel.id != 0 && m_staticSel.edit.HasSelection(); }
	void ClearFocus() noexcept { m_focus = 0; }
	/// Bloque toute interaction (survol, clic, saisie) — utilisé par un modal pour
	/// désactiver l'interface en arrière-plan ; le dessin reste intact.
	void SetBlocked(bool on) noexcept { m_blocked = on; }
	[[nodiscard]] bool Blocked() const noexcept { return m_blocked; }
	/// Le champ `id` (même chaîne que pour TextInput/TextInputAt) a le focus clavier.
	[[nodiscard]] bool HasFocus(std::string_view id) const { return m_focus != 0 && m_focus == MakeId(id); }
	void SetFocus(std::string_view id) { m_focus = MakeId(id); }
	/// Clic d'interface de l'image (pour un son de clic, par exemple).
	[[nodiscard]] bool Clicked() const noexcept { return m_clickedThisFrame; }
	/// Durée de l'image en cours (secondes) — pour les animations des widgets.
	[[nodiscard]] float Dt() const noexcept { return m_dt; }

	// ── Identifiants ─────────────────────────────────────────────────────
	void PushId(std::string_view s);
	void PushId(uint64_t n);
	void PopId();
	[[nodiscard]] Id MakeId(std::string_view label) const;

	// ── Mise en page ─────────────────────────────────────────────────────
	void BeginColumn(const Rect &area);
	void EndColumn();
	Rect Next(float h = 0);
	void Row(std::initializer_list<float> weights, float h = 0);
	void Row(std::span<const float> weights, float h = 0);
	void Space(float h);
	/// Curseur Y de la colonne courante (début de la prochaine ligne) — permet aux
	/// conteneurs imbriqués de mesurer la hauteur consommée.
	[[nodiscard]] float ColumnY() const;
	[[nodiscard]] Rect Remaining() const;

	// ── Conteneurs ───────────────────────────────────────────────────────
	void BeginPanel(const Rect &r, std::string_view title = {});
	void EndPanel();
	/// Zone de défilement : les barres (si le contenu déborde) sont retranchées de la zone
	/// avant la mise en page ; la largeur du contenu suit les widgets plus larges que la
	/// colonne (`ExtendContent`), d'où une barre horizontale au besoin.
	void BeginScroll(std::string_view id, const Rect &r, const ScrollOptions &options = {});
	void EndScroll();
	/// Signale du contenu jusqu'à l'abscisse `right` dans la colonne courante (barre
	/// horizontale de la zone de défilement englobante si elle dépasse).
	void ExtendContent(float right);
	/// Zone visible de la zone de défilement courante (sans les barres).
	[[nodiscard]] Rect ScrollViewport() const;

	// ── Barres de défilement ─────────────────────────────────────────────
	/// Barre verticale dans `bar` (flèches comprises) pour un contenu de hauteur `content`
	/// vu dans `viewport` pixels. Flèches : un pas par clic (répété si maintenu) ; clic sur la
	/// piste : le curseur saute sous la souris et suit le glissé. Vrai si `offset` a changé.
	bool ScrollbarV(std::string_view id, const Rect &bar, float &offset, float content, float viewport, const ScrollbarOptions &options = {});
	bool ScrollbarH(std::string_view id, const Rect &bar, float &offset, float content, float viewport, const ScrollbarOptions &options = {});
	/// Molette (Y), molette horizontale / pavé tactile (X, ou Maj + molette) et glissé tactile
	/// au-dessus de `area` ; consomme l'entrée utilisée (les zones imbriquées passent d'abord).
	/// Une zone sans débordement vertical reçoit la molette verticale en horizontal.
	bool ScrollInput(const Rect &area, float &offsetX, float &offsetY, float maxX, float maxY, float wheelStep = 48);

	// ── Chargement ───────────────────────────────────────────────────────
	/// Indicateur de chargement animé (arc qui tourne sur lui-même) centré dans `r`.
	void Spinner(const Rect &r, float size = 32, Color c = {0, 0, 0, 0});
	/// Ligne de mise en page : indicateur centré et libellé en dessous.
	void Loader(std::string_view label = "Chargement…", float h = 110);
	/// Fond de carte arrondi (sans colonne).
	void Card(const Rect &r, bool hovered = false, bool selected = false);
	/// En-tête de page : flèche retour, titre, sous-titre. Vrai si retour cliqué.
	bool PageHeader(const Rect &r, std::string_view title, std::string_view subtitle);

	// ── Widgets de base ──────────────────────────────────────────────────
	void Label(std::string_view text, Color c = {0, 0, 0, 0}, float size = 0);
	void Heading(std::string_view text);
	void Wrapped(std::string_view text, Color c = {0, 0, 0, 0}, float size = 0);
	void Separator();
	/// Titre de section : icône + texte vert en capitales + filet.
	void Section(char32_t icon, std::string_view title);
	void Icon(const Rect &r, char32_t icon, Color c = {0, 0, 0, 0}, float size = 20.f);
	bool IconButton(const Rect &r, char32_t icon, std::string_view tooltip, bool enabled = true, Color tint = {0, 0, 0, 0});
	bool Button(std::string_view label, bool enabled = true);
	bool ButtonAt(const Rect &r, std::string_view label, bool enabled = true, bool selected = false);
	bool PrimaryButton(const Rect &r, std::string_view label, bool enabled = true);
	bool DangerButton(const Rect &r, std::string_view label, bool enabled = true);
	/// Grand bouton de menu : icône, titre, sous-titre (style « Jouer » si `primary`).
	bool MenuButton(std::string_view id, char32_t icon, std::string_view title, std::string_view subtitle, bool primary = false);
	bool Checkbox(std::string_view label, bool &value);
	bool Toggle(const Rect &r, std::string_view id, bool &value);
	bool SliderFloat(std::string_view label, float &value, float min, float max, std::string_view format = "{:.2f}");
	bool SliderInt(std::string_view label, int &value, int min, int max);
	bool SliderAt(const Rect &r, std::string_view id, float &value, float min, float max);
	bool TextInput(std::string_view id, std::string &value, bool *submitted = nullptr, std::string_view placeholder = {});
	bool TextInputAt(const Rect &r, std::string_view id, std::string &value, bool *submitted = nullptr, std::string_view placeholder = {});
	/// Champ multi-ligne : Entrée insère une ligne, ↑ ↓ Page préc. / suiv. déplacent, molette fait défiler.
	bool TextArea(const Rect &r, std::string_view id, std::string &value, std::string_view placeholder = {});
	/// Éditeur de code : coloration (`options.syntax`), numéros de ligne, ligne d'erreur,
	/// indentation automatique, Tab / Maj+Tab sur plusieurs lignes ; molette (Maj : horizontale).
	bool CodeEditor(const Rect &r, std::string_view id, std::string &text, const CodeEditorOptions &options = {});
	/// Couleurs de l'éditeur de code tirées du thème.
	[[nodiscard]] CodePalette CodeColors() const noexcept;
	/// Édition en cours d'un champ (`id` de TextInput/TextArea/CodeEditor) : curseur et sélection.
	[[nodiscard]] TextEdit *EditState(std::string_view id);
	/// Champ numérique : glisser horizontalement change la valeur (`speed` par pixel,
	/// Maj : ×0,1) ; un clic sans glisser passe en saisie clavier (Entrée ou clic ailleurs
	/// valide, Échap annule). `accent` : repère coloré dans le coin (axes X/Y/Z).
	bool DragNumber(const Rect &r, std::string_view id, float &value, float speed = 0.05f, std::string_view format = "{:.2f}",
					Color accent = {0, 0, 0, 0}, float min = -3.0e38f, float max = 3.0e38f);
	/// Libellé puis 1 à 4 champs côte à côte (repères X/Y/Z/W rouge, vert, bleu, gris).
	bool DragFloats(std::string_view label, std::span<float> values, float speed = 0.05f, std::string_view format = "{:.2f}",
					float min = -3.0e38f, float max = 3.0e38f);
	bool Selectable(std::string_view label, bool selected, Color c = {0, 0, 0, 0});
	int Tabs(std::string_view id, std::span<const std::string_view> labels, int selected);
	/// Valeur parmi `options` : flèches ‹ › pour la précédente / suivante, clic sur la valeur pour
	/// une liste déroulante (au premier plan, ouverte jusqu'au choix, à un clic dehors ou à Échap).
	bool Choice(const Rect &r, std::string_view id, int &index, std::span<const std::string_view> options);
	/// Valeur texte parmi des suggestions : comme `Choice`, mais avec `options.editable` la valeur
	/// se saisit aussi au clavier, qu'elle figure dans la liste ou non (taille de police « 10.5 »,
	/// famille de police tapée…). La liste (bouton ▾ ou saisie) est filtrée par le texte tapé.
	/// Vrai quand une valeur est validée : Entrée, choix dans la liste, flèches, ou sortie du champ
	/// après modification. La validation du contenu revient à l'appelant.
	bool Choice(const Rect &r, std::string_view id, std::string &value, std::span<const std::string_view> options,
				const ChoiceOptions &choiceOptions = {});
	/// Barre de menus : titres dans `r`, menu déroulant au premier plan (un clic l'ouvre, le
	/// survol d'un titre voisin bascule, Échap ou un clic ailleurs ferme). Rend l'entrée choisie.
	MenuChoice MenuBar(const Rect &r, std::span<const BarMenu> menus);
	/// Un menu de la barre est ouvert (raccourcis clavier de l'application à suspendre).
	[[nodiscard]] bool MenuOpen() const noexcept { return m_menuPopup.menu >= 0; }
	void ProgressBar(float fraction, std::string_view overlay = {});
	void Plot(std::string_view label, std::span<const float> values, float min = 0, float max = 0, float h = 70, Color c = {0, 0, 0, 0});
	void Badge(const Rect &r, std::string_view text, Color c);
	/// Sélecteur de couleur : carré saturation × valeur, barre de teinte, aperçu et code
	/// hexadécimal (l'alpha est conservé). Vrai si la couleur a changé. Libellés traduits
	/// par le traducteur global (`ui.color.r/g/b/h/s/v`).
	bool ColorPicker(std::string_view id, Color &color, float height = 196.f);
	/// Oscilloscope : grille, axe zéro, tracé de `samples` (−gain…+gain sur la hauteur),
	/// réduit à une enveloppe min / max par pixel quand il y a plus d'échantillons que de pixels.
	void Scope(const Rect &r, std::span<const float> samples, Color color = {0, 0, 0, 0}, float gain = 1.f);
	/// Spectre : `db[i]` à la fréquence `freqs[i]`, axe des fréquences logarithmique gradué
	/// (`minHz`…`maxHz`), grille en dB (`minDb`…`maxDb`), aire remplie sous la courbe.
	void SpectrumView(const Rect &r, std::span<const float> freqs, std::span<const float> db, float minHz = 20.f,
					  float maxHz = 20000.f, float minDb = -90.f, float maxDb = 0.f, Color color = {0, 0, 0, 0});
	/// Vumètre : niveau RMS (barre) et crête (repère), échelle −60…0 dB vert / jaune / rouge.
	void LevelMeter(const Rect &r, float rms, float peak, std::string_view label = {});
	/// Calendrier mensuel : mois précédent / suivant, jour choisi au clic, aujourd'hui
	/// entouré, semaines commençant le lundi. Vrai si la date a changé. Textes traduits par
	/// le traducteur global (`ui.month.1`…`12`, `ui.weekday.0`…`6`, `ui.calendar.previous/next`).
	bool DatePicker(std::string_view id, Date &date);
	void Tooltip(std::string_view text);
	/// Texte dans `r` (centré verticalement) avec gestion du débordement (`Marquee` :
	/// défilement automatique aller-retour, pauses aux extrémités).
	void TextIn(const Rect &r, std::string_view text, const TextStyle &style = {});
	/// Barre d'onglets : icône, libellé stylé, icônes d'état (« point » si modifié),
	/// boutons d'icônes (« fermer » par défaut), défilement horizontal (molette ou barre
	/// sous les onglets), glisser-déposer pour réordonner. `area.h` : `TabBarHeight(options)`.
	TabBarResult TabBar(std::string_view id, const Rect &area, std::span<const TabItem> tabs, int active, const TabBarOptions &options = {});
	[[nodiscard]] static float TabBarHeight(const TabBarOptions &options = {}) noexcept { return options.tabHeight + options.scrollbar.thickness + 3; }
	/// Arborescence de fichiers : rend `roots` récursivement. `expanded` (nœuds dépliés)
	/// et `selected` (nœud en focus) sont mis à jour par l'interface. Interactions :
	/// clic droit = sélection, double-clic = déplier/plier (répertoire) ou ouvrir (fichier),
	/// clic gauche = menu contextuel (ouvrir / renommer / dupliquer / supprimer), F2 = renommer.
	/// Les nœuds doivent rester dans `BeginScroll`/`EndScroll` d'un défilement englobant.
	TreeResult Tree(std::string_view id, const std::vector<TreeNode> &roots, std::set<std::string> &expanded, std::string &selected,
					const TreeOptions &options = {});
	/// Double-clic : même identifiant cliqué deux fois en moins de 0,4 s.
	[[nodiscard]] bool DoubleClicked(Id id) const;
	/// Débordement par défaut des libellés et descriptions des lignes de paramètres.
	TextOverflow settingOverflow = TextOverflow::Marquee;

	// ── Paramètres (libellé, description, contrôle, valeur par défaut) ────
	bool SettingFloat(std::string_view label, std::string_view description, float &value, float min, float max, float defaultValue,
					  std::string_view format = "{:.2f}");
	bool SettingInt(std::string_view label, std::string_view description, int &value, int min, int max, int defaultValue,
					std::string_view unit = {});
	bool SettingToggle(std::string_view label, std::string_view description, bool &value, bool defaultValue);
	bool SettingChoice(std::string_view label, std::string_view description, int &index, std::span<const std::string_view> options,
					   int defaultValue);
	bool SettingText(std::string_view label, std::string_view description, std::string &value, std::string_view defaultValue,
					 std::string_view placeholder = {});

	// ── Notifications ────────────────────────────────────────────────────
	void Notify(std::string text, Color c = {0, 0, 0, 0}, float seconds = 3.f);
	void DrawNotifications(float dt, float topOffset = 12.f);

	struct Interaction {
		bool hovered = false, held = false, leftClicked = false, rightClicked = false, doubleClicked = false;
	};
	Interaction Interact(Id id, const Rect &r);
	[[nodiscard]] bool MouseIn(const Rect &r) const noexcept;

private:
	struct Column {
		Rect area;
		float cursorY;
		std::vector<Rect> rowCells;
		size_t rowIndex = 0;
		float maxRight = 0; // abscisse maximale du contenu (barre horizontale)
	};
	struct ScrollState {
		float offsetX = 0, offsetY = 0;
		float contentH = 0;	 // hauteur du contenu (image précédente)
		float overflowW = 0; // largeur du contenu au-delà de la colonne (image précédente)
	};
	struct ScrollFrame {
		Id id;
		Rect outer, view;
		bool vertical, horizontal;
		ScrollOptions options;
	};
	struct ScrollbarState {
		float grab = 0;	  // position de la souris dans le curseur au début du glissé
		float repeat = 0; // délai avant la prochaine répétition d'une flèche
	};
	bool Scrollbar(Id id, const Rect &bar, bool vertical, float &offset, float content, float viewport, const ScrollbarOptions &options);
	struct Toast {
		std::string text;
		Color color;
		float remaining;
	};
	/// Ligne de paramètre : dessine le fond, le libellé, la description ; retourne la zone du contrôle.
	Rect SettingRow(std::string_view label, std::string_view description, bool modified, bool &resetClicked, std::string_view defaultText);

	sdl3::Canvas *m_canvas;
	sdl3::EventsManager *m_events;
	float m_wheelX = 0.f, m_wheelY = 0.f; ///< molette de l'image (consommée par les widgets)
	float m_panX = 0.f, m_panY = 0.f;     ///< glissé tactile de l'image (consommé)
	sdl3::Gesture m_gesture{};            ///< geste multi-doigts de l'image (copie)
	bool m_gestureZoom = false;           ///< pincement → zoom autorisé
	bool m_gestureRotate = false;         ///< rotation autorisée
	int m_lastCursor = -1;                ///< dernier curseur système posé
	bool m_touchDown = false;
	uint64_t m_touchFinger = 0;           ///< doigt suivi (le premier posé)
	float m_touchTravel = 0.f;
	Theme m_theme;
	std::vector<uint64_t> m_idStack;
	std::vector<Column> m_columns;
	std::vector<ScrollFrame> m_scrollStack;
	core::RobinMap<Id, ScrollState> m_scroll;
	core::RobinMap<Id, ScrollbarState> m_scrollbars;
	bool m_touchScrolling = false; // glissé tactile en cours : les widgets ignorent le doigt
	// Champ numérique (DragNumber) : glissé en cours ou saisie clavier.
	Id m_numberEdit = 0;
	std::string m_numberText;
	float m_dragStartX = 0, m_dragStartValue = 0;
	bool m_dragMoved = false;
	core::RobinMap<Id, TextEdit> m_textEdits; ///< curseur, sélection, défilement de chaque champ
	Id m_textClickId = 0;					  ///< double-clic dans un champ (sélection d'un mot)
	double m_textClickTime = -1.0, m_caretTime = 0.0;
	bool EditText(const Rect &r, Id id, std::string &value, bool multiline, bool *submitted, std::string_view placeholder);
	/// Texte statique sélectionnable : une ligne = (début, longueur, position).
	struct TextLine {
		size_t start = 0, length = 0;
		float x = 0, y = 0;
	};
	void SelectableText(std::string_view text, std::span<const TextLine> lines, float size, sdl3::CanvasFont font, Color color);
	struct StaticSelection {
		Id id = 0;
		TextEdit edit;
		std::string text;
		bool dragging = false;
	} m_staticSel;
	bool m_staticPressed = false;			  ///< appui de l'image reçu par un texte statique
	core::RobinMap<Id, int> m_staticSerial;	  ///< textes identiques dans l'image : numéros distincts
	struct TabBarState {
		float scroll = 0;
		std::string lastActive;
		int pressIndex = -1;
		float pressX = 0, grabOffset = 0;
		bool dragging = false;
	};
	core::RobinMap<Id, TabBarState> m_tabBars;
	struct TreeMenuState {
		std::string target;
		Rect anchor;
	};
	core::RobinMap<Id, TreeMenuState> m_treeMenus; // menu contextuel ouvert d'une arborescence
	/// Liste déroulante ouverte d'un Choice (dessinée au premier plan dans End()).
	struct ChoicePopup {
		Id id = 0;
		Rect box;							///< liste, sous le Choice
		std::vector<std::string> options;
		int current = 0, picked = -1, first = 0; ///< valeur affichée, choix fait, première ligne visible
		uint64_t openedFrame = 0;
	} m_choicePopup;
	void OpenChoiceList(Id self, const Rect &r, std::span<const std::string_view> options, std::string_view value);
	/// Saisies en cours des `Choice` éditables (identifiant → texte tapé, non encore validé).
	std::map<Id, std::string> m_choiceEdits;
	/// Menu déroulant ouvert d'une barre de menus (dessiné au premier plan dans End()).
	struct MenuPopup {
		Id bar = 0;
		int menu = -1;
		Rect box, barRect;
		struct Entry {
			std::string label, shortcut;
			bool enabled = true, checked = false;
		};
		std::vector<Entry> entries;
		MenuChoice picked;
		uint64_t openedFrame = 0;
	} m_menuPopup;
	void DrawMenuPopup();
	bool m_popupHover = false; ///< souris sur la liste ouverte : les widgets dessous l'ignorent
	uint64_t m_frame = 0;
	void DrawChoicePopup();
	core::RobinMap<Id, float> m_pickerHue;		   // teinte d'un ColorPicker (gardée quand la saturation est nulle)
	core::RobinMap<Id, int> m_calendarMonth;	   // mois affiché d'un DatePicker (année × 12 + mois − 1)
	Id m_lastClickId = 0;						   // détection du double-clic
	double m_lastClickTime = 0;
	double m_time = 0; // secondes (défilement des textes)
	float m_dt = 0;
	core::RobinMap<uint64_t, double> m_hoverStart; // début du survol des textes « MarqueeOnHover »
	Id m_hot = 0, m_active = 0, m_focus = 0;
	// Widgets superposés (bouton sur une ligne cliquable) : le dernier soumis sous la souris est
	// au-dessus (ordre de dessin). Celui de l'image précédente garde le survol tant que la souris
	// reste dans son rectangle ; à l'appui, un widget soumis après reprend l'activation.
	Id m_top = 0, m_topNext = 0;
	Rect m_topRect{}, m_topNextRect{};
	bool m_topSeen = false;		///< `m_top` déjà soumis à cette image : les suivants sont au-dessus de lui
	uint64_t m_activeFrame = 0; ///< image où `m_active` a été pris
	bool m_wantsMouse = false;
	bool m_blocked = false;
	bool m_clickedThisFrame = false;
	std::string m_tooltip;
	std::vector<Toast> m_toasts;
};

} // namespace ui
