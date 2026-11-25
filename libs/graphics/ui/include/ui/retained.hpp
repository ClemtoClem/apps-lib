#pragma once
/**
 * @file retained.hpp
 * @brief Hôte d'une interface retenue : montage de l'arbre, feuilles CSS,
 *        affichage automatique (style → mise en page → rendu → hit-test).
 *
 * L'application construit son arbre avec `ui::WidgetBuilder` (builder.hpp),
 * le monte une fois via `Root(...)`, charge ses feuilles avec `Sheet(...)`,
 * puis appelle simplement `Present(input, dt)` chaque image : le style est
 * résolu (CSS + inline), la mise en page flex/grid est calculée, et chaque
 * widget est dessiné automatiquement — y compris les widgets au contenu
 * personnalisé (`ui::Custom`), dont le callback reçoit la boîte de contenu du
 * parent déjà bornée par le clip rect.
 */
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <sdl3/render/canvas.hpp>

#include "builder.hpp"
#include "css.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace sdl3 { class EventsManager; }

namespace ui {

/// Couleurs par défaut du rendu retenu (identiques à celles d'avant les thèmes).
[[nodiscard]] Theme DefaultRetainedTheme();

class Ui {
public:
	explicit Ui(sdl3::Canvas &canvas, sdl3::EventsManager &events) : m_canvas(&canvas), m_events(&events) {}

	/// Ajoute une feuille de style globale (portée : la racine).
	void Sheet(StyleSheetPtr sheet) { m_sheets.push_back(std::move(sheet)); }
	/// Retire toutes les feuilles globales (changement de thème : puis `Sheet(...)`).
	void ClearSheets() noexcept { m_sheets.clear(); }
	/// Couleurs du rendu des widgets que le CSS ne décrit pas (pistes, sélections,
	/// menus, popups, en-têtes de tableau…) : celles du thème de l'interface.
	void SetTheme(const Theme &theme) { m_theme = theme; }
	[[nodiscard]] const Theme &GetTheme() const noexcept { return m_theme; }
	/// Traduit les widgets par leur identifiant (`.Id("clé")`) : texte, `clé.placeholder`,
	/// `clé.tooltip`, `clé.N` (onglets, options, en-têtes de tableau, titres de menus), `clé.N.M` (items de menus),
	/// réappliqués quand le traducteur change (`Translator::Version`). nullptr : textes des sources.
	void SetTranslator(const Translator *translator) noexcept {
		m_translator = translator;
		m_i18nVersion = 0;
	}

	/// Remplace l'arbre de widgets (à appeler une fois, ou quand la structure
	/// change ; les feuilles et l'état sont conservés).
	void Root(WidgetBuilder builder);

	/// Affiche l'interface : résout les styles, calcule la mise en page,
	/// dessine, met à jour le survol et les clics. L'entrée vient de
	/// `sdl3::EventsManager` (déjà alimenté par `Poll()`).
	void Present(float dt);

	/// Comme Present, mais borne la racine à `viewport` (utile pour intégrer un
	/// sous-arbre retenu dans une zone de l'interface existante).
	void Present(float dt, const sdl3::FRect &viewport);

	// ── Interface Begin / Render / End (intégration en trois temps) ──────────
	/// Prépare l'image : mémorise l'entrée (à appeler avant `Render`).
	void Begin(float dt);
	/// Affiche l'interface retenue (à appeler entre `Begin` et `End`).
	void Render();
	/// Clôt l'image.
	void End();

	/// Retrouve un widget par son identifiant (`Id("...")` dans le builder).
	[[nodiscard]] Widget Find(std::string_view id) const;

	/// Taille de police de référence de la racine (unité `rem`).
	void SetRootFontSize(float px) noexcept { m_rootFontSize = px; }
	/// Contexte immédiat utilisé par les feuilles `Custom(std::function<void(Context&)>)`.
	void SetImmediateContext(Context *ctx) noexcept { m_immediate = ctx; }
	[[nodiscard]] float Width() const noexcept { return m_canvas->Width(); }
	[[nodiscard]] float Height() const noexcept { return m_canvas->Height(); }

	/// Vrai si le pointeur est au-dessus d'un widget (et non du fond).
	[[nodiscard]] bool OverWidget() const noexcept { return m_hovered != nullptr; }

	/// Force le curseur système (par ex. `SystemCursor::WAIT` pendant un
	/// chargement) ; il remplace la sélection automatique jusqu'au prochain appel.
	void SetCursor(sdl3::SystemCursor cursor) noexcept { m_cursorOverride = Some(cursor); }
	/// Rétablit la sélection automatique du curseur (survol, redimensionnement…).
	void ClearCursor() noexcept { m_cursorOverride = NONE; }

