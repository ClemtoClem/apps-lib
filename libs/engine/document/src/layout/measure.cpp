// Mesure des blocs : style de texte, coupure des lignes, alignement, images, formes, tableaux.
#include <algorithm>
#include <cmath>
#include <deque>
#include <format>

#include <core/string.hpp>

#include "internal.hpp"
#include "document/tables.hpp"

namespace doc {

namespace {

constexpr double kLineFactor = 1.2;		 ///< hauteur de ligne « simple » = 1,2 × corps
constexpr double kPixelToPoint = 0.75;	 ///< image à 96 ppp
constexpr double kDefaultShapeHeight = 36;
constexpr double kListIndent = 18;	  ///< retrait par niveau de liste (le marqueur est dans le dernier)
constexpr double kTocIndent = 16;	  ///< retrait par niveau d'une table des matières
constexpr double kTocNumberSlot = 30; ///< place des numéros de page d'une table des matières

double Clamp(double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); }

PageSize Oriented(PageSize s, Orientation o) { return o == Orientation::Landscape ? PageSize{s.height, s.width} : s; }

struct RunSource {
	NodeId node = kNoNode;
	TextStyle style;
	FieldKind field = FieldKind::None;
	std::string text;
	bool generated = false;
	NodeId target = kNoNode;
};

struct Segment {
	size_t run = 0;
	size_t offset = 0; ///< position (octets) du segment dans le texte de son run
	std::string text;
	double width = 0;
	bool tab = false; ///< tabulation (largeur de quatre espaces, jamais fusionnée dans un run)
};

struct Item {
	enum Kind : uint8_t { Word, Space, Break } kind = Word;
	std::vector<Segment> segments;
	double width = 0;
};

double SegmentWidth(const RunSource &r, std::string_view text) { return r.style.font.TextWidth(text, r.style.size); }

/// Découpe le texte des segments en mots, espaces et sauts de ligne (les mots traversent les runs).
std::vector<Item> Tokenize(const std::vector<RunSource> &runs) {
	std::vector<Item> items;
	for (size_t ri = 0; ri < runs.size(); ++ri) {
		const std::string &text = runs[ri].text;
		for (size_t i = 0; i < text.size();) {
			const size_t start = i;
			const char32_t cp = unicode::DecodeUtf8(text, i);
			const std::string_view ch(text.data() + start, i - start);
			// L'espace insécable (U+00A0) reste un caractère de mot : pas de coupure.
			const Item::Kind kind = cp == U'\n' ? Item::Break : (cp == U' ' || cp == U'\t') ? Item::Space : Item::Word;
			if (kind == Item::Break) {
				items.push_back({Item::Break, {}, 0});
				continue;
			}
			const bool tab = cp == U'\t';
			if (items.empty() || items.back().kind != kind)
				items.push_back({kind, {}, 0});
			Item &item = items.back();
			// Nouveau segment : autre run, ou tabulation (isolée : son texte n'entre pas dans les runs).
			if (item.segments.empty() || item.segments.back().run != ri || tab || item.segments.back().tab)
				item.segments.push_back({ri, start, {}, 0, tab});
			item.segments.back().text += ch;
		}
	}
	for (Item &item : items) {
		item.width = 0;
		for (Segment &s : item.segments) {
			s.width = s.tab ? 4 * SegmentWidth(runs[s.run], " ") : SegmentWidth(runs[s.run], s.text);
			item.width += s.width;
		}
	}
	return items;
}

/// Coupe un mot trop long pour la ligne en morceaux de largeur ≤ `avail` (au moins un caractère).
std::vector<Item> SplitWord(const Item &word, const std::vector<RunSource> &runs, double avail) {
	std::vector<Item> parts(1);
	double width = 0;
	for (const Segment &s : word.segments)
		for (size_t i = 0; i < s.text.size();) {
			const size_t start = i;
			(void)unicode::DecodeUtf8(s.text, i);
			const std::string_view ch(s.text.data() + start, i - start);
			const double w = SegmentWidth(runs[s.run], ch);
			if (width + w > avail && width > 0) {
				parts.emplace_back();
				width = 0;
			}
			Item &part = parts.back();
			if (part.segments.empty() || part.segments.back().run != s.run)
				part.segments.push_back({s.run, s.offset + start, {}, 0});
			part.segments.back().text += ch;
			part.segments.back().width += w;
			part.width += w;
			width += w;
		}
	return parts;
}

struct ParagraphStyle {
	HorizontalAlign align = HorizontalAlign::Left;
	double lineSpacing = 1.15;
	double firstIndent = 0;
	bool preserveSpaces = false; ///< espaces de début de ligne conservés (code)
	TextStyle base;				 ///< style des lignes vides
};

/// Construit une ligne à partir de mots et d'espaces (espaces de fin retirés).
LineBox BuildLine(const std::vector<const Item *> &items, const std::vector<RunSource> &runs, const ParagraphStyle &ps,
				  double offset, double avail, bool justify) {
	size_t end = items.size();
	while (end > 0 && items[end - 1]->kind == Item::Space)
		--end;
	double natural = 0;
	size_t spaces = 0;
	for (size_t i = 0; i < end; ++i) {
		natural += items[i]->width;
		spaces += items[i]->kind == Item::Space ? 1u : 0u;
	}
	LineBox line;
	line.width = natural;
	double x = offset;
	switch (ps.align) {
	case HorizontalAlign::Center: x += std::max(0.0, (avail - natural) / 2); break;
	case HorizontalAlign::Right: x += std::max(0.0, avail - natural); break;
	default: break;
	}
	const bool spread = justify && ps.align == HorizontalAlign::Justify && spaces > 0 && natural < avail;
	const double extra = spread ? (avail - natural) / double(spaces) : 0.0;
	if (spread)
		line.width = avail;

	double maxSize = 0, ascent = 0, descent = 0;
	for (size_t i = 0; i < end; ++i) {
		const Item &item = *items[i];
		for (const Segment &s : item.segments) {
			const RunSource &r = runs[s.run];
			maxSize = std::max(maxSize, r.style.size);
			ascent = std::max(ascent, r.style.font.Ascent(r.style.size) + r.style.rise);
			descent = std::max(descent, r.style.font.Descent(r.style.size) - r.style.rise);
			// Fusion avec le run précédent du même nœud s'il est contigu (à l'écran et dans le texte).
			const bool merge = !spread && !s.tab && !line.runs.empty() && line.runs.back().node == r.node && line.runs.back().field == r.field &&
							   line.runs.back().generated == r.generated && line.runs.back().target == r.target &&
							   std::abs(line.runs.back().x + line.runs.back().width - x) < 1e-6 &&
							   line.runs.back().offset + line.runs.back().text.size() == s.offset;
			if (item.kind == Item::Space) {
				if (merge) { // espace intérieur d'un même run : conservé dans le texte
					line.runs.back().text += s.text;
					line.runs.back().width += s.width;
				}
				x += s.width + (spread ? extra / double(item.segments.size()) : 0.0);
				continue;
			}
			if (merge) {
				line.runs.back().text += s.text;
				line.runs.back().width += s.width;
			} else {
				line.runs.push_back({r.node, s.text, s.offset, x, s.width, r.style, r.field, r.generated, r.target});
			}
			x += s.width;
		}
	}
	if (line.runs.empty()) {
		maxSize = ps.base.size;
		ascent = ps.base.font.Ascent(ps.base.size);
		descent = ps.base.font.Descent(ps.base.size);
	}
	line.height = maxSize * kLineFactor * ps.lineSpacing;
	line.baseline = (line.height - (ascent + descent)) / 2 + ascent;
	return line;
}

std::vector<LineBox> BreakLines(const std::vector<RunSource> &runs, const ParagraphStyle &ps, double inner) {
	std::vector<LineBox> lines;
	const std::vector<Item> tokens = Tokenize(runs);
	std::deque<Item> splitStorage; // morceaux de mots trop longs (adresses stables)
	std::vector<const Item *> current;
	double x = 0;
	bool hasWord = false;
	const auto offset = [&] { return lines.empty() ? ps.firstIndent : 0.0; };
	const auto avail = [&] { return std::max(1.0, inner - offset()); };
	const auto flush = [&](bool justify) {
		LineBox line = BuildLine(current, runs, ps, offset(), avail(), justify);
		line.y = lines.empty() ? 0 : lines.back().y + lines.back().height;
		lines.push_back(std::move(line));
		current.clear();
		x = 0;
		hasWord = false;
	};
	for (const Item &item : tokens) {
		if (item.kind == Item::Break) {
			flush(false);
			continue;
		}
		if (item.kind == Item::Space) {
			if (hasWord || ps.preserveSpaces) { // code : indentation de début de ligne conservée
				current.push_back(&item);
				x += item.width;
			}
			continue;
		}
		if (hasWord && x + item.width > avail()) {
			while (!current.empty() && current.back()->kind == Item::Space)
				current.pop_back();
			flush(true);
		}
		if (item.width > avail()) {
			std::vector<Item> parts = SplitWord(item, runs, avail());
			for (size_t k = 0; k < parts.size(); ++k) {
				splitStorage.push_back(std::move(parts[k]));
				current.push_back(&splitStorage.back());
				x += splitStorage.back().width;
				hasWord = true;
				if (k + 1 < parts.size())
					flush(true);
			}
			continue;
		}
		current.push_back(&item);
		x += item.width;
		hasWord = true;
	}
	if (!current.empty() || lines.empty() || (!tokens.empty() && tokens.back().kind == Item::Break))
		flush(false);
	return lines;
}

Insets PaddingOf(const DocumentView &view, PropertyCache &cache, NodeId id) {
	return {cache.Get(view, id, props::PaddingTop), cache.Get(view, id, props::PaddingRight), cache.Get(view, id, props::PaddingBottom),
			cache.Get(view, id, props::PaddingLeft)};
}

void ReadRules(const DocumentView &view, PropertyCache &cache, NodeId id, BlockLayout &b) {
	b.spaceBefore = cache.Get(view, id, props::SpaceBefore);
	b.spaceAfter = cache.Get(view, id, props::SpaceAfter);
	b.keepTogether = cache.Get(view, id, props::KeepTogether);
	b.keepWithNext = cache.Get(view, id, props::KeepWithNext);
	b.pageBreakBefore = cache.Get(view, id, props::PageBreakBefore);
	b.allowSplit = cache.Get(view, id, props::AllowSplit);
	b.minLines = int(cache.Get(view, id, props::MinimumLines));
	b.maxLines = int(cache.Get(view, id, props::MaximumLines));
}

double AlignOffset(HorizontalAlign a, double available, double width) {
	switch (a) {
	case HorizontalAlign::Center: return std::max(0.0, (available - width) / 2);
	case HorizontalAlign::Right: return std::max(0.0, available - width);
	default: return 0;
	}
}

std::shared_ptr<BlockLayout> MeasureParagraph(const DocumentView &view, PropertyCache &cache, const Node &node, double width,
											  const detail::FieldValues &fields, const FontLibrary *fonts) {
	auto b = std::make_shared<BlockLayout>();
	b->node = node.Id();
	b->type = NodeType::Paragraph;
	ReadRules(view, cache, node.Id(), *b);
	const double indentLeft = cache.Get(view, node.Id(), props::IndentLeft);
	const double indentRight = cache.Get(view, node.Id(), props::IndentRight);
	b->x = indentLeft;
	b->width = std::max(1.0, width - indentLeft - indentRight);
	b->padding = PaddingOf(view, cache, node.Id());
	b->border = cache.Get(view, node.Id(), props::BorderWidth);
	const double inner = std::max(1.0, b->width - b->padding.left - b->padding.right - 2 * b->border);

	ParagraphStyle ps;
	ps.align = cache.Get(view, node.Id(), props::HAlign);
	ps.lineSpacing = cache.Get(view, node.Id(), props::LineSpacing);
	ps.firstIndent = cache.Get(view, node.Id(), props::FirstLineIndent);
	ps.preserveSpaces = cache.Get(view, node.Id(), props::PreserveSpaces);
	ps.base = ResolveTextStyle(view, cache, node.Id(), fonts);

	std::vector<RunSource> runs;
	for (NodeId c : node.Children()) {
		const Node *t = view.Find(c);
		if (!t || t->Type() != NodeType::Text || !cache.Get(view, c, props::Visible))
			continue;
		RunSource r;
		r.node = c;
		r.style = ResolveTextStyle(view, cache, c, fonts);
		r.field = cache.Get(view, c, props::Field);
		r.target = LinkTarget(r.style.link);
		if (r.field == FieldKind::Reference)
			r.text = fields.outline ? fields.outline->Reference(r.target) : std::string("?");
		else if (r.field != FieldKind::None)
			r.text = detail::FieldText(r.field, fields);
		else
			r.text = cache.Get(view, c, props::Text);
		if (!r.text.empty())
			runs.push_back(std::move(r));
	}
	// Préfixe généré : numéro de titre ou légende en tête, marqueur de liste dans le retrait.
	const ParagraphPrefix *prefix = fields.outline ? fields.outline->Prefix(node.Id()) : nullptr;
	if (prefix && !prefix->marker) {
		RunSource r;
		r.node = node.Id();
		r.style = runs.empty() ? ps.base : runs.front().style;
		r.style.link.clear();
		r.text = prefix->text + "\u00A0 ";
		r.generated = true;
		runs.insert(runs.begin(), std::move(r));
	}
	double hanging = 0;
	if (prefix && prefix->marker) {
		hanging = kListIndent * double(prefix->listLevel + 1);
		b->x += hanging;
		b->width = std::max(1.0, b->width - hanging);
	}
	b->lines = BreakLines(runs, ps, std::max(1.0, inner - hanging));
	if (prefix && prefix->marker && !b->lines.empty()) {
		TextStyle style = runs.empty() ? ps.base : runs.front().style;
		style.link.clear();
		style.underline = style.strike = false;
		style.rise = 0;
		GlyphRun marker{node.Id(), prefix->text, 0, ps.firstIndent - kListIndent, style.font.TextWidth(prefix->text, style.size), style};
		marker.generated = true;
		b->lines.front().runs.insert(b->lines.front().runs.begin(), std::move(marker));
	}
	double content = b->lines.empty() ? 0 : b->lines.back().y + b->lines.back().height;
	b->height = std::max(content + b->padding.top + b->padding.bottom + 2 * b->border, cache.Get(view, node.Id(), props::MinHeight));
	return b;
}

void SizeBox(const DocumentView &view, PropertyCache &cache, NodeId id, double &w, double &h) {
	const double minW = cache.Get(view, id, props::MinWidth), maxW = cache.Get(view, id, props::MaxWidth);
	const double minH = cache.Get(view, id, props::MinHeight), maxH = cache.Get(view, id, props::MaxHeight);
	if (maxW > 0)
		w = std::min(w, maxW);
	if (maxH > 0)
		h = std::min(h, maxH);
	w = std::max(w, minW);
	h = std::max(h, minH);
}

/// Paragraphes d'une forme empilés à la largeur `width` de sa zone de texte ; rend leur hauteur.
double MeasureShapeText(const DocumentView &view, PropertyCache &cache, const Node &node, double width, const detail::FieldValues &fields,
						const FontLibrary *fonts, BlockLayout &b) {
	double y = 0, lastAfter = 0;
	bool first = true;
	for (NodeId c : node.Children()) {
		if (!detail::IsLaidOut(view, cache, c))
			continue;
		BlockPtr p = detail::Measure(view, cache, c, std::max(width, 1.0), 1e9, fields, fonts);
		if (!first)
			y += p->spaceBefore;
		b.content.emplace_back(y, p);
		y += p->height + p->spaceAfter;
		lastAfter = p->spaceAfter;
		first = false;
	}
	return first ? 0 : y - lastAfter;
}

/// `free` : élément d'une zone de dessin (ni réduit à la colonne, ni aligné : seule sa position compte).
std::shared_ptr<BlockLayout> MeasureImageOrShape(const DocumentView &view, PropertyCache &cache, const Node &node, double width,
												 double maxHeight, const detail::FieldValues &fields, const FontLibrary *fonts, bool free) {
	auto b = std::make_shared<BlockLayout>();
	b->node = node.Id();
	b->type = node.Type();
	ReadRules(view, cache, node.Id(), *b);
	b->keepTogether = true; // une image ou une forme ne se coupe pas
	b->absolute = cache.Get(view, node.Id(), props::Position) == Positioning::Absolute;
	b->positionX = cache.Get(view, node.Id(), props::PositionX);
	b->positionY = cache.Get(view, node.Id(), props::PositionY);
	b->base = cache.Get(view, node.Id(), props::PositionReference);
	b->rotation = cache.Get(view, node.Id(), props::Rotation);
	b->originX = cache.Get(view, node.Id(), props::RotationOriginX);
	b->originY = cache.Get(view, node.Id(), props::RotationOriginY);
	b->originBase = cache.Get(view, node.Id(), props::RotationOriginReference);
	b->z = cache.Get(view, node.Id(), props::ZIndex);
	b->border = cache.Get(view, node.Id(), props::BorderWidth);
	double w = cache.Get(view, node.Id(), props::Width), h = cache.Get(view, node.Id(), props::Height);
	if (node.Type() == NodeType::Image) {
		double nw = 100, nh = 75; // ressource absente : cadre de substitution
		if (const ImageResource *img = view.FindResource(ResourceId(cache.Get(view, node.Id(), props::Resource))); img && img->width && img->height) {
			nw = img->width * kPixelToPoint;
			nh = img->height * kPixelToPoint;
		}
		if (w <= 0 && h <= 0) {
			w = nw;
			h = nh;
		} else if (w <= 0) {
			w = h * nw / nh;
		} else if (h <= 0) {
			h = w * nh / nw;
		}
		SizeBox(view, cache, node.Id(), w, h);
		// Réduction homothétique pour tenir dans la colonne et la page.
		const double scale = std::min({1.0, width / std::max(w, 1e-9), maxHeight / std::max(h, 1e-9)});
		if (!b->absolute) {
			w *= scale;
			h *= scale;
		}
	} else {
		const ShapeKind kind = cache.Get(view, node.Id(), props::Shape);
		if (w <= 0)
			w = width;
		if (!b->absolute && !free)
			w = std::min(w, width);
		// Texte de la forme : paragraphes dans la boîte, moins bordure et rembourrage.
		b->padding = PaddingOf(view, cache, node.Id());
		b->contentAlign = cache.Get(view, node.Id(), props::VAlign);
		if (!node.Children().empty())
			b->contentHeight = MeasureShapeText(view, cache, node, w - 2 * b->border - b->padding.left - b->padding.right, fields, fonts, *b);
		if (h <= 0) // hauteur automatique : celle du texte, sinon une hauteur par défaut
			h = IsLineShape(kind) ? std::max(1.0, b->border)
				: b->content.empty() ? kDefaultShapeHeight
									 : b->contentHeight + 2 * b->border + b->padding.top + b->padding.bottom;
		SizeBox(view, cache, node.Id(), w, h);
		if (!b->absolute && !free) {
			w = std::min(w, width);
			h = std::min(h, maxHeight);
		}
	}
	b->width = w;
	b->height = h;
	b->x = b->absolute || free ? 0 : AlignOffset(cache.Get(view, node.Id(), props::HAlign), width, w);
	return b;
}

/// Zone de dessin : boîte de la largeur demandée (colonne par défaut) et de la hauteur demandée
/// (200 pt par défaut), éléments mesurés librement dans sa zone intérieure.
std::shared_ptr<BlockLayout> MeasureDrawing(const DocumentView &view, PropertyCache &cache, const Node &node, double width, double maxHeight,
											const detail::FieldValues &fields, const FontLibrary *fonts) {
	constexpr double kDefaultDrawingHeight = 200;
	auto b = std::make_shared<BlockLayout>();
	b->node = node.Id();
	b->type = NodeType::Drawing;
	ReadRules(view, cache, node.Id(), *b);
	b->keepTogether = true;
	b->border = cache.Get(view, node.Id(), props::BorderWidth);
	b->padding = PaddingOf(view, cache, node.Id());
	double w = cache.Get(view, node.Id(), props::Width), h = cache.Get(view, node.Id(), props::Height);
	if (w <= 0)
		w = width;
	if (h <= 0)
		h = kDefaultDrawingHeight;
	SizeBox(view, cache, node.Id(), w, h);
	w = std::min(w, width);
	h = std::min(h, maxHeight);
	b->width = w;
	b->height = h;
	b->x = AlignOffset(cache.Get(view, node.Id(), props::HAlign), width, w);
	const double inner = std::max(1.0, w - 2 * b->border - b->padding.left - b->padding.right);
	for (NodeId c : node.Children())
		if (const Node *child = view.Find(c); child && cache.Get(view, c, props::Visible))
			b->content.emplace_back(0, MeasureImageOrShape(view, cache, *child, inner, 1e9, fields, fonts, true));
	return b;
}

std::shared_ptr<BlockLayout> MeasureTable(const DocumentView &view, PropertyCache &cache, const Node &node, double width,
										  double maxHeight, const detail::FieldValues &fields, const FontLibrary *fonts) {
	auto b = std::make_shared<BlockLayout>();
	b->node = node.Id();
	b->type = NodeType::Table;
	ReadRules(view, cache, node.Id(), *b);
	b->border = cache.Get(view, node.Id(), props::BorderWidth);
	const double requested = cache.Get(view, node.Id(), props::Width);
	b->width = requested > 0 ? std::min(requested, width) : width;
	b->x = AlignOffset(cache.Get(view, node.Id(), props::HAlign), width, b->width);

	std::vector<const Node *> rows;
	size_t columns = 0;
	for (NodeId r : node.Children())
		if (const Node *row = view.Find(r); row && row->Type() == NodeType::TableRow && cache.Get(view, r, props::Visible)) {
			rows.push_back(row);
			columns = std::max(columns, row->Children().size());
		}
	// Cellules fusionnées (lignes visibles) : l'ancre couvre un rectangle, les cases couvertes
	// ne sont pas mises en page.
	std::vector<std::vector<NodeId>> grid;
	for (const Node *row : rows)
		grid.emplace_back(row->Children().begin(), row->Children().begin() + std::ptrdiff_t(std::min(row->Children().size(), columns)));
	const std::vector<CellSpan> spans = table::Spans(view, grid);
	std::vector<std::vector<int>> spanOf(grid.size()); // indice de la fusion ancrée ici, -2 : case couverte, -1 : simple
	for (size_t r = 0; r < grid.size(); ++r)
		spanOf[r].assign(grid[r].size(), -1);
	for (size_t i = 0; i < spans.size(); ++i)
		for (size_t r = spans[i].row; r < spans[i].row + spans[i].rows; ++r)
			for (size_t c = spans[i].column; c < spans[i].column + spans[i].columns; ++c)
				spanOf[r][c] = r == spans[i].row && c == spans[i].column ? int(i) : -2;
	// Largeurs des colonnes : Width explicite de la première cellule (non fusionnée en largeur)
	// qui en porte une, le reste partagé à parts égales ; réduction proportionnelle si la somme
	// dépasse la largeur.
	std::vector<double> widths(columns, 0.0);
	for (size_t c = 0; c < columns; ++c)
		for (size_t r = 0; r < rows.size(); ++r)
			if (c < grid[r].size() && (spanOf[r][c] == -1 || (spanOf[r][c] >= 0 && spans[size_t(spanOf[r][c])].columns == 1)))
				if (const double w = cache.Get(view, grid[r][c], props::Width); w > 0) {
					widths[c] = w;
					break;
				}
	double fixed = 0;
	size_t automatic = 0;
	for (double w : widths) {
		fixed += w;
		automatic += w <= 0 ? 1 : 0;
	}
	const double rest = std::max(0.0, b->width - fixed);
	for (double &w : widths)
		if (w <= 0)
			w = automatic ? rest / double(automatic) : 0;
	double total = 0;
	for (double w : widths)
		total += w;
	if (total > b->width && total > 0)
		for (double &w : widths)
			w *= b->width / total;

	std::vector<double> left(columns + 1, 0.0); // abscisse du bord gauche de chaque colonne
	for (size_t c = 0; c < columns; ++c)
		left[c + 1] = left[c] + widths[c];
	// 1. Géométrie des cellules et liste des blocs à mesurer (dans l'ordre).
	std::vector<detail::NestedRequest> requests;
	std::vector<size_t> spanJoins; // lignes liées à la suivante par une fusion verticale
	bool header = true;
	for (size_t ri = 0; ri < rows.size(); ++ri) {
		const Node *row = rows[ri];
		RowLayout rl;
		rl.node = row->Id();
		rl.header = header && cache.Get(view, row->Id(), props::RepeatHeader);
		header = rl.header;
		const auto cells = row->Children();
		for (size_t c = 0; c < cells.size() && c < columns; ++c) {
			if (spanOf[ri][c] == -2) // couverte par une fusion
				continue;
			const CellSpan span = spanOf[ri][c] >= 0 ? spans[size_t(spanOf[ri][c])] : CellSpan{ri, c, 1, 1};
			CellLayout cl;
			cl.node = cells[c];
			cl.x = left[c];
			cl.width = left[c + span.columns] - left[c];
			cl.rows = span.rows;
			cl.columns = span.columns;
			for (size_t k = 0; k + 1 < span.rows; ++k) // lignes liées : jamais coupées
				if (ri + k < rows.size())
					spanJoins.push_back(ri + k);
			cl.padding = PaddingOf(view, cache, cl.node);
			cl.border = cache.Get(view, cl.node, props::BorderWidth);
			cl.align = cache.Get(view, cl.node, props::VAlign);
			const double inner = std::max(1.0, cl.width - cl.padding.left - cl.padding.right - 2 * cl.border);
			if (const Node *cell = view.Find(cl.node); cell && cache.Get(view, cl.node, props::Visible))
				for (NodeId child : cell->Children())
					if (detail::IsLaidOut(view, cache, child)) {
						requests.push_back({child, inner, maxHeight});
						cl.blocks.emplace_back(0.0, nullptr); // rempli après la mesure
					}
			rl.cells.push_back(std::move(cl));
		}
		b->rows.push_back(std::move(rl));
	}
	// 2. Mesure : cache des cellules de la mise en page (cellules absentes en parallèle), sinon
	//    mesure directe.
	std::vector<BlockPtr> measured;
	if (fields.nested) {
		fields.nested->MeasureAll(requests, measured);
	} else {
		measured.reserve(requests.size());
		for (const detail::NestedRequest &r : requests)
			measured.push_back(detail::Measure(view, cache, r.id, r.width, r.maxHeight, fields, fonts));
	}
	// 3. Empilement des blocs et hauteur des lignes (une cellule fusionnée verticalement ne
	//    compte pas ici : elle agrandit au besoin sa dernière ligne ensuite).
	for (size_t r : spanJoins)
		b->rows[r].joinNext = true;
	size_t next = 0;
	std::vector<std::pair<size_t, const CellLayout *>> tall; // (ligne, cellule fusionnée verticalement)
	for (size_t ri = 0; ri < b->rows.size(); ++ri) {
		RowLayout &rl = b->rows[ri];
		double height = 0;
		for (CellLayout &cl : rl.cells) {
			double y = 0;
			bool first = true;
			for (auto &[by, block] : cl.blocks) {
				block = measured[next++];
				if (!first)
					y += block->spaceBefore;
				by = y;
				y += block->height + block->spaceAfter;
				first = false;
			}
			if (!cl.blocks.empty())
				y -= cl.blocks.back().second->spaceAfter;
			cl.contentHeight = y + cl.padding.top + cl.padding.bottom + 2 * cl.border;
			if (cl.rows > 1)
				tall.emplace_back(ri, &cl);
			else
				height = std::max(height, cl.contentHeight);
		}
		height = std::max({height, cache.Get(view, rows[ri]->Id(), props::MinHeight), cache.Get(view, rows[ri]->Id(), props::Height)});
		rl.height = height;
	}
	for (const auto &[ri, cl] : tall) { // contenu plus haut que ses lignes : la dernière s'agrandit
		const size_t last = std::min(b->rows.size(), ri + cl->rows) - 1;
		double have = 0;
		for (size_t r = ri; r <= last; ++r)
			have += b->rows[r].height;
		if (cl->contentHeight > have)
			b->rows[last].height += cl->contentHeight - have;
	}
	for (const RowLayout &rl : b->rows)
		b->height += rl.height;
	return b;
}

/// Table des matières : une entrée par titre (ou légende), retrait par niveau, points de conduite
/// et numéro de page aligné à droite (provisoire : rempli après la pagination).
std::shared_ptr<BlockLayout> MeasureToc(const DocumentView &view, PropertyCache &cache, const Node &node, double width,
										const detail::FieldValues &fields, const FontLibrary *fonts) {
	auto b = std::make_shared<BlockLayout>();
	b->node = node.Id();
	b->type = NodeType::TableOfContents;
	ReadRules(view, cache, node.Id(), *b);
	b->width = width;
	b->padding = PaddingOf(view, cache, node.Id());
	b->border = cache.Get(view, node.Id(), props::BorderWidth);
	const double inner = std::max(1.0, b->width - b->padding.left - b->padding.right - 2 * b->border);
	ParagraphStyle ps;
	ps.lineSpacing = cache.Get(view, node.Id(), props::LineSpacing);
	ps.base = ResolveTextStyle(view, cache, node.Id(), fonts);
	const TocContent content = cache.Get(view, node.Id(), props::TocContent);
	const bool links = cache.Get(view, node.Id(), props::Hyperlinks);
	const int depth = int(cache.Get(view, node.Id(), props::TocDepth));
	const std::string family = cache.Get(view, node.Id(), props::FontFamily);
	const bool italic = cache.Get(view, node.Id(), props::Italic);
	TextStyle bold = ps.base;
	if (fonts)
		bold.font = fonts->Resolve(family, true, italic);
	else
		bold.font.standard = pdf::MatchFont(family, true, italic);

	const std::vector<const OutlineEntry *> entries = fields.outline ? fields.outline->Entries(content, depth) : std::vector<const OutlineEntry *>{};
	double y = 0;
	for (const OutlineEntry *e : entries) {
		const double indent = content == TocContent::Headings ? kTocIndent * double(e->level - 1) : 0;
		RunSource r;
		r.node = node.Id();
		r.style = content == TocContent::Headings && e->level == 1 ? bold : ps.base;
		r.style.link = links ? InternalLink(e->node) : std::string();
		r.generated = true;
		r.target = e->node;
		r.text = e->label.empty() ? e->text : e->label + (e->kind == OutlineKind::Heading ? "\u00A0 " : "\u00A0:\u00A0") + e->text;
		if (r.text.empty())
			r.text = "\u00A0";
		const double avail = std::max(1.0, inner - indent - kTocNumberSlot);
		std::vector<LineBox> lines = BreakLines({r}, ps, avail);
		for (LineBox &l : lines) {
			for (GlyphRun &g : l.runs)
				g.x += indent;
			l.y = y;
			y += l.height;
		}
		LineBox &last = lines.back();
		const double end = last.runs.empty() ? indent : last.runs.back().x + last.runs.back().width;
		// Numéro de page provisoire « 000 », bord droit sur la marge droite du bloc.
		GlyphRun number{node.Id(), "000", 0, 0, 0, r.style, FieldKind::PageReference, true, e->node};
		number.width = r.style.font.TextWidth(number.text, r.style.size);
		number.x = inner - number.width;
		const double dot = r.style.font.TextWidth(" .", r.style.size);
		const double room = inner - kTocNumberSlot + 4 - end - 6;
		if (dot > 0 && room > dot) {
			const auto count = size_t(room / dot);
			GlyphRun leader{node.Id(), {}, 0, 0, 0, r.style, FieldKind::None, true, e->node};
			for (size_t k = 0; k < count; ++k)
				leader.text += " .";
			leader.width = double(count) * dot;
			leader.x = inner - kTocNumberSlot + 4 - leader.width; // points alignés d'une entrée à l'autre
			last.runs.push_back(std::move(leader));
		}
		last.runs.push_back(std::move(number));
		last.width = inner;
		for (LineBox &l : lines)
			b->lines.push_back(std::move(l));
	}
	if (b->lines.empty()) { // aucune entrée : une ligne vide (le bloc reste visible et sélectionnable)
		LineBox l = BuildLine({}, {}, ps, 0, inner, false);
		b->lines.push_back(std::move(l));
	}
	const double linesHeight = b->lines.back().y + b->lines.back().height;
	b->height = linesHeight + b->padding.top + b->padding.bottom + 2 * b->border;
	return b;
}

} // namespace

