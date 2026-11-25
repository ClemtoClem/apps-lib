// Pagination : pages successives d'une section, règles de coupure des blocs, en-têtes et pieds
// de page, champs de numérotation, contenu propre des pages, cache incrémental des mesures.
#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_map>

#include <core/hash.hpp>
#include <jobs/jobs.hpp>

#include "internal.hpp"

namespace doc {

namespace {

constexpr double kMinContentHeight = 36; ///< zone de flux minimale si l'en-tête / le pied l'écrasent
constexpr double kEpsilon = 1e-6;

int64_t Quantize(double v) { return std::llround(v * 64.0); }

/// Bloc mis en page en lignes (paragraphe, table des matières).
bool HasLines(const BlockLayout &b) { return b.type == NodeType::Paragraph || b.type == NodeType::TableOfContents; }

} // namespace

class Layouter final : public detail::NestedMeasure {
public:
	Layouter(const DocumentView &view, LayoutCache *cache, const LayoutOptions &options)
		: m_view(view), m_cache(cache), m_options(options) {
		if (m_cache) {
			const uint64_t fonts = options.fonts ? options.fonts->Identity() : 0;
			if (m_cache->m_stamp != view.Stamp() || m_cache->m_fonts != fonts) { // autre document ou autres polices
				m_cache->m_entries.Clear();
				m_cache->m_outline.Clear();
				m_cache->m_stamp = view.Stamp();
				m_cache->m_fonts = fonts;
			}
			++m_cache->m_generation;
		}
	}

	LayoutResult Run() {
		m_result.stamp = m_view.Stamp();
		m_result.revision = m_view.Revision();
		// Structure (numéros, préfixes, tables des matières) avant toute mesure : contribution de
		// chaque bloc reprise du cache tant que le bloc et ses styles ne changent pas.
		m_outline = detail::AssembleOutline(m_view, m_styles, [this](NodeId block) -> const detail::OutlineItems & { return OutlineOf(block); });
		for (NodeId section : m_view.Sections())
			LayoutSection(section);
		Finish();
		if (m_cache) { // mesures non utilisées par cette mise en page : retirées
			const uint64_t generation = m_cache->m_generation;
			m_cache->m_entries.EraseIf([generation](const auto &, const LayoutCache::Entry &e) { return e.generation != generation; });
			m_cache->m_outline.EraseIf([generation](const auto &, const LayoutCache::OutlineMemo &e) { return e.generation != generation; });
		}
		return std::move(m_result);
	}

private:
	struct Key {
		uint64_t hash;
		LayoutCache::Entry probe;
	};

	/// Révision du nœud (descendants compris), style partagé (groupes, document) et, pour chaque
	/// ancêtre, son identité et la révision de son style transmis. Un style posé sur un autre
	/// paragraphe ne change donc pas la signature de ce bloc.
	void Signature(NodeId id, uint64_t &revision, uint64_t &style, uint64_t &ancestry) const {
		const Node *n = m_view.Find(id);
		revision = n ? n->Revision() : 0;
		style = m_view.SharedStyleRevision();
		ancestry = 0;
		for (const Node *a = n ? m_view.Find(n->Parent()) : nullptr; a; a = m_view.Find(a->Parent()))
			ancestry = core::HashCombine(core::HashCombine(ancestry, a->Id()), a->InheritedStyleRevision());
	}

	const detail::OutlineItems &OutlineOf(NodeId block) {
		if (!m_cache) {
			m_outlineScratch = detail::CollectOutline(m_view, m_styles, block);
			return m_outlineScratch;
		}
		uint64_t revision = 0, style = 0, ancestry = 0;
		Signature(block, revision, style, ancestry);
		LayoutCache::OutlineMemo *memo = m_cache->m_outline.Find(block);
		if (!memo || memo->revision != revision || memo->style != style || memo->ancestry != ancestry) {
			LayoutCache::OutlineMemo fresh{revision, style, ancestry, 0,
										   std::make_shared<const detail::OutlineItems>(detail::CollectOutline(m_view, m_styles, block))};
			m_cache->m_outline.InsertOrAssign(block, std::move(fresh));
			memo = m_cache->m_outline.Find(block);
		}
		memo->generation = m_cache->m_generation;
		return *memo->items;
	}

	Key MakeKey(NodeId id, double width, double maxHeight) const {
		LayoutCache::Entry e;
		e.node = id;
		Signature(id, e.revision, e.style, e.ancestry);
		e.width = Quantize(width);
		e.height = Quantize(maxHeight);
		if (const auto it = m_outline.contexts.find(id); it != m_outline.contexts.end())
			e.context = it->second;
		uint64_t h = core::HashCombine(core::Mix64(e.node), e.revision);
		h = core::HashCombine(h, e.style);
		h = core::HashCombine(h, e.ancestry);
		h = core::HashCombine(h, uint64_t(e.width));
		h = core::HashCombine(h, uint64_t(e.height));
		h = core::HashCombine(h, std::hash<std::string>{}(e.context));
		return {h, std::move(e)};
	}

