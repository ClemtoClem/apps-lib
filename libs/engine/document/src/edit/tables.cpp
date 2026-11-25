// Tableaux : grille, création rapide, lignes et colonnes, largeurs, bordures, lignes alternées.
#include "document/tables.hpp"

#include <algorithm>
#include <format>
#include <functional>

namespace doc {

// ── Grille ──

TableGrid TableGrid::Of(const DocumentView &view, NodeId table) {
	TableGrid g;
	g.table = table;
	if (const Node *t = view.Find(table); t && t->Type() == NodeType::Table)
		for (NodeId r : t->Children())
			if (const Node *row = view.Find(r); row && row->Type() == NodeType::TableRow) {
				g.rows.push_back(r);
				g.cells.emplace_back(row->Children().begin(), row->Children().end());
				g.columns = std::max(g.columns, g.cells.back().size());
			}
	return g;
}

NodeId TableGrid::Cell(size_t row, size_t column) const noexcept {
	return row < cells.size() && column < cells[row].size() ? cells[row][column] : kNoNode;
}

bool TableGrid::Find(NodeId cell, size_t &row, size_t &column) const noexcept {
	for (size_t r = 0; r < cells.size(); ++r)
		for (size_t c = 0; c < cells[r].size(); ++c)
			if (cells[r][c] == cell) {
				row = r;
				column = c;
				return true;
			}
	return false;
}

bool TableGrid::Find(const DocumentView &view, NodeId node, size_t &row, size_t &column) const {
	for (NodeId id = node; id != kNoNode;) {
		const Node *n = view.Find(id);
		if (!n)
			return false;
		if (n->Type() == NodeType::TableCell && Find(id, row, column))
			return true;
		id = n->Parent();
	}
	return false;
}

std::vector<NodeId> TableGrid::Range(size_t r0, size_t c0, size_t r1, size_t c1) const {
	if (r0 > r1)
		std::swap(r0, r1);
	if (c0 > c1)
		std::swap(c0, c1);
	std::vector<NodeId> out;
	for (size_t r = r0; r <= r1 && r < cells.size(); ++r)
		for (size_t c = c0; c <= c1 && c < cells[r].size(); ++c)
			out.push_back(cells[r][c]);
	return out;
}

std::vector<NodeId> TableGrid::Column(size_t column) const {
	std::vector<NodeId> out;
	for (const auto &row : cells)
		if (column < row.size())
			out.push_back(row[column]);
	return out;
}

namespace table {

namespace {

constexpr size_t kMaxColumns = 1000;
constexpr size_t kMaxCells = 1000000;

/// Exécute `body` dans une transaction (ou dans celle en cours) ; tout est annulé en cas d'échec.
Status Transact(History &history, std::string name, const std::function<Status()> &body) {
	const bool own = !history.InTransaction();
	if (own)
		history.BeginTransaction(std::move(name));
	Status s = body();
	if (!own)
		return s;
	if (!s) {
		(void)history.Rollback();
		return s;
	}
	return history.Commit();
}

/// Exécute une commande de création ; rend le nœud créé.
DocResult<NodeId> Make(History &history, CommandPtr command) {
	Command *raw = command.get();
	if (auto s = history.Execute(std::move(command)); !s)
		return Err(s.Error());
	return Ok(raw->Created());
}

Status Exec(History &history, CommandPtr command) { return history.Execute(std::move(command)); }

/// Propriétés explicites de `from` recopiées sur `to`, sauf celles de `skip`.
Status CopyStyle(History &history, NodeId from, NodeId to, std::initializer_list<PropertyId> skip) {
	const Node *n = history.Target().Find(from);
	if (!n)
		return Success();
	const PropertyMap props = n->Properties(); // copie : les commandes modifient le document
	for (const auto &[id, value] : props)
		if (std::ranges::find(skip, id) == skip.end())
			if (auto s = Exec(history, doc::SetProperty(to, id, value)); !s)
				return s;
	return Success();
}

NodeId FirstChildOfType(const Document &d, NodeId parent, NodeType type) {
	if (const Node *n = d.Find(parent))
		for (NodeId c : n->Children())
			if (d.Find(c)->Type() == type)
				return c;
	return kNoNode;
}

/// Cellule vide (paragraphe et segment) au rang `position` de `row`, mise en forme comme
/// `model` (cellule, premier paragraphe, premier segment) si elle existe.
DocResult<NodeId> EmptyCell(History &history, NodeId row, size_t position, NodeId model) {
	auto cell = Make(history, CreateNode(NodeType::TableCell, row, position));
	if (!cell)
		return cell;
	const Document &d = history.Target();
	if (model != kNoNode)
		if (auto s = CopyStyle(history, model, cell.Value(), {PropertyId::Name}); !s)
			return Err(s.Error());
	auto paragraph = Make(history, CreateNode(NodeType::Paragraph, cell.Value()));
	if (!paragraph)
		return paragraph;
	const NodeId modelParagraph = model != kNoNode ? FirstChildOfType(d, model, NodeType::Paragraph) : kNoNode;
	if (modelParagraph != kNoNode)
		if (auto s = CopyStyle(history, modelParagraph, paragraph.Value(), {PropertyId::Name, PropertyId::CaptionKind, PropertyId::OutlineLevel}); !s)
			return Err(s.Error());
	auto text = Make(history, CreateNode(NodeType::Text, paragraph.Value()));
	if (!text)
		return text;
	const NodeId modelText = modelParagraph != kNoNode ? FirstChildOfType(d, modelParagraph, NodeType::Text) : kNoNode;
	if (modelText != kNoNode)
		if (auto s = CopyStyle(history, modelText, text.Value(), {PropertyId::Name, PropertyId::Text, PropertyId::Field, PropertyId::Link}); !s)
			return Err(s.Error());
	return cell;
}

Status CheckTable(const Document &d, NodeId table) {
	const Node *n = d.Find(table);
	if (!n || n->Type() != NodeType::Table)
		return Failure(ErrorCode::NotFound, std::format("tableau {} introuvable", table));
	return Success();
}

} // namespace

DocResult<NodeId> Create(History &history, NodeId parent, size_t position, size_t rows, size_t columns, const PropertyMap &cell) {
	if (rows < 1 || columns < 1 || columns > kMaxColumns || rows > kMaxCells / columns)
		return Failure(ErrorCode::InvalidValue,
					   std::format("dimensions de tableau invalides : {} × {} (au plus {} colonnes et {} cellules)", rows, columns, kMaxColumns, kMaxCells));
	NodeId table = kNoNode;
	auto s = Transact(history, std::format("Insérer un tableau {}×{}", rows, columns), [&]() -> Status {
		auto t = Make(history, CreateNode(NodeType::Table, parent, position));
		if (!t)
			return Err(t.Error());
		table = t.Value();
		auto row = Make(history, CreateNode(NodeType::TableRow, table));
		if (!row)
			return Err(row.Error());
		for (size_t c = 0; c < columns; ++c) {
			auto created = EmptyCell(history, row.Value(), kAppend, kNoNode);
			if (!created)
				return Err(created.Error());
			for (const auto &[id, value] : cell)
				if (auto e = Exec(history, doc::SetProperty(created.Value(), id, value)); !e)
					return e;
		}
		// Lignes suivantes : copies de la première (une commande par ligne).
		for (size_t r = 1; r < rows; ++r)
			if (auto e = Exec(history, Duplicate(row.Value(), table, kAppend)); !e)
				return e;
		return Success();
	});
	if (!s)
		return Err(s.Error());
	return Ok(table);
}

DocResult<std::vector<NodeId>> InsertRows(History &history, NodeId table, size_t at, size_t count) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return Err(s.Error());
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (at > g.rows.size() || count == 0)
		return Failure(ErrorCode::InvalidValue, std::format("rang de ligne invalide : {}", at));
	if ((g.rows.size() + count) * std::max<size_t>(g.columns, 1) > kMaxCells)
		return Failure(ErrorCode::InvalidValue, "tableau trop grand");
	const size_t model = g.rows.empty() ? SIZE_MAX : at > 0 ? at - 1 : 0;
	std::vector<NodeId> created;
	auto s = Transact(history, count > 1 ? std::format("Insérer {} lignes", count) : "Insérer une ligne", [&]() -> Status {
		auto row = Make(history, CreateNode(NodeType::TableRow, table, at));
		if (!row)
			return Err(row.Error());
		if (model != SIZE_MAX)
			if (auto e = CopyStyle(history, g.rows[model], row.Value(), {PropertyId::Name, PropertyId::RepeatHeader}); !e)
				return e;
		const size_t columns = std::max<size_t>(g.columns, 1);
		for (size_t c = 0; c < columns; ++c)
			if (auto cell = EmptyCell(history, row.Value(), kAppend, model != SIZE_MAX ? g.Cell(model, c) : kNoNode); !cell)
				return Err(cell.Error());
		created.push_back(row.Value());
		for (size_t k = 1; k < count; ++k) {
			auto copy = Make(history, Duplicate(created.back()));
			if (!copy)
				return Err(copy.Error());
			created.push_back(copy.Value());
		}
		return Success();
	});
	if (!s)
		return Err(s.Error());
	return Ok(std::move(created));
}

Status InsertColumns(History &history, NodeId table, size_t at, size_t count) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (at > g.columns || count == 0)
		return Failure(ErrorCode::InvalidValue, std::format("rang de colonne invalide : {}", at));
	if (g.columns + count > kMaxColumns || g.rows.size() * (g.columns + count) > kMaxCells)
		return Failure(ErrorCode::InvalidValue, "tableau trop grand");
	return Transact(history, count > 1 ? std::format("Insérer {} colonnes", count) : "Insérer une colonne", [&]() -> Status {
		for (size_t r = 0; r < g.rows.size(); ++r) {
			const size_t position = std::min(at, g.cells[r].size());
			const NodeId model = g.Cell(r, at > 0 ? at - 1 : 0);
			auto cell = EmptyCell(history, g.rows[r], position, model);
			if (!cell)
				return Err(cell.Error());
			if (r == 0) // la largeur de la colonne voisine ne s'applique pas à la nouvelle
				if (history.Target().Find(cell.Value())->Properties().Contains(PropertyId::Width))
					if (auto e = Exec(history, ClearProperty(cell.Value(), PropertyId::Width)); !e)
						return e;
			NodeId last = cell.Value();
			for (size_t k = 1; k < count; ++k) {
				auto copy = Make(history, Duplicate(last));
				if (!copy)
					return Err(copy.Error());
				last = copy.Value();
			}
		}
		return Success();
	});
}