namespace detail {

std::string FieldText(FieldKind field, const FieldValues &values) {
	switch (field) {
	case FieldKind::None: return {};
	case FieldKind::PageNumber: return values.known ? std::to_string(values.page) : std::string("00");
	case FieldKind::PageCount: return values.known ? std::to_string(values.count) : std::string("00");
	case FieldKind::PageReference: return "00"; // rempli après la pagination (page de la cible)
	case FieldKind::Reference: return "?";		// texte calculé par la mesure (structure)
	case FieldKind::Chapter: return values.chapter;
	}
	return {};
}

bool IsLaidOut(const DocumentView &view, PropertyCache &cache, NodeId id) {
	const Node *n = view.Find(id);
	return n && TypeInfo(n->Type()).flowBlock && cache.Get(view, id, props::Visible);
}

BlockPtr Measure(const DocumentView &view, PropertyCache &cache, NodeId id, double width, double maxHeight, const FieldValues &fields,
				 const FontLibrary *fonts) {
	const Node *node = view.Find(id);
	if (!node)
		return std::make_shared<BlockLayout>();
	switch (node->Type()) {
	case NodeType::Paragraph: return MeasureParagraph(view, cache, *node, width, fields, fonts);
	case NodeType::Table: return MeasureTable(view, cache, *node, width, maxHeight, fields, fonts);
	case NodeType::Image:
	case NodeType::Shape: return MeasureImageOrShape(view, cache, *node, width, maxHeight, fields, fonts, false);
	case NodeType::Drawing: return MeasureDrawing(view, cache, *node, width, maxHeight, fields, fonts);
	case NodeType::TableOfContents: return MeasureToc(view, cache, *node, width, fields, fonts);
	default: {
		auto b = std::make_shared<BlockLayout>();
		b->node = id;
		b->type = node->Type();
		b->width = width;
		return b;
	}
	}
}

} // namespace detail

