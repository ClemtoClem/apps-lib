#pragma once
/**
 * @file display.hpp
 * @brief Affichage des pages : disposition des pages à l'écran (zoom, défilement), conversion
 *        écran ↔ page, dessin des listes d'affichage du moteur sur le canevas SDL3.
 *
 * Les positions viennent du moteur : chaque mot est placé à l'abscisse calculée par le moteur,
 * avec le fichier de police utilisé pour la mesure (police installée choisie dans le document),
 * ou, pour une police standard du PDF (Helvetica, Times, Courier), une police installée aux
 * mêmes métriques (Nimbus, Liberation) : la disposition à l'écran est celle du PDF exporté.
 * Les nœuds tournés sont dessinés tournés autour de leur centre de rotation.
 */
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <memory>
#include <utility>

#include <document/fonts.hpp>
#include <document/render.hpp>
#include <sdl3/render/canvas.hpp>
#include <sdl3/render/render.hpp>

namespace scribe {

inline constexpr float kPixelsPerPoint = 96.f / 72.f; ///< zoom 100 % : 1 pt = 1/72 pouce à 96 ppp
inline constexpr float kPageGap = 24.f;

/// Page placée à l'écran.
struct PageView {
	size_t index = 0;
	sdl3::FRect rect; ///< pixels
	float scale = 1;  ///< pixels par point
};

struct PageLayoutResult {
	std::vector<PageView> pages;
	float contentHeight = 0; ///< hauteur totale (défilement)
	float contentWidth = 0;
};

/// Pages empilées verticalement et centrées dans `area`, décalées de `scrollY`.
[[nodiscard]] PageLayoutResult PlacePages(const std::vector<doc::DisplayList> &pages, const sdl3::FRect &area, float zoom, float scrollY);
/// Zoom qui ajuste la page la plus large à la largeur de la zone.
[[nodiscard]] float FitWidthZoom(const std::vector<doc::DisplayList> &pages, float areaWidth);

struct PagePoint {
	size_t page = 0;
	double x = 0, y = 0; ///< points, repère de la page
};
/// Point d'écran → page sous le point (et coordonnées en points) ; nullopt hors des pages.
[[nodiscard]] std::optional<PagePoint> ScreenToPage(const std::vector<PageView> &views, float x, float y);
/// Rectangle de page (points) → écran.
[[nodiscard]] sdl3::FRect PageToScreen(const PageView &view, const doc::Rect &r);

/// Textures des images du document (créées à la demande, oubliées quand le document change).
class ImageCache {
public:
	sdl3::Texture *Get(sdl3::Renderer &renderer, const doc::DocumentView &view, doc::ResourceId id);
	void Clear() { m_textures.clear(); }

private:
	uint64_t m_stamp = 0;
	std::map<doc::ResourceId, sdl3::Texture> m_textures;
};

/// Polices du canevas correspondant aux polices du moteur.
class FontCache {
public:
	struct Face {
		sdl3::CanvasFont font = sdl3::CanvasFont::Regular;
		double ascent = 780; ///< ascendante de la police du canevas, 1/1000 em
	};
	/// Bibliothèque des polices installées (substituts des polices standard) ; vide le cache.
	void SetLibrary(std::shared_ptr<const doc::FontLibrary> fonts);
	/// Police du canevas pour `font` ; sans fichier ni substitut : police du canevas (régulière ou grasse).
	[[nodiscard]] Face Get(sdl3::Canvas &canvas, const pdf::FontRef &font);
	/// Familles installées essayées, dans l'ordre, pour une police standard.
	[[nodiscard]] static std::span<const std::string_view> Substitutes(pdf::StandardFont font);

private:
	std::shared_ptr<const doc::FontLibrary> m_fonts;
	std::map<std::pair<int, const void *>, Face> m_faces; ///< clé : police standard, fichier
};

// ── Cadre de transformation des formes et images ──

/// Rectangle (avant rotation), rotation et centre de rotation d'une boîte, coordonnées de la page.
struct ShapeFrame {
	doc::Rect rect;
	double rotation = 0; ///< degrés, sens horaire
	double pivotX = 0, pivotY = 0;
};
[[nodiscard]] ShapeFrame FrameOf(const doc::LayoutBox &box);

/// Poignées 0…7 : haut-gauche, haut, haut-droit, droit, bas-droit, bas, bas-gauche, gauche.
inline constexpr int kHandleCount = 8;
inline constexpr int kFrameBorder = 8; ///< FrameHit : sur le bord, hors poignée
inline constexpr int kFrameInside = 9; ///< FrameHit : à l'intérieur
/// Position (page) de la poignée `handle`, rotation comprise.
[[nodiscard]] std::pair<double, double> HandlePoint(const ShapeFrame &frame, int handle);
/// Élément du cadre sous le point (page) : poignée 0…7, kFrameBorder, kFrameInside ; −1 dehors.
[[nodiscard]] int FrameHit(const ShapeFrame &frame, double x, double y, double tolerance);
/// Rectangle (avant rotation) après glissement de la poignée de (dx, dy) en coordonnées de la
/// page : le déplacement est ramené dans les axes de la forme ; taille minimale 1 pt.
/// `keepRatio` : rapport largeur / hauteur conservé (coin : la plus grande variation l'emporte,
/// coin opposé fixe ; bord : l'autre dimension suit, centrée).
[[nodiscard]] doc::Rect ResizedRect(const ShapeFrame &frame, int handle, double dx, double dy, bool keepRatio = false);

// ── Formes ──

/// Formes proposées (ordre de l'énumération) et leur nom (langue des sources, traduit par l'interface).
struct ShapeInfo {
	doc::ShapeKind kind;
	std::string_view name;
};
[[nodiscard]] std::span<const ShapeInfo> ShapeCatalog() noexcept;
[[nodiscard]] std::string_view ShapeName(doc::ShapeKind kind) noexcept;
/// Aperçu d'une forme dans `r` (pixels) : son contour du moteur, rempli et tracé.
void DrawShapePreview(sdl3::Canvas &canvas, const sdl3::FRect &r, doc::ShapeKind kind, sdl3::Color fill, sdl3::Color stroke);

// ── Grille ──

/// Valeur aimantée au multiple de `step` le plus proche (`step` ≤ 0 : inchangée).
[[nodiscard]] double SnapToGrid(double value, double step);
/// Déplacement (dx, dy) d'une boîte : son coin haut gauche aimanté à la grille ; `lockAxis` :
/// seul l'axe du plus grand déplacement est gardé (Maj).
[[nodiscard]] std::pair<double, double> SnappedMove(const doc::Rect &rect, double dx, double dy, double step, bool lockAxis);
/// Grille de pas `step` (points) sur la page, au premier plan ; les lignes trop serrées à l'écran
/// sont espacées (pas doublé jusqu'à 6 pixels au moins).
void DrawGrid(sdl3::Canvas &canvas, const PageView &page, double step, sdl3::Color color);
/// Rotation après avoir tourné de `from` vers `to` autour du centre ; `snap` : pas de 15°.
[[nodiscard]] double DraggedRotation(const ShapeFrame &frame, double fromX, double fromY, double toX, double toY, bool snap);
/// Cadre et 8 poignées (bleus), ou en mode rotation : cadre rouge et centre de rotation.
void DrawShapeFrame(sdl3::Canvas &canvas, const PageView &page, const ShapeFrame &frame, bool rotateMode);

// ── Rembourrage : marges intérieures des boîtes (outil au survol) ──

enum class PaddingSide : uint8_t { Top, Right, Bottom, Left };

/// Bord intérieur d'une boîte à marge intérieure (paragraphe, cellule, table des matières).
struct PaddingHandle {
	doc::NodeId node = doc::kNoNode;
	doc::NodeType type = doc::NodeType::Paragraph;
	PaddingSide side = PaddingSide::Top;
	doc::Rect box;		 ///< boîte (coordonnées de la page)
	double border = 0;	 ///< épaisseur de la bordure
	doc::Insets padding; ///< marges intérieures actuelles
	[[nodiscard]] double Value() const noexcept;
	/// Coordonnée du bord intérieur : x (gauche, droite) ou y (haut, bas).
	[[nodiscard]] double Edge() const noexcept;
	[[nodiscard]] doc::PropertyId Property() const noexcept;
	[[nodiscard]] bool Horizontal() const noexcept { return side == PaddingSide::Left || side == PaddingSide::Right; }
};

/// Bord intérieur sous le point (± `tolerance`, marge intérieure > 0) de la boîte la plus
/// imbriquée de la page ; le haut d'un fragment qui continue une page précédente et le bas d'un
/// fragment qui se poursuit ne comptent pas.
[[nodiscard]] std::optional<PaddingHandle> PaddingAt(const doc::DocumentView &view, const doc::PageLayout &page, double x, double y,
													 double tolerance);
/// Marge intérieure obtenue en amenant le bord intérieur à `coordinate` : aimantée au
/// demi-millimètre (au dixième si `fine`), ≥ 0, en laissant au moins 6 pt de contenu.
[[nodiscard]] double DraggedPadding(const PaddingHandle &handle, double coordinate, bool fine);
/// « Rembourrage gauche » … (langue des sources, traduit par l'interface)
[[nodiscard]] std::string_view PaddingName(PaddingSide side) noexcept;

// ── Marges de la page ──

enum class MarginSide : uint8_t { Top, Right, Bottom, Left };

/// « Marge haute » … (langue des sources, traduit par l'interface)
[[nodiscard]] std::string_view MarginName(MarginSide side) noexcept;
[[nodiscard]] doc::PropertyId MarginProperty(MarginSide side) noexcept;
[[nodiscard]] double MarginOf(const doc::Insets &margins, MarginSide side) noexcept;
/// Plus grande marge possible de ce côté : la marge opposée en place, au moins 36 pt (0,5 pouce)
/// de largeur ou de hauteur de contenu.
[[nodiscard]] double MaxMargin(const doc::PageGeometry &page, MarginSide side) noexcept;

/// Dessine une page (ombre, papier compris dans la liste) à sa place à l'écran.
void DrawPage(sdl3::Canvas &canvas, sdl3::Renderer &renderer, const doc::DocumentView &view, const doc::DisplayList &list,
			  const PageView &page, ImageCache &images, FontCache &fonts);

} // namespace scribe