Status DeleteRows(History &history, NodeId table, size_t first, size_t count) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (count == 0 || first >= g.rows.size() || first + count > g.rows.size())
		return Failure(ErrorCode::InvalidValue, "lignes hors du tableau");
	return Transact(history, count > 1 ? std::format("Supprimer {} lignes", count) : "Supprimer une ligne", [&]() -> Status {
		if (count == g.rows.size()) // toutes les lignes : le tableau disparaît
			return Exec(history, DeleteNode(table));
		for (size_t k = count; k-- > 0;)
			if (auto e = Exec(history, DeleteNode(g.rows[first + k])); !e)
				return e;
		return Success();
	});
}

Status DeleteColumns(History &history, NodeId table, size_t first, size_t count) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (count == 0 || first >= g.columns || first + count > g.columns)
		return Failure(ErrorCode::InvalidValue, "colonnes hors du tableau");
	return Transact(history, count > 1 ? std::format("Supprimer {} colonnes", count) : "Supprimer une colonne", [&]() -> Status {
		if (count == g.columns)
			return Exec(history, DeleteNode(table));
		for (size_t r = 0; r < g.rows.size(); ++r)
			for (size_t k = count; k-- > 0;)
				if (const NodeId cell = g.Cell(r, first + k); cell != kNoNode)
					if (auto e = Exec(history, DeleteNode(cell)); !e)
						return e;
		return Success();
	});
}