	BlockPtr FromCache(const Key &key) {
		if (!m_cache)
			return nullptr;
		LayoutCache::Entry *e = m_cache->m_entries.Find(key.hash);
		if (!e || e->node != key.probe.node || e->revision != key.probe.revision || e->style != key.probe.style || e->ancestry != key.probe.ancestry ||
e->width != key.probe.width || e->height != key.probe.height || e->context != key.probe.context)
			return nullptr;
		if (e->generation != m_cache->m_generation) // mesure d'une mise en page précédente
			++m_result.stats.reused;
		Touch(*e);
		return e->block;
	}

	/// Garde l'entrée et, pour un tableau, les blocs de ses cellules (non revisités quand le
	/// tableau entier est repris).
	void Touch(LayoutCache::Entry &e) {
		e.generation = m_cache->m_generation;
		for (uint64_t h : e.nested)
			if (LayoutCache::Entry *n = m_cache->m_entries.Find(h); n && n->generation != m_cache->m_generation)
				Touch(*n);
	}

	void Store(Key key, BlockPtr block, std::vector<uint64_t> nested = {}) {
		if (!m_cache)
			return;
		key.probe.block = std::move(block);
		key.probe.nested = std::move(nested);
		key.probe.generation = m_cache->m_generation;
		m_cache->m_entries.InsertOrAssign(key.hash, std::move(key.probe));
	}

	/// Mesure d'un bloc du flux (cache, sinon mesure synchrone).
	/// Valeurs des champs pendant la pagination (numéros de page encore inconnus).
	[[nodiscard]] detail::FieldValues Fields() { return {0, 0, false, &m_outline, {}, this}; }

	/// Blocs des cellules d'un tableau : repris du cache (même clé que les blocs du corps),
	/// absents mesurés en parallèle s'ils sont nombreux. Modifier une cellule d'un tableau de
	/// 80 000 cellules ne remesure qu'elle.
	void MeasureAll(std::span<const detail::NestedRequest> requests, std::vector<BlockPtr> &out) override {
		out.assign(requests.size(), nullptr);
		std::vector<size_t> missing;
		std::vector<Key> keys;
		for (size_t i = 0; i < requests.size(); ++i) {
			const detail::NestedRequest &r = requests[i];
			if (m_outline.dependents.contains(r.id)) {
				out[i] = detail::Measure(m_view, m_styles, r.id, r.width, r.maxHeight, Fields(), m_options.fonts.get());
				++m_result.stats.cells;
				continue;
			}
			Key key = MakeKey(r.id, r.width, r.maxHeight);
			if (m_collect)
				m_collect->push_back(key.hash);
			if (BlockPtr hit = FromCache(key)) {
				out[i] = std::move(hit);
				continue;
			}
			missing.push_back(i);
			keys.push_back(std::move(key));
		}
		constexpr size_t kParallelThreshold = 64;
		if (missing.size() >= kParallelThreshold && m_options.parallel) {
			// Tâches parallèles : sans cache imbriqué (une cellule contenant un tableau le mesure
			// directement) ; une PropertyCache par tâche.
			detail::FieldValues fields{0, 0, false, &m_outline, {}, nullptr};
			jobs::ThreadPool &pool = m_options.pool ? *m_options.pool : jobs::ThreadPool::Global();
			jobs::ParallelFor(pool, 0, missing.size(), 16, [&](size_t k) {
				PropertyCache styles;
				const detail::NestedRequest &r = requests[missing[k]];
				out[missing[k]] = detail::Measure(m_view, styles, r.id, r.width, r.maxHeight, fields, m_options.fonts.get());
			});
			for (size_t k = 0; k < missing.size(); ++k)
				Store(std::move(keys[k]), out[missing[k]]);
		} else {
			for (size_t k = 0; k < missing.size(); ++k) { // tableau dans une cellule : ses cellules en cache aussi
				const detail::NestedRequest &r = requests[missing[k]];
				std::vector<uint64_t> nested;
				out[missing[k]] = MeasureCollecting(r.id, r.width, r.maxHeight, nested);
				Store(std::move(keys[k]), out[missing[k]], std::move(nested));
			}
		}
		m_result.stats.cells += missing.size();
	}

	BlockPtr Body(NodeId id, double width, double maxHeight) {
		if (m_outline.dependents.contains(id)) { // dépend de toute la structure : jamais en cache
			++m_result.stats.measured;
			return detail::Measure(m_view, m_styles, id, width, maxHeight, Fields(), m_options.fonts.get());
		}
		const Key key = MakeKey(id, width, maxHeight);
		if (BlockPtr hit = FromCache(key))
			return hit;
		std::vector<uint64_t> nested;
		BlockPtr b = MeasureCollecting(id, width, maxHeight, nested);
		++m_result.stats.measured;
		Store(key, b, std::move(nested));
		return b;
	}

	/// Mesure sur ce fil en relevant les clés des blocs de cellules consultés (tableaux).
	BlockPtr MeasureCollecting(NodeId id, double width, double maxHeight, std::vector<uint64_t> &nested) {
		std::vector<uint64_t> *saved = m_collect;
		m_collect = &nested;
		BlockPtr b = detail::Measure(m_view, m_styles, id, width, maxHeight, Fields(), m_options.fonts.get());
		m_collect = saved;
		return b;
	}

