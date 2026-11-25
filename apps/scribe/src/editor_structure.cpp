// Éditeur : styles de paragraphe (groupes du document), listes, légendes, liens, renvois,
// tables des matières, navigation dans la structure.
#include <algorithm>
#include <format>

#include "scribe/editor.hpp"
#include "scribe/i18n.hpp"

namespace scribe {

using namespace doc;

namespace {

/// Couleur nommée de la table de sdl3.
Color Named(std::string_view name) { return Color::FromName(name).value_or(Color::BLACK()); }

constexpr std::string_view kStyleNames[] = {"Normal", "Titre",	  "Sous-titre", "Titre 1", "Titre 2", "Titre 3",
											"Titre 4", "Citation", "Code",		 "Encadré", "Légende"};
static_assert(std::size(kStyleNames) == size_t(ParagraphStyle::Count));

/// Propriétés d'un style prédéfini (posées sur son groupe).
PropertyMap StyleProperties(ParagraphStyle style) {
	PropertyMap m;
	const auto set = [&m](PropertyId id, PropertyValue v) { m.Set(id, std::move(v)); };
	switch (style) {
	case ParagraphStyle::Normal:
	case ParagraphStyle::Count: break;
	case ParagraphStyle::Title:
		set(PropertyId::FontSize, 28.0);
		set(PropertyId::Bold, true);
		set(PropertyId::TextColor, Named("MIDNIGHT_BLUE"));
		set(PropertyId::SpaceAfter, 6.0);
		break;
	case ParagraphStyle::Subtitle:
		set(PropertyId::FontSize, 15.0);
		set(PropertyId::Italic, true);
		set(PropertyId::TextColor, Named("DIM_GREY"));
		set(PropertyId::SpaceAfter, 18.0);
		break;
	case ParagraphStyle::Heading1:
	case ParagraphStyle::Heading2:
	case ParagraphStyle::Heading3:
	case ParagraphStyle::Heading4: {
		const int level = int(style) - int(ParagraphStyle::Heading1) + 1;
		static constexpr double kSizes[] = {18, 14, 12, 11};
		static constexpr double kBefore[] = {18, 14, 10, 8};
		set(PropertyId::OutlineLevel, int64_t(level));
		set(PropertyId::FontSize, kSizes[level - 1]);
		set(PropertyId::Bold, true);
		set(PropertyId::TextColor, Named(level <= 2 ? "FIRE_BRICK" : "BLACK"));
		set(PropertyId::SpaceBefore, kBefore[level - 1]);
		set(PropertyId::SpaceAfter, level == 1 ? 8.0 : 4.0);
		set(PropertyId::KeepWithNext, true);
		if (level == 4)
			set(PropertyId::Italic, true);
		break;
	}
	case ParagraphStyle::Quote:
		set(PropertyId::Italic, true);
		set(PropertyId::TextColor, Named("DIM_GREY"));
		set(PropertyId::IndentLeft, 24.0);
		set(PropertyId::IndentRight, 24.0);
		set(PropertyId::PaddingLeft, 10.0);
		set(PropertyId::BorderWidth, 2.0);
		set(PropertyId::BorderSides, border::kLeft);
		set(PropertyId::BorderColor, Named("SILVER"));
		break;
	case ParagraphStyle::Code:
		set(PropertyId::FontFamily, std::string("Courier"));
		set(PropertyId::FontSize, 9.5);
		set(PropertyId::LineSpacing, 1.0);
		set(PropertyId::PreserveSpaces, true);
		set(PropertyId::BackgroundColor, Named("WHITE_SMOKE"));
		set(PropertyId::PaddingTop, 6.0);
		set(PropertyId::PaddingBottom, 6.0);
		set(PropertyId::PaddingLeft, 8.0);
		set(PropertyId::PaddingRight, 8.0);
		set(PropertyId::BorderWidth, 0.5);
		set(PropertyId::BorderColor, Named("GAINSBORO"));
		break;
	case ParagraphStyle::Note:
		set(PropertyId::BackgroundColor, Named("ALICE_BLUE"));
		set(PropertyId::BorderWidth, 1.0);
		set(PropertyId::BorderSides, border::kTop | border::kBottom);
		set(PropertyId::BorderColor, Named("COBALT_BLUE"));
		set(PropertyId::PaddingTop, 6.0);
		set(PropertyId::PaddingBottom, 6.0);
		set(PropertyId::PaddingLeft, 8.0);
		set(PropertyId::PaddingRight, 8.0);
		set(PropertyId::SpaceBefore, 6.0);
		set(PropertyId::SpaceAfter, 10.0);
		break;
	case ParagraphStyle::Caption:
		set(PropertyId::FontSize, 9.5);
		set(PropertyId::Bold, true);
		set(PropertyId::HAlign, int64_t(HorizontalAlign::Center));
		set(PropertyId::SpaceBefore, 4.0);
		set(PropertyId::SpaceAfter, 10.0);
		break;
	}
	return m;
}

} // namespace

std::string_view StyleName(ParagraphStyle style) noexcept { return kStyleNames[std::min(size_t(style), std::size(kStyleNames) - 1)]; }

// ── Styles ──

NodeId Editor::FindStyleGroup(ParagraphStyle style) const {
	if (style == ParagraphStyle::Normal)
		return kNoNode;
	for (NodeId g : m_document->AllGroups())
		if (m_document->Get(g, props::Name) == StyleName(style))
			return g;
	return kNoNode;
}

NodeId Editor::StyleGroup(ParagraphStyle style) {
	if (const NodeId g = FindStyleGroup(style); g != kNoNode || style == ParagraphStyle::Normal)
		return g;
	const NodeId g = Create(doc::CreateGroup(std::string(StyleName(style))));
	if (g == kNoNode)
		return kNoNode;
	for (const auto &[id, value] : StyleProperties(style))
		if (!m_history->Execute(doc::SetProperty(g, id, value)))
			return kNoNode;
	return g;
}

Status Editor::ApplyStyle(ParagraphStyle style) {
	const std::vector<NodeId> paragraphs = ParagraphTargets(); // copie : les commandes invalident le mémo
	if (paragraphs.empty())
		return Failure(ErrorCode::Nothing, "aucun paragraphe visé");
	return Transaction(std::format("Style « {} »", StyleName(style)), [&]() -> Status {
		const NodeId group = StyleGroup(style);
		if (style != ParagraphStyle::Normal && group == kNoNode)
			return Failure(ErrorCode::InvalidValue, "création du style impossible");
		const PropertyMap styleProps = StyleProperties(style);
		for (NodeId p : paragraphs) {
			// Un seul style par paragraphe ; la mise en forme directe du paragraphe que le style
			// définit est retirée (comme dans un traitement de texte), celle des segments reste.
			for (size_t s = 1; s < size_t(ParagraphStyle::Count); ++s)
				if (const NodeId other = FindStyleGroup(ParagraphStyle(s)); other != kNoNode && other != group && m_document->Find(p)->InGroup(other))
					if (auto r = m_history->Execute(doc::RemoveFromGroup(p, other)); !r)
						return r;
			for (const auto &[id, value] : styleProps)
				if (m_document->Find(p)->Properties().Contains(id))
					if (auto r = m_history->Execute(doc::ClearProperty(p, id)); !r)
						return r;
			if (style == ParagraphStyle::Normal) { // retour au corps de texte : plus de niveau de titre ni de légende
				for (PropertyId id : {PropertyId::OutlineLevel, PropertyId::CaptionKind})
					if (m_document->Find(p)->Properties().Contains(id))
						if (auto r = m_history->Execute(doc::ClearProperty(p, id)); !r)
							return r;
			} else if (!m_document->Find(p)->InGroup(group)) {
				if (auto r = m_history->Execute(doc::AddToGroup(p, group)); !r)
					return r;
			}
		}
		return Success();
	});
}

ParagraphStyle Editor::CurrentStyle() const {
	const std::vector<NodeId> &paragraphs = ParagraphTargets();
	if (paragraphs.empty())
		return ParagraphStyle::Normal;
	const Node *p = m_document->Find(paragraphs.front());
	for (size_t s = 1; s < size_t(ParagraphStyle::Count); ++s)
		if (const NodeId g = FindStyleGroup(ParagraphStyle(s)); g != kNoNode && p && p->InGroup(g))
			return ParagraphStyle(s);
	return ParagraphStyle::Normal;
}

// ── Listes et retraits ──

Status Editor::ToggleList(ListStyle style) {
	const std::vector<NodeId> paragraphs = ParagraphTargets();
	if (paragraphs.empty())
		return Failure(ErrorCode::Nothing, "aucun paragraphe visé");
	const bool all = std::ranges::all_of(paragraphs, [&](NodeId p) { return m_document->Get(p, props::ListStyle) == style; });
	return Transaction(all ? "Retirer la liste" : "Liste", [&]() -> Status {
		for (NodeId p : paragraphs)
			if (auto s = m_history->Execute(all ? doc::ClearProperty(p, PropertyId::ListStyle) : doc::SetProperty(p, props::ListStyle, style)); !s)
				return s;
		return Success();
	});
}

Status Editor::ChangeIndent(int delta) {
	const std::vector<NodeId> paragraphs = ParagraphTargets();
	if (paragraphs.empty())
		return Failure(ErrorCode::Nothing, "aucun paragraphe visé");
	return Transaction(delta > 0 ? "Augmenter le retrait" : "Diminuer le retrait", [&]() -> Status {
		for (NodeId p : paragraphs) {
			if (m_document->Get(p, props::ListStyle) != ListStyle::None) {
				const int64_t level = std::clamp<int64_t>(m_document->Get(p, props::ListLevel) + delta, 0, 8);
				if (auto s = m_history->Execute(doc::SetProperty(p, props::ListLevel, level)); !s)
					return s;
			} else {
				const double indent = std::max(0.0, m_document->Get(p, props::IndentLeft) + 18.0 * delta);
				if (auto s = m_history->Execute(doc::SetProperty(p, props::IndentLeft, indent)); !s)
					return s;
			}
		}
		return Success();
	});
}

Status Editor::SetHeadingNumbering(doc::HeadingNumbering numbering) { return Run(doc::SetProperty(m_document->Root(), props::HeadingNumbering, numbering)); }

doc::HeadingNumbering Editor::Numbering() const { return m_document->Get(m_document->Root(), props::HeadingNumbering); }

// ── Blocs insérés ──

NodeId Editor::InsertBlockParagraph(std::string_view text, size_t *position, NodeId *parent) {
	size_t at = 0;
	const NodeId container = parent && *parent != kNoNode ? *parent : InsertionPoint(at);
	if (position && parent && *parent != kNoNode)
		at = *position;
	return NewParagraph(container, at, text);
}

Status Editor::InsertStyled(ParagraphStyle style, std::string_view text) {
	NodeId t = kNoNode;
	auto s = Transaction(std::format("Insérer : {}", StyleName(style)), [&]() -> Status {
		t = InsertBlockParagraph(text);
		if (t == kNoNode)
			return Failure(ErrorCode::InvalidParent, "insertion impossible ici");
		const NodeId group = StyleGroup(style);
		if (group != kNoNode)
			return m_history->Execute(doc::AddToGroup(m_document->Find(t)->Parent(), group));
		return Success();
	});
	if (s)
		PlaceCaret({t, text.size()});
	return s;
}

Status Editor::InsertCaption(CaptionKind kind, std::string_view text) {
	// Cible : image / forme sélectionnée (légende dessous), tableau (dessus), sinon bloc courant.
	NodeId block = m_selection.Primary();
	for (NodeId id = block; id != kNoNode; id = m_document->Find(id) ? m_document->Find(id)->Parent() : kNoNode) {
		const Node *n = m_document->Find(id);
		if (!n)
			break;
		if (n->Type() == NodeType::Image || n->Type() == NodeType::Shape || n->Type() == NodeType::Table) {
			block = id;
			break;
		}
	}
	const Node *target = m_document->Find(block);
	const bool table = target && target->Type() == NodeType::Table;
	const bool figure = target && (target->Type() == NodeType::Image || target->Type() == NodeType::Shape);
	NodeId t = kNoNode;
	auto s = Transaction("Insérer une légende", [&]() -> Status {
		if (table || figure) {
			NodeId parent = target->Parent();
			size_t position = m_document->Find(parent)->IndexOfChild(block) + (table ? 0 : 1);
			t = InsertBlockParagraph(text, &position, &parent);
		} else {
			t = InsertBlockParagraph(text);
		}
		if (t == kNoNode)
			return Failure(ErrorCode::InvalidParent, "légende impossible ici");
		const NodeId p = m_document->Find(t)->Parent();
		if (auto r = m_history->Execute(doc::SetProperty(p, props::CaptionKind, kind)); !r)
			return r;
		// Libellé dans la langue de l'interface (« Table » en anglais) ; français : défaut du moteur.
		static constexpr std::string_view kWords[] = {"", "Figure", "Tableau", "Listing"};
		if (const std::string word = Tr(kWords[size_t(kind)]); word != kWords[size_t(kind)])
			if (auto r = m_history->Execute(doc::SetProperty(p, props::CaptionLabel, word)); !r)
				return r;
		if (table) { // une légende de tableau reste avec lui
			if (auto r = m_history->Execute(doc::SetProperty(p, props::KeepWithNext, true)); !r)
				return r;
		} else if (const Node *parent = m_document->Find(m_document->Find(p)->Parent()); parent) {
			// Légende sous son contenu (figure, code…) : le bloc légendé reste avec elle.
			const size_t index = parent->IndexOfChild(p);
			if (index > 0)
				if (auto r = m_history->Execute(doc::SetProperty(parent->Children()[index - 1], props::KeepWithNext, true)); !r)
					return r;
		}
		const NodeId group = StyleGroup(ParagraphStyle::Caption);
		return group != kNoNode ? m_history->Execute(doc::AddToGroup(p, group)) : Success();
	});
	if (s)
		PlaceCaret({t, text.size()});
	return s;
}

Status Editor::InsertTableOfContents(TocContent content, std::string_view title) {
	static constexpr std::string_view kTitles[] = {"Table des matières", "Table des figures", "Liste des tableaux", "Liste des listings"};
	const std::string heading = title.empty() ? Tr(kTitles[size_t(content)]) : std::string(title); // titre dans la langue de l'interface
	NodeId toc = kNoNode;
	auto s = Transaction(std::string(heading), [&]() -> Status {
		size_t position = 0;
		const NodeId parent = InsertionPoint(position);
		const NodeId t = NewParagraph(parent, position, heading);
		if (t == kNoNode)
			return Failure(ErrorCode::InvalidParent, "table des matières impossible ici");
		const NodeId titleParagraph = m_document->Find(t)->Parent();
		for (auto [id, value] : {std::pair{PropertyId::FontSize, PropertyValue(18.0)}, std::pair{PropertyId::Bold, PropertyValue(true)},
								 std::pair{PropertyId::SpaceBefore, PropertyValue(12.0)}, std::pair{PropertyId::SpaceAfter, PropertyValue(10.0)},
								 std::pair{PropertyId::KeepWithNext, PropertyValue(true)}})
			if (auto r = m_history->Execute(doc::SetProperty(titleParagraph, id, value)); !r)
				return r;
		toc = Create(doc::CreateNode(NodeType::TableOfContents, parent, position == kAppend ? kAppend : position + 1));
		if (toc == kNoNode)
			return Failure(ErrorCode::InvalidParent, "table des matières impossible ici");
		if (content != TocContent::Headings)
			if (auto r = m_history->Execute(doc::SetProperty(toc, props::TocContent, content)); !r)
				return r;
		return m_history->Execute(doc::SetProperty(toc, props::SpaceAfter, 12.0));
	});
	if (s) {
		LeaveText();
		m_selection.Select(*m_document, toc);
	}
	return s;
}

// ── Liens et renvois ──

Status Editor::SetLink(std::string link) {
	if (link.empty())
		return RemoveLink();
	const Color color = Named("COBALT_BLUE");
	return Transaction("Lien", [&]() -> Status {
		for (auto [id, value] : {std::pair{PropertyId::Link, PropertyValue(link)}, std::pair{PropertyId::TextColor, PropertyValue(color)},
								 std::pair{PropertyId::Underline, PropertyValue(true)}})
			if (auto s = SetText(id, value); !s)
				return s;
		return Success();
	});
}

Status Editor::RemoveLink() {
	std::vector<NodeId> targets = TextTargets();
	std::erase_if(targets, [&](NodeId t) { return !m_document->Find(t)->Properties().Contains(PropertyId::Link); });
	if (targets.empty())
		return Failure(ErrorCode::Nothing, "aucun lien visé");
	return Transaction("Retirer le lien", [&]() -> Status {
		for (NodeId t : targets)
			for (PropertyId id : {PropertyId::Link, PropertyId::TextColor, PropertyId::Underline})
				if (m_document->Find(t)->Properties().Contains(id))
					if (auto s = m_history->Execute(doc::ClearProperty(t, id)); !s)
						return s;
		return Success();
	});
}

Status Editor::InsertReference(NodeId target, FieldKind kind) {
	if (!Editing())
		return Failure(ErrorCode::Nothing, "placer le curseur dans le texte");
	if (kind != FieldKind::Reference && kind != FieldKind::PageReference)
		return Failure(ErrorCode::InvalidValue, "renvoi attendu");
	if (!m_document->Contains(target))
		return Failure(ErrorCode::NotFound, "cible du renvoi introuvable");
	TextFragment fragment;
	TextFragment::Run run;
	run.properties.Set(PropertyId::Field, int64_t(kind));
	run.properties.Set(PropertyId::Link, InternalLink(target));
	run.properties.Set(PropertyId::TextColor, Named("COBALT_BLUE"));
	fragment.paragraphs.push_back({{}, {std::move(run)}});
	m_history->BeginTransaction("Insérer un renvoi");
	TextPosition at = m_caret;
	if (HasTextRange()) {
		auto erased = text::Erase(*m_history, m_anchor, m_caret);
		if (!erased) {
			(void)m_history->Rollback();
			return Err(erased.Error());
		}
		at = erased.Value();
	}
	auto pasted = text::Paste(*m_history, at, fragment);
	if (!pasted) {
		(void)m_history->Rollback();
		Changed();
		return Err(pasted.Error());
	}
	(void)m_history->Commit();
	Changed();
	PlaceCaret(pasted.Value());
	return Success();
}

// ── Navigation ──

const Outline &Editor::DocumentOutline() const {
	if (m_outlineRevision != m_document->Revision() || m_outlineStamp != m_document->Stamp()) {
		m_outline = BuildOutline(*m_document);
		m_outlineRevision = m_document->Revision();
		m_outlineStamp = m_document->Stamp();
	}
	return m_outline;
}

bool Editor::GoTo(NodeId node) {
	const Node *n = m_document->Find(node);
	if (!n)
		return false;
	if (n->Type() == NodeType::Paragraph)
		if (const NodeId t = Order().FirstText(node); t != kNoNode) {
			PlaceCaret({t, 0});
			return true;
		}
	if (n->Type() == NodeType::Text && Order().Contains(node)) {
		PlaceCaret({node, 0});
		return true;
	}
	SelectNode(node);
	return true;
}

std::string Editor::LinkAt(size_t page, double x, double y) const {
	const Presentation *p = Current();
	if (!p)
		return {};
	const GlyphRun *run = p->layout.RunAt(page, x, y);
	// Entrée d'une table des matières non interactive (Hyperlinks faux) : pas de lien.
	return run ? run->style.link : std::string();
}

} // namespace scribe