DocResult<std::vector<NodeId>> DuplicateRows(History &history, NodeId table, size_t first, size_t count) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return Err(s.Error());
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (count == 0 || first + count > g.rows.size())
		return Failure(ErrorCode::InvalidValue, "lignes hors du tableau");
	if ((g.rows.size() + count) * std::max<size_t>(g.columns, 1) > kMaxCells)
		return Failure(ErrorCode::InvalidValue, "tableau trop grand");
	std::vector<NodeId> created;
	auto s = Transact(history, count > 1 ? std::format("Dupliquer {} lignes", count) : "Dupliquer une ligne", [&]() -> Status {
		for (size_t k = 0; k < count; ++k) {
			auto copy = Make(history, Duplicate(g.rows[first + k], table, first + count + k));
			if (!copy)
				return Err(copy.Error());
			if (history.Target().Find(copy.Value())->Properties().Contains(PropertyId::RepeatHeader)) // une copie n'est pas un en-tête
				if (auto e = Exec(history, ClearProperty(copy.Value(), PropertyId::RepeatHeader)); !e)
					return e;
			created.push_back(copy.Value());
		}
		return Success();
	});
	if (!s)
		return Err(s.Error());
	return Ok(std::move(created));
}

Status DuplicateColumns(History &history, NodeId table, size_t first, size_t count) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (count == 0 || first + count > g.columns)
		return Failure(ErrorCode::InvalidValue, "colonnes hors du tableau");
	if (g.columns + count > kMaxColumns || g.rows.size() * (g.columns + count) > kMaxCells)
		return Failure(ErrorCode::InvalidValue, "tableau trop grand");
	return Transact(history, count > 1 ? std::format("Dupliquer {} colonnes", count) : "Dupliquer une colonne", [&]() -> Status {
		for (size_t r = 0; r < g.rows.size(); ++r)
			for (size_t k = 0; k < count; ++k)
				if (const NodeId cell = g.Cell(r, first + k); cell != kNoNode)
					if (auto e = Exec(history, Duplicate(cell, g.rows[r], std::min(first + count + k, g.cells[r].size() + k))); !e)
						return e;
		return Success();
	});
}