PageGeometry ResolvePageGeometry(const DocumentView &view, NodeId section, NodeId page) {
	const auto get = [&](PropertyId p) {
		return ResolvePageProperty(view, section, page, p).value;
	};
	PageGeometry g;
	g.format = Prop<PageFormat>::FromValue(get(PropertyId::Format));
	g.orientation = Prop<Orientation>::FromValue(get(PropertyId::PageOrientation));
	PageSize size = StandardSize(g.format);
	if (g.format == PageFormat::Custom) {
		size = {Prop<double>::FromValue(get(PropertyId::PageWidth)), Prop<double>::FromValue(get(PropertyId::PageHeight))};
		if (size.width <= 0 || size.height <= 0)
			size = StandardSize(PageFormat::A4);
	}
	size = Oriented(size, g.orientation);
	g.width = size.width;
	g.height = size.height;
	g.margins = {Prop<double>::FromValue(get(PropertyId::MarginTop)), Prop<double>::FromValue(get(PropertyId::MarginRight)),
				 Prop<double>::FromValue(get(PropertyId::MarginBottom)), Prop<double>::FromValue(get(PropertyId::MarginLeft))};
	// Marges incohérentes (plus grandes que la page) : ramenées pour garder une zone utile.
	g.margins.left = Clamp(g.margins.left, 0, g.width * 0.45);
	g.margins.right = Clamp(g.margins.right, 0, g.width * 0.45);
	g.margins.top = Clamp(g.margins.top, 0, g.height * 0.45);
	g.margins.bottom = Clamp(g.margins.bottom, 0, g.height * 0.45);
	g.headerDistance = Prop<double>::FromValue(get(PropertyId::HeaderDistance));
	g.footerDistance = Prop<double>::FromValue(get(PropertyId::FooterDistance));
	return g;
}

