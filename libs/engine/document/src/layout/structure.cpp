// Structure du document : titres numérotés, légendes, listes, renvois.
#include "document/outline.hpp"

#include "internal.hpp"

#include <array>
#include <charconv>
#include <format>

namespace doc {

namespace {

constexpr std::string_view kNbsp = " ";

std::string_view DefaultCaptionLabel(CaptionKind kind) {
	switch (kind) {
	case CaptionKind::Figure: return "Figure";
	case CaptionKind::Table: return "Tableau";
	case CaptionKind::Listing: return "Listing";
	case CaptionKind::None: break;
	}
	return {};
}

OutlineKind KindOf(CaptionKind kind) {
	switch (kind) {
	case CaptionKind::Table: return OutlineKind::Table;
	case CaptionKind::Listing: return OutlineKind::Listing;
	default: return OutlineKind::Figure;
	}
}

detail::OutlineItem Mark(detail::OutlineItem::Kind kind, NodeId node) {
	detail::OutlineItem item;
	item.kind = kind;
	item.node = node;
	return item;
}

/// Éléments d'un bloc : lecture des propriétés, sans numérotation (mise en cache par bloc).
class Collector {
public:
	Collector(const DocumentView &view, PropertyCache &cache) : m_view(view), m_cache(cache) {}

	detail::OutlineItems Run(NodeId block) {
		Block(block);
		return std::move(m_items);
	}

private:
	void Block(NodeId id) {
		const Node *n = m_view.Find(id);
		if (!n || !TypeInfo(n->Type()).flowBlock || !m_cache.Get(m_view, id, props::Visible))
			return;
		switch (n->Type()) {
		case NodeType::Paragraph: Paragraph(*n); break;
		case NodeType::Table:
			m_items.push_back(Mark(detail::OutlineItem::Table, id));
			for (NodeId r : n->Children())
				if (const Node *row = m_view.Find(r); row && m_cache.Get(m_view, r, props::Visible))
					for (NodeId cell : row->Children()) {
						const Node *c = m_view.Find(cell);
						if (!c || !m_cache.Get(m_view, cell, props::Visible))
							continue;
						m_items.push_back(Mark(detail::OutlineItem::Enter, cell));
						for (NodeId child : c->Children())
							Block(child);
						m_items.push_back(Mark(detail::OutlineItem::Leave, cell));
					}
			break;
		case NodeType::TableOfContents: m_items.push_back(Mark(detail::OutlineItem::Dependent, id)); break;
		default: m_items.push_back(Mark(detail::OutlineItem::Break, id)); break;
		}
	}

	void Paragraph(const Node &p) {
		detail::OutlineItem item;
		item.node = p.Id();
		item.level = m_cache.Get(m_view, p.Id(), props::OutlineLevel);
		item.caption = m_cache.Get(m_view, p.Id(), props::CaptionKind);
		item.list = m_cache.Get(m_view, p.Id(), props::ListStyle);
		if (item.caption != CaptionKind::None)
			item.label = m_cache.Get(m_view, p.Id(), props::CaptionLabel);
		if (item.list != ListStyle::None) {
			item.listLevel = m_cache.Get(m_view, p.Id(), props::ListLevel);
			item.listStart = m_cache.Get(m_view, p.Id(), props::ListStart);
		}
		const bool entry = item.level > 0 || item.caption != CaptionKind::None;
		for (NodeId c : p.Children()) {
			const Node *t = m_view.Find(c);
			if (!t || t->Type() != NodeType::Text)
				continue;
			// Field et Text sont locaux (ni hérités ni portés par un groupe) : lecture directe.
			const PropertyValue *field = t->Properties().Find(PropertyId::Field);
			const auto kind = field ? FieldKind(std::get<int64_t>(*field)) : FieldKind::None;
			if (kind == FieldKind::Reference || kind == FieldKind::PageReference)
				item.depends = true;
			if (entry && kind == FieldKind::None)
				if (const PropertyValue *text = t->Properties().Find(PropertyId::Text))
					item.text += std::get<std::string>(*text);
		}
		m_items.push_back(std::move(item));
	}