namespace {

/// Déplace les nœuds `items[first … first+count)` (enfants de `parent`, dans cet ordre) avant
/// `items[to]`, situé après eux (fin si `to == items.size()`) : chacun se place devant l'ancre,
/// donc à la suite du précédent.
Status MoveAfter(History &history, NodeId parent, const std::vector<NodeId> &items, size_t first, size_t count, size_t to) {
	const Document &d = history.Target();
	for (size_t k = 0; k < count; ++k) {
		const size_t position = to < items.size() ? d.Find(parent)->IndexOfChild(items[to]) : kAppend;
		if (auto s = Exec(history, MoveNode(items[first + k], parent, position)); !s)
			return s;
	}
	return Success();
}

} // namespace

Status MoveRows(History &history, NodeId table, size_t first, size_t count, size_t to) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (count == 0 || first + count > g.rows.size() || to > g.rows.size())
		return Failure(ErrorCode::InvalidValue, "lignes hors du tableau");
	if (to >= first && to <= first + count)
		return Failure(ErrorCode::Nothing, "les lignes sont déjà à cette place");
	return Transact(history, count > 1 ? std::format("Déplacer {} lignes", count) : "Déplacer une ligne", [&]() -> Status {
		if (to < first) { // avant : chaque ligne se place à la suite de la précédente
			for (size_t k = 0; k < count; ++k)
				if (auto s = Exec(history, MoveNode(g.rows[first + k], table, to + k)); !s)
					return s;
			return Success();
		}
		return MoveAfter(history, table, g.rows, first, count, to);
	});
}

Status MoveColumns(History &history, NodeId table, size_t first, size_t count, size_t to) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (count == 0 || first + count > g.columns || to > g.columns)
		return Failure(ErrorCode::InvalidValue, "colonnes hors du tableau");
	if (to >= first && to <= first + count)
		return Failure(ErrorCode::Nothing, "les colonnes sont déjà à cette place");
	return Transact(history, count > 1 ? std::format("Déplacer {} colonnes", count) : "Déplacer une colonne", [&]() -> Status {
		for (size_t r = 0; r < g.rows.size(); ++r) {
			const std::vector<NodeId> &cells = g.cells[r];
			if (first + count > cells.size()) // ligne plus courte : cellules absentes ignorées
				continue;
			if (to < first) {
				for (size_t k = 0; k < count; ++k)
					if (auto s = Exec(history, MoveNode(cells[first + k], g.rows[r], to + k)); !s)
						return s;
			} else if (auto s = MoveAfter(history, g.rows[r], cells, first, count, std::min(to, cells.size())); !s) {
				return s;
			}
		}
		return Success();
	});
}

