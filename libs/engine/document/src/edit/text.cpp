// Édition du texte : ordre de lecture, géométrie du curseur, modifications annulables.
#include "document/text.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include <core/string.hpp>

namespace doc {

namespace {

bool IsWordChar(char32_t c) {
	return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_' || c == U'\'' || c == U'’' || c >= 0xC0;
}

size_t NextBoundary(std::string_view s, size_t i) {
	if (i >= s.size())
		return s.size();
	(void)unicode::DecodeUtf8(s, i);
	return i;
}

size_t PreviousBoundary(std::string_view s, size_t i) {
	if (i == 0)
		return 0;
	--i;
	while (i > 0 && (uint8_t(s[i]) & 0xC0) == 0x80)
		--i;
	return i;
}

std::string TextOfNode(const DocumentView &view, NodeId id) {
	const auto *v = view.Explicit(id, PropertyId::Text);
	return v ? std::get<std::string>(*v) : std::string();
}

bool IsField(const DocumentView &view, NodeId id) {
	const auto *v = view.Explicit(id, PropertyId::Field);
	return v && std::get<int64_t>(*v) != int64_t(FieldKind::None);
}

PropertyMap RunProperties(const DocumentView &view, NodeId id) {
	PropertyMap out;
	for (const auto &[p, v] : view.Find(id)->Properties())
		if (p != PropertyId::Text && p != PropertyId::Name)
			out.Set(p, v);
	return out;
}

PropertyMap ParagraphProperties(const DocumentView &view, NodeId id) {
	PropertyMap out;
	for (const auto &[p, v] : view.Find(id)->Properties())
		if (p != PropertyId::Name && p != PropertyId::PageBreakBefore)
			out.Set(p, v);
	return out;
}

} // namespace

// ── TextOrder ──

TextOrder::TextOrder(const DocumentView &view) : m_view(&view) {
	view.Visit(view.Root(), [&](const Node &n) {
		if (n.Type() != NodeType::Text)
			return;
		const Node *paragraph = view.Find(n.Parent());
		if (!paragraph || paragraph->Type() != NodeType::Paragraph)
			return;
		NodeId story = kNoNode;
		for (const Node *a = paragraph; a; a = view.Find(a->Parent()))
			if (a->Type() == NodeType::Body || a->Type() == NodeType::Header || a->Type() == NodeType::Footer || a->Type() == NodeType::Page ||
				a->Type() == NodeType::Shape) { // texte d'une forme : son propre fil
				story = a->Id();
				break;
			}
		const bool field = IsField(view, n.Id());
		m_index.InsertOrAssign(n.Id(), m_entries.size());
		m_entries.push_back({n.Id(), paragraph->Id(), story, field ? 0 : TextOfNode(view, n.Id()).size(), field});
	});
}

const TextOrder::Entry *TextOrder::Find(NodeId text) const noexcept {
	const size_t *i = m_index.Find(text);
	return i ? &m_entries[*i] : nullptr;
}

std::string_view TextOrder::TextOf(const Entry &e) const {
	if (e.field)
		return {};
	const auto *v = m_view->Explicit(e.text, PropertyId::Text);
	return v ? std::string_view(std::get<std::string>(*v)) : std::string_view();
}

int TextOrder::Compare(const TextPosition &a, const TextPosition &b) const noexcept {
	const size_t *ia = m_index.Find(a.text), *ib = m_index.Find(b.text);
	const size_t xa = ia ? *ia : SIZE_MAX, xb = ib ? *ib : SIZE_MAX;
	if (xa != xb)
		return xa < xb ? -1 : 1;
	return a.offset < b.offset ? -1 : a.offset > b.offset ? 1 : 0;
}

TextPosition TextOrder::Normalize(TextPosition p) const {
	const Entry *e = Find(p.text);
	if (!e)
		return {};
	if (e->field)
		return {p.text, 0};
	const std::string_view s = TextOf(*e);
	p.offset = std::min(p.offset, s.size());
	while (p.offset > 0 && p.offset < s.size() && (uint8_t(s[p.offset]) & 0xC0) == 0x80)
		--p.offset;
	return p;
}

TextPosition TextOrder::Next(TextPosition p) const {
	p = Normalize(p);
	const Entry *e = Find(p.text);
	if (!e)
		return p;
	if (!e->field && p.offset < e->length)
		return {p.text, NextBoundary(TextOf(*e), p.offset)};
	// Segment suivant du même récit : même paragraphe, sa fin vaut le début du suivant.
	for (size_t i = *m_index.Find(p.text) + 1; i < m_entries.size(); ++i) {
		const Entry &n = m_entries[i];
		if (n.story != e->story)
			break;
		if (n.paragraph != e->paragraph)
			return {n.text, 0}; // saut de paragraphe : un pas
		if (n.field)
			return {n.text, 0};
		if (n.length > 0)
			return {n.text, NextBoundary(TextOf(n), 0)};
	}
	return p;
}

TextPosition TextOrder::Previous(TextPosition p) const {
	p = Normalize(p);
	const Entry *e = Find(p.text);
	if (!e)
		return p;
	if (!e->field && p.offset > 0)
		return {p.text, PreviousBoundary(TextOf(*e), p.offset)};
	for (size_t i = *m_index.Find(p.text); i-- > 0;) {
		const Entry &n = m_entries[i];
		if (n.story != e->story)
			break;
		if (n.paragraph != e->paragraph)
			return {n.text, n.length}; // fin du paragraphe précédent
		if (n.field)
			return {n.text, 0};
		if (n.length > 0)
			return {n.text, PreviousBoundary(TextOf(n), n.length)};
	}
	return p;
}

namespace {

/// Caractère juste après / avant une position, dans son paragraphe (0 : bord ou champ).
char32_t CharAfter(const TextOrder &order, const DocumentView &view, TextPosition p) {
	const TextPosition n = order.Next(p);
	if (n == p)
		return 0;
	if (n.text == p.text) {
		const std::string s = TextOfNode(view, p.text);
		size_t i = p.offset;
		return unicode::DecodeUtf8(s, i);
	}
	if (view.Find(n.text)->Parent() != view.Find(p.text)->Parent() || IsField(view, n.text))
		return 0;
	const std::string s = TextOfNode(view, n.text);
	size_t i = 0;
	return s.empty() ? 0 : unicode::DecodeUtf8(s, i);
}

char32_t CharBefore(const TextOrder &order, const DocumentView &view, TextPosition p) {
	const TextPosition b = order.Previous(p);
	if (b == p || view.Find(b.text)->Parent() != view.Find(p.text)->Parent() || IsField(view, b.text))
		return 0;
	const std::string s = TextOfNode(view, b.text);
	size_t i = b.offset;
	return i < s.size() ? unicode::DecodeUtf8(s, i) : 0;
}

} // namespace

TextPosition TextOrder::NextWord(TextPosition p) const {
	p = Normalize(p);
	if (CharAfter(*this, *m_view, p) == 0)
		return Next(p);
	while (IsWordChar(CharAfter(*this, *m_view, p)))
		p = Next(p);
	for (char32_t c; (c = CharAfter(*this, *m_view, p)) != 0 && !IsWordChar(c);)
		p = Next(p);
	return p;
}

TextPosition TextOrder::PreviousWord(TextPosition p) const {
	p = Normalize(p);
	if (CharBefore(*this, *m_view, p) == 0)
		return Previous(p);
	for (char32_t c; (c = CharBefore(*this, *m_view, p)) != 0 && !IsWordChar(c);)
		p = Previous(p);
	while (IsWordChar(CharBefore(*this, *m_view, p)))
		p = Previous(p);
	return p;
}

std::pair<TextPosition, TextPosition> TextOrder::WordAt(TextPosition p) const {
	p = Normalize(p);
	TextPosition a = p, b = p;
	while (IsWordChar(CharBefore(*this, *m_view, a)))
		a = Previous(a);
	while (IsWordChar(CharAfter(*this, *m_view, b)))
		b = Next(b);
	return {a, b};
}

std::pair<TextPosition, TextPosition> TextOrder::ParagraphRange(TextPosition p) const {
	const Entry *e = Find(p.text);
	if (!e)
		return {p, p};
	TextPosition a{}, b{};
	for (const Entry &x : m_entries)
		if (x.paragraph == e->paragraph) {
			if (!a.Valid())
				a = {x.text, 0};
			b = {x.text, x.length};
		}
	return {a, b};
}

std::pair<TextPosition, TextPosition> TextOrder::StoryRange(TextPosition p) const {
	const Entry *e = Find(p.text);
	if (!e)
		return {p, p};
	TextPosition a{}, b{};
	for (const Entry &x : m_entries)
		if (x.story == e->story) {
			if (!a.Valid())
				a = {x.text, 0};
			b = {x.text, x.length};
		}
	return {a, b};
}

NodeId TextOrder::FirstText(NodeId paragraph) const {
	for (const Entry &x : m_entries)
		if (x.paragraph == paragraph)
			return x.text;
	return kNoNode;
}

// ── Fragments ──

bool TextFragment::Empty() const noexcept {
	for (const Paragraph &p : paragraphs)
		for (const Run &r : p.runs)
			if (!r.text.empty() || r.properties.Contains(PropertyId::Field))
				return false;
	return paragraphs.size() <= 1;
}

std::string TextFragment::PlainText() const {
	std::string out;
	for (size_t i = 0; i < paragraphs.size(); ++i) {
		if (i)
			out.push_back('\n');
		for (const Run &r : paragraphs[i].runs)
			out += r.text;
	}
	return out;
}

TextFragment TextFragment::FromPlainText(std::string_view text) {
	TextFragment f;
	size_t start = 0;
	for (;;) {
		size_t end = text.find('\n', start);
		std::string_view line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		Paragraph p;
		if (!line.empty())
			p.runs.push_back({{}, std::string(line)});
		f.paragraphs.push_back(std::move(p));
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return f;
}

// ── Géométrie ──

namespace text {

namespace {

struct RunRef {
	size_t page;
	const PlacedLine *line;
	const GlyphRun *run;
};

double XAt(const GlyphRun &run, size_t offset) {
	const size_t local = std::clamp(offset, run.offset, run.offset + run.text.size()) - run.offset;
	return run.x + run.style.font.TextWidth(std::string_view(run.text).substr(0, local), run.style.size);
}

/// Segment affiché qui porte la position (le dernier dont le début la précède).
std::optional<RunRef> RunOf(const LayoutResult &layout, TextPosition p) {
	std::optional<RunRef> best;
	for (size_t page = 0; page < layout.pages.size(); ++page)
		for (const PlacedLine &line : layout.pages[page].lines)
			for (const GlyphRun &run : line.runs)
				if (run.node == p.text && (run.field != FieldKind::None || run.offset <= p.offset)) {
					if (!best || run.offset >= best->run->offset)
						best = RunRef{page, &line, &run};
				}
	return best;
}

/// Décalage le plus proche de l'abscisse `x` dans un segment.
size_t OffsetAt(const GlyphRun &run, double x) {
	if (run.field != FieldKind::None)
		return 0;
	double pen = run.x;
	size_t best = run.offset;
	double bestDist = std::abs(x - pen);
	for (size_t i = 0; i < run.text.size();) {
		const size_t start = i;
		(void)unicode::DecodeUtf8(run.text, i);
		pen += run.style.font.TextWidth(std::string_view(run.text).substr(start, i - start), run.style.size);
		if (const double d = std::abs(x - pen); d < bestDist) {
			bestDist = d;
			best = run.offset + i;
		}
	}
	return best;
}

NodeId FirstTextChild(const DocumentView &view, NodeId paragraph) {
	if (const Node *p = view.Find(paragraph))
		for (NodeId c : p->Children())
			if (view.Find(c)->Type() == NodeType::Text)
				return c;
	return kNoNode;
}

/// Position juste après un saut de ligne (« \n ») : début de la ligne suivante du paragraphe
/// (la ligne qui suit celle du texte avant le saut ; vide si le saut termine le paragraphe).
std::optional<CaretBox> CaretAfterBreak(const DocumentView &view, const LayoutResult &layout, TextPosition p) {
	const std::string text = TextOfNode(view, p.text);
	if (p.offset == 0 || p.offset > text.size() || text[p.offset - 1] != '\n')
		return std::nullopt;
	const Node *n = view.Find(p.text);
	if (!n)
		return std::nullopt;
	for (size_t page = 0; page < layout.pages.size(); ++page) {
		const auto &lines = layout.pages[page].lines;
		for (size_t i = 0; i < lines.size(); ++i) {
			// Ligne du texte avant le saut : un segment de ce nœud qui commence avant lui.
			const bool before = std::ranges::any_of(lines[i].runs, [&](const GlyphRun &r) {
				return r.node == p.text && !r.generated && r.offset < p.offset && r.offset + r.text.size() <= p.offset - 1;
			});
			if (!before)
				continue;
			// Ligne suivante du même paragraphe (même page, ou première de la page suivante).
			const PlacedLine *next = nullptr;
			size_t nextPage = page;
			for (size_t j = i + 1; j < lines.size() && !next; ++j)
				if (lines[j].paragraph == n->Parent())
					next = &lines[j];
			if (!next && page + 1 < layout.pages.size())
				for (const PlacedLine &l : layout.pages[page + 1].lines)
					if (l.paragraph == n->Parent()) {
						next = &l, nextPage = page + 1;
						break;
					}
			if (!next)
				continue;
			// Le segment qui suit le saut dans ce nœud, s'il est sur cette ligne.
			double x = next->runs.empty() ? next->rect.x : next->runs.front().x;
			for (const GlyphRun &r : next->runs)
				if (r.node == p.text && r.offset >= p.offset) {
					x = r.x;
					break;
				}
			return CaretBox{nextPage, Rect(x, next->rect.y, 0, next->rect.h), next->rotation, next->pivotX, next->pivotY};
		}
	}
	return std::nullopt;
}

std::optional<TextPosition> PositionInLine(const DocumentView &view, const PlacedLine &line, double x) {
	const GlyphRun *best = nullptr;
	double bestDist = 1e300;
	for (const GlyphRun &run : line.runs) {
		if (run.generated) // numéro, puce, entrée de table des matières : pas une position de texte
			continue;
		const double d = x < run.x ? run.x - x : x > run.x + run.width ? x - run.x - run.width : 0;
		if (d < bestDist) {
			bestDist = d;
			best = &run;
		}
	}
	if (!best) { // ligne vide : après le saut de ligne final, sinon début du paragraphe
		if (const Node *para = view.Find(line.paragraph))
			for (auto it = para->Children().rbegin(); it != para->Children().rend(); ++it)
				if (const Node *t = view.Find(*it); t && t->Type() == NodeType::Text) {
					const std::string text = TextOfNode(view, *it);
					if (!text.empty() && text.back() == '\n')
						return TextPosition{*it, text.size()};
					break;
				}
		const NodeId t = FirstTextChild(view, line.paragraph);
		if (t == kNoNode)
			return std::nullopt;
		return TextPosition{t, 0};
	}
	if (best->field != FieldKind::None) // un champ : avant ou après lui
		return TextPosition{best->node, 0};
	return TextPosition{best->node, OffsetAt(*best, x)};
}

} // namespace

std::optional<CaretBox> Caret(const DocumentView &view, const LayoutResult &layout, TextPosition p) {
	if (const auto broken = CaretAfterBreak(view, layout, p))
		return broken;
	if (const auto ref = RunOf(layout, p)) {
		const double x = ref->run->field != FieldKind::None ? ref->run->x : XAt(*ref->run, p.offset);
		return CaretBox{ref->page, Rect(x, ref->line->rect.y, 0, ref->line->rect.h), ref->line->rotation, ref->line->pivotX, ref->line->pivotY};
	}
	// Segment vide (ou absent de la mise en page) : première ligne de son paragraphe.
	const Node *n = view.Find(p.text);
	if (!n)
		return std::nullopt;
	for (size_t page = 0; page < layout.pages.size(); ++page)
		for (const PlacedLine &line : layout.pages[page].lines)
			if (line.paragraph == n->Parent()) {
				double x = line.rect.x;
				for (const GlyphRun &run : line.runs) // segment vide entre deux autres : après le précédent (et après un préfixe)
					if (run.generated ? run.x >= line.rect.x - 1e-6
									  : view.Find(run.node) && view.Find(n->Parent())->IndexOfChild(run.node) < view.Find(n->Parent())->IndexOfChild(p.text))
						x = std::max(x, run.x + run.width);
				return CaretBox{page, Rect(x, line.rect.y, 0, line.rect.h), line.rotation, line.pivotX, line.pivotY};
			}
	return std::nullopt;
}

std::optional<TextPosition> PositionAt(const DocumentView &view, const LayoutResult &layout, size_t page, double x, double y) {
	if (page >= layout.pages.size())
		return std::nullopt;
	const PlacedLine *best = nullptr;
	double bestDist = 1e300, bestX = x;
	for (const PlacedLine &line : layout.pages[page].lines) {
		const auto [lx, ly] = line.Unrotate(x, y); // texte d'une forme tournée : repère de la ligne
		const double dy = ly < line.rect.y ? line.rect.y - ly : ly > line.rect.Bottom() ? ly - line.rect.Bottom() : 0;
		// À distance verticale égale (même ligne de tableau), la colonne la plus proche.
		const double dx = lx < line.rect.x ? line.rect.x - lx : lx > line.rect.Right() ? lx - line.rect.Right() : 0;
		const double d = dy * 1000 + dx;
		if (d < bestDist) {
			bestDist = d;
			best = &line;
			bestX = lx;
		}
	}
	if (!best)
		return std::nullopt;
	return PositionInLine(view, *best, bestX);
}

std::optional<TextPosition> LineMove(const DocumentView &view, const LayoutResult &layout, TextPosition p, int direction, double x) {
	const auto caret = Caret(view, layout, p);
	if (!caret)
		return std::nullopt;
	const Node *n = view.Find(p.text);
	const auto storyOf = [&](NodeId paragraph) {
		for (const Node *a = view.Find(paragraph); a; a = view.Find(a->Parent()))
			if (a->Type() == NodeType::Body || a->Type() == NodeType::Header || a->Type() == NodeType::Footer || a->Type() == NodeType::Page ||
				a->Type() == NodeType::Shape)
				return a->Id();
		return kNoNode;
	};
	const NodeId story = n ? storyOf(n->Parent()) : kNoNode;
	const double y = caret->rect.y + caret->rect.h / 2;
	// Pages parcourues dans le sens du déplacement, à partir de celle du curseur.
	for (long page = long(caret->page); page >= 0 && page < long(layout.pages.size()); page += direction) {
		const PlacedLine *best = nullptr;
		double bestScore = 1e300;
		for (const PlacedLine &line : layout.pages[size_t(page)].lines) {
			const double cy = line.rect.y + line.rect.h / 2;
			const bool beyond = page != long(caret->page) || (direction > 0 ? cy > y + 0.5 : cy < y - 0.5);
			if (!beyond || storyOf(line.paragraph) != story)
				continue;
			const double vertical = page != long(caret->page) ? (direction > 0 ? cy : -cy) : std::abs(cy - y);
			const double horizontal = x < line.rect.x ? line.rect.x - x : x > line.rect.Right() ? x - line.rect.Right() : 0;
			const double score = vertical + horizontal * 0.01 + (horizontal > 0 ? 50 : 0);
			if (score < bestScore) {
				bestScore = score;
				best = &line;
			}
		}
		if (best)
			return PositionInLine(view, *best, x);
	}
	return std::nullopt;
}

std::optional<TextPosition> LineEdge(const DocumentView &view, const LayoutResult &layout, TextPosition p, bool end) {
	const auto ref = RunOf(layout, p);
	if (!ref)
		return p;
	const PlacedLine &line = *ref->line;
	const GlyphRun *edge = nullptr;
	for (const GlyphRun &r : line.runs)
		if (!r.generated && (!edge || end))
			edge = &r;
	if (!edge)
		return p;
	const GlyphRun &run = *edge;
	(void)view;
	if (run.field != FieldKind::None)
		return TextPosition{run.node, 0};
	return TextPosition{run.node, end ? run.offset + run.text.size() : run.offset};
}

std::vector<std::pair<size_t, Rect>> RangeRects(const LayoutResult &layout, const TextOrder &order, TextPosition a, TextPosition b) {
	std::vector<std::pair<size_t, Rect>> out;
	if (order.Compare(a, b) > 0)
		std::swap(a, b);
	if (a == b)
		return out;
	for (size_t page = 0; page < layout.pages.size(); ++page)
		for (const PlacedLine &line : layout.pages[page].lines)
			for (const GlyphRun &run : line.runs) {
				if (run.generated)
					continue;
				const size_t len = run.field != FieldKind::None ? 0 : run.text.size();
				const TextPosition start{run.node, run.field != FieldKind::None ? 0 : run.offset};
				const TextPosition stop{run.node, run.field != FieldKind::None ? 0 : run.offset + len};
				if (run.field != FieldKind::None) {
					// Un champ est sélectionné s'il est strictement à l'intérieur de la plage.
					if (order.Compare(a, start) <= 0 && order.Compare(start, b) < 0 && run.node != b.text)
						out.emplace_back(page, Rect(run.x, line.rect.y, run.width, line.rect.h));
					continue;
				}
				if (order.Compare(stop, a) <= 0 || order.Compare(b, start) <= 0)
					continue;
				const size_t s = run.node == a.text ? std::max(run.offset, a.offset) : run.offset;
				const size_t e = run.node == b.text ? std::min(run.offset + len, b.offset) : run.offset + len;
				if (e <= s)
					continue;
				const double x0 = XAt(run, s), x1 = XAt(run, e);
				out.emplace_back(page, Rect(x0, line.rect.y, x1 - x0, line.rect.h));
			}
	return out;
}

// ── Lecture ──

TextFragment Copy(const DocumentView &view, const TextOrder &order, TextPosition a, TextPosition b) {
	TextFragment f;
	a = order.Normalize(a);
	b = order.Normalize(b);
	if (order.Compare(a, b) > 0)
		std::swap(a, b);
	if (!a.Valid() || a == b)
		return f;
	NodeId paragraph = kNoNode;
	bool inside = false;
	view.Visit(view.Root(), [&](const Node &n) {
		if (n.Type() != NodeType::Text || !order.Contains(n.Id()))
			return true;
		if (n.Id() == a.text)
			inside = true;
		if (!inside)
			return true;
		if (n.Parent() != paragraph) {
			paragraph = n.Parent();
			f.paragraphs.push_back({ParagraphProperties(view, paragraph), {}});
		}
		const std::string text = TextOfNode(view, n.Id());
		const size_t s = n.Id() == a.text ? std::min(a.offset, text.size()) : 0;
		const size_t e = n.Id() == b.text ? std::min(b.offset, text.size()) : text.size();
		const bool field = IsField(view, n.Id());
		if ((field && n.Id() != b.text) || e > s)
			f.paragraphs.back().runs.push_back({RunProperties(view, n.Id()), field ? std::string() : text.substr(s, e - s)});
		if (n.Id() == b.text)
			inside = false;
		return true;
	});
	return f;
}

std::string PlainText(const DocumentView &view, const TextOrder &order, TextPosition a, TextPosition b) {
	return Copy(view, order, a, b).PlainText();
}

// ── Modifications ──

namespace {

/// Opérations primitives exécutées par l'historique, qui tiennent à jour des positions suivies
/// (curseur, cible d'un déplacement…) quand les segments sont coupés, fusionnés ou supprimés.
class Ops {
public:
	explicit Ops(History &h) : m_h(h), m_d(h.Target()) {}

	std::vector<TextPosition *> tracked;
	Status status = Success();

	[[nodiscard]] bool Ok() const noexcept { return status.IsOk(); }
	[[nodiscard]] Document &D() noexcept { return m_d; }
	[[nodiscard]] std::string Text(NodeId id) const { return TextOfNode(m_d, id); }
	[[nodiscard]] std::vector<NodeId> Runs(NodeId paragraph) const {
		std::vector<NodeId> out;
		if (const Node *p = m_d.Find(paragraph))
			for (NodeId c : p->Children())
				if (m_d.Find(c)->Type() == NodeType::Text)
					out.push_back(c);
		return out;
	}
	[[nodiscard]] NodeId Paragraph(NodeId text) const { return m_d.Find(text)->Parent(); }

	void Exec(CommandPtr c) {
		if (Ok())
			status = m_h.Execute(std::move(c));
	}
	NodeId Create(CommandPtr c) {
		if (!Ok())
			return kNoNode;
		Command *raw = c.get();
		status = m_h.Execute(std::move(c));
		return Ok() ? raw->Created() : kNoNode;
	}

	void SetText(NodeId id, const std::string &text) {
		if (Text(id) != text)
			Exec(SetProperty(id, props::Text, text));
	}

	NodeId NewRun(NodeId paragraph, size_t index, const PropertyMap &properties, const std::string &text, NodeId groupsFrom = kNoNode) {
		const NodeId run = Create(CreateNode(NodeType::Text, paragraph, index));
		for (const auto &[p, v] : properties)
			Exec(SetProperty(run, p, v));
		if (!text.empty())
			Exec(SetProperty(run, props::Text, text));
		CopyGroups(groupsFrom, run);
		return run;
	}

	/// Appartenances (non automatiques) d'un nœud recopiées sur un autre (segment coupé…).
	void CopyGroups(NodeId from, NodeId to) {
		if (from == kNoNode || !m_d.Contains(from) || to == kNoNode)
			return;
		const auto groups = m_d.Find(from)->Groups();
		for (const Membership &m : std::vector<Membership>(groups.begin(), groups.end()))
			if (!m.automatic)
				Exec(AddToGroup(to, m.group));
	}

	NodeId NewParagraph(NodeId container, size_t index, const PropertyMap &properties, NodeId groupsFrom = kNoNode) {
		const NodeId p = Create(CreateNode(NodeType::Paragraph, container, index));
		for (const auto &[id, v] : properties)
			Exec(SetProperty(p, id, v));
		CopyGroups(groupsFrom, p);
		return p;
	}

	/// Coupe le segment à la position : la partie gauche devient un nouveau segment inséré avant,
	/// le segment d'origine garde la partie droite (positions suivies ajustées). Rend l'indice de
	/// la frontière dans le paragraphe.
	size_t Boundary(TextPosition p) {
		const NodeId paragraph = Paragraph(p.text);
		const size_t index = m_d.Find(paragraph)->IndexOfChild(p.text);
		if (IsField(m_d, p.text) || p.offset == 0)
			return index;
		const std::string text = Text(p.text);
		if (p.offset >= text.size())
			return index + 1;
		const NodeId left = NewRun(paragraph, index, RunProperties(m_d, p.text), text.substr(0, p.offset), p.text);
		SetText(p.text, text.substr(p.offset));
		for (TextPosition *t : tracked)
			if (t->text == p.text) {
				if (t->offset <= p.offset)
					t->text = left;
				else
					t->offset -= p.offset;
			}
		return index + 1;
	}

	void DeleteNode(NodeId id) {
		for (TextPosition *t : tracked)
			if (t->text == id || (m_d.Contains(t->text) && m_d.IsAncestor(id, t->text)))
				*t = {}; // replacé par l'appelant
		Exec(doc::DeleteNode(id));
	}

	void MoveRun(NodeId run, NodeId paragraph, size_t position = kAppend) { Exec(doc::MoveNode(run, paragraph, position)); }

	/// Insère du texte dans un segment (positions suivies au-delà décalées).
	void InsertText(TextPosition at, std::string_view text) {
		std::string s = Text(at.text);
		s.insert(std::min(at.offset, s.size()), text);
		SetText(at.text, s);
		for (TextPosition *t : tracked)
			if (t->text == at.text && t->offset > at.offset)
				t->offset += text.size();
	}

	/// Nettoyage d'un paragraphe : segments vides retirés (s'il en reste d'autres), voisins de
	/// mêmes propriétés fusionnés. Les positions suivies suivent le texte.
	void Tidy(NodeId paragraph) {
		if (!Ok() || !m_d.Contains(paragraph))
			return;
		std::vector<NodeId> runs = Runs(paragraph);
		for (size_t i = 0; i < runs.size() && runs.size() > 1;) {
			if (!IsField(m_d, runs[i]) && Text(runs[i]).empty()) {
				const NodeId gone = runs[i];
				const NodeId neighbor = i + 1 < runs.size() ? runs[i + 1] : runs[i - 1];
				const size_t at = i + 1 < runs.size() ? 0 : Text(neighbor).size();
				for (TextPosition *t : tracked)
					if (t->text == gone)
						*t = {neighbor, at};
				Exec(doc::DeleteNode(gone));
				runs.erase(runs.begin() + std::ptrdiff_t(i));
				continue;
			}
			++i;
		}
		for (size_t i = 0; i + 1 < runs.size();) {
			const NodeId left = runs[i], right = runs[i + 1];
			if (!IsField(m_d, left) && !IsField(m_d, right) && RunProperties(m_d, left) == RunProperties(m_d, right)) {
				const size_t shift = Text(left).size();
				SetText(left, Text(left) + Text(right));
				for (TextPosition *t : tracked)
					if (t->text == right)
						*t = {left, t->offset + shift};
				Exec(doc::DeleteNode(right));
				runs.erase(runs.begin() + std::ptrdiff_t(i + 1));
				continue;
			}
			++i;
		}
	}

	/// Garantit au moins un segment dans le paragraphe (curseur possible).
	NodeId EnsureRun(NodeId paragraph, const PropertyMap &style) {
		const auto runs = Runs(paragraph);
		if (!runs.empty())
			return runs.front();
		return NewRun(paragraph, 0, style, {});
	}

private:
	History &m_h;
	Document &m_d;
};

/// Transaction ouverte seulement si aucune n'est en cours (composition des opérations).
class Transaction {
public:
	Transaction(History &h, std::string name) : m_h(h), m_own(!h.InTransaction()) {
		if (m_own)
			m_h.BeginTransaction(std::move(name));
	}
	Status Finish(const Status &s) {
		if (!m_own)
			return s;
		if (!s) {
			(void)m_h.Rollback();
			return s;
		}
		return m_h.Commit();
	}

private:
	History &m_h;
	bool m_own;
};

/// Suppression (sans nettoyage final) : rend le curseur.
TextPosition EraseImpl(Ops &ops, TextPosition a, TextPosition b) {
	Document &d = ops.D();
	const NodeId pa = ops.Paragraph(a.text), pb = ops.Paragraph(b.text);
	const PropertyMap styleA = RunProperties(d, a.text);
	TextPosition caret = a;
	ops.tracked.push_back(&caret);
	// Frontière droite d'abord (le segment de b garde la partie droite, hors plage) ; la coupe
	// de gauche ne déplace pas ce segment. `a` est suivie pendant la première coupe.
	const bool bAtEnd = !IsField(d, b.text) && b.offset > 0 && b.offset >= ops.Text(b.text).size();
	ops.tracked.push_back(&a);
	(void)ops.Boundary(b);
	ops.tracked.pop_back();
	const size_t left = ops.Boundary(a);
	const size_t rightIndex = d.Find(pb)->IndexOfChild(b.text) + (bAtEnd ? 1 : 0);
	if (pa == pb) {
		const auto runs = ops.Runs(pa);
		for (size_t i = left; i < rightIndex && i < runs.size(); ++i)
			ops.DeleteNode(runs[i]);
	} else {
		const auto runsA = ops.Runs(pa);
		for (size_t i = left; i < runsA.size(); ++i)
			ops.DeleteNode(runsA[i]);
		const auto runsB = ops.Runs(pb);
		for (size_t i = 0; i < rightIndex && i < runsB.size(); ++i)
			ops.DeleteNode(runsB[i]);
		const NodeId container = d.Find(pa)->Parent();
		if (container == d.Find(pb)->Parent()) {
			// Blocs entre les deux paragraphes, puis fusion du second dans le premier.
			const auto children = std::vector<NodeId>(d.Find(container)->Children().begin(), d.Find(container)->Children().end());
			const size_t ia = d.Find(container)->IndexOfChild(pa), ib = d.Find(container)->IndexOfChild(pb);
			for (size_t i = ia + 1; i < ib; ++i)
				ops.DeleteNode(children[i]);
			for (NodeId run : ops.Runs(pb))
				ops.MoveRun(run, pa);
			ops.DeleteNode(pb);
		} else {
			// Conteneurs différents (corps et cellule…) : texte des paragraphes intermédiaires vidé.
			std::vector<NodeId> middle;
			bool between = false;
			d.Visit(d.Root(), [&](const Node &n) {
				if (n.Id() == pa)
					between = true;
				else if (n.Id() == pb)
					between = false;
				else if (between && n.Type() == NodeType::Paragraph && n.Id() != pa)
					middle.push_back(n.Id());
			});
			for (NodeId p : middle) {
				const auto runs = ops.Runs(p);
				const PropertyMap style = runs.empty() ? PropertyMap{} : RunProperties(d, runs.front());
				for (NodeId r : runs)
					ops.DeleteNode(r);
				ops.EnsureRun(p, style);
			}
			ops.EnsureRun(pb, styleA);
		}
	}
	const NodeId kept = ops.EnsureRun(pa, styleA);
	if (!caret.Valid()) { // segment du curseur supprimé : fin de la partie gauche, sinon début du reste
		const auto runs = ops.Runs(pa);
		const size_t i = std::min(left, runs.size());
		caret = i > 0 ? TextPosition{runs[i - 1], ops.Text(runs[i - 1]).size()} : TextPosition{kept, 0};
		if (i > 0 && IsField(d, runs[i - 1]) && i < runs.size())
			caret = {runs[i], 0};
	}
	ops.tracked.erase(std::ranges::find(ops.tracked, &caret));
	for (TextPosition *t : ops.tracked) // positions suivies tombées dans la plage : au curseur
		if (!t->Valid())
			*t = caret;
	return caret;
}

/// Coupe de paragraphe ; rend le début du nouveau paragraphe.
TextPosition SplitImpl(Ops &ops, TextPosition at) {
	Document &d = ops.D();
	const NodeId p = ops.Paragraph(at.text);
	const NodeId container = d.Find(p)->Parent();
	const size_t pIndex = d.Find(container)->IndexOfChild(p);
	const PropertyMap style = RunProperties(d, at.text);
	const size_t boundary = ops.Boundary(at);
	const auto runs = ops.Runs(p);
	const NodeId p2 = ops.NewParagraph(container, pIndex + 1, ParagraphProperties(d, p), p);
	for (size_t i = boundary; i < runs.size(); ++i)
		ops.MoveRun(runs[i], p2);
	ops.EnsureRun(p, style);
	const NodeId first = ops.EnsureRun(p2, style);
	return {first, 0};
}

DocResult<TextPosition> Done(Ops &ops, Transaction &tx, TextPosition caret) {
	if (auto s = tx.Finish(ops.status); !s)
		return Err(s.Error());
	return Ok(caret);
}

/// Position de saisie : un champ n'accueille pas de texte (segment voisin, ou nouveau segment).
TextPosition Editable(Ops &ops, TextPosition at) {
	if (!IsField(ops.D(), at.text))
		return at;
	const NodeId p = ops.Paragraph(at.text);
	const auto runs = ops.Runs(p);
	const size_t i = ops.D().Find(p)->IndexOfChild(at.text);
	if (i + 1 < runs.size() && !IsField(ops.D(), runs[i + 1]))
		return {runs[i + 1], 0};
	PropertyMap style = RunProperties(ops.D(), at.text);
	style.Erase(PropertyId::Field);
	return {ops.NewRun(p, i + 1, style, {}), 0};
}

TextPosition PasteImpl(Ops &ops, TextPosition at, const TextFragment &f) {
	Document &d = ops.D();
	at = Editable(ops, at);
	if (f.paragraphs.empty())
		return at;
	ops.tracked.push_back(&at);
	if (f.paragraphs.size() == 1) {
		const NodeId p = ops.Paragraph(at.text);
		const size_t boundary = ops.Boundary(at);
		TextPosition caret = at;
		size_t index = boundary;
		for (const TextFragment::Run &r : f.paragraphs[0].runs) {
			const NodeId run = ops.NewRun(p, index++, r.properties, r.text);
			caret = {run, r.text.size()};
		}
		ops.tracked.pop_back();
		ops.tracked.push_back(&caret);
		ops.Tidy(p);
		ops.tracked.pop_back();
		return caret;
	}
	const TextPosition start = SplitImpl(ops, at);
	ops.tracked.pop_back();
	const NodeId p2 = ops.Paragraph(start.text);
	const NodeId container = d.Find(p2)->Parent();
	const NodeId p1 = d.Find(container)->Children()[d.Find(container)->IndexOfChild(p2) - 1];
	for (const TextFragment::Run &r : f.paragraphs.front().runs)
		ops.NewRun(p1, kAppend, r.properties, r.text);
	size_t index = d.Find(container)->IndexOfChild(p2);
	for (size_t k = 1; k + 1 < f.paragraphs.size(); ++k) {
		const NodeId p = ops.NewParagraph(container, index++, f.paragraphs[k].properties);
		for (const TextFragment::Run &r : f.paragraphs[k].runs)
			ops.NewRun(p, kAppend, r.properties, r.text);
		ops.EnsureRun(p, {});
	}
	TextPosition caret = start;
	size_t at2 = 0;
	for (const TextFragment::Run &r : f.paragraphs.back().runs) {
		const NodeId run = ops.NewRun(p2, at2++, r.properties, r.text);
		caret = {run, r.text.size()};
	}
	ops.tracked.push_back(&caret);
	ops.Tidy(p1);
	ops.Tidy(p2);
	ops.tracked.pop_back();
	return caret;
}

} // namespace

DocResult<TextPosition> Insert(History &history, TextPosition at, std::string_view utf8) {
	const TextOrder order(history.Target());
	at = order.Normalize(at);
	if (!at.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	if (utf8.empty())
		return Ok(at);
	// Frappe simple : une seule commande, fusionnée avec les frappes suivantes dans l'historique.
	if (utf8.find('\n') == std::string_view::npos && !IsField(history.Target(), at.text)) {
		std::string s = TextOfNode(history.Target(), at.text);
		s.insert(at.offset, utf8);
		if (auto r = history.Execute(SetProperty(at.text, props::Text, s)); !r)
			return Err(r.Error());
		return Ok(TextPosition{at.text, at.offset + utf8.size()});
	}
	Ops ops(history);
	Transaction tx(history, "Saisie");
	TextPosition caret = Editable(ops, at);
	size_t start = 0;
	for (;;) {
		const size_t end = utf8.find('\n', start);
		const std::string_view line = utf8.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
		if (!line.empty()) {
			ops.InsertText(caret, line);
			caret.offset += line.size();
		}
		if (end == std::string_view::npos || !ops.Ok())
			break;
		caret = SplitImpl(ops, caret);
		start = end + 1;
	}
	return Done(ops, tx, caret);
}

DocResult<TextPosition> InsertLineBreak(History &history, TextPosition at) {
	const TextOrder order(history.Target());
	at = order.Normalize(at);
	if (!at.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	Ops ops(history);
	Transaction tx(history, "Saut de ligne");
	TextPosition caret = Editable(ops, at); // pas dans un champ
	ops.InsertText(caret, "\n");
	caret.offset += 1;
	return Done(ops, tx, caret);
}

DocResult<TextPosition> Erase(History &history, TextPosition a, TextPosition b) {
	const TextOrder order(history.Target());
	a = order.Normalize(a);
	b = order.Normalize(b);
	if (!a.Valid() || !b.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	if (order.Compare(a, b) > 0)
		std::swap(a, b);
	if (a == b)
		return Ok(a);
	if (a.text == b.text) { // dans un seul segment : commande simple (fusionnable)
		std::string s = TextOfNode(history.Target(), a.text);
		s.erase(a.offset, b.offset - a.offset);
		if (auto r = history.Execute(SetProperty(a.text, props::Text, s)); !r)
			return Err(r.Error());
		return Ok(a);
	}
	Ops ops(history);
	Transaction tx(history, "Supprimer le texte");
	TextPosition caret = EraseImpl(ops, a, b);
	const NodeId paragraph = caret.Valid() && history.Target().Contains(caret.text) ? ops.Paragraph(caret.text) : kNoNode;
	ops.tracked.push_back(&caret);
	if (paragraph != kNoNode)
		ops.Tidy(paragraph);
	return Done(ops, tx, caret);
}

DocResult<TextPosition> Split(History &history, TextPosition at) {
	const TextOrder order(history.Target());
	at = order.Normalize(at);
	if (!at.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	Ops ops(history);
	Transaction tx(history, "Nouveau paragraphe");
	const TextPosition caret = SplitImpl(ops, at);
	return Done(ops, tx, caret);
}

DocResult<std::pair<TextPosition, TextPosition>> ApplyStyle(History &history, TextPosition a, TextPosition b, PropertyId property,
															PropertyValue value) {
	Document &d = history.Target();
	const TextOrder before(d);
	a = before.Normalize(a);
	b = before.Normalize(b);
	if (!a.Valid() || !b.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	if (before.Compare(a, b) > 0)
		std::swap(a, b);
	if (a == b)
		return Ok(std::pair{a, b});
	Ops ops(history);
	Transaction tx(history, std::format("Mise en forme : {}", Info(property).name));
	// Coupes : le segment de b garde la partie droite (hors plage), puis le segment de a garde
	// la partie droite (début de la plage). Bornes en fin de segment : pas de coupe.
	const bool bAtEnd = !IsField(d, b.text) && b.offset > 0 && b.offset >= TextOfNode(d, b.text).size();
	ops.tracked.push_back(&a);
	(void)ops.Boundary(b);
	ops.tracked.pop_back();
	const bool aAtEnd = !IsField(d, a.text) && a.offset > 0 && a.offset >= TextOfNode(d, a.text).size();
	(void)ops.Boundary(a);
	const TextOrder order(d);
	std::vector<NodeId> styled;
	bool inside = false, startNext = false, done = false;
	d.Visit(d.Root(), [&](const Node &n) {
		if (done || n.Type() != NodeType::Text || !order.Contains(n.Id()))
			return;
		if (startNext) {
			inside = true;
			startNext = false;
		}
		if (n.Id() == b.text && !bAtEnd) { // début de la partie hors plage
			done = true;
			return;
		}
		if (n.Id() == a.text) {
			if (aAtEnd)
				startNext = true;
			else
				inside = true;
		}
		if (inside)
			styled.push_back(n.Id());
		if (n.Id() == b.text) // borne en fin de segment : segment inclus
			done = true;
	});
	for (NodeId id : styled)
		ops.Exec(SetProperty(id, property, value));
	TextPosition first{}, last{};
	if (!styled.empty()) {
		first = {styled.front(), 0};
		last = {styled.back(), IsField(d, styled.back()) ? 0 : TextOfNode(d, styled.back()).size()};
	}
	ops.tracked = {&first, &last};
	for (NodeId p : {first.Valid() ? ops.Paragraph(first.text) : kNoNode, last.Valid() ? ops.Paragraph(last.text) : kNoNode})
		if (p != kNoNode)
			ops.Tidy(p);
	if (auto s = tx.Finish(ops.status); !s)
		return Err(s.Error());
	return Ok(std::pair{first, last});
}

DocResult<TextPosition> Paste(History &history, TextPosition at, const TextFragment &fragment) {
	const TextOrder order(history.Target());
	at = order.Normalize(at);
	if (!at.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	Ops ops(history);
	Transaction tx(history, "Coller");
	const TextPosition caret = PasteImpl(ops, at, fragment);
	return Done(ops, tx, caret);
}

DocResult<std::pair<TextPosition, TextPosition>> MoveRange(History &history, TextPosition a, TextPosition b, TextPosition target, bool copy) {
	Document &d = history.Target();
	const TextOrder order(d);
	a = order.Normalize(a);
	b = order.Normalize(b);
	target = order.Normalize(target);
	if (!a.Valid() || !b.Valid() || !target.Valid())
		return Failure(ErrorCode::NotFound, "position de texte invalide");
	if (order.Compare(a, b) > 0)
		std::swap(a, b);
	if (a == b || (order.Compare(a, target) <= 0 && order.Compare(target, b) <= 0))
		return Ok(std::pair{a, b}); // dépôt dans la plage elle-même : rien à faire
	const TextFragment fragment = Copy(d, order, a, b);
	Ops ops(history);
	Transaction tx(history, copy ? "Copier le texte" : "Déplacer le texte");
	TextPosition start = target;
	const bool after = order.Compare(target, b) > 0;
	if (!copy && after) { // supprimer d'abord : la cible, suivie, reste valide
		ops.tracked.push_back(&start);
		(void)EraseImpl(ops, a, b);
		ops.tracked.clear();
	}
	TextPosition begin{}, end{};
	if (ops.Ok()) {
		if (!copy && !after) { // la plage d'origine, après la cible, est supprimée ensuite
			ops.tracked.push_back(&a);
			ops.tracked.push_back(&b);
		}
		ops.tracked.push_back(&start);
		start = Editable(ops, start);
		// Frontière à la cible : le texte collé s'insère entre la partie gauche (dont la fin,
		// suivie, marque le début du collage) et la partie droite.
		const NodeId p = ops.Paragraph(start.text);
		const size_t boundary = ops.Boundary(start);
		const auto runs = ops.Runs(p);
		const bool atStart = boundary == 0;
		if (!atStart) {
			begin = {runs[boundary - 1], IsField(d, runs[boundary - 1]) ? 0 : ops.Text(runs[boundary - 1]).size()};
			ops.tracked.push_back(&begin);
		}
		const TextPosition pasteAt = boundary < runs.size() ? TextPosition{runs[boundary], 0}
															: TextPosition{runs.back(), IsField(d, runs.back()) ? 0 : ops.Text(runs.back()).size()};
		end = PasteImpl(ops, pasteAt, fragment);
		if (atStart && ops.Ok()) // collage en tête de paragraphe : le début est celui du paragraphe
			begin = {ops.Runs(p).front(), 0};
		ops.tracked.clear();
	}
	if (!copy && !after && ops.Ok()) {
		ops.tracked = {&begin, &end};
		(void)EraseImpl(ops, a, b);
		ops.tracked.clear();
	}
	if (auto s = tx.Finish(ops.status); !s)
		return Err(s.Error());
	return Ok(std::pair{begin, end});
}

} // namespace text

} // namespace doc