	/// Mesure en parallèle les blocs absents du cache (une PropertyCache par tâche).
	void Premeasure(const std::vector<NodeId> &blocks, double width, double maxHeight) {
		std::vector<NodeId> missing;
		std::vector<Key> keys;
		for (NodeId id : blocks) {
			// Tableaux mesurés sur ce fil : leurs cellules passent par le cache (et en parallèle).
			if (m_outline.dependents.contains(id) || m_view.Find(id)->Type() == NodeType::Table)
				continue;
			Key key = MakeKey(id, width, maxHeight);
			if (!m_cache || !FromCache(key)) {
				missing.push_back(id);
				keys.push_back(std::move(key));
			}
		}
		if (missing.size() < 2 || !m_options.parallel || !m_cache)
			return; // mesure à la demande pendant la pagination
		std::vector<BlockPtr> measured(missing.size());
		jobs::ThreadPool &pool = m_options.pool ? *m_options.pool : jobs::ThreadPool::Global();
		detail::FieldValues fields = Fields();
		fields.nested = nullptr; // tâches parallèles : le cache n'est écrit qu'après
		jobs::ParallelFor(pool, 0, missing.size(), 1, [&](size_t i) {
			PropertyCache styles;
			measured[i] = detail::Measure(m_view, styles, missing[i], width, maxHeight, fields, m_options.fonts.get());
		});
		for (size_t i = 0; i < missing.size(); ++i)
			Store(std::move(keys[i]), std::move(measured[i]));
		m_result.stats.measured += missing.size();
	}

	// ── En-têtes et pieds ──

	NodeId Choose(NodeType type, NodeId section, NodeId page, size_t indexInSection, size_t number) {
		if (page != kNoNode)
			if (const NodeId own = m_view.ChildOfType(page, type); own != kNoNode && m_styles.Get(m_view, own, props::Visible))
				return own;
		NodeId variants[4] = {kNoNode, kNoNode, kNoNode, kNoNode};
		if (const Node *s = m_view.Find(section))
			for (NodeId c : s->Children())
				if (const Node *n = m_view.Find(c); n && n->Type() == type && m_styles.Get(m_view, c, props::Visible)) {
					const auto v = size_t(m_styles.Get(m_view, c, props::Variant));
					if (variants[v] == kNoNode)
						variants[v] = c;
				}
		using V = HeaderFooterVariant;
		if (indexInSection == 0 && variants[size_t(V::First)] != kNoNode)
			return variants[size_t(V::First)];
		if (number % 2 == 0 && variants[size_t(V::Even)] != kNoNode)
			return variants[size_t(V::Even)];
		if (number % 2 == 1 && variants[size_t(V::Odd)] != kNoNode)
			return variants[size_t(V::Odd)];
		return variants[size_t(V::Default)];
	}

	/// Blocs d'un en-tête / pied empilés ; rend la hauteur totale.
	double StackHeight(NodeId container, double width, const detail::FieldValues &fields, std::vector<std::pair<double, BlockPtr>> *out) {
		double y = 0, lastAfter = 0;
		bool first = true;
		if (const Node *n = m_view.Find(container))
			for (NodeId c : n->Children()) {
				if (!detail::IsLaidOut(m_view, m_styles, c))
					continue;
				BlockPtr b = detail::Measure(m_view, m_styles, c, width, 1e9, fields, m_options.fonts.get());
				if (b->absolute)
					continue;
				if (!first)
					y += b->spaceBefore;
				if (out)
					out->emplace_back(y, b);
				y += b->height + b->spaceAfter;
				lastAfter = b->spaceAfter;
				first = false;
			}
		return y - lastAfter; // pas d'espace après le dernier bloc
	}

	// ── Placement ──

	void PlaceLines(PageLayout &page, const BlockLayout &b, double x, double y, size_t first, size_t count, uint16_t depth, bool whole,
					bool overflow) {
		const bool last = first + count >= b.lines.size();
		const double top = first == 0 ? b.padding.top + b.border : 0;
		double height = top;
		for (size_t i = first; i < first + count; ++i)
			height += b.lines[i].height;
		height += last ? b.padding.bottom + b.border : 0;
		if (whole)
			height = b.height;
		LayoutBox box{b.node, b.type, Rect(x + b.x, y, b.width, height), depth, first > 0, !last, overflow};
		page.boxes.push_back(box);
		const double originX = x + b.x + b.border + b.padding.left;
		const double baseY = count ? b.lines[first].y : 0;
		for (size_t i = first; i < first + count; ++i) {
			const LineBox &l = b.lines[i];
			PlacedLine pl;
			pl.paragraph = b.node;
			const double lineTop = y + top + (l.y - baseY);
			pl.rect = Rect(originX, lineTop, std::max(l.width, 0.0), l.height);
			pl.baseline = lineTop + l.baseline;
			pl.runs = l.runs;
			for (GlyphRun &r : pl.runs)
				r.x += originX;
			if (!pl.runs.empty()) {
				pl.rect.x = pl.runs.front().x;
				pl.rect.w = pl.runs.back().x + pl.runs.back().width - pl.rect.x;
			}
			page.lines.push_back(std::move(pl));
		}
	}