Status SetColumnWidth(History &history, NodeId table, size_t column, double width) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	if (column >= g.columns)
		return Failure(ErrorCode::InvalidValue, "colonne hors du tableau");
	const std::vector<NodeId> cells = g.Column(column);
	return Transact(history, "Largeur de colonne", [&]() -> Status {
		// La mesure retient la première cellule de la colonne qui porte une largeur : la nouvelle
		// valeur va sur la première, les autres largeurs explicites de la colonne sont retirées.
		for (size_t i = 1; i < cells.size(); ++i)
			if (history.Target().Find(cells[i])->Properties().Contains(PropertyId::Width))
				if (auto s = Exec(history, ClearProperty(cells[i], PropertyId::Width)); !s)
					return s;
		if (width <= 0)
			return history.Target().Find(cells.front())->Properties().Contains(PropertyId::Width)
					   ? Exec(history, ClearProperty(cells.front(), PropertyId::Width))
					   : Success();
		return Exec(history, doc::SetProperty(cells.front(), props::Width, width));
	});
}

Status DistributeColumns(History &history, NodeId table) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	return Transact(history, "Colonnes égales", [&]() -> Status {
		for (const auto &row : g.cells)
			for (NodeId cell : row)
				if (history.Target().Find(cell)->Properties().Contains(PropertyId::Width))
					if (auto s = Exec(history, ClearProperty(cell, PropertyId::Width)); !s)
						return s;
		return Success();
	});
}

Status SetRowHeight(History &history, NodeId row, double height) {
	const Node *n = history.Target().Find(row);
	if (!n || n->Type() != NodeType::TableRow)
		return Failure(ErrorCode::NotFound, std::format("ligne {} introuvable", row));
	if (height <= 0)
		return n->Properties().Contains(PropertyId::MinHeight) ? Exec(history, ClearProperty(row, PropertyId::MinHeight))
															   : Failure(ErrorCode::Nothing, "hauteur déjà automatique");
	return Exec(history, doc::SetProperty(row, props::MinHeight, height));
}

Status SetProperty(History &history, std::span<const NodeId> nodes, PropertyId property, PropertyValue value) {
	if (nodes.empty())
		return Failure(ErrorCode::Nothing, "aucun nœud");
	return Transact(history, std::format("{} ({} nœuds)", Info(property).name, nodes.size()), [&]() -> Status {
		for (NodeId id : nodes)
			if (auto s = Exec(history, doc::SetProperty(id, property, value)); !s)
				return s;
		return Success();
	});
}

Status ApplyBorders(History &history, NodeId table, BorderPreset preset, double width, Color color) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	return Transact(history, "Bordures du tableau", [&]() -> Status {
		if (auto s = Exec(history, doc::SetProperty(table, props::BorderWidth, 0.0)); !s) // le quadrillage est porté par les cellules
			return s;
		for (size_t r = 0; r < g.rows.size(); ++r)
			for (size_t c = 0; c < g.cells[r].size(); ++c) {
				const bool top = r == 0, bottom = r + 1 == g.rows.size(), left = c == 0, right = c + 1 == g.cells[r].size();
				int64_t sides = border::kAll;
				switch (preset) {
				case BorderPreset::All: break;
				case BorderPreset::None: sides = 0; break;
				case BorderPreset::Outer:
					sides = (top ? border::kTop : 0) | (bottom ? border::kBottom : 0) | (left ? border::kLeft : 0) | (right ? border::kRight : 0);
					break;
				case BorderPreset::Horizontal: sides = border::kTop | border::kBottom; break;
				case BorderPreset::Inner:
					sides = (top ? 0 : border::kTop) | (bottom ? 0 : border::kBottom) | (left ? 0 : border::kLeft) | (right ? 0 : border::kRight);
					break;
				}
				const NodeId cell = g.cells[r][c];
				const double w = sides == 0 ? 0.0 : width;
				if (auto s = Exec(history, doc::SetProperty(cell, props::BorderWidth, w)); !s)
					return s;
				if (w > 0) {
					if (auto s = Exec(history, doc::SetProperty(cell, props::BorderSides, sides)); !s)
						return s;
					if (auto s = Exec(history, doc::SetProperty(cell, props::BorderColor, color)); !s)
						return s;
				}
			}
		return Success();
	});
}

