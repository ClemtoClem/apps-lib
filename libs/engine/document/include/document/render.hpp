#pragma once
/**
 * @file render.hpp
 * @brief Rendu indépendant de l'interface : une `DisplayList` par page (rectangles, traits,
 *        ellipses, texte positionné, images), en points, construite depuis la mise en page.
 *
 * Aucune dépendance graphique : l'application convertit les commandes vers son canevas,
 * l'export PDF vers un `pdf::Canvas`. Les deux dessinent donc exactement la même chose.
 *
 *   doc::DisplayList list = doc::RenderPage(*snapshot, layout, 0);
 *   for (const doc::DrawCommand &c : list.commands) { … }
 *
 * Ordre de dessin : papier, fond et cadre de la page, puis les boîtes dans l'ordre de la mise
 * en page (fonds, bordures, images, formes), enfin le texte (surlignage, glyphes, soulignement).
 */
#include <string>
#include <vector>

#include <pdf/fonts.hpp>

#include "document/layout.hpp"

namespace jobs {
class ThreadPool;
}

namespace doc {

enum class DrawKind : uint8_t {
	FillRect,
	StrokeRect,
	FillEllipse,
	StrokeEllipse,
	Line,  ///< de (rect.x, rect.y) à (rect.Right(), rect.Bottom())
	Text,  ///< `text` à partir de (rect.x, baseline) ; rect : boîte du segment
	Image, ///< ressource `image` étirée dans `rect`
	FillPolygon,   ///< contour fermé `points` (x, y, x, y…) rempli (règle non nulle)
	StrokePolygon, ///< contour fermé `points` tracé
};

struct DrawCommand {
	DrawKind kind = DrawKind::FillRect;
	Rect rect;
	Color color = Color::BLACK();
	double lineWidth = 0;
	NodeId node = kNoNode; ///< nœud d'origine (sélection, débogage)
	// Texte
	std::string text; ///< UTF-8
	pdf::FontRef font;
	double size = 0;
	double baseline = 0;
	// Image
	ResourceId image = kNoResource;
	double opacity = 1;
	/// Rotation (degrés, sens horaire) autour de (pivotX, pivotY) : images, formes et leur texte.
	double rotation = 0, pivotX = 0, pivotY = 0;
	/// Polygone : sommets (x, y) à la suite, en points ; `rect` est sa boîte englobante.
	std::vector<double> points;
};

struct DisplayList {
	double width = 0, height = 0;
	NodeId page = kNoNode;
	size_t number = 0;
	std::vector<DrawCommand> commands;
};

struct RenderOptions {
	bool paper = true;			 ///< fond blanc du papier (désactivé : page transparente)
	bool blockBounds = false;	 ///< contour de chaque boîte (Affichage → limites des blocs)
	Color boundsColor = Color(120, 160, 220, 160);
	bool overflowMarkers = false; ///< cadre rouge autour des blocs en débordement
	bool parallel = true;		  ///< RenderDocument : pages en parallèle
	jobs::ThreadPool *pool = nullptr;
};

/// Contour d'une forme fermée inscrite dans `box` : sommets (x, y) à la suite (courbes du cœur, de
/// l'ellipse et des coins arrondis découpées en segments). Vide pour Line et ArrowLine (traits).
[[nodiscard]] std::vector<double> ShapeOutline(ShapeKind kind, const Rect &box);
/// Pointe d'une flèche (ArrowLine) au bout droit du trait à mi-hauteur de `box`, épaisseur `width`.
[[nodiscard]] std::vector<double> ArrowHead(const Rect &box, double width);

/// Liste d'affichage d'une page (vide si l'indice est hors limites).
[[nodiscard]] DisplayList RenderPage(const DocumentView &view, const LayoutResult &layout, size_t page, const RenderOptions &options = {});
/// Toutes les pages (en parallèle sur le pool si demandé).
[[nodiscard]] std::vector<DisplayList> RenderDocument(const DocumentView &view, const LayoutResult &layout, const RenderOptions &options = {});

} // namespace doc