	void PlaceRows(PageLayout &page, const BlockLayout &b, double x, double y, const std::vector<size_t> &rows, uint16_t depth,
				   bool continued, bool continues, bool overflow) {
		double height = 0;
		for (size_t r : rows)
			height += b.rows[r].height;
		page.boxes.push_back({b.node, NodeType::Table, Rect(x + b.x, y, b.width, height), depth, continued, continues, overflow});
		double rowY = y;
		for (size_t r : rows) {
			const RowLayout &row = b.rows[r];
			page.boxes.push_back({row.node, NodeType::TableRow, Rect(x + b.x, rowY, b.width, row.height), uint16_t(depth + 1)});
			for (const CellLayout &cell : row.cells) {
				const double cellX = x + b.x + cell.x;
				// Cellule fusionnée verticalement : hauteur de ses lignes (jamais coupées entre deux pages).
				double cellH = 0;
				for (size_t k = r; k < std::min(b.rows.size(), r + cell.rows); ++k)
					cellH += b.rows[k].height;
				page.boxes.push_back({cell.node, NodeType::TableCell, Rect(cellX, rowY, cell.width, cellH), uint16_t(depth + 2)});
				double offset = 0;
				if (cell.align == VerticalAlign::Middle)
					offset = (cellH - cell.contentHeight) / 2;
				else if (cell.align == VerticalAlign::Bottom)
					offset = cellH - cell.contentHeight;
				const auto saved = std::pair{m_parentX, m_parentY};
				m_parentX = cellX + cell.border + cell.padding.left; // repère « parent » : contenu de la cellule
				m_parentY = rowY + offset + cell.border + cell.padding.top;
				for (const auto &[by, block] : cell.blocks)
					PlaceWhole(page, *block, m_parentX, m_parentY + by, uint16_t(depth + 3));
				std::tie(m_parentX, m_parentY) = saved;
			}
			rowY += row.height;
		}
	}

	void PlaceWhole(PageLayout &page, const BlockLayout &b, double x, double y, uint16_t depth, bool overflow = false) {
		switch (b.type) {
		case NodeType::Paragraph:
		case NodeType::TableOfContents: PlaceLines(page, b, x, y, 0, b.lines.size(), depth, true, overflow); break;
		case NodeType::Table: {
			std::vector<size_t> rows(b.rows.size());
			for (size_t i = 0; i < rows.size(); ++i)
				rows[i] = i;
			PlaceRows(page, b, x, y, rows, depth, false, false, overflow);
			break;
		}
		case NodeType::Image:
		case NodeType::Shape: PlaceShape(page, b, x + b.x, y, depth, overflow); break;
		case NodeType::Drawing: PlaceDrawing(page, b, x + b.x, y, depth, overflow); break;
		default: break;
		}
	}

	/// Zone de dessin : sa boîte, puis ses éléments dans le repère de sa zone intérieure (repère
	/// « nœud » et « parent » de ses éléments).
	void PlaceDrawing(PageLayout &page, const BlockLayout &b, double x, double y, uint16_t depth, bool overflow) {
		page.boxes.push_back({b.node, NodeType::Drawing, Rect(x, y, b.width, b.height), depth, false, false, overflow});
		const auto saved = std::pair{m_parentX, m_parentY};
		m_parentX = x + b.border + b.padding.left;
		m_parentY = y + b.border + b.padding.top;
		for (const auto &[unused, item] : b.content)
			PlaceShape(page, *item, m_parentX, m_parentY, uint16_t(depth + 1), overflow);
		std::tie(m_parentX, m_parentY) = saved;
	}

	/// Image ou forme : position dans son repère (place dans le flux, conteneur ou page), puis
	/// centre de rotation dans le sien.
	void PlaceShape(PageLayout &page, const BlockLayout &b, double slotX, double slotY, uint16_t depth, bool overflow = false) {
		PositionBase base = b.base;
		if (base == PositionBase::Auto)
			base = b.absolute ? PositionBase::Page : PositionBase::Node;
		double ox = 0, oy = 0;
		if (base == PositionBase::Node) {
			ox = slotX;
			oy = slotY;
		} else if (base == PositionBase::Parent) {
			ox = m_parentX;
			oy = m_parentY;
		}
		LayoutBox box{b.node, b.type, Rect(ox + b.positionX, oy + b.positionY, b.width, b.height), depth, false, false, overflow};
		box.rotation = b.rotation;
		box.z = b.z;
		switch (b.originBase) {
		case OriginBase::Node:
			box.pivotX = box.rect.x + box.rect.w / 2 + b.originX;
			box.pivotY = box.rect.y + box.rect.h / 2 + b.originY;
			break;
		case OriginBase::Parent:
			box.pivotX = m_parentX + b.originX;
			box.pivotY = m_parentY + b.originY;
			break;
		case OriginBase::Page:
			box.pivotX = b.originX;
			box.pivotY = b.originY;
			break;
		}
		box.slotX = slotX;
		box.slotY = slotY;
		box.parentX = m_parentX;
		box.parentY = m_parentY;
		box.alignX = b.x;
		page.boxes.push_back(box);
		if (b.content.empty())
			return;
		// Texte de la forme : paragraphes dans sa zone intérieure (alignement vertical), tournés avec elle.
		const Rect inner(box.rect.x + b.border + b.padding.left, box.rect.y + b.border + b.padding.top,
						 box.rect.w - 2 * b.border - b.padding.left - b.padding.right, box.rect.h - 2 * b.border - b.padding.top - b.padding.bottom);
		double top = inner.y;
		if (b.contentAlign == VerticalAlign::Middle)
			top += (inner.h - b.contentHeight) / 2;
		else if (b.contentAlign == VerticalAlign::Bottom)
			top += inner.h - b.contentHeight;
		const size_t firstBox = page.boxes.size(), firstLine = page.lines.size();
		const auto saved = std::pair{m_parentX, m_parentY};
		for (const auto &[py, paragraph] : b.content)
			PlaceWhole(page, *paragraph, inner.x, top + py, uint16_t(depth + 1), overflow);
		std::tie(m_parentX, m_parentY) = saved;
		for (size_t i = firstBox; i < page.boxes.size(); ++i) {
			LayoutBox &p = page.boxes[i];
			p.rotation = box.rotation;
			p.pivotX = box.pivotX;
			p.pivotY = box.pivotY;
			p.z = box.z; // dessiné avec sa forme
		}
		for (size_t i = firstLine; i < page.lines.size(); ++i) {
			page.lines[i].rotation = box.rotation;
			page.lines[i].pivotX = box.pivotX;
			page.lines[i].pivotY = box.pivotY;
		}
	}

