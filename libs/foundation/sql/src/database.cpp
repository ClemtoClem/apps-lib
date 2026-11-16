#include "sql/database.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>

#include <dataio/binary.hpp>

#include "parser.hpp"

namespace sql {
namespace {

// ── Évaluation d'expressions ───────────────────────────────────────────────
struct EvalContext {
	const Table *table = nullptr;
	RowId row = kNoRow;
	std::span<const Value> params;
	const std::vector<int> *columnOf = nullptr; // emplacement → index de colonne
};

bool Truthy(const Value &v) {
	if (v.IsNull())
		return false;
	if (v.Type() == ColumnType::Real)
		return v.AsReal() != 0.0;
	if (v.Type() == ColumnType::Text)
		return !v.AsText().empty();
	return v.AsInt() != 0;
}

bool LikeMatch(std::string_view s, std::string_view p) {
	// '%' : toute suite, '_' : un caractère. Algorithme glouton avec retour arrière.
	size_t si = 0, pi = 0, star = std::string_view::npos, mark = 0;
	while (si < s.size()) {
		if (pi < p.size() && (p[pi] == '_' || p[pi] == s[si])) {
			++si;
			++pi;
		} else if (pi < p.size() && p[pi] == '%') {
			star = pi++;
			mark = si;
		} else if (star != std::string_view::npos) {
			pi = star + 1;
			si = ++mark;
		} else {
			return false;
		}
	}
	while (pi < p.size() && p[pi] == '%')
		++pi;
	return pi == p.size();
}

Value Eval(const Expr &e, const EvalContext &cx) {
	switch (e.kind) {
	case Expr::Kind::Literal: return e.literal;
	case Expr::Kind::Param:
		return static_cast<size_t>(e.param) < cx.params.size() ? cx.params[static_cast<size_t>(e.param)] : Value();
	case Expr::Kind::Column: {
		const int c = (*cx.columnOf)[static_cast<size_t>(e.columnSlot)];
		return cx.table->Get(cx.row, c);
	}
	case Expr::Kind::Unary: {
		const Value v = Eval(*e.args[0], cx);
		if (e.op == Op::Not)
			return Value(!Truthy(v));
		if (v.Type() == ColumnType::Int)
			return Value(-v.AsInt());
		return v.IsNull() ? Value() : Value(-v.AsReal());
	}
	case Expr::Kind::IsNull: return Value(Eval(*e.args[0], cx).IsNull() != e.negated);
	case Expr::Kind::Between: {
		const Value v = Eval(*e.args[0], cx);
		const bool in = !v.IsNull() && v >= Eval(*e.args[1], cx) && v <= Eval(*e.args[2], cx);
		return Value(in != e.negated);
	}
	case Expr::Kind::In: {
		const Value v = Eval(*e.args[0], cx);
		bool found = false;
		for (size_t i = 1; i < e.args.size() && !found; ++i)
			found = v == Eval(*e.args[i], cx);
		return Value(found != e.negated);
	}
	case Expr::Kind::Like: {
		const Value v = Eval(*e.args[0], cx), p = Eval(*e.args[1], cx);
		const std::string sv = v.ToString(), sp = p.ToString();
		return Value(LikeMatch(sv, sp) != e.negated);
	}
	case Expr::Kind::Binary: break;
	}
	if (e.op == Op::And)
		return Value(Truthy(Eval(*e.args[0], cx)) && Truthy(Eval(*e.args[1], cx)));
	if (e.op == Op::Or)
		return Value(Truthy(Eval(*e.args[0], cx)) || Truthy(Eval(*e.args[1], cx)));
	const Value a = Eval(*e.args[0], cx), b = Eval(*e.args[1], cx);
	switch (e.op) {
	case Op::Eq: return (a.IsNull() || b.IsNull()) ? Value(false) : Value(a == b);
	case Op::Ne: return (a.IsNull() || b.IsNull()) ? Value(false) : Value(a != b);
	case Op::Lt: return Value(!a.IsNull() && !b.IsNull() && a < b);
	case Op::Le: return Value(!a.IsNull() && !b.IsNull() && a <= b);
	case Op::Gt: return Value(!a.IsNull() && !b.IsNull() && a > b);
	case Op::Ge: return Value(!a.IsNull() && !b.IsNull() && a >= b);
	default: break;
	}
	if (a.IsNull() || b.IsNull())
		return Value();
	if (e.op == Op::Add && (a.Type() == ColumnType::Text || b.Type() == ColumnType::Text))
		return Value(a.ToString() + b.ToString());
	if (a.Type() == ColumnType::Int && b.Type() == ColumnType::Int) {
		const int64_t x = a.AsInt(), y = b.AsInt();
		switch (e.op) {
		case Op::Add: return Value(x + y);
		case Op::Sub: return Value(x - y);
		case Op::Mul: return Value(x * y);
		case Op::Div: return y == 0 ? Value() : Value(x / y);
		case Op::Mod: return y == 0 ? Value() : Value(x % y);
		default: return Value();
		}
	}
	const double x = a.AsReal(), y = b.AsReal();
	switch (e.op) {
	case Op::Add: return Value(x + y);
	case Op::Sub: return Value(x - y);
	case Op::Mul: return Value(x * y);
	case Op::Div: return y == 0 ? Value() : Value(x / y);
	case Op::Mod: return y == 0 ? Value() : Value(std::fmod(x, y));
	default: return Value();
	}
}

/// Résout les noms de colonnes d'une expression vers leurs index.
bool Resolve(const Expr *e, const Schema &schema, std::vector<int> &columnOf, std::string &err) {
	if (!e)
		return true;
	if (e->kind == Expr::Kind::Column) {
		const int c = schema.Find(e->column);
		if (c < 0) {
			err = std::format("colonne inconnue '{}'", e->column);
			return false;
		}
		columnOf[static_cast<size_t>(e->columnSlot)] = c;
	}
	for (const auto &a : e->args)
		if (!Resolve(a.get(), schema, columnOf, err))
			return false;
	return true;
}

/// Une expression sans colonne peut être évaluée une fois (constante ou paramètre).
bool IsConstant(const Expr &e) {
	if (e.kind == Expr::Kind::Column)
		return false;
	return std::ranges::all_of(e.args, [](const ExprPtr &a) { return IsConstant(*a); });
}

void FlattenAnd(const Expr *e, std::vector<const Expr *> &out) {
	if (!e)
		return;
	if (e->kind == Expr::Kind::Binary && e->op == Op::And) {
		FlattenAnd(e->args[0].get(), out);
		FlattenAnd(e->args[1].get(), out);
	} else {
		out.push_back(e);
	}
}

// ── Planificateur ──────────────────────────────────────────────────────────
struct Plan {
	enum class Kind { Scan, Key, Range } kind = Kind::Scan;
	RowId keyRow = kNoRow;
	int column = -1;
	int64_t lo = INT64_MIN, hi = INT64_MAX;
	std::string description = "scan";
};

Plan ChoosePlan(const Table &t, const Expr *where, const EvalContext &cx) {
	Plan best;
	std::vector<const Expr *> conj;
	FlattenAnd(where, conj);
	const int pk = t.GetSchema().PrimaryKey();
	auto columnOf = [&](const Expr &e) { return (*cx.columnOf)[static_cast<size_t>(e.columnSlot)]; };
	for (const Expr *c : conj) {
		if (c->kind == Expr::Kind::Between && !c->negated && c->args[0]->kind == Expr::Kind::Column && IsConstant(*c->args[1]) &&
			IsConstant(*c->args[2])) {
			const int col = columnOf(*c->args[0]);
			if (t.Index(col) && best.kind == Plan::Kind::Scan) {
				best.kind = Plan::Kind::Range;
				best.column = col;
				best.lo = Eval(*c->args[1], cx).AsInt();
				best.hi = Eval(*c->args[2], cx).AsInt();
				best.description = std::format("btree range({})", t.GetSchema().columns[static_cast<size_t>(col)].name);
			}
			continue;
		}
		if (c->kind != Expr::Kind::Binary || c->op > Op::Ge || c->op == Op::Ne)
			continue;
		const Expr *colE = c->args[0].get(), *valE = c->args[1].get();
		Op op = c->op;
		if (colE->kind != Expr::Kind::Column) {
			std::swap(colE, valE);
			switch (op) { // inverse le sens : 5 < x  ⇔  x > 5
			case Op::Lt: op = Op::Gt; break;
			case Op::Le: op = Op::Ge; break;
			case Op::Gt: op = Op::Lt; break;
			case Op::Ge: op = Op::Le; break;
			default: break;
			}
		}
		if (colE->kind != Expr::Kind::Column || !IsConstant(*valE))
			continue;
		const int col = columnOf(*colE);
		const Value v = Eval(*valE, cx);
		if (col == pk && op == Op::Eq) {
			best.kind = Plan::Kind::Key;
			best.keyRow = t.FindByKey(v);
			best.description = "hash pk";
			return best; // meilleur plan possible
		}
		if (!t.Index(col) || !v.IsNumeric())
			continue;
		Plan p;
		p.kind = Plan::Kind::Range;
		p.column = col;
		const double d = v.AsReal();
		switch (op) {
		case Op::Eq:
			if (d != std::floor(d))
				continue;
			p.lo = p.hi = v.AsInt();
			break;
		case Op::Lt: p.hi = static_cast<int64_t>(std::ceil(d)) - 1; break;
		case Op::Le: p.hi = static_cast<int64_t>(std::floor(d)); break;
		case Op::Gt: p.lo = static_cast<int64_t>(std::floor(d)) + 1; break;
		case Op::Ge: p.lo = static_cast<int64_t>(std::ceil(d)); break;
		default: continue;
		}
		p.description = std::format("btree {}({})", op == Op::Eq ? "eq" : "range", t.GetSchema().columns[static_cast<size_t>(col)].name);
		// Une égalité est plus sélective qu'une plage.
		if (best.kind == Plan::Kind::Scan || (op == Op::Eq && best.lo != best.hi))
			best = p;
	}
	return best;
}

/// Collecte les lignes satisfaisant `where` selon le plan. `fn(RowId)` retourne false pour arrêter.
template <typename Fn> void RunPlan(const Table &t, const Plan &plan, const Expr *where, EvalContext &cx, Fn &&fn) {
	auto test = [&](RowId r) {
		if (!where)
			return true;
		cx.row = r;
		return Truthy(Eval(*where, cx));
	};
	switch (plan.kind) {
	case Plan::Kind::Key:
		if (plan.keyRow != kNoRow && test(plan.keyRow))
			fn(plan.keyRow);
		return;
	case Plan::Kind::Range:
		t.Index(plan.column)->Range(plan.lo, plan.hi, [&](int64_t, uint32_t r) { return test(r) ? fn(r) : true; });
		return;
	case Plan::Kind::Scan:
		t.Scan([&](RowId r) { return test(r) ? fn(r) : true; });
		return;
	}
}

// ── Instantané binaire ────────────────────────────────────────────────────
constexpr uint32_t kMagic = 0x42445156; // "VQDB"
constexpr uint32_t kVersion = 1;

void WriteValue(dataio::ByteWriter &w, const Value &v) {
	w.PutU8(static_cast<uint8_t>(v.Type()));
	switch (v.Type()) {
	case ColumnType::Int: w.PutVarInt(v.AsInt()); break;
	case ColumnType::Real: w.Put<double>(v.AsReal()); break;
	case ColumnType::Text: w.PutString(v.AsText()); break;
	case ColumnType::Blob: w.PutBlob(v.AsBlob()); break;
	default: break;
	}
}

Value ReadValue(dataio::ByteReader &r) {
	switch (static_cast<ColumnType>(r.GetU8())) {
	case ColumnType::Int: return Value(r.GetVarInt());
	case ColumnType::Real: return Value(r.Get<double>());
	case ColumnType::Text: return Value(r.GetString());
	case ColumnType::Blob: {
		auto b = r.GetBlob();
		return Value(Blob(b.begin(), b.end()));
	}
	default: return Value();
	}
}

} // namespace

// ── PreparedStatement ─────────────────────────────────────────────────────
PreparedStatement::PreparedStatement() = default;
PreparedStatement::~PreparedStatement() = default;
PreparedStatement::PreparedStatement(PreparedStatement &&) noexcept = default;
PreparedStatement &PreparedStatement::operator=(PreparedStatement &&) noexcept = default;
size_t PreparedStatement::ParameterCount() const noexcept { return m_stmt ? static_cast<size_t>(m_stmt->paramCount) : 0; }

// ── Database ──────────────────────────────────────────────────────────────
Database::Database() = default;
Database::~Database() = default;

Result<ResultSet, std::string> Database::Execute(std::string_view sql, std::span<const Value> params) {
	auto st = ParseStatement(sql);
	if (!st)
		return Err(std::string("sql: ") + st.Error());
	return Run(*st.Value(), params);
}

Result<PreparedStatement, std::string> Database::Prepare(std::string_view sql) {
	auto st = ParseStatement(sql);
	if (!st)
		return Err(std::string("sql: ") + st.Error());
	PreparedStatement p;
	p.m_stmt = std::move(st.Value());
	return Ok(std::move(p));
}

Result<ResultSet, std::string> Database::Execute(const PreparedStatement &stmt, std::span<const Value> params) {
	if (!stmt.m_stmt)
		return Err(std::string("sql: requête préparée invalide"));
	return Run(*stmt.m_stmt, params);
}

Result<size_t, std::string> Database::ExecuteScript(std::string_view sql) {
	size_t count = 0;
	while (true) {
		const size_t first = sql.find_first_not_of(" \t\r\n;");
		if (first == std::string_view::npos)
			break;
		sql.remove_prefix(first);
		size_t consumed = 0;
		auto st = ParseStatement(sql, &consumed);
		if (!st)
			return Err(std::format("sql (instruction {}): {}", count + 1, st.Error()));
		auto r = Run(*st.Value(), {});
		if (!r)
			return Err(r.Error());
		++count;
		sql.remove_prefix(std::min(consumed, sql.size()));
	}
	return Ok(count);
}

Result<ResultSet, std::string> Database::Run(const Statement &st, std::span<const Value> params) {
	if (params.size() < static_cast<size_t>(st.paramCount))
		return Err(std::format("sql: {} paramètre(s) attendu(s), {} fourni(s)", st.paramCount, params.size()));
	std::vector<int> columnOf(static_cast<size_t>(st.columnSlots), -1);
	std::string err;
	ResultSet rs;

	if (const auto *s = std::get_if<SelectStmt>(&st.body)) {
		auto lock = LockRead();
		const Table *t = GetTable(s->table);
		if (!t)
			return Err(std::format("sql: table inconnue '{}'", s->table));
		if (!Resolve(s->where.get(), t->GetSchema(), columnOf, err))
			return Err("sql: " + err);
		std::vector<int> cols;
		if (s->columns.empty() && !s->countStar) {
			for (size_t c = 0; c < t->GetSchema().columns.size(); ++c) {
				cols.push_back(static_cast<int>(c));
				rs.columns.push_back(t->GetSchema().columns[c].name);
			}
		} else {
			for (const auto &name : s->columns) {
				const int c = t->GetSchema().Find(name);
				if (c < 0)
					return Err(std::format("sql: colonne inconnue '{}'", name));
				cols.push_back(c);
				rs.columns.push_back(name);
			}
		}
		const int orderCol = s->orderBy.empty() ? -1 : t->GetSchema().Find(s->orderBy);
		if (!s->orderBy.empty() && orderCol < 0)
			return Err(std::format("sql: colonne inconnue '{}' dans ORDER BY", s->orderBy));
		EvalContext cx{t, kNoRow, params, &columnOf};
		Plan plan = ChoosePlan(*t, s->where.get(), cx);
		if (s->countStar) {
			size_t n = 0;
			RunPlan(*t, plan, s->where.get(), cx, [&](RowId) {
				++n;
				return true;
			});
			rs.columns = {"COUNT(*)"};
			rs.rows.push_back({Value(static_cast<int64_t>(n))});
			rs.plan = plan.description;
			return Ok(std::move(rs));
		}
		// ORDER BY sur colonne indexée sans autre plan : lecture ordonnée, arrêt anticipé.
		const bool orderedScan = orderCol >= 0 && plan.kind == Plan::Kind::Scan && t->Index(orderCol);
		if (orderedScan) {
			plan.kind = Plan::Kind::Range;
			plan.column = orderCol;
			plan.description = std::format("btree order({})", s->orderBy);
		}
		std::vector<RowId> rows;
		const size_t want = (orderCol < 0 || orderedScan) && s->limit != SIZE_MAX ? s->offset + s->limit : SIZE_MAX;
		if (orderedScan && s->descending) {
			auto test = [&](RowId r) {
				cx.row = r;
				return !s->where || Truthy(Eval(*s->where, cx));
			};
			t->Index(orderCol)->RangeReverse(INT64_MIN, INT64_MAX, [&](int64_t, uint32_t r) {
				if (test(r))
					rows.push_back(r);
				return rows.size() < want;
			});
		} else {
			RunPlan(*t, plan, s->where.get(), cx, [&](RowId r) {
				rows.push_back(r);
				return rows.size() < want;
			});
		}
		if (orderCol >= 0 && !orderedScan) {
			std::ranges::stable_sort(rows, [&](RowId a, RowId b) {
				const auto c = t->Get(a, orderCol).Compare(t->Get(b, orderCol));
				return s->descending ? c > 0 : c < 0;
			});
		}
		const size_t begin = std::min(s->offset, rows.size());
		const size_t end = s->limit == SIZE_MAX ? rows.size() : std::min(rows.size(), begin + s->limit);
		rs.rows.reserve(end - begin);
		for (size_t i = begin; i < end; ++i) {
			Row row;
			row.reserve(cols.size());
			for (int c : cols)
				row.push_back(t->Get(rows[i], c));
			rs.rows.push_back(std::move(row));
		}
		rs.plan = plan.description;
		return Ok(std::move(rs));
	}

	auto lock = LockWrite();
	if (const auto *s = std::get_if<CreateTableStmt>(&st.body)) {
		if (m_tables.contains(s->table)) {
			if (s->ifNotExists)
				return Ok(std::move(rs));
			return Err(std::format("sql: la table '{}' existe déjà", s->table));
		}
		m_tables.emplace(s->table, std::make_unique<Table>(s->table, s->schema));
		return Ok(std::move(rs));
	}
	if (const auto *s = std::get_if<CreateIndexStmt>(&st.body)) {
		Table *t = GetTable(s->table);
		if (!t)
			return Err(std::format("sql: table inconnue '{}'", s->table));
		const int c = t->GetSchema().Find(s->column);
		if (c < 0)
			return Err(std::format("sql: colonne inconnue '{}'", s->column));
		if (t->GetSchema().columns[static_cast<size_t>(c)].type != ColumnType::Int)
			return Err(std::string("sql: CREATE INDEX n'accepte que des colonnes INT"));
		t->CreateIndex(c);
		return Ok(std::move(rs));
	}
	if (const auto *s = std::get_if<DropTableStmt>(&st.body)) {
		if (!m_tables.erase(s->table) && !s->ifExists)
			return Err(std::format("sql: table inconnue '{}'", s->table));
		return Ok(std::move(rs));
	}
	if (const auto *s = std::get_if<InsertStmt>(&st.body)) {
		Table *t = GetTable(s->table);
		if (!t)
			return Err(std::format("sql: table inconnue '{}'", s->table));
		const Schema &schema = t->GetSchema();
		std::vector<int> target;
		if (s->columns.empty()) {
			for (size_t c = 0; c < schema.columns.size(); ++c)
				target.push_back(static_cast<int>(c));
		} else {
			for (const auto &name : s->columns) {
				const int c = schema.Find(name);
				if (c < 0)
					return Err(std::format("sql: colonne inconnue '{}'", name));
				target.push_back(c);
			}
		}
		EvalContext cx{t, kNoRow, params, &columnOf};
		for (const auto &exprs : s->rows) {
			if (exprs.size() != target.size())
				return Err(std::format("sql: {} valeur(s) pour {} colonne(s)", exprs.size(), target.size()));
			Row row(schema.columns.size());
			for (size_t i = 0; i < exprs.size(); ++i) {
				if (!IsConstant(*exprs[i]))
					return Err(std::string("sql: VALUES n'accepte pas de référence de colonne"));
				row[static_cast<size_t>(target[i])] = Eval(*exprs[i], cx);
			}
			if (s->replace) {
				if (t->Upsert(row) == kNoRow)
					return Err(std::format("sql: insertion impossible dans '{}'", s->table));
			} else if (!t->Insert(row, &err)) {
				return Err("sql: " + err);
			}
			++rs.affected;
		}
		return Ok(std::move(rs));
	}
	if (const auto *s = std::get_if<UpdateStmt>(&st.body)) {
		Table *t = GetTable(s->table);
		if (!t)
			return Err(std::format("sql: table inconnue '{}'", s->table));
		if (!Resolve(s->where.get(), t->GetSchema(), columnOf, err))
			return Err("sql: " + err);
		std::vector<int> setCols;
		for (const auto &[name, e] : s->sets) {
			const int c = t->GetSchema().Find(name);
			if (c < 0)
				return Err(std::format("sql: colonne inconnue '{}'", name));
			if (!Resolve(e.get(), t->GetSchema(), columnOf, err))
				return Err("sql: " + err);
			setCols.push_back(c);
		}
		EvalContext cx{t, kNoRow, params, &columnOf};
		const Plan plan = ChoosePlan(*t, s->where.get(), cx);
		std::vector<RowId> rows;
		RunPlan(*t, plan, s->where.get(), cx, [&](RowId r) {
			rows.push_back(r);
			return true;
		});
		for (RowId r : rows) {
			cx.row = r;
			std::vector<Value> values;
			for (const auto &[name, e] : s->sets)
				values.push_back(Eval(*e, cx));
			for (size_t i = 0; i < setCols.size(); ++i)
				if (!t->Set(r, setCols[i], values[i], &err))
					return Err("sql: " + err);
		}
		rs.affected = rows.size();
		rs.plan = plan.description;
		return Ok(std::move(rs));
	}
	if (const auto *s = std::get_if<DeleteStmt>(&st.body)) {
		Table *t = GetTable(s->table);
		if (!t)
			return Err(std::format("sql: table inconnue '{}'", s->table));
		if (!Resolve(s->where.get(), t->GetSchema(), columnOf, err))
			return Err("sql: " + err);
		EvalContext cx{t, kNoRow, params, &columnOf};
		const Plan plan = ChoosePlan(*t, s->where.get(), cx);
		std::vector<RowId> rows;
		RunPlan(*t, plan, s->where.get(), cx, [&](RowId r) {
			rows.push_back(r);
			return true;
		});
		for (RowId r : rows)
			t->Erase(r);
		rs.affected = rows.size();
		rs.plan = plan.description;
		return Ok(std::move(rs));
	}
	return Err(std::string("sql: instruction non prise en charge"));
}

bool Database::CreateTable(const std::string &name, Schema schema) {
	auto lock = LockWrite();
	if (m_tables.contains(name))
		return false;
	m_tables.emplace(name, std::make_unique<Table>(name, std::move(schema)));
	return true;
}

bool Database::DropTable(const std::string &name) {
	auto lock = LockWrite();
	return m_tables.erase(name) > 0;
}

std::vector<std::string> Database::TableNames() const {
	auto lock = LockRead();
	std::vector<std::string> out;
	for (const auto &[name, t] : m_tables)
		out.push_back(name);
	return out;
}

Table *Database::GetTable(std::string_view name) {
	auto it = m_tables.find(name);
	return it == m_tables.end() ? nullptr : it->second.get();
}
const Table *Database::GetTable(std::string_view name) const {
	auto it = m_tables.find(name);
	return it == m_tables.end() ? nullptr : it->second.get();
}

bool Database::SaveSnapshot(const std::string &path) const {
	dataio::ByteWriter w(1 << 16);
	{
		auto lock = LockRead();
		w.Put<uint32_t>(kMagic);
		w.Put<uint32_t>(kVersion);
		w.PutVarUint(m_tables.size());
		for (const auto &[name, t] : m_tables) {
			w.PutString(name);
			const Schema &s = t->GetSchema();
			w.PutVarUint(s.columns.size());
			for (const auto &c : s.columns) {
				w.PutString(c.name);
				w.PutU8(static_cast<uint8_t>(c.type));
				w.PutU8(static_cast<uint8_t>((c.primaryKey ? 1 : 0) | (c.indexed ? 2 : 0)));
			}
			w.PutVarUint(t->RowCount());
			t->Scan([&](RowId r) {
				for (size_t c = 0; c < s.columns.size(); ++c)
					WriteValue(w, t->Get(r, static_cast<int>(c)));
				return true;
			});
		}
	}
	w.Put<uint32_t>(dataio::Crc32(w.Bytes()));
	// Écriture atomique : fichier temporaire puis renommage.
	const std::string tmp = path + ".tmp";
	{
		std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
		if (!f)
			return false;
		f.write(reinterpret_cast<const char *>(w.Bytes().data()), static_cast<std::streamsize>(w.Size()));
		if (!f)
			return false;
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	return !ec;
}

Result<bool, std::string> Database::LoadSnapshot(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return Err(std::format("sql: impossible d'ouvrir '{}'", path));
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (bytes.size() < 12)
		return Err(std::string("sql: instantané tronqué"));
	const std::span<const uint8_t> body(bytes.data(), bytes.size() - 4);
	uint32_t crc;
	std::memcpy(&crc, bytes.data() + bytes.size() - 4, 4);
	if (crc != dataio::Crc32(body))
		return Err(std::string("sql: CRC invalide (fichier corrompu)"));
	dataio::ByteReader r(body);
	if (r.Get<uint32_t>() != kMagic || r.Get<uint32_t>() != kVersion)
		return Err(std::string("sql: format d'instantané inconnu"));
	std::map<std::string, std::unique_ptr<Table>, std::less<>> tables;
	const uint64_t tableCount = r.GetVarUint();
	for (uint64_t ti = 0; ti < tableCount && r.Ok(); ++ti) {
		std::string name = r.GetString();
		Schema s;
		const uint64_t nc = r.GetVarUint();
		for (uint64_t c = 0; c < nc && r.Ok(); ++c) {
			ColumnDef def;
			def.name = r.GetString();
			def.type = static_cast<ColumnType>(r.GetU8());
			const uint8_t flags = r.GetU8();
			def.primaryKey = flags & 1;
			def.indexed = flags & 2;
			s.columns.push_back(std::move(def));
		}
		auto t = std::make_unique<Table>(name, s);
		const uint64_t rows = r.GetVarUint();
		Row row(s.columns.size());
		for (uint64_t i = 0; i < rows && r.Ok(); ++i) {
			for (auto &v : row)
				v = ReadValue(r);
			(void)t->Insert(row);
		}
		tables.emplace(std::move(name), std::move(t));
	}
	if (!r.Ok())
		return Err(std::string("sql: instantané illisible"));
	auto lock = LockWrite();
	m_tables = std::move(tables);
	return Ok(true);
}

// ── QueryBuilder ──────────────────────────────────────────────────────────
QueryBuilder &QueryBuilder::Select(std::vector<std::string> cols) {
	m_columns = std::move(cols);
	return *this;
}
QueryBuilder &QueryBuilder::Where(std::string column, std::string op, Value v) {
	m_conditions.push_back(std::format("\"{}\" {} ?", column, op));
	m_params.push_back(std::move(v));
	return *this;
}
QueryBuilder &QueryBuilder::OrderBy(std::string column, bool descending) {
	m_order = std::format("\"{}\"{}", column, descending ? " DESC" : "");
	return *this;
}
QueryBuilder &QueryBuilder::Limit(size_t n, size_t offset) {
	m_limit = n;
	m_offset = offset;
	return *this;
}
std::string QueryBuilder::Sql() const {
	std::string s = "SELECT ";
	if (m_columns.empty()) {
		s += "*";
	} else {
		for (size_t i = 0; i < m_columns.size(); ++i)
			s += std::format("{}\"{}\"", i ? ", " : "", m_columns[i]);
	}
	s += std::format(" FROM \"{}\"", m_table);
	for (size_t i = 0; i < m_conditions.size(); ++i)
		s += (i ? " AND " : " WHERE ") + m_conditions[i];
	if (!m_order.empty())
		s += " ORDER BY " + m_order;
	if (m_limit)
		s += std::format(" LIMIT {} OFFSET {}", m_limit, m_offset);
	return s;
}

} // namespace sql
