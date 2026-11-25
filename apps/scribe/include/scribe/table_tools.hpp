#pragma once
/**
 * @file table_tools.hpp
 * @brief Outils des tableaux à l'écran (sans dessin) : géométrie d'un fragment de tableau sur une
 *        page (bords des colonnes et des lignes), élément sous le pointeur : bord à glisser
 *        (largeur de colonne, hauteur de ligne), bouton « + » d'insertion au-dessus ou à gauche
 *        d'un bord, poignée de sélection d'une colonne, d'une ligne ou du tableau.
 */
#include <vector>

#include <document/layout.hpp>

namespace scribe {

/// Fragment d'un tableau sur une page (coordonnées de la page).
struct TableGeometry {
	doc::NodeId table = doc::kNoNode;
	size_t page = 0;
	doc::Rect rect;
	std::vector<double> columns;		 ///< bords verticaux x0 … xN (cellules de la première ligne du fragment)
	std::vector<double> rows;			 ///< bords horizontaux y0 … yM
	std::vector<doc::NodeId> rowNodes;	 ///< M lignes du fragment
	std::vector<size_t> rowIndices;		 ///< rang de chaque ligne dans le tableau
	[[nodiscard]] size_t ColumnCount() const noexcept { return columns.empty() ? 0 : columns.size() - 1; }
	[[nodiscard]] size_t RowCount() const noexcept { return rowNodes.size(); }
	/// Colonne / ligne (du fragment) sous une abscisse / ordonnée ; SIZE_MAX hors du tableau.
	[[nodiscard]] size_t ColumnAt(double x) const noexcept;
	[[nodiscard]] size_t RowAt(double y) const noexcept;
};

/// Fragments des tableaux (de premier niveau ou imbriqués) dessinés sur une page.
[[nodiscard]] std::vector<TableGeometry> TablesOn(const doc::DocumentView &view, const doc::LayoutResult &layout, size_t page);

enum class TableHitKind : uint8_t { None, ColumnBorder, RowBorder, InsertColumn, InsertRow, SelectColumn, SelectRow, SelectTable };

struct TableHit {
	TableHitKind kind = TableHitKind::None;
	/// ColumnBorder : bord i (1…N, à droite de la colonne i − 1) ; RowBorder : ligne du fragment
	/// dont le bas est visé ; InsertColumn : rang d'insertion 0…N ; InsertRow : rang d'insertion
	/// dans le tableau ; SelectColumn : colonne ; SelectRow : rang de la ligne dans le tableau.
	size_t index = 0;
};

/// Élément du tableau sous le point (page) : bords (± `tolerance`), puis, dans la bande de
/// largeur `margin` au-dessus ou à gauche du tableau, bouton « + » près d'un bord, sinon
/// poignée de sélection ; coin supérieur gauche : tableau entier.
[[nodiscard]] TableHit HitTable(const TableGeometry &table, double x, double y, double tolerance, double margin);

} // namespace scribe