	void PlaceAbsolute(PageLayout &page, const BlockLayout &b, double slotX, double slotY, uint16_t depth) {
		PlaceShape(page, b, slotX, slotY, depth);
	}

	// ── Pages ──

	struct Cursor {
		size_t page = 0; ///< indice dans m_result.pages
		double y = 0;
		bool empty = true;
	};

	void NewPage(NodeId section, const std::vector<NodeId> &existing, Cursor &cursor) {
		PageLayout p;
		p.section = section;
		p.indexInSection = m_sectionPages;
		p.number = m_result.pages.size() + 1;
		p.page = m_sectionPages < existing.size() ? existing[m_sectionPages] : kNoNode;
		p.geometry = ResolvePageGeometry(m_view, section, p.page);
		const PageGeometry &g = p.geometry;
		const double width = std::max(1.0, g.width - g.margins.left - g.margins.right);
		p.header = Choose(NodeType::Header, section, p.page, p.indexInSection, p.number);
		p.footer = Choose(NodeType::Footer, section, p.page, p.indexInSection, p.number);
		double top = g.margins.top, bottom = g.height - g.margins.bottom;
		if (p.header != kNoNode)
			top = std::max(top, g.headerDistance + StackHeight(p.header, width, Fields(), nullptr));
		if (p.footer != kNoNode)
			bottom = std::min(bottom, g.height - g.footerDistance - StackHeight(p.footer, width, Fields(), nullptr));
		if (bottom - top < kMinContentHeight)
			bottom = top + kMinContentHeight;
		p.content = Rect(g.margins.left, top, width, bottom - top);
		m_result.pages.push_back(std::move(p));
		++m_sectionPages;
		cursor = {m_result.pages.size() - 1, m_result.pages.back().content.y, true};
	}

	PageLayout &Page(const Cursor &c) { return m_result.pages[c.page]; }

	void LayoutSection(NodeId section) {
		const size_t firstPage = m_result.pages.size();
		m_sectionPages = 0;
		std::vector<NodeId> existing;
		if (const Node *s = m_view.Find(section))
			for (NodeId c : s->Children())
				if (const Node *n = m_view.Find(c); n && n->Type() == NodeType::Page)
					existing.push_back(c);
		std::vector<NodeId> blocks;
		if (const Node *body = m_view.Find(m_view.Body(section)))
			for (NodeId c : body->Children())
				if (detail::IsLaidOut(m_view, m_styles, c))
					blocks.push_back(c);

		Cursor cur;
		NewPage(section, existing, cur);
		Premeasure(blocks, Page(cur).content.w, Page(cur).content.h);

		for (size_t i = 0; i < blocks.size();) {
			const PageLayout &page = Page(cur);
			const double width = page.content.w, contentHeight = page.content.h, left = page.content.x;
			const double bottom = page.content.Bottom();
			BlockPtr bp = Body(blocks[i], width, contentHeight);
			const BlockLayout &b = *bp;
			if (b.type == NodeType::PageBreak) {
				NewPage(section, existing, cur);
				++i;
				continue;
			}
			if (b.absolute) {
				m_parentX = page.content.x; // repère « parent » du corps : zone de contenu de la page
				m_parentY = page.content.y;
				PlaceAbsolute(Page(cur), b, left, cur.y, 1);
				++i;
				continue;
			}
			if (b.pageBreakBefore && !cur.empty) {
				NewPage(section, existing, cur);
				continue;
			}
			const double before = cur.empty ? 0 : b.spaceBefore;
			const double available = bottom - cur.y;
			// KeepWithNext : ce bloc et le début du suivant sur la même page.
			double need = before + b.height;
			if (b.keepWithNext && i + 1 < blocks.size()) {
				BlockPtr next = Body(blocks[i + 1], width, contentHeight);
				double start = next->height;
				if (HasLines(*next) && next->allowSplit && !next->keepTogether && !next->lines.empty()) {
					const size_t n = std::min<size_t>(next->lines.size(), size_t(std::max(1, next->minLines)));
					start = next->padding.top + next->border + next->lines[n - 1].y + next->lines[n - 1].height;
				}
				if (next->type != NodeType::PageBreak && !next->absolute)
					need += b.spaceAfter + next->spaceBefore + start;
			}
			if (b.keepWithNext && need > available + kEpsilon && !cur.empty && need <= contentHeight) {
				NewPage(section, existing, cur);
				continue;
			}
			const bool tooManyLines = HasLines(b) && b.maxLines > 0 && b.lines.size() > size_t(b.maxLines) &&
									  b.allowSplit && !b.keepTogether;
			m_parentX = page.content.x;
			m_parentY = page.content.y;
			if (before + b.height <= available + kEpsilon && !tooManyLines) {
				PlaceWhole(Page(cur), b, left, cur.y + before, 1);
				cur.y += before + b.height + b.spaceAfter;
				cur.empty = false;
				++i;
				continue;
			}
			const bool splitParagraph = HasLines(b) && b.allowSplit && !b.keepTogether && b.lines.size() > 1;
			const bool splitTable = b.type == NodeType::Table && b.allowSplit && !b.keepTogether && b.rows.size() > 1;
			if (splitParagraph) {
				if (!PlaceParagraph(section, existing, cur, b)) {
					NewPage(section, existing, cur); // déplacé entier sur une nouvelle page : nouvel essai
					continue;
				}
				++i;
				continue;
			}
			if (splitTable) {
				if (!PlaceTable(section, existing, cur, b)) {
					NewPage(section, existing, cur);
					continue;
				}
				++i;
				continue;
			}
			// Bloc insécable : page suivante s'il y tient, sinon placé en débordement.
			if (!cur.empty && b.height <= contentHeight + kEpsilon) {
				NewPage(section, existing, cur);
				continue;
			}
			PlaceWhole(Page(cur), b, left, cur.y + before, 1, true);
			cur.y += before + b.height + b.spaceAfter;
			cur.empty = false;
			++i;
		}

		// Pages portant un contenu propre au-delà du flux : conservées.
		size_t keep = 0;
		for (size_t k = 0; k < existing.size(); ++k)
			if (const Node *n = m_view.Find(existing[k]); n && !n->Children().empty())
				keep = k + 1;
		while (m_sectionPages < keep)
			NewPage(section, existing, cur);
		m_result.sections.push_back({section, firstPage, m_result.pages.size() - firstPage});
	}

