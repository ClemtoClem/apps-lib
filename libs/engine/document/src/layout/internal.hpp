#pragma once
// Outils internes de la mise en page (partagés par la mesure et la pagination).
#include <functional>
#include <span>
#include <vector>

#include "document/layout.hpp"

namespace doc::detail {

/// Bloc imbriqué à mesurer (contenu d'une cellule).
struct NestedRequest {
	NodeId id = kNoNode;
	double width = 0, maxHeight = 0;
};

/// Mesure des blocs des cellules : reprise du cache de la mise en page et mesure parallèle des
/// absents (fournie par la pagination ; sans elle, mesure directe).
class NestedMeasure {
public:
	virtual ~NestedMeasure() = default;
	virtual void MeasureAll(std::span<const NestedRequest> requests, std::vector<BlockPtr> &out) = 0;
};

/// Valeurs des champs de numérotation ; `known` faux : texte provisoire de largeur « 00 ».
struct FieldValues {
	size_t page = 0, count = 0;
	bool known = false;
	const Outline *outline = nullptr; ///< numéros, préfixes, entrées des tables des matières
	std::string chapter;			  ///< champ Chapter de la page
	NestedMeasure *nested = nullptr;  ///< cache des cellules (fil de la pagination seulement)
};

/// Contribution d'un bloc du flux à la structure (mise en cache par bloc, voir `LayoutCache`).
struct OutlineItem {
	/// Table : début d'un tableau ; Enter / Leave : cellule ; Break : autre bloc (coupe les listes).
	enum Kind : uint8_t { Paragraph, Table, Enter, Leave, Break, Dependent } kind = Paragraph;
	NodeId node = kNoNode;
	int64_t level = 0;
	CaptionKind caption = CaptionKind::None;
	ListStyle list = ListStyle::None;
	int64_t listLevel = 0, listStart = 0;
	bool depends = false; ///< renvois (Reference, PageReference)
	std::string label;	  ///< CaptionLabel
	std::string text;	  ///< texte (titres et légendes seulement)
};
using OutlineItems = std::vector<OutlineItem>;

/// Éléments d'un bloc du corps (paragraphe, tableau et ses cellules, table des matières…).
[[nodiscard]] OutlineItems CollectOutline(const DocumentView &view, PropertyCache &cache, NodeId block);
/// Numérotation des éléments, section par section : `items(section, block)` rend ceux d'un bloc.
[[nodiscard]] Outline AssembleOutline(const DocumentView &view, PropertyCache &cache,
									  const std::function<const OutlineItems &(NodeId block)> &items);

[[nodiscard]] std::string FieldText(FieldKind field, const FieldValues &values);
[[nodiscard]] BlockPtr Measure(const DocumentView &view, PropertyCache &cache, NodeId block, double width, double maxHeight,
							   const FieldValues &fields, const FontLibrary *fonts);
/// Le bloc doit-il être mis en page (visible, type de bloc) ?
[[nodiscard]] bool IsLaidOut(const DocumentView &view, PropertyCache &cache, NodeId id);

} // namespace doc::detail