Resolved ResolvePageProperty(const DocumentView &view, NodeId section, NodeId page, PropertyId property) {
	if (page != kNoNode && view.Contains(page))
		return view.Resolve(page, property);
	// Page virtuelle : membre du groupe de pages de sa section (et de ses groupes ancêtres).
	if (const auto *g = view.Explicit(section, PropertyId::PageGroup))
		if (const auto group = NodeId(std::get<int64_t>(*g)); view.Contains(group)) {
			const Resolved r = view.Resolve(group, property);
			if (r.source != ValueSource::Default)
				return {r.value, ValueSource::Group, r.origin};
		}
	const PropertyInfo &info = Info(property);
	if (info.inheritable) {
		Resolved r = view.Resolve(section, property);
		if (r.source == ValueSource::Explicit || r.source == ValueSource::Group)
			r.source = ValueSource::Parent; // vu depuis la page
		return r;
	}
	if ((info.targets & MaskOf(NodeType::Page)) != 0)
		if (const PropertyValue *v = view.Explicit(view.Root(), property))
			return {*v, ValueSource::Document, view.Root()};
	return {info.defaultValue, ValueSource::Default, kNoNode};
}

TextStyle ResolveTextStyle(const DocumentView &view, PropertyCache &cache, NodeId node, const FontLibrary *fonts) {
	TextStyle s;
	const std::string family = cache.Get(view, node, props::FontFamily); // copie : Get peut réorganiser le cache
	const bool bold = cache.Get(view, node, props::Bold), italic = cache.Get(view, node, props::Italic);
	if (fonts)
		s.font = fonts->Resolve(family, bold, italic);
	else
		s.font.standard = pdf::MatchFont(family, bold, italic);
	s.size = cache.Get(view, node, props::FontSize);
	s.color = cache.Get(view, node, props::TextColor);
	s.background = cache.Get(view, node, props::BackgroundColor);
	s.underline = cache.Get(view, node, props::Underline);
	s.strike = cache.Get(view, node, props::Strike);
	switch (cache.Get(view, node, props::Script)) { // corps réduit, ligne de base décalée
	case TextScript::Superscript:
		s.rise = s.size * 0.33;
		s.size *= 0.65;
		break;
	case TextScript::Subscript:
		s.rise = -s.size * 0.14;
		s.size *= 0.65;
		break;
	case TextScript::Normal: break;
	}
	if (const Node *n = view.Find(node); n && n->Type() == NodeType::Text)
		s.link = cache.Get(view, node, props::Link);
	return s;
}

BlockPtr MeasureBlock(const DocumentView &view, PropertyCache &cache, NodeId block, double width, double maxHeight, const FontLibrary *fonts) {
	return detail::Measure(view, cache, block, width, maxHeight, {}, fonts);
}

} // namespace doc
