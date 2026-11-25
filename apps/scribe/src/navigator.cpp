#include "scribe/navigator.hpp"

namespace scribe {

using namespace doc;

bool NavigatorFilter::Shows(OutlineKind kind) const noexcept {
	switch (kind) {
	case OutlineKind::Heading: return headings;
	case OutlineKind::Figure: return figures;
	case OutlineKind::Table: return tables;
	case OutlineKind::Listing: return listings;
	}
	return false;
}

std::vector<NavigatorRow> NavigatorRows(const Outline &outline, const NavigatorFilter &filter, const std::set<NodeId> &collapsed) {
	std::vector<NavigatorRow> rows;
	const std::vector<OutlineEntry> &entries = outline.entries;
	int currentLevel = 0;  // niveau du dernier titre rencontré (profondeur des légendes)
	int hiddenBelow = 10; // titres repliés : entrées masquées jusqu'au prochain titre de niveau ≤
	for (size_t i = 0; i < entries.size(); ++i) {
		const OutlineEntry &e = entries[i];
		const bool heading = e.kind == OutlineKind::Heading;
		if (heading && e.level <= hiddenBelow)
			hiddenBelow = 10;
		const bool hidden = heading ? e.level > hiddenBelow : currentLevel >= hiddenBelow;
		if (heading)
			currentLevel = e.level;
		if (hidden || !filter.Shows(e.kind)) {
			if (heading && !hidden && collapsed.contains(e.node))
				hiddenBelow = e.level;
			continue;
		}
		NavigatorRow row;
		row.entry = &e;
		row.depth = heading ? e.level - 1 : currentLevel;
		if (heading) // enfants visibles : entrées suivantes jusqu'au prochain titre de niveau ≤
			for (size_t k = i + 1; k < entries.size(); ++k) {
				const OutlineEntry &next = entries[k];
				if (next.kind == OutlineKind::Heading && next.level <= e.level)
					break;
				if (filter.Shows(next.kind)) {
					row.hasChildren = true;
					break;
				}
			}
		row.collapsed = row.hasChildren && collapsed.contains(e.node);
		if (row.collapsed)
			hiddenBelow = e.level;
		rows.push_back(row);
	}
	return rows;
}

} // namespace scribe