	/// Désactive toute interaction (survol, clic, défilement) tout en conservant
	/// le dessin — utilisé par un modal pour « geler » l'interface en arrière-plan.
	void SetBlocked(bool on) noexcept { m_blocked = on; }
	[[nodiscard]] bool Blocked() const noexcept { return m_blocked; }

	/// Affiche une notification non-modale (toast) en bas de l'interface, qui
	/// disparaît après `seconds` (3 s par défaut). `color` teinte la barre et la
	/// bordure ; vide → couleur d'accent du thème.
	void Notify(std::string text, Color color = {}, float seconds = 3.f);

private:
	struct Toast {
		std::string text;
		Color color{0, 0, 0, 0}; ///< teinte (vide → accent du thème)
		float remaining = 0.f;    ///< secondes restantes
		float duration = 0.f;     ///< durée totale (fondu)
	};
	void TickToasts(float dt);
	void RenderToasts();
	struct LayoutContext {
		float viewportW = 0.f, viewportH = 0.f;
		float rootFontSize = 14.f;
	};

	void ResolveStyle(Node &node, const Node *parent, const ComputedStyle *parentStyle, bool isRoot);
	/// L'interface retenue bloque-t-elle l'entrée ? (modal visible, menu/ComboBox
	/// ouvert, ou `SetBlocked`) — propagé au contexte immédiat lié.
	[[nodiscard]] bool IsBlocking() const;
	void LayoutNode(Node &node, const sdl3::FRect &content, const LayoutContext &ctx);
	/// Nœud `position: absolute` : taille et place dans `container` (boîte de remplissage du parent).
	void LayoutPositioned(Node &node, const sdl3::FRect &container, const LayoutContext &ctx);
	void RenderNode(Node &node);
	/// Champ de saisie (LineEdit, TextArea) : sélection, texte, curseur, défilement.
	void DrawEditableText(Node &node, const ComputedStyle &c, float size, sdl3::CanvasFont font, bool multiline);
	void HandleWidgetInput(Node &node, Node *hit, Node *pressed);
	void HitTest(Node &node, float x, float y, Node *&hit);
	void ClearTransient(Node &node);
	/// Pose (ou retire, `tr` nul) les traductions sur `node` et sa descendance.
	void ApplyTranslations(Node &node, const Translator *tr);
	/// Affiche la tooltip du widget survolé (au premier plan, à la souris).
	void RenderTooltip(Node &tip, float mouseX, float mouseY);
	/// Affiche le menu déroulant d'un ComboBox ouvert (au premier plan).
	void RenderComboPopup(Node &combo, float mouseX, float mouseY);
	/// Affiche le menu déroulant d'une MenuBar ouverte (au premier plan).
	void RenderMenuPopup(Node &bar, float mouseX, float mouseY);
	/// Boîte du menu déroulant ouvert d'une MenuBar, ou d'un menu contextuel (au point du clic).
	[[nodiscard]] sdl3::FRect MenuPopupRect(const Node &owner);
	/// Boîte du sous-menu d'un item (`item.children` non vide), alignée sur sa ligne.
	[[nodiscard]] sdl3::FRect SubMenuRect(const Node &owner, const MenuItem &item, float itemY);
	/// Item de la liste `items` sous l'ordonnée `my` dans la boîte `popup`, ou -1.
	[[nodiscard]] static int MenuItemAt(const std::vector<MenuItem> &items, const sdl3::FRect &popup, float my, float itemH);
	/// Ordonnée (top, relative à `popup.y`) de l'item `index` de la liste `items`.
	[[nodiscard]] static float MenuItemY(const std::vector<MenuItem> &items, int index, float itemH);