	/// Paragraphe coupé entre pages (lignes orphelines / veuves : MinimumLines ; MaximumLines par
	/// page). Rend faux si le paragraphe entier doit d'abord passer à la page suivante.
	bool PlaceParagraph(NodeId section, const std::vector<NodeId> &existing, Cursor &cur, const BlockLayout &b) {
		const size_t n = b.lines.size();
		const size_t minLines = size_t(std::max(1, b.minLines));
		size_t first = 0;
		while (first < n) {
			const PageLayout &page = Page(cur);
			const double before = (first == 0 && !cur.empty) ? b.spaceBefore : 0;
			double used = before + (first == 0 ? b.padding.top + b.border : 0);
			const double available = page.content.Bottom() - cur.y;
			size_t k = 0;
			while (first + k < n) {
				const bool last = first + k + 1 == n;
				const double h = b.lines[first + k].height + (last ? b.padding.bottom + b.border : 0);
				if (used + h > available + kEpsilon)
					break;
				used += h;
				++k;
			}
			if (b.maxLines > 0)
				k = std::min(k, size_t(b.maxLines));
			const size_t remaining = n - first - k;
			if (remaining > 0 && remaining < minLines && first + k >= minLines) // veuves
				k = std::min(k, n - first - minLines);
			const bool tooFewHere = first == 0 && k < std::min(minLines, n); // orphelines
			if ((k == 0 || tooFewHere) && !cur.empty) {
				if (first == 0)
					return false;
				NewPage(section, existing, cur);
				continue;
			}
			if (k == 0)
				k = 1; // page vide : au moins une ligne (débordement)
			const bool overflow = used > available + kEpsilon;
			PlaceLines(Page(cur), b, Page(cur).content.x, cur.y + before, first, k, 1, false, overflow);
			double height = (first == 0 ? b.padding.top + b.border : 0) + (first + k >= n ? b.padding.bottom + b.border : 0);
			for (size_t j = first; j < first + k; ++j)
				height += b.lines[j].height;
			first += k;
			cur.y += before + height;
			cur.empty = false;
			if (first < n)
				NewPage(section, existing, cur);
		}
		cur.y += b.spaceAfter;
		return true;
	}

