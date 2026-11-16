#include "sql/table.hpp"

#include <format>

namespace sql {

int Schema::Find(std::string_view name) const noexcept {
	for (size_t i = 0; i < columns.size(); ++i)
		if (columns[i].name == name)
			return static_cast<int>(i);
	return -1;
}

int Schema::PrimaryKey() const noexcept {
	for (size_t i = 0; i < columns.size(); ++i)
		if (columns[i].primaryKey)
			return static_cast<int>(i);
	return -1;
}

Table::Table(std::string name, Schema schema) : m_name(std::move(name)), m_schema(std::move(schema)) {
	m_pk = m_schema.PrimaryKey();
	m_columns.resize(m_schema.columns.size());
	for (size_t c = 0; c < m_columns.size(); ++c) {
		switch (m_schema.columns[c].type) {
		case ColumnType::Real: m_columns[c].data = std::vector<double>{}; break;
		case ColumnType::Text: m_columns[c].data = std::vector<std::string>{}; break;
		case ColumnType::Blob: m_columns[c].data = std::vector<Blob>{}; break;
		default: m_columns[c].data = std::vector<int64_t>{}; break;
		}
		if (m_schema.columns[c].indexed && m_schema.columns[c].type == ColumnType::Int && !m_schema.columns[c].primaryKey)
			m_columns[c].index = std::make_unique<BTreeIndex>();
	}
}

std::optional<Value> Table::Coerce(const Value &v, ColumnType t) {
	if (v.IsNull())
		return Value();
	switch (t) {
	case ColumnType::Int:
		if (v.Type() == ColumnType::Int)
			return v;
		if (v.Type() == ColumnType::Real)
			return Value(v.AsInt());
		return std::nullopt;
	case ColumnType::Real:
		if (v.IsNumeric())
			return Value(v.AsReal());
		return std::nullopt;
	case ColumnType::Text:
		if (v.Type() == ColumnType::Text)
			return v;
		if (v.IsNumeric())
			return Value(v.ToString());
		return std::nullopt;
	case ColumnType::Blob:
		if (v.Type() == ColumnType::Blob)
			return v;
		if (v.Type() == ColumnType::Text) {
			const auto s = v.AsText();
			return Value(Blob(s.begin(), s.end()));
		}
		return std::nullopt;
	default: return std::nullopt;
	}
}

void Table::WriteCell(RowId r, int c, const Value &v) {
	Column &col = m_columns[static_cast<size_t>(c)];
	col.isNull[r] = v.IsNull();
	std::visit(
		[&](auto &vec) {
			using V = typename std::decay_t<decltype(vec)>::value_type;
			if (v.IsNull())
				vec[r] = V{};
			else if constexpr (std::is_same_v<V, int64_t>)
				vec[r] = v.AsInt();
			else if constexpr (std::is_same_v<V, double>)
				vec[r] = v.AsReal();
			else if constexpr (std::is_same_v<V, std::string>)
				vec[r] = std::string(v.AsText());
			else
				vec[r] = std::get<Blob>(v.Raw());
		},
		col.data);
}

void Table::IndexInsert(RowId r) {
	if (m_pk >= 0 && !IsNull(r, m_pk)) {
		if (m_schema.columns[static_cast<size_t>(m_pk)].type == ColumnType::Text)
			m_textKeys.InsertOrAssign(std::string(GetText(r, m_pk)), r);
		else
			m_intKeys.InsertOrAssign(GetInt(r, m_pk), r);
	}
	for (size_t c = 0; c < m_columns.size(); ++c)
		if (m_columns[c].index && !m_columns[c].isNull[r])
			m_columns[c].index->Insert(GetInt(r, static_cast<int>(c)), r);
}

void Table::IndexErase(RowId r) {
	if (m_pk >= 0 && !IsNull(r, m_pk)) {
		if (m_schema.columns[static_cast<size_t>(m_pk)].type == ColumnType::Text)
			m_textKeys.Erase(GetText(r, m_pk));
		else
			m_intKeys.Erase(GetInt(r, m_pk));
	}
	for (size_t c = 0; c < m_columns.size(); ++c)
		if (m_columns[c].index && !m_columns[c].isNull[r])
			m_columns[c].index->Erase(GetInt(r, static_cast<int>(c)), r);
}

std::optional<RowId> Table::Insert(const Row &row, std::string *error) {
	if (row.size() != m_columns.size()) {
		if (error)
			*error = std::format("{} valeur(s) pour {} colonne(s)", row.size(), m_columns.size());
		return std::nullopt;
	}
	Row coerced(row.size());
	for (size_t c = 0; c < row.size(); ++c) {
		auto v = Coerce(row[c], m_schema.columns[c].type);
		if (!v) {
			if (error)
				*error = std::format("type incompatible pour la colonne '{}' ({} attendu)", m_schema.columns[c].name,
									 TypeName(m_schema.columns[c].type));
			return std::nullopt;
		}
		coerced[c] = std::move(*v);
	}
	if (m_pk >= 0 && FindByKey(coerced[static_cast<size_t>(m_pk)]) != kNoRow) {
		if (error)
			*error = std::format("clé primaire dupliquée : {}", coerced[static_cast<size_t>(m_pk)].ToString());
		return std::nullopt;
	}
	RowId r;
	if (!m_free.empty()) {
		r = m_free.back();
		m_free.pop_back();
	} else {
		r = static_cast<RowId>(m_alive.size());
		m_alive.push_back(0);
		for (Column &col : m_columns) {
			col.isNull.push_back(1);
			std::visit([](auto &vec) { vec.emplace_back(); }, col.data);
		}
	}
	for (size_t c = 0; c < coerced.size(); ++c)
		WriteCell(r, static_cast<int>(c), coerced[c]);
	m_alive[r] = 1;
	++m_live;
	IndexInsert(r);
	return r;
}

RowId Table::Upsert(const Row &row) {
	if (m_pk >= 0 && row.size() == m_columns.size()) {
		if (auto key = Coerce(row[static_cast<size_t>(m_pk)], m_schema.columns[static_cast<size_t>(m_pk)].type)) {
			if (const RowId existing = FindByKey(*key); existing != kNoRow)
				Erase(existing);
		}
	}
	return Insert(row).value_or(kNoRow);
}

bool Table::Erase(RowId r) {
	if (!IsAlive(r))
		return false;
	IndexErase(r);
	m_alive[r] = 0;
	for (size_t c = 0; c < m_columns.size(); ++c)
		WriteCell(r, static_cast<int>(c), Value()); // libère texte/blob
	m_free.push_back(r);
	--m_live;
	return true;
}

bool Table::Set(RowId r, int column, const Value &v, std::string *error) {
	if (!IsAlive(r) || column < 0 || static_cast<size_t>(column) >= m_columns.size())
		return false;
	auto coerced = Coerce(v, m_schema.columns[static_cast<size_t>(column)].type);
	if (!coerced) {
		if (error)
			*error = std::format("type incompatible pour '{}'", m_schema.columns[static_cast<size_t>(column)].name);
		return false;
	}
	if (column == m_pk) {
		const RowId other = FindByKey(*coerced);
		if (other != kNoRow && other != r) {
			if (error)
				*error = "clé primaire dupliquée";
			return false;
		}
	}
	IndexErase(r);
	WriteCell(r, column, *coerced);
	IndexInsert(r);
	return true;
}

Value Table::Get(RowId r, int c) const {
	const Column &col = m_columns[static_cast<size_t>(c)];
	if (col.isNull[r])
		return Value();
	return std::visit([&](const auto &vec) { return Value(vec[r]); }, col.data);
}

Row Table::GetRow(RowId r) const {
	Row row(m_columns.size());
	for (size_t c = 0; c < m_columns.size(); ++c)
		row[c] = Get(r, static_cast<int>(c));
	return row;
}

int64_t Table::GetInt(RowId r, int c) const noexcept {
	const Column &col = m_columns[static_cast<size_t>(c)];
	if (const auto *v = std::get_if<std::vector<int64_t>>(&col.data))
		return (*v)[r];
	if (const auto *d = std::get_if<std::vector<double>>(&col.data))
		return static_cast<int64_t>((*d)[r]);
	return 0;
}

std::string_view Table::GetText(RowId r, int c) const noexcept {
	if (const auto *v = std::get_if<std::vector<std::string>>(&m_columns[static_cast<size_t>(c)].data))
		return (*v)[r];
	return {};
}

bool Table::IsNull(RowId r, int c) const noexcept { return m_columns[static_cast<size_t>(c)].isNull[r] != 0; }

RowId Table::FindByKey(const Value &key) const {
	if (m_pk < 0 || key.IsNull())
		return kNoRow;
	if (m_schema.columns[static_cast<size_t>(m_pk)].type == ColumnType::Text) {
		const RowId *r = m_textKeys.Find(key.Type() == ColumnType::Text ? key.AsText() : std::string_view(key.ToString()));
		return r ? *r : kNoRow;
	}
	if (!key.IsNumeric())
		return kNoRow;
	const RowId *r = m_intKeys.Find(key.AsInt());
	return r ? *r : kNoRow;
}

void Table::CreateIndex(int column) {
	if (column < 0 || static_cast<size_t>(column) >= m_columns.size())
		return;
	Column &col = m_columns[static_cast<size_t>(column)];
	m_schema.columns[static_cast<size_t>(column)].indexed = true;
	if (col.index || !std::holds_alternative<std::vector<int64_t>>(col.data))
		return;
	col.index = std::make_unique<BTreeIndex>();
	Scan([&](RowId r) {
		if (!col.isNull[r])
			col.index->Insert(GetInt(r, column), r);
		return true;
	});
}

const BTreeIndex *Table::Index(int column) const noexcept {
	if (column < 0 || static_cast<size_t>(column) >= m_columns.size())
		return nullptr;
	return m_columns[static_cast<size_t>(column)].index.get();
}

void Table::Clear() {
	for (Column &col : m_columns) {
		std::visit([](auto &v) { v.clear(); }, col.data);
		col.isNull.clear();
		if (col.index)
			col.index->Clear();
	}
	m_alive.clear();
	m_free.clear();
	m_live = 0;
	m_intKeys.Clear();
	m_textKeys.Clear();
}

} // namespace sql