Status BandRows(History &history, NodeId table, Color even, Color odd, bool skipHeader) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	const TableGrid g = TableGrid::Of(history.Target(), table);
	return Transact(history, "Lignes alternées", [&]() -> Status {
		// Fond porté par les cellules (BackgroundColor ne cible pas les lignes : un groupe de fond
		// ne doit pas peindre une ligne entière).
		for (size_t r = skipHeader ? 1 : 0; r < g.rows.size(); ++r) {
			const Color color = (r - (skipHeader ? 1 : 0)) % 2 == 0 ? even : odd;
			for (NodeId cell : g.cells[r])
				if (auto s = Exec(history, doc::SetProperty(cell, props::BackgroundColor, color)); !s)
					return s;
		}
		return Success();
	});
}

// ── Cellules fusionnées ──

std::vector<CellSpan> Spans(const DocumentView &view, const std::vector<std::vector<NodeId>> &cells) {
	std::vector<CellSpan> out;
	std::vector<std::vector<bool>> covered(cells.size());
	for (size_t r = 0; r < cells.size(); ++r)
		covered[r].assign(cells[r].size(), false);
	for (size_t r = 0; r < cells.size(); ++r)
		for (size_t c = 0; c < cells[r].size(); ++c) {
			if (covered[r][c])
				continue;
			const NodeId cell = cells[r][c];
			if (!view.Contains(cell))
				continue;
			size_t rows = size_t(std::max<int64_t>(1, view.Get(cell, props::RowSpan)));
			size_t columns = size_t(std::max<int64_t>(1, view.Get(cell, props::ColumnSpan)));
			if (rows == 1 && columns == 1)
				continue;
			// Bornée à la grille, avant une case déjà couverte de sa première ligne.
			columns = std::min(columns, cells[r].size() - c);
			for (size_t k = 1; k < columns; ++k)
				if (covered[r][c + k]) {
					columns = k;
					break;
				}
			rows = std::min(rows, cells.size() - r);
			for (size_t k = 1; k < rows; ++k) // lignes plus courtes : la fusion s'arrête
				if (cells[r + k].size() < c + columns) {
					rows = k;
					break;
				}
			if (rows == 1 && columns == 1)
				continue;
			for (size_t i = 0; i < rows; ++i)
				for (size_t j = 0; j < columns; ++j)
					covered[r + i][c + j] = true;
			out.push_back({r, c, rows, columns});
		}
	return out;
}

CellSpan SpanAt(std::span<const CellSpan> spans, size_t r, size_t c) noexcept {
	for (const CellSpan &s : spans)
		if (s.Contains(r, c))
			return s;
	return {r, c, 1, 1};
}

namespace {

/// La cellule ne contient qu'un paragraphe sans texte (ou rien).
bool EmptyCell(const DocumentView &view, NodeId cell) {
	const Node *n = view.Find(cell);
	if (!n)
		return true;
	for (NodeId child : n->Children()) {
		const Node *c = view.Find(child);
		if (!c || c->Type() != NodeType::Paragraph)
			return false;
		for (NodeId t : c->Children())
			if (const Node *text = view.Find(t); !text || text->Type() != NodeType::Text || !view.Get(t, props::Text).empty())
				return false;
	}
	return n->Children().size() <= 1;
}

} // namespace

