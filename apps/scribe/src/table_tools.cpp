#include "scribe/table_tools.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

#include <document/tables.hpp>

namespace scribe {

using namespace doc;

size_t TableGeometry::ColumnAt(double x) const noexcept {
	for (size_t i = 0; i + 1 < columns.size(); ++i)
		if (x >= columns[i] && x < columns[i + 1])
			return i;
	return SIZE_MAX;
}

size_t TableGeometry::RowAt(double y) const noexcept {
	for (size_t i = 0; i + 1 < rows.size(); ++i)
		if (y >= rows[i] && y < rows[i + 1])
			return i;
	return SIZE_MAX;
}

std::vector<TableGeometry> TablesOn(const DocumentView &view, const LayoutResult &layout, size_t page) {
	std::vector<TableGeometry> out;
	if (page >= layout.pages.size())
		return out;
	const PageLayout &p = layout.pages[page];
	std::map<NodeId, size_t> index; // tableau → indice dans `out`
	for (size_t b = 0; b < p.boxes.size(); ++b) {
		const LayoutBox &box = p.boxes[b];
		if (box.type == NodeType::Table) {
			if (index.contains(box.node)) // un seul fragment par page
				continue;
			index[box.node] = out.size();
			TableGeometry g;
			g.table = box.node;
			g.page = page;
			g.rect = box.rect;
			out.push_back(std::move(g));
			continue;
		}
		if (box.type != NodeType::TableRow)
			continue;
		const Node *row = view.Find(box.node);
		if (!row)
			continue;
		const auto it = index.find(row->Parent());
		if (it == index.end())
			continue;
		TableGeometry &g = out[it->second];
		if (g.rows.empty())
			g.rows.push_back(box.rect.y);
		g.rows.push_back(box.rect.Bottom());
		g.rowNodes.push_back(box.node);
		g.rowIndices.push_back(view.Find(row->Parent())->IndexOfChild(box.node));
	}
	// Colonnes : bords des cellules du fragment, cellules fusionnées comprises (leur bord gauche
	// est celui de leur première colonne, leur bord droit celui de la dernière) ; un bord que
	// seules des fusions recouvrent est interpolé entre ses voisins connus.
	for (TableGeometry &g : out) {
		const TableGrid grid = TableGrid::Of(view, g.table);
		const std::vector<CellSpan> spans = table::Spans(view, grid.cells);
		std::vector<double> edges(grid.columns + 1, std::numeric_limits<double>::quiet_NaN());
		for (const LayoutBox &cell : p.boxes) {
			size_t r = 0, c = 0;
			if (cell.type != NodeType::TableCell || !std::ranges::contains(g.rowNodes, view.Find(cell.node) ? view.Find(cell.node)->Parent() : kNoNode) ||
				!grid.Find(cell.node, r, c))
				continue;
			const CellSpan span = table::SpanAt(spans, r, c);
			edges[c] = cell.rect.x;
			edges[std::min(grid.columns, c + span.columns)] = cell.rect.Right();
		}
		size_t last = SIZE_MAX;
		for (size_t i = 0; i < edges.size(); ++i) {
			if (std::isnan(edges[i]))
				continue;
			if (last != SIZE_MAX && i > last + 1) // trou : réparti également
				for (size_t k = last + 1; k < i; ++k)
					edges[k] = edges[last] + (edges[i] - edges[last]) * double(k - last) / double(i - last);
			last = i;
		}
		if (std::ranges::all_of(edges, [](double e) { return !std::isnan(e); }))
			g.columns = std::move(edges);
	}
	std::erase_if(out, [](const TableGeometry &g) { return g.rowNodes.empty() || g.columns.size() < 2; });
	return out;
}

TableHit HitTable(const TableGeometry &t, double x, double y, double tolerance, double margin) {
	TableHit hit;
	if (t.columns.size() < 2 || t.rows.size() < 2)
		return hit;
	const double left = t.columns.front(), right = t.columns.back(), top = t.rows.front(), bottom = t.rows.back();
	const bool inX = x >= left - tolerance && x <= right + tolerance, inY = y >= top - tolerance && y <= bottom + tolerance;
	// Bords à glisser (intérieur et bord droit / bas).
	if (inX && inY) {
		for (size_t i = 1; i < t.columns.size(); ++i)
			if (std::abs(x - t.columns[i]) <= tolerance && y >= top && y <= bottom)
				return {TableHitKind::ColumnBorder, i};
		for (size_t i = 1; i < t.rows.size(); ++i)
			if (std::abs(y - t.rows[i]) <= tolerance && x >= left && x <= right)
				return {TableHitKind::RowBorder, i - 1};
		return hit;
	}
	// Bande au-dessus : « + » près d'un bord vertical, sinon sélection de la colonne.
	if (y >= top - margin && y < top && x >= left - margin && x <= right + tolerance) {
		if (x < left - tolerance)
			return {TableHitKind::SelectTable, 0};
		for (size_t i = 0; i < t.columns.size(); ++i)
			if (std::abs(x - t.columns[i]) <= std::max(tolerance, margin * 0.4))
				return {TableHitKind::InsertColumn, i};
		if (const size_t c = t.ColumnAt(x); c != SIZE_MAX)
			return {TableHitKind::SelectColumn, c};
		return hit;
	}
	// Bande à gauche : « + » près d'un bord horizontal, sinon sélection de la ligne.
	if (x >= left - margin && x < left && y >= top && y <= bottom + tolerance) {
		for (size_t i = 0; i < t.rows.size(); ++i)
			if (std::abs(y - t.rows[i]) <= std::max(tolerance, margin * 0.4)) {
				const size_t at = i < t.rowIndices.size() ? t.rowIndices[i] : t.rowIndices.back() + 1;
				return {TableHitKind::InsertRow, at};
			}
		if (const size_t r = t.RowAt(y); r != SIZE_MAX)
			return {TableHitKind::SelectRow, t.rowIndices[r]};
	}
	return hit;
}

} // namespace scribe
