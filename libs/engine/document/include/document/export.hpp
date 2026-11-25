#pragma once
/**
 * @file export.hpp
 * @brief Exports : PDF (depuis les listes d'affichage, via `foundation.pdf`), HTML (document
 *        sémantique avec styles), CSV (tableaux, pour les tableurs).
 *
 * Toutes ces fonctions lisent une vue en lecture seule : elles s'exécutent sur un `Snapshot`,
 * sur un fil de fond. Les fichiers sont écrits de façon atomique (temporaire puis renommage).
 *
 *   const doc::Snapshot snap = document.TakeSnapshot();
 *   const doc::LayoutResult layout = doc::Layout(*snap);
 *   auto pdf = doc::SavePdf(*snap, layout, "rapport.pdf");
 */
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "document/layout.hpp"

namespace doc {

struct PdfExportOptions {
	bool compress = true;
	/// Signets : titres de la structure (OutlineLevel, numéro compris), hiérarchisés par niveau ;
	/// sans titre, nœuds du flux portant un nom (propriété Name). Les liens des segments (Link,
	/// entrées des tables des matières) deviennent des zones cliquables dans tous les cas.
	bool bookmarks = true;
	std::string title;	///< vide : nom (Name) du nœud Document
	std::string author, subject, keywords;
	std::string creator = "engine.document";
	int64_t creationTime = 0; ///< secondes Unix ; 0 : date absente (sortie déterministe)
};

/// PDF de toutes les pages de la mise en page (même rendu que l'écran : `RenderDocument`).
[[nodiscard]] DocResult<std::vector<uint8_t>> ExportPdf(const DocumentView &view, const LayoutResult &layout,
														const PdfExportOptions &options = {});
Status SavePdf(const DocumentView &view, const LayoutResult &layout, const std::filesystem::path &path, const PdfExportOptions &options = {});

struct HtmlExportOptions {
	std::string title;		 ///< vide : nom du nœud Document, sinon « Document »
	bool embedImages = true; ///< images en PNG dans des URI `data:` (sinon cadres de substitution)
	std::string language = "fr";
};

/// HTML5 autonome : en-tête et pied par défaut de la première section, flux de chaque section
/// (paragraphes, segments stylés, tableaux avec en-têtes répétés en `<thead>`, images, formes,
/// sauts de page), règles de pagination traduites en CSS d'impression, format de page (`@page`).
[[nodiscard]] DocResult<std::string> ExportHtml(const DocumentView &view, const HtmlExportOptions &options = {});
Status SaveHtml(const DocumentView &view, const std::filesystem::path &path, const HtmlExportOptions &options = {});

/// CSV (RFC 4180) d'un tableau : une ligne par TableRow visible, texte des cellules (paragraphes
/// séparés par un retour à la ligne, tableaux imbriqués aplatis), lignes complétées au nombre de
/// colonnes le plus grand.
[[nodiscard]] DocResult<std::string> ExportCsv(const DocumentView &view, NodeId table);
Status SaveCsv(const DocumentView &view, NodeId table, const std::filesystem::path &path);

} // namespace doc
