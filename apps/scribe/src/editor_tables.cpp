// Éditeur : sélection de lignes, colonnes, plages de cellules et opérations sur les tableaux
// (insertion, suppression, duplication, déplacement, largeurs, hauteurs, bordures, bandes).
#include <algorithm>
#include <format>
#include <functional>

#include "scribe/editor.hpp"

namespace scribe {

using namespace doc;

TableSelection Editor::CurrentTable() const {
	TableSelection t;
	// Cellules visées : cellules sélectionnées, ou cellule du curseur / des nœuds sélectionnés.
	std::vector<NodeId> cells;
	const auto cellOf = [&](NodeId id) {
		const Node *n = m_document->Find(id);
		if (!n)
			return kNoNode;
		return n->Type() == NodeType::TableCell ? id : m_document->AncestorOfType(id, NodeType::TableCell);
	};
	if (Editing()) {
		cells.push_back(cellOf(m_caret.text));
		if (HasTextRange())
			cells.push_back(cellOf(m_anchor.text));
	} else {
		for (NodeId id : m_selection.Nodes()) {
			const Node *n = m_document->Find(id);
			if (n && n->Type() == NodeType::TableRow) { // ligne sélectionnée : ses cellules
				for (NodeId c : n->Children())
					cells.push_back(c);
				continue;
			}
			if (n && n->Type() == NodeType::Table) {
				t.table = id;
				continue;
			}
			cells.push_back(cellOf(id));
		}
	}
	std::erase(cells, kNoNode);
	if (cells.empty() && t.table == kNoNode)
		return {};
	const NodeId table = !cells.empty() ? m_document->AncestorOfType(cells.front(), NodeType::Table) : t.table;
	const TableGrid g = TableGrid::Of(*m_document, table);
	if (g.rows.empty())
		return {};
	t.table = table;
	t.rows = g.rows.size();
	t.columns = g.columns;
	const std::vector<CellSpan> spans = table::Spans(*m_document, g.cells);
	std::vector<bool> rowHit(t.rows, false), columnHit(t.columns, false);
	if (cells.empty()) { // tableau entier
		t.row1 = t.rows - 1;
		t.column1 = t.columns - 1;
		rowHit.assign(t.rows, true);
		columnHit.assign(t.columns, true);
	} else {
		t.row0 = t.column0 = SIZE_MAX;
		size_t count = 0;
		for (NodeId c : cells) {
			size_t r = 0, col = 0;
			if (m_document->AncestorOfType(c, NodeType::Table) != table || !g.Find(c, r, col))
				continue; // autre tableau : seul le premier compte
			// Cellule fusionnée : tout son rectangle.
			const CellSpan span = table::SpanAt(spans, r, col);
			t.row0 = std::min(t.row0, span.row);
			t.row1 = std::max(t.row1, span.row + span.rows - 1);
			t.column0 = std::min(t.column0, span.column);
			t.column1 = std::max(t.column1, span.column + span.columns - 1);
			for (size_t k = 0; k < span.rows; ++k)
				rowHit[span.row + k] = true;
			for (size_t k = 0; k < span.columns; ++k)
				columnHit[span.column + k] = true;
			++count;
		}
		if (count == 0)
			return {};
	}
	for (size_t r = 0; r < t.rows; ++r)
		if (rowHit[r])
			t.rowList.push_back(r);
	for (size_t c = 0; c < t.columns; ++c)
		if (columnHit[c])
			t.columnList.push_back(c);
	// Lignes / colonnes entières : toutes leurs cellules sélectionnées (pas seulement le cadre).
	if (!Editing()) {
		size_t selected = 0;
		for (size_t r = t.row0; r <= t.row1; ++r)
			for (size_t c = t.column0; c <= t.column1; ++c)
				if (const NodeId cell = g.Cell(r, c); cell != kNoNode && (m_selection.Contains(cell) || m_selection.Contains(g.rows[r]) || m_selection.Contains(table)))
					++selected;
		const bool full = selected == t.RowCount() * t.ColumnCount();
		t.wholeRows = full && t.column0 == 0 && t.column1 + 1 == t.columns;
		t.wholeColumns = full && t.row0 == 0 && t.row1 + 1 == t.rows;
	}
	return t;
}

void Editor::SelectCells(NodeId table, size_t row0, size_t column0, size_t row1, size_t column1, TableSelect op) {
	const TableGrid g = TableGrid::Of(*m_document, table);
	if (g.rows.empty())
		return;
	row0 = std::min(row0, g.rows.size() - 1);
	row1 = std::min(row1, g.rows.size() - 1);
	column0 = std::min(column0, g.columns - 1);
	column1 = std::min(column1, g.columns - 1);
	if (row0 > row1)
		std::swap(row0, row1);
	if (column0 > column1)
		std::swap(column0, column1);
	// Cellules fusionnées touchées : le rectangle les contient entières (jusqu'à stabilité).
	const std::vector<CellSpan> spans = table::Spans(*m_document, g.cells);
	for (bool grown = true; grown;) {
		grown = false;
		for (const CellSpan &s : spans)
			if (s.row <= row1 && s.row + s.rows > row0 && s.column <= column1 && s.column + s.columns > column0) {
				const size_t r0 = std::min(row0, s.row), r1 = std::max(row1, s.row + s.rows - 1);
				const size_t c0 = std::min(column0, s.column), c1 = std::max(column1, s.column + s.columns - 1);
				grown = grown || r0 != row0 || r1 != row1 || c0 != column0 || c1 != column1;
				row0 = r0, row1 = r1, column0 = c0, column1 = c1;
			}
	}
	LeaveText();
	const std::vector<NodeId> cells = g.Range(row0, column0, row1, column1);
	SelectMode mode = SelectMode::Replace;
	if (op == TableSelect::Add)
		mode = SelectMode::Add;
	else if (op == TableSelect::Toggle) // tout déjà sélectionné : retiré ; sinon ajouté
		mode = std::ranges::all_of(cells, [&](NodeId c) { return m_selection.Contains(c); }) ? SelectMode::Remove : SelectMode::Add;
	m_selection.SelectIds(*m_document, cells, mode, true);
}

void Editor::SelectTable(NodeId table) { SelectCells(table, 0, 0, SIZE_MAX, SIZE_MAX); }

void Editor::SelectRows(NodeId table, size_t first, size_t last, TableSelect op) { SelectCells(table, first, 0, last, SIZE_MAX, op); }

void Editor::SelectColumns(NodeId table, size_t first, size_t last, TableSelect op) { SelectCells(table, 0, first, SIZE_MAX, last, op); }

namespace {

/// Suites contiguës d'une liste triée de rangs : (premier, nombre), de la dernière à la première
/// (supprimer ou dupliquer par la fin garde les rangs des suites précédentes).
std::vector<std::pair<size_t, size_t>> RunsFromEnd(const std::vector<size_t> &sorted) {
	std::vector<std::pair<size_t, size_t>> runs;
	for (size_t i = 0; i < sorted.size();) {
		size_t j = i + 1;
		while (j < sorted.size() && sorted[j] == sorted[j - 1] + 1)
			++j;
		runs.emplace_back(sorted[i], j - i);
		i = j;
	}
	std::ranges::reverse(runs);
	return runs;
}

} // namespace

Status Editor::MergeCells() {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	auto s = table::MergeCells(*m_history, t.table, t.row0, t.column0, t.row1, t.column1);
	Changed();
	if (s)
		SelectCells(t.table, t.row0, t.column0, t.row0, t.column0); // la cellule fusionnée
	return s;
}

Status Editor::SplitCells() {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	auto s = table::SplitCells(*m_history, t.table, t.row0, t.column0, t.row1, t.column1);
	Changed();
	if (s)
		SelectCells(t.table, t.row0, t.column0, t.row1, t.column1);
	return s;
}

bool Editor::SelectionHasMergedCells() const {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return false;
	for (const CellSpan &s : table::Spans(*m_document, TableGrid::Of(*m_document, t.table).cells))
		if (s.row <= t.row1 && s.row + s.rows > t.row0 && s.column <= t.column1 && s.column + s.columns > t.column0)
			return true;
	return false;
}

Status Editor::InsertRows(bool below, size_t count) {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	auto rows = table::InsertRows(*m_history, t.table, below ? t.row1 + 1 : t.row0, count);
	Changed();
	if (!rows)
		return Err(rows.Error());
	const size_t first = below ? t.row1 + 1 : t.row0;
	SelectRows(t.table, first, first + count - 1);
	return Success();
}

Status Editor::InsertColumns(bool right, size_t count) {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	const size_t at = right ? t.column1 + 1 : t.column0;
	auto s = table::InsertColumns(*m_history, t.table, at, count);
	Changed();
	if (s)
		SelectColumns(t.table, at, at + count - 1);
	return s;
}

Status Editor::InsertRowAt(NodeId tableId, size_t at) {
	auto rows = table::InsertRows(*m_history, tableId, at, 1);
	Changed();
	if (!rows)
		return Err(rows.Error());
	SelectCell(rows.Value().front(), 0);
	if (const NodeId t = m_selection.Primary(); m_document->Find(t) && m_document->Find(t)->Type() == NodeType::Text)
		PlaceCaret({t, 0});
	return Success();
}

Status Editor::InsertColumnAt(NodeId tableId, size_t at) {
	auto s = table::InsertColumns(*m_history, tableId, at, 1);
	Changed();
	if (s)
		SelectColumns(tableId, at, at);
	return s;
}

/// Lignes (ou colonnes) touchées par la sélection — non contiguës comprises — traitées par
/// suites, de la dernière à la première, en une seule entrée d'historique.
Status Editor::ForEachRun(bool rows, std::string_view name, const std::function<Status(size_t first, size_t count)> &run) {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	const auto runs = RunsFromEnd(rows ? t.rowList : t.columnList);
	if (runs.empty())
		return Failure(ErrorCode::Nothing, rows ? "aucune ligne visée" : "aucune colonne visée");
	return Transaction(std::string(name), [&]() -> Status {
		for (const auto &[first, count] : runs)
			if (auto s = run(first, count); !s)
				return s;
		return Success();
	});
}

Status Editor::DeleteRows() {
	const TableSelection t = CurrentTable();
	auto s = ForEachRun(true, "Supprimer des lignes", [&](size_t first, size_t count) { return table::DeleteRows(*m_history, t.table, first, count); });
	LeaveText();
	Changed();
	if (s && m_document->Contains(t.table)) { // la ligne suivante (ou la dernière) prend la sélection
		const size_t left = TableGrid::Of(*m_document, t.table).rows.size();
		if (left > 0)
			SelectRows(t.table, std::min(t.rowList.front(), left - 1), std::min(t.rowList.front(), left - 1));
	}
	return s;
}

Status Editor::DeleteColumns() {
	const TableSelection t = CurrentTable();
	auto s = ForEachRun(false, "Supprimer des colonnes",
						[&](size_t first, size_t count) { return table::DeleteColumns(*m_history, t.table, first, count); });
	LeaveText();
	Changed();
	if (s && m_document->Contains(t.table)) {
		const size_t left = TableGrid::Of(*m_document, t.table).columns;
		if (left > 0)
			SelectColumns(t.table, std::min(t.columnList.front(), left - 1), std::min(t.columnList.front(), left - 1));
	}
	return s;
}

Status Editor::DuplicateRows() {
	const TableSelection t = CurrentTable();
	auto s = ForEachRun(true, "Dupliquer des lignes", [&](size_t first, size_t count) -> Status {
		auto copies = table::DuplicateRows(*m_history, t.table, first, count);
		return copies ? Success() : Err(copies.Error());
	});
	Changed();
	if (s && t.ContiguousRows()) // une suite : ses copies sélectionnées
		SelectRows(t.table, t.row1 + 1, t.row1 + t.RowCount());
	return s;
}

Status Editor::DuplicateColumns() {
	const TableSelection t = CurrentTable();
	auto s = ForEachRun(false, "Dupliquer des colonnes",
						[&](size_t first, size_t count) { return table::DuplicateColumns(*m_history, t.table, first, count); });
	Changed();
	if (s && t.ContiguousColumns())
		SelectColumns(t.table, t.column1 + 1, t.column1 + t.ColumnCount());
	return s;
}

Status Editor::MoveRows(int delta) {
	const TableSelection t = CurrentTable();
	if (!t.Valid() || delta == 0)
		return Failure(ErrorCode::Nothing, "aucune ligne visée");
	if (!t.ContiguousRows())
		return Failure(ErrorCode::InvalidValue, "lignes non contiguës : impossible de les déplacer ensemble");
	const auto first = int64_t(t.row0) + delta;
	if (first < 0 || size_t(first) + t.RowCount() > t.rows)
		return Failure(ErrorCode::Nothing, "déjà au bord du tableau");
	const size_t to = delta < 0 ? size_t(first) : t.row1 + 1 + size_t(delta);
	auto s = table::MoveRows(*m_history, t.table, t.row0, t.RowCount(), to);
	Changed();
	if (s)
		SelectCells(t.table, size_t(first), t.column0, size_t(first) + t.RowCount() - 1, t.column1);
	return s;
}

Status Editor::MoveColumns(int delta) {
	const TableSelection t = CurrentTable();
	if (!t.Valid() || delta == 0)
		return Failure(ErrorCode::Nothing, "aucune colonne visée");
	if (!t.ContiguousColumns())
		return Failure(ErrorCode::InvalidValue, "colonnes non contiguës : impossible de les déplacer ensemble");
	const auto first = int64_t(t.column0) + delta;
	if (first < 0 || size_t(first) + t.ColumnCount() > t.columns)
		return Failure(ErrorCode::Nothing, "déjà au bord du tableau");
	const size_t to = delta < 0 ? size_t(first) : t.column1 + 1 + size_t(delta);
	auto s = table::MoveColumns(*m_history, t.table, t.column0, t.ColumnCount(), to);
	Changed();
	if (s)
		SelectCells(t.table, t.row0, size_t(first), t.row1, size_t(first) + t.ColumnCount() - 1);
	return s;
}

Status Editor::SetColumnWidth(NodeId tableId, size_t column, double width) {
	auto s = table::SetColumnWidth(*m_history, tableId, column, width);
	Changed();
	return s;
}

Status Editor::SetRowHeight(NodeId row, double height) {
	auto s = table::SetRowHeight(*m_history, row, height);
	Changed();
	return s;
}

Status Editor::DistributeColumns() {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	auto s = table::DistributeColumns(*m_history, t.table);
	Changed();
	return s;
}

Status Editor::ApplyBorders(BorderPreset preset, double width, Color color) {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	auto s = table::ApplyBorders(*m_history, t.table, preset, width, color);
	Changed();
	return s;
}

Status Editor::BandRows(Color even, Color odd) {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	const bool header = m_document->Get(TableGrid::Of(*m_document, t.table).rows.front(), props::RepeatHeader);
	auto s = table::BandRows(*m_history, t.table, even, odd, header);
	Changed();
	return s;
}

Status Editor::ToggleHeaderRow() {
	const TableSelection t = CurrentTable();
	if (!t.Valid())
		return Failure(ErrorCode::Nothing, "aucun tableau visé");
	const NodeId first = TableGrid::Of(*m_document, t.table).rows.front();
	return Run(doc::SetProperty(first, props::RepeatHeader, !m_document->Get(first, props::RepeatHeader)));
}

} // namespace scribe