Status MergeCells(History &history, NodeId table, size_t r0, size_t c0, size_t r1, size_t c1) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	if (r0 > r1)
		std::swap(r0, r1);
	if (c0 > c1)
		std::swap(c0, c1);
	const Document &d = history.Target();
	const TableGrid g = TableGrid::Of(d, table);
	if (r1 >= g.rows.size() || c1 >= g.columns)
		return Failure(ErrorCode::InvalidValue, "cellules hors du tableau");
	for (size_t r = r0; r <= r1; ++r)
		if (g.cells[r].size() <= c1)
			return Failure(ErrorCode::InvalidValue, "ligne trop courte pour la fusion");
	if (r0 == r1 && c0 == c1)
		return Failure(ErrorCode::Nothing, "une seule cellule : rien à fusionner");
	const CellSpan target{r0, c0, r1 - r0 + 1, c1 - c0 + 1};
	const std::vector<CellSpan> spans = Spans(d, g.cells);
	std::vector<CellSpan> inside;
	for (const CellSpan &s : spans) {
		const bool overlaps = s.row <= r1 && s.row + s.rows > r0 && s.column <= c1 && s.column + s.columns > c0;
		const bool contained = s.row >= r0 && s.row + s.rows <= r1 + 1 && s.column >= c0 && s.column + s.columns <= c1 + 1;
		if (overlaps && !contained)
			return Failure(ErrorCode::InvalidValue, "la fusion couperait une fusion existante");
		if (contained)
			inside.push_back(s);
	}
	const NodeId anchor = g.cells[r0][c0];
	return Transact(history, "Fusionner les cellules", [&]() -> Status {
		for (const CellSpan &s : inside) // absorbées : la nouvelle fusion les couvre
			if (const NodeId cell = g.cells[s.row][s.column]; cell != anchor) {
				if (auto e = Exec(history, SetProperty(cell, props::RowSpan, int64_t(1))); !e)
					return e;
				if (auto e = Exec(history, SetProperty(cell, props::ColumnSpan, int64_t(1))); !e)
					return e;
			}
		// Contenu des autres cellules (ordre de lecture) ajouté à l'ancre ; elles gardent un
		// paragraphe vide (une cellule couverte reste un nœud du tableau).
		bool anchorEmpty = EmptyCell(history.Target(), anchor);
		for (size_t r = r0; r <= r1; ++r)
			for (size_t c = c0; c <= c1; ++c) {
				const NodeId cell = g.cells[r][c];
				if (cell == anchor || EmptyCell(history.Target(), cell))
					continue;
				if (anchorEmpty) { // ancre vide : son paragraphe vide laisse la place au contenu
					const std::vector<NodeId> old(history.Target().Find(anchor)->Children().begin(), history.Target().Find(anchor)->Children().end());
					for (NodeId child : old)
						if (auto e = Exec(history, DeleteNode(child)); !e)
							return e;
					anchorEmpty = false;
				}
				const std::vector<NodeId> moved(history.Target().Find(cell)->Children().begin(), history.Target().Find(cell)->Children().end());
				for (NodeId child : moved)
					if (auto e = Exec(history, MoveNode(child, anchor, kAppend)); !e)
						return e;
				if (auto e = Exec(history, CreateNode(NodeType::Paragraph, cell, kAppend)); !e)
					return e;
				const NodeId para = history.Target().Find(cell)->Children().back();
				if (auto e = Exec(history, CreateNode(NodeType::Text, para, kAppend)); !e)
					return e;
			}
		if (auto e = Exec(history, SetProperty(anchor, props::RowSpan, int64_t(target.rows))); !e)
			return e;
		return Exec(history, SetProperty(anchor, props::ColumnSpan, int64_t(target.columns)));
	});
}

Status SplitCells(History &history, NodeId table, size_t r0, size_t c0, size_t r1, size_t c1) {
	if (auto s = CheckTable(history.Target(), table); !s)
		return s;
	if (r0 > r1)
		std::swap(r0, r1);
	if (c0 > c1)
		std::swap(c0, c1);
	const Document &d = history.Target();
	const TableGrid g = TableGrid::Of(d, table);
	std::vector<NodeId> anchors;
	for (const CellSpan &s : Spans(d, g.cells))
		if (s.row <= r1 && s.row + s.rows > r0 && s.column <= c1 && s.column + s.columns > c0)
			anchors.push_back(g.cells[s.row][s.column]);
	if (anchors.empty())
		return Failure(ErrorCode::Nothing, "aucune cellule fusionnée à séparer");
	return Transact(history, "Séparer les cellules", [&]() -> Status {
		for (NodeId cell : anchors)
			for (PropertyId p : {PropertyId::RowSpan, PropertyId::ColumnSpan})
				if (history.Target().Find(cell)->Properties().Contains(p))
					if (auto e = Exec(history, ClearProperty(cell, p)); !e)
						return e;
		return Success();
	});
}

} // namespace table

} // namespace doc