	/// Tableau coupé entre lignes (lignes insécables, lignes d'en-tête répétées).
	bool PlaceTable(NodeId section, const std::vector<NodeId> &existing, Cursor &cur, const BlockLayout &b) {
		std::vector<size_t> header;
		for (size_t r = 0; r < b.rows.size() && b.rows[r].header; ++r)
			header.push_back(r);
		double headerHeight = 0;
		for (size_t r : header)
			headerHeight += b.rows[r].height;
		size_t next = header.size();
		bool firstFragment = true;
		if (next >= b.rows.size()) // uniquement des lignes d'en-tête : bloc entier
			next = 0, header.clear(), headerHeight = 0;
		while (next < b.rows.size()) {
			const PageLayout &page = Page(cur);
			const double before = firstFragment && !cur.empty ? b.spaceBefore : 0;
			const double available = page.content.Bottom() - cur.y - before;
			std::vector<size_t> rows = header;
			double used = headerHeight;
			const size_t start = next;
			// Lignes par groupes : celles qu'une cellule fusionnée relie restent ensemble.
			const auto group = [&](size_t from, double &height) {
				size_t end = from;
				height = b.rows[end].height;
				while (end + 1 < b.rows.size() && b.rows[end].joinNext)
					height += b.rows[++end].height;
				return end + 1;
			};
			double h = 0;
			while (next < b.rows.size()) {
				const size_t end = group(next, h);
				if (used + h > available + kEpsilon)
					break;
				used += h;
				next = end;
			}
			if (next == start) {
				if (!cur.empty) {
					if (firstFragment)
						return false;
					NewPage(section, existing, cur);
					continue;
				}
				next = group(next, h); // page vide : un groupe au moins (débordement)
				used += h;
			}
			for (size_t r = start; r < next; ++r)
				rows.push_back(r);
			PlaceRows(Page(cur), b, page.content.x, cur.y + before, rows, 1, !firstFragment, next < b.rows.size(), used > available + kEpsilon);
			cur.y += before + used;
			cur.empty = false;
			firstFragment = false;
			if (next < b.rows.size())
				NewPage(section, existing, cur);
		}
		cur.y += b.spaceAfter;
		return true;
	}

	/// Après la pagination : en-têtes / pieds avec les vrais numéros, champs du flux, contenu
	/// propre des pages.
	void Finish() {
		const size_t total = m_result.pages.size();
		// Première page de chaque paragraphe du flux (renvois de page, titres courants).
		std::unordered_map<NodeId, size_t> firstPage;
		for (size_t i = 0; i < total; ++i)
			for (const PlacedLine &line : m_result.pages[i].lines)
				firstPage.try_emplace(line.paragraph, i);
		for (size_t i = 0; i < total; ++i)
			for (const LayoutBox &box : m_result.pages[i].boxes)
				firstPage.try_emplace(box.node, i);
		// Titre courant de chaque page (champ Chapter) : premier titre de niveau 1 de la page,
		// sinon le dernier des pages précédentes.
		std::vector<std::string> chapters(total);
		{
			std::vector<std::vector<const OutlineEntry *>> starts(total);
			for (const OutlineEntry &e : m_outline.entries)
				if (e.kind == OutlineKind::Heading && e.level == 1)
					if (const auto it = firstPage.find(e.node); it != firstPage.end())
						starts[it->second].push_back(&e);
			std::string current;
			for (size_t i = 0; i < total; ++i) {
				const auto label = [](const OutlineEntry *e) { return e->number.empty() ? e->text : e->number + "\u00A0\u00A0" + e->text; };
				chapters[i] = starts[i].empty() ? current : label(starts[i].front());
				if (!starts[i].empty())
					current = label(starts[i].back());
			}
		}
		const auto pageOf = [&](NodeId target) -> std::string {
			const auto it = firstPage.find(target);
			return it == firstPage.end() ? std::string("?") : std::to_string(m_result.pages[it->second].number);
		};
		for (size_t i = 0; i < total; ++i) {
			PageLayout &page = m_result.pages[i];
			const detail::FieldValues fields{page.number, total, true, &m_outline, chapters[i]};
			const PageGeometry &g = page.geometry;
			for (PlacedLine &line : page.lines) {
				double shift = 0; // un champ du texte plus étroit (ou plus large) que « 00 » décale la suite de sa ligne
				for (GlyphRun &run : line.runs) {
					run.x += shift;
					if (run.field == FieldKind::None || run.field == FieldKind::Reference)
						continue;
					const double right = run.x + run.width;
					run.text = run.field == FieldKind::PageReference ? pageOf(run.target) : detail::FieldText(run.field, fields);
					const double width = run.style.font.TextWidth(run.text, run.style.size);
					if (run.generated) // numéro d'une table des matières : bord droit conservé
						run.x = right - width;
					else
						shift += width - run.width;
					run.width = width;
				}
				line.rect.w += shift;
			}
			if (page.header != kNoNode) {
				std::vector<std::pair<double, BlockPtr>> blocks;
				const double h = StackHeight(page.header, page.content.w, fields, &blocks);
				page.boxes.push_back({page.header, NodeType::Header, Rect(page.content.x, g.headerDistance, page.content.w, h), 1});
				m_parentX = page.content.x;
				m_parentY = g.headerDistance;
				for (const auto &[y, b] : blocks)
					PlaceWhole(page, *b, page.content.x, g.headerDistance + y, 2);
			}
			if (page.footer != kNoNode) {
				std::vector<std::pair<double, BlockPtr>> blocks;
				const double h = StackHeight(page.footer, page.content.w, fields, &blocks);
				const double top = g.height - g.footerDistance - h;
				page.boxes.push_back({page.footer, NodeType::Footer, Rect(page.content.x, top, page.content.w, h), 1});
				m_parentX = page.content.x;
				m_parentY = top;
				for (const auto &[y, b] : blocks)
					PlaceWhole(page, *b, page.content.x, top + y, 2);
			}
			if (page.page != kNoNode)
				if (const Node *n = m_view.Find(page.page))
					for (NodeId c : n->Children()) {
						const Node *child = m_view.Find(c);
						if (!child || (child->Type() != NodeType::Shape && child->Type() != NodeType::Image) ||
							!m_styles.Get(m_view, c, props::Visible))
							continue;
						BlockPtr b = detail::Measure(m_view, m_styles, c, g.width, g.height, fields, m_options.fonts.get());
						m_parentX = m_parentY = 0; // contenu propre d'une page : repère de la page
						PlaceAbsolute(page, *b, 0, 0, 1);
					}
		}
	}

