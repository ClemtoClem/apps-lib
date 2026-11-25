#pragma once
/**
 * @file tables.hpp
 * @brief Tableaux : grille (lignes × colonnes), création rapide de grands tableaux, insertion,
 *        suppression, duplication et déplacement de lignes ou de colonnes, largeurs de colonnes,
 *        hauteurs de lignes, bordures prédéfinies, lignes alternées, cellules fusionnées.
 *
 * Toutes les opérations passent par `History` : chacune est une transaction (une entrée
 * d'historique), ou rejoint la transaction en cours. Une colonne n'est pas un nœud : c'est
 * l'ensemble des cellules de même rang de chaque ligne.
 *
 *   auto table = doc::table::Create(history, body, doc::kAppend, 2000, 40);   // 2 000 × 40
 *   doc::table::InsertColumns(history, table.Value(), 3, 2);                // 2 colonnes au rang 3
 *   doc::table::MoveRows(history, table.Value(), 10, 5, 0);                 // lignes 10 à 14 en tête
 */
#include <span>
#include <vector>

#include "document/commands.hpp"

namespace doc {

/// Grille d'un tableau (toutes les lignes, visibles ou non).
struct TableGrid {
	NodeId table = kNoNode;
	std::vector<NodeId> rows;				///< TableRow
	std::vector<std::vector<NodeId>> cells; ///< [ligne][colonne] (lignes de longueurs inégales possibles)
	size_t columns = 0;						///< plus longue ligne

	[[nodiscard]] static TableGrid Of(const DocumentView &view, NodeId table);
	[[nodiscard]] NodeId Cell(size_t row, size_t column) const noexcept;
	/// Ligne et colonne d'une cellule (ou d'un nœud qu'elle contient : `view` requis).
	[[nodiscard]] bool Find(NodeId cell, size_t &row, size_t &column) const noexcept;
	[[nodiscard]] bool Find(const DocumentView &view, NodeId node, size_t &row, size_t &column) const;
	/// Cellules du rectangle [r0, r1] × [c0, c1] (bornes incluses, dans n'importe quel ordre).
	[[nodiscard]] std::vector<NodeId> Range(size_t r0, size_t c0, size_t r1, size_t c1) const;
	[[nodiscard]] std::vector<NodeId> Column(size_t column) const;
};

/// Fusion de cellules : cellule d'ancrage (coin haut gauche) et étendue en lignes et colonnes.
struct CellSpan {
	size_t row = 0, column = 0;
	size_t rows = 1, columns = 1;
	[[nodiscard]] bool Contains(size_t r, size_t c) const noexcept { return r >= row && r < row + rows && c >= column && c < column + columns; }
	friend bool operator==(const CellSpan &, const CellSpan &) = default;
};

enum class BorderPreset : uint8_t {
	All,		///< toutes les bordures
	None,		///< aucune
	Outer,		///< cadre extérieur seulement
	Horizontal, ///< filets horizontaux (en-tête et pied du tableau, séparation de l'en-tête)
	Inner,		///< séparations intérieures seulement
};

namespace table {

/// Fusions effectives d'une grille de cellules ([ligne][colonne]) : étendues demandées par
/// `RowSpan` / `ColumnSpan`, bornées à la grille ; une cellule déjà couverte n'ancre rien (lecture
/// ligne par ligne), une fusion s'arrête avant une cellule déjà couverte de sa première ligne.
[[nodiscard]] std::vector<CellSpan> Spans(const DocumentView &view, const std::vector<std::vector<NodeId>> &cells);
/// Fusion qui contient la case (r, c), sinon la case seule.
[[nodiscard]] CellSpan SpanAt(std::span<const CellSpan> spans, size_t r, size_t c) noexcept;

/// Tableau vide de `rows` × `columns` (une cellule : un paragraphe vide) ; `cell` : propriétés
/// de chaque cellule (marges intérieures, bordure…). Limites : 1 000 000 de cellules, 1 000
/// colonnes. Rend le tableau.
DocResult<NodeId> Create(History &history, NodeId parent, size_t position, size_t rows, size_t columns, const PropertyMap &cell = {});

/// Insère `count` lignes vides au rang `at` (0 … nombre de lignes), mises en forme comme la
/// ligne voisine (propriétés des cellules, du paragraphe et du segment, sans le texte ni
/// l'en-tête répété). Rend les lignes créées.
DocResult<std::vector<NodeId>> InsertRows(History &history, NodeId table, size_t at, size_t count = 1);
/// Insère `count` colonnes vides au rang `at`, mises en forme comme la colonne voisine.
Status InsertColumns(History &history, NodeId table, size_t at, size_t count = 1);
Status DeleteRows(History &history, NodeId table, size_t first, size_t count = 1);
Status DeleteColumns(History &history, NodeId table, size_t first, size_t count = 1);
/// Copies des lignes [first, first + count) insérées juste après elles. Rend les copies.
DocResult<std::vector<NodeId>> DuplicateRows(History &history, NodeId table, size_t first, size_t count = 1);
Status DuplicateColumns(History &history, NodeId table, size_t first, size_t count = 1);
/// Déplace les lignes [first, first + count) avant la ligne `to` (rang avant le déplacement,
/// 0 … nombre de lignes) ; refusé si `to` est à l'intérieur du bloc déplacé.
Status MoveRows(History &history, NodeId table, size_t first, size_t count, size_t to);
Status MoveColumns(History &history, NodeId table, size_t first, size_t count, size_t to);

/// Largeur d'une colonne (points ; 0 : automatique) : portée par la cellule de la première ligne.
Status SetColumnWidth(History &history, NodeId table, size_t column, double width);
/// Largeurs explicites retirées : colonnes à parts égales.
Status DistributeColumns(History &history, NodeId table);
/// Hauteur minimale d'une ligne (points ; 0 : selon le contenu).
Status SetRowHeight(History &history, NodeId row, double height);
/// Même propriété pour plusieurs nœuds (cellules, lignes) en une opération.
Status SetProperty(History &history, std::span<const NodeId> nodes, PropertyId property, PropertyValue value);
/// Bordures prédéfinies (`width` et `color` des traits ; `None` les retire).
Status ApplyBorders(History &history, NodeId table, BorderPreset preset, double width, Color color);
/// Fonds alternés des lignes, portés par leurs cellules (la première ligne n'est pas touchée si
/// `skipHeader`).
Status BandRows(History &history, NodeId table, Color even, Color odd, bool skipHeader);

/// Fusionne le rectangle [r0, r1] × [c0, c1] (bornes dans n'importe quel ordre) : la cellule du
/// coin haut gauche le couvre ; le contenu non vide des autres lui est ajouté (ordre de lecture),
/// elles gardent un paragraphe vide. Les fusions entièrement comprises sont absorbées ; refusé si
/// le rectangle n'a qu'une case ou coupe une fusion existante.
Status MergeCells(History &history, NodeId table, size_t r0, size_t c0, size_t r1, size_t c1);
/// Sépare les fusions qui touchent le rectangle (étendues remises à 1, contenu gardé par la
/// cellule d'ancrage) ; rien à séparer : `ErrorCode::Nothing`.
Status SplitCells(History &history, NodeId table, size_t r0, size_t c0, size_t r1, size_t c1);

} // namespace table

} // namespace doc
