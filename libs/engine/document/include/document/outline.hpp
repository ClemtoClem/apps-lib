#pragma once
/**
 * @file outline.hpp
 * @brief Structure du document : titres numérotés (OutlineLevel, HeadingNumbering), légendes
 *        numérotées (CaptionKind), marqueurs de listes (ListStyle), cibles des renvois.
 *
 * Calculée à chaque mise en page (avant la mesure des blocs) et utilisable seule : panneau de
 * navigation, signets du PDF, tables des matières.
 *
 *   doc::Outline outline = doc::BuildOutline(*document.TakeSnapshot());
 *   for (const doc::OutlineEntry &e : outline.entries)
 *       ...; // e.level, e.label (« 2.3 », « Figure 4 »), e.text
 *
 * Parcours : corps des sections, dans l'ordre du document, cellules de tableaux comprises ; les
 * en-têtes et pieds de page n'ont ni titres numérotés ni légendes.
 */
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "document/document.hpp"

namespace doc {

enum class OutlineKind : uint8_t { Heading, Figure, Table, Listing };

struct OutlineEntry {
	NodeId node = kNoNode; ///< paragraphe
	OutlineKind kind = OutlineKind::Heading;
	int level = 1;		 ///< titres : 1…9 ; légendes : 1
	std::string number;	 ///< « 1.4.2 » (vide si les titres ne sont pas numérotés), « 3 »
	std::string label;	 ///< « 1.4.2. », « Figure 3 » (vide : titre non numéroté)
	std::string text;	 ///< texte du paragraphe (sans le préfixe)
};

/// Préfixe généré d'un paragraphe.
struct ParagraphPrefix {
	std::string text;	 ///< « 1.4.2. », « Figure 3 : », « • », « 2. »
	bool marker = false; ///< marqueur de liste (dans le retrait) ; sinon en tête de la première ligne
	int listLevel = 0;
};

struct Outline {
	std::vector<OutlineEntry> entries; ///< ordre du document
	std::unordered_map<NodeId, ParagraphPrefix> prefixes;
	/// Préfixes des paragraphes de chaque bloc du flux (le bloc et ses cellules) : clé du cache
	/// de mesure (un tableau dont une légende interne change de numéro est remesuré).
	std::unordered_map<NodeId, std::string> contexts;
	/// Blocs dont la mesure dépend de toute la structure (tables des matières, renvois).
	std::unordered_set<NodeId> dependents;

	[[nodiscard]] const OutlineEntry *Find(NodeId node) const;
	[[nodiscard]] const ParagraphPrefix *Prefix(NodeId paragraph) const;
	/// Texte d'un renvoi vers `target` : numéro du titre (« 2.3 »), libellé de la légende
	/// (« Tableau 4 »), sinon texte du titre non numéroté ; « ? » si la cible est inconnue.
	[[nodiscard]] std::string Reference(NodeId target) const;
	/// Entrées d'une table des matières (titres jusqu'au niveau `depth`, ou légendes d'un type).
	[[nodiscard]] std::vector<const OutlineEntry *> Entries(TocContent content, int depth) const;
};

[[nodiscard]] Outline BuildOutline(const DocumentView &view);
[[nodiscard]] Outline BuildOutline(const DocumentView &view, PropertyCache &cache);

/// Cible interne d'un lien « #id » (kNoNode : lien externe ou invalide).
[[nodiscard]] NodeId LinkTarget(std::string_view link) noexcept;
[[nodiscard]] std::string InternalLink(NodeId target);
/// Marqueur d'un élément de liste (« • », « 3. », « c) », « iv. »).
[[nodiscard]] std::string ListMarker(ListStyle style, int level, int64_t number);
/// Chiffres romains (1…3999 ; au-delà : chiffres arabes).
[[nodiscard]] std::string Roman(int64_t n, bool upper);

} // namespace doc