	const DocumentView &m_view;
	LayoutCache *m_cache;
	LayoutOptions m_options;
	PropertyCache m_styles;
	Outline m_outline;
	detail::OutlineItems m_outlineScratch; ///< sans cache : éléments du bloc en cours
	std::vector<uint64_t> *m_collect = nullptr; ///< clés des cellules du tableau en cours de mesure
	LayoutResult m_result;
	size_t m_sectionPages = 0;
	double m_parentX = 0, m_parentY = 0; ///< origine du repère « parent » du bloc en cours de placement
};

LayoutResult Layout(const DocumentView &view, LayoutCache *cache, const LayoutOptions &options) {
	LayoutCache local; // sans cache fourni : un cache le temps de l'appel (mesure parallèle)
	return Layouter(view, cache ? cache : &local, options).Run();
}

std::pair<double, double> PlacedLine::Unrotate(double x, double y) const noexcept {
	if (rotation == 0)
		return {x, y};
	const double a = -rotation * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
	const double dx = x - pivotX, dy = y - pivotY;
	return {pivotX + dx * c - dy * s, pivotY + dx * s + dy * c};
}

bool LayoutBox::Contains(double x, double y) const noexcept {
	if (rotation != 0) { // point ramené dans le repère de la boîte non tournée
		const double a = -rotation * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
		const double dx = x - pivotX, dy = y - pivotY;
		x = pivotX + dx * c - dy * s;
		y = pivotY + dx * s + dy * c;
	}
	return rect.Contains(x, y);
}

NodeId LayoutResult::HitTest(size_t page, double x, double y) const {
	if (page >= pages.size())
		return kNoNode;
	const PageLayout &p = pages[page];
	for (auto it = p.lines.rbegin(); it != p.lines.rend(); ++it) {
		const auto [lx, ly] = it->Unrotate(x, y); // texte d'une forme tournée
		if (ly >= it->rect.y && ly <= it->rect.Bottom())
			for (const GlyphRun &run : it->runs)
				if (lx >= run.x && lx <= run.x + run.width)
					return run.node;
	}
	// Boîte du dessus : couche (ZIndex des images et formes), puis profondeur, puis ordre.
	const LayoutBox *best = nullptr;
	for (const LayoutBox &box : p.boxes)
		if (box.Contains(x, y) && (!best || std::pair{box.Layer(), box.depth} >= std::pair{best->Layer(), best->depth}))
			best = &box;
	return best ? best->node : kNoNode;
}

bool LayoutResult::Locate(NodeId node, size_t &page, double &y) const {
	for (size_t i = 0; i < pages.size(); ++i) {
		const PageLayout &p = pages[i];
		for (const PlacedLine &l : p.lines)
			if (l.paragraph == node || std::ranges::any_of(l.runs, [node](const GlyphRun &r) { return r.node == node && !r.generated; })) {
				page = i;
				y = l.rect.y;
				return true;
			}
		for (const LayoutBox &b : p.boxes)
			if (b.node == node) {
				page = i;
				y = b.rect.y;
				return true;
			}
	}
	return false;
}

const GlyphRun *LayoutResult::RunAt(size_t page, double x, double y) const {
	if (page >= pages.size())
		return nullptr;
	for (const PlacedLine &l : pages[page].lines) {
		const auto [lx, ly] = l.Unrotate(x, y);
		if (ly >= l.rect.y && ly <= l.rect.Bottom())
			for (const GlyphRun &r : l.runs)
				if (lx >= r.x && lx <= r.x + r.width)
					return &r;
	}
	return nullptr;
}

std::vector<size_t> LayoutResult::PagesOf(NodeId node) const {
	std::vector<size_t> out;
	for (size_t i = 0; i < pages.size(); ++i) {
		const PageLayout &p = pages[i];
		const bool inBoxes = std::ranges::any_of(p.boxes, [node](const LayoutBox &b) { return b.node == node; });
		const bool inLines = std::ranges::any_of(p.lines, [node](const PlacedLine &l) {
			return l.paragraph == node || std::ranges::any_of(l.runs, [node](const GlyphRun &r) { return r.node == node; });
		});
		if (inBoxes || inLines)
			out.push_back(i);
	}
	return out;
}

size_t SyncPages(Document &document, LayoutResult &layout) {
	size_t changes = 0;
	for (const SectionLayout &s : layout.sections) {
		std::vector<NodeId> existing;
		if (const Node *section = document.Find(s.section))
			for (NodeId c : section->Children())
				if (const Node *n = document.Find(c); n && n->Type() == NodeType::Page)
					existing.push_back(c);
		for (size_t i = 0; i < s.pageCount; ++i) {
			NodeId id = i < existing.size() ? existing[i] : kNoNode;
			if (id == kNoNode) {
				auto added = document.AddPage(s.section);
				if (!added)
					continue;
				id = added.Value();
				++changes;
			}
			layout.pages[s.firstPage + i].page = id;
		}
		for (size_t i = existing.size(); i-- > s.pageCount;)
			if (document.RemovePage(existing[i]).IsOk())
				++changes;
	}
	return changes;
}

} // namespace doc
