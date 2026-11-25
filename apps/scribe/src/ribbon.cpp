#include "scribe/ribbon.hpp"

namespace scribe {

std::string_view TabName(RibbonTab tab) noexcept {
	switch (tab) {
	case RibbonTab::File: return "Fichier";
	case RibbonTab::Home: return "Accueil";
	case RibbonTab::Insert: return "Insertion";
	case RibbonTab::References: return "Références";
	case RibbonTab::PageLayout: return "Mise en page";
	case RibbonTab::Table: return "Tableau";
	case RibbonTab::Arrange: return "Disposition";
	case RibbonTab::View: return "Affichage";
	case RibbonTab::Count: break;
	}
	return "?";
}

RibbonTab SuggestTab(const doc::SelectionContext &context, RibbonTab current) noexcept {
	if (context.Empty())
		return current;
	if (context.table)
		return RibbonTab::Table;
	if (context.group || ((context.image || context.shape) && !context.text))
		return RibbonTab::Arrange;
	if (context.page && !context.text)
		return RibbonTab::PageLayout;
	if (context.text)
		return RibbonTab::Home;
	return current;
}

} // namespace scribe