	const DocumentView &m_view;
	PropertyCache &m_cache;
	detail::OutlineItems m_items;
};

/// Numérotation : titres (par section), légendes (par type), listes (par conteneur).
class Assembler {
public:
	explicit Assembler(Outline &out) : m_out(out) {}

	void Section(HeadingNumbering numbering) {
		m_numbering = numbering;
		m_lists.assign(1, {});
		m_open.clear();
	}

	void Items(const detail::OutlineItems &items) {
		for (const detail::OutlineItem &item : items)
			switch (item.kind) {
			case detail::OutlineItem::Paragraph: Paragraph(item); break;
			case detail::OutlineItem::Enter:
				m_lists.emplace_back();
				break;
			case detail::OutlineItem::Leave:
				if (m_lists.size() > 1)
					m_lists.pop_back();
				break;
			case detail::OutlineItem::Table:
				m_lists.back().fill(0);
				m_open.push_back(item.node);
				break;
			case detail::OutlineItem::Break: m_lists.back().fill(0); break;
			case detail::OutlineItem::Dependent:
				m_lists.back().fill(0);
				Depend(item.node);
				break;
			}
		m_open.clear(); // fin du bloc du corps
	}

private:
	void Depend(NodeId id) {
		m_out.dependents.insert(id);
		for (NodeId t : m_open)
			m_out.dependents.insert(t);
	}

	void Paragraph(const detail::OutlineItem &item) {
		std::array<int64_t, 9> &list = m_lists.back();
		const NodeId id = item.node;
		if (item.depends)
			Depend(id);
		ParagraphPrefix prefix;
		if (item.level > 0) {
			list.fill(0);
			const size_t l = size_t(item.level);
			++m_headings[l - 1];
			for (size_t k = l; k < m_headings.size(); ++k)
				m_headings[k] = 0;
			OutlineEntry e{id, OutlineKind::Heading, int(item.level), {}, {}, item.text};
			if (m_numbering != HeadingNumbering::None) {
				for (size_t k = 0; k < l; ++k)
					e.number += (k ? "." : "") + std::to_string(m_headings[k]);
				e.label = e.number + (m_numbering == HeadingNumbering::DecimalDot ? "." : "");
				prefix.text = e.label;
			}
			m_out.entries.push_back(std::move(e));
		} else if (item.caption != CaptionKind::None) {
			list.fill(0);
			const int64_t n = ++m_captions[size_t(item.caption)];
			const std::string_view word = item.label.empty() ? DefaultCaptionLabel(item.caption) : std::string_view(item.label);
			OutlineEntry e{id, KindOf(item.caption), 1, std::to_string(n), std::format("{}{}{}", word, kNbsp, n), item.text};
			prefix.text = e.label + std::string(kNbsp) + ":";
			m_out.entries.push_back(std::move(e));
		} else if (item.list != ListStyle::None) {
			const size_t l = size_t(item.listLevel);
			list[l] = item.listStart > 0 ? item.listStart : list[l] + 1;
			for (size_t k = l + 1; k < list.size(); ++k)
				list[k] = 0;
			prefix.text = ListMarker(item.list, int(l), list[l]);
			prefix.marker = true;
			prefix.listLevel = int(l);
		} else {
			list.fill(0);
		}
		if (prefix.text.empty())
			return;
		const std::string key = (prefix.marker ? "m" : "p") + std::to_string(prefix.listLevel) + ":" + prefix.text + "\x1f";
		m_out.contexts[id] += key;
		for (NodeId t : m_open)
			m_out.contexts[t] += key;
		m_out.prefixes.emplace(id, std::move(prefix));
	}