	sdl3::Canvas *m_canvas;
	sdl3::EventsManager *m_events = nullptr; ///< entrée mémorisée (Begin / Present)
	Context *m_immediate = nullptr; ///< contexte immédiat (feuilles Custom)
	NodePtr m_root;
	std::unordered_map<std::string, NodePtr> m_index; ///< identifiant → nœud (Find O(1))
	std::vector<StyleSheetPtr> m_sheets;
	Theme m_theme = DefaultRetainedTheme();
	sdl3::FRect m_viewport{}; ///< zone de l'interface (clip des fenêtres flottantes)
	const Translator *m_translator = nullptr;
	uint64_t m_i18nVersion = 0; ///< version du traducteur appliquée (0 : à refaire)
	bool m_i18nApplied = false; ///< des traductions ont été posées (à retirer sans traducteur)
	float m_rootFontSize = 14.f;
	Node *m_hovered = nullptr;
	Node *m_pressed = nullptr;
	bool m_blocked = false;
	Node *m_focus = nullptr;      ///< widget au focus clavier (champ de saisie…)
	// Widgets survolé / au focus à l'image précédente (callbacks OnMouseEnter/Leave,
	// OnGainFocus/OnLoseFocus) — remis à zéro au remontage de l'arbre.
	Node *m_prevHovered = nullptr;
	Node *m_prevFocus = nullptr;
	int m_lastCursor = -1;        ///< dernier curseur système posé (évite les appels redondants)
	Option<sdl3::SystemCursor> m_cursorOverride; ///< curseur forcé (NONE : automatique)
	const SDL_Surface *m_lastCursorImage = nullptr; ///< dernière image de curseur posée
	std::unordered_map<const SDL_Surface *, sdl3::Texture> m_textures; ///< cache Surface → Texture
	std::unordered_map<const SDL_Surface *, sdl3::Cursor> m_cursors;   ///< cache Surface → Cursor
	/// Texture GPU d'une surface (créée et mise en cache à la demande).
	[[nodiscard]] sdl3::Texture *TextureFor(const sdl3::Surface &surf);
	/// Dessine l'icône du nœud (image custom sinon glyphe de police), centrée dans `r`.
	void DrawWidgetIcon(const Node &node, const sdl3::FRect &r, Color color, float size);
	Node *m_openCombo = nullptr;  ///< ComboBox ouvert (menu déroulant)
	sdl3::FRect m_comboPopup{};     ///< boîte du menu déroulant ouvert
	Node *m_openMenuBar = nullptr; ///< MenuBar avec un menu déroulant ouvert
	sdl3::FRect m_menuPopup{};      ///< boîte du menu déroulant ouvert
	sdl3::FRect m_subMenuPopup{};   ///< boîte du sous-menu ouvert (le cas échéant)
	// Menu : délais d'intention au survol (transition) + triangle de sécurité.
	int m_menuHover = -1;          ///< titre de menu survolé (bascule en attente)
	float m_menuHoverTime = 0.f;   ///< durée de survol continu du titre
	int m_menuItemHover = -1;      ///< item survolé dans le menu ouvert
	float m_menuItemTime = 0.f;    ///< durée de survol continu de l'item
	float m_menuPrevX = 0.f, m_menuPrevY = 0.f; ///< position souris de l'image précédente
	float m_menuHoverDelay = 0.25f;  ///< délai avant bascule de menu au survol
	float m_submenuDelay = 0.20f;    ///< délai avant ouverture d'un sous-menu au survol
	Node *m_bringToFront = nullptr; ///< fenêtre flottante à amener au premier plan
	Node *m_dragSource = nullptr;   ///< source du glisser-déposer en cours
	bool m_dragActive = false;      ///< glisser-déposer en cours
	Node *m_scrollDrag = nullptr;   ///< barre de défilement glissée (nœud conteneur)
	float m_scrollGrab = 0.f;       ///< position de la souris dans le curseur au début du glissé
	float m_mouseX = 0.f, m_mouseY = 0.f; ///< position de la souris (logique, image courante)
	float m_pendingDt = 0.f;
	float m_dt = 0.f;                  ///< durée de l'image courante (répétition des flèches)
	double m_time = 0;               ///< temps accumulé (double-clic)
	Node *m_lastClickNode = nullptr; ///< dernier nœud cliqué (double-clic)
	Node *m_selNode = nullptr;		 ///< texte statique sélectionné (copie)
	bool m_selDragging = false;		 ///< sélection à la souris en cours
	Node *m_textClickNode = nullptr; ///< double-clic dans un texte (mot)
	double m_textClickTime = -1.0;
	/// Position dans le texte (affiché) du point (x, y), d'après la géométrie du dernier rendu.
	[[nodiscard]] size_t TextHit(const Node &node, std::string_view shown, float x, float y) const;
	/// Idem pour un éditeur de code (géométrie de `LayoutCode`).
	[[nodiscard]] size_t CodeHitOf(const Node &node, float x, float y) const;
	double m_lastClickTime = 0;      ///< instant du dernier clic

	std::vector<Toast> m_toasts;    ///< notifications non-modales actives
};

} // namespace ui
