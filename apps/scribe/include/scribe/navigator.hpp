#pragma once
/**
 * @file navigator.hpp
 * @brief Navigateur du plan (panneau « Plan ») sans dessin : arbre des titres, sous-titres et
 *        légendes du document, filtres par type, branches repliées.
 */
#include <set>
#include <vector>

#include <document/outline.hpp>

namespace scribe {

struct NavigatorFilter {
	bool headings = true, figures = true, tables = true, listings = true;
	[[nodiscard]] bool Shows(doc::OutlineKind kind) const noexcept;
};

struct NavigatorRow {
	const doc::OutlineEntry *entry = nullptr;
	int depth = 0;			 ///< retrait : niveau du titre − 1 ; légende : sous le titre qui la précède
	bool hasChildren = false; ///< titre suivi d'entrées plus profondes (repliable)
	bool collapsed = false;
};

/// Lignes visibles : entrées gardées par le filtre, descendants des titres repliés masqués.
[[nodiscard]] std::vector<NavigatorRow> NavigatorRows(const doc::Outline &outline, const NavigatorFilter &filter,
													  const std::set<doc::NodeId> &collapsed);

} // namespace scribe