	Outline &m_out;
	HeadingNumbering m_numbering = HeadingNumbering::None;
	std::array<int64_t, 9> m_headings{};
	std::array<int64_t, 4> m_captions{};
	std::vector<std::array<int64_t, 9>> m_lists{1}; ///< compteurs des listes par conteneur ouvert
	std::vector<NodeId> m_open;						 ///< tableau du bloc en cours (renvois et préfixes internes)
};

} // namespace

const OutlineEntry *Outline::Find(NodeId node) const {
	for (const OutlineEntry &e : entries)
		if (e.node == node)
			return &e;
	return nullptr;
}

const ParagraphPrefix *Outline::Prefix(NodeId paragraph) const {
	const auto it = prefixes.find(paragraph);
	return it == prefixes.end() ? nullptr : &it->second;
}

std::string Outline::Reference(NodeId target) const {
	const OutlineEntry *e = Find(target);
	if (!e)
		return "?";
	if (e->kind == OutlineKind::Heading)
		return e->number.empty() ? e->text : e->number;
	return e->label;
}

std::vector<const OutlineEntry *> Outline::Entries(TocContent content, int depth) const {
	std::vector<const OutlineEntry *> out;
	for (const OutlineEntry &e : entries) {
		bool keep = false;
		switch (content) {
		case TocContent::Headings: keep = e.kind == OutlineKind::Heading && e.level <= depth; break;
		case TocContent::Figures: keep = e.kind == OutlineKind::Figure; break;
		case TocContent::Tables: keep = e.kind == OutlineKind::Table; break;
		case TocContent::Listings: keep = e.kind == OutlineKind::Listing; break;
		}
		if (keep)
			out.push_back(&e);
	}
	return out;
}

namespace detail {

OutlineItems CollectOutline(const DocumentView &view, PropertyCache &cache, NodeId block) { return Collector(view, cache).Run(block); }

Outline AssembleOutline(const DocumentView &view, PropertyCache &cache, const std::function<const OutlineItems &(NodeId block)> &items) {
	Outline out;
	Assembler a(out);
	for (NodeId section : view.Sections()) {
		a.Section(cache.Get(view, section, props::HeadingNumbering));
		if (const Node *body = view.Find(view.Body(section)))
			for (NodeId block : body->Children())
				a.Items(items(block));
	}
	return out;
}

} // namespace detail

Outline BuildOutline(const DocumentView &view, PropertyCache &cache) {
	detail::OutlineItems scratch;
	return detail::AssembleOutline(view, cache, [&](NodeId block) -> const detail::OutlineItems & {
		scratch = detail::CollectOutline(view, cache, block);
		return scratch;
	});
}

Outline BuildOutline(const DocumentView &view) {
	PropertyCache cache;
	return BuildOutline(view, cache);
}

NodeId LinkTarget(std::string_view link) noexcept {
	if (link.size() < 2 || link.front() != '#')
		return kNoNode;
	NodeId id = 0;
	const auto [ptr, ec] = std::from_chars(link.data() + 1, link.data() + link.size(), id);
	return ec == std::errc{} && ptr == link.data() + link.size() ? id : kNoNode;
}

std::string InternalLink(NodeId target) { return "#" + std::to_string(target); }

std::string Roman(int64_t n, bool upper) {
	if (n <= 0 || n >= 4000)
		return std::to_string(n);
	static constexpr std::pair<int, std::string_view> kTable[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
																  {40, "xl"},  {10, "x"},	{9, "ix"},	{5, "v"},	{4, "iv"},	{1, "i"}};
	std::string out;
	for (const auto &[value, digits] : kTable)
		while (n >= value) {
			out += digits;
			n -= value;
		}
	if (upper)
		for (char &c : out)
			c = char(c - 'a' + 'A');
	return out;
}

std::string ListMarker(ListStyle style, int level, int64_t number) {
	const auto alpha = [](int64_t n, char base) {
		std::string s;
		for (; n > 0; n = (n - 1) / 26)
			s.insert(s.begin(), char(base + (n - 1) % 26));
		return s.empty() ? std::string(1, base) : s;
	};
	switch (style) {
	case ListStyle::None: return {};
	case ListStyle::Bullet: return level % 2 == 0 ? "•" : "–"; // • puis –
	case ListStyle::Dash: return "–";
	case ListStyle::EmDash: return "—";
	case ListStyle::Chevron: return "»";
	case ListStyle::Decimal: return std::to_string(number) + ".";
	case ListStyle::DecimalParen: return std::to_string(number) + ")";
	case ListStyle::LowerAlpha: return alpha(number, 'a') + ".";
	case ListStyle::UpperAlpha: return alpha(number, 'A') + ".";
	case ListStyle::LowerRoman: return Roman(number, false) + ".";
	case ListStyle::UpperRoman: return Roman(number, true) + ".";
	}
	return {};
}

} // namespace doc
