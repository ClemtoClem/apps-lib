#pragma once
/**
 * @file table.hpp
 * @brief Table en colonnes typées.
 *
 * - Stockage **colonne par colonne** (`std::vector<int64_t>`, `double`, `string`,
 *   `Blob`) + masque de NULL : un balayage `WHERE level > 10` ne lit que la colonne
 *   concernée.
 * - Identifiants de lignes stables (`RowId`) : les lignes supprimées laissent un
 *   emplacement libre réutilisé à l'insertion suivante.
 * - Clé primaire : index de hachage `core::RobinMap` (entier ou texte) → O(1).
 * - Index secondaires sur colonnes entières : B+arbre (égalité, plages, ordre).
 */
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <core/robin_map.hpp>

#include "btree.hpp"
#include "value.hpp"

namespace sql {

using RowId = uint32_t;
inline constexpr RowId kNoRow = UINT32_MAX;

struct ColumnDef {
	std::string name;
	ColumnType type = ColumnType::Int;
	bool primaryKey = false;
	bool indexed = false;
};

struct Schema {
	std::vector<ColumnDef> columns;
	[[nodiscard]] int Find(std::string_view name) const noexcept;
	[[nodiscard]] int PrimaryKey() const noexcept;
};

class Table {
public:
	Table(std::string name, Schema schema);

	[[nodiscard]] const std::string &Name() const noexcept { return m_name; }
	[[nodiscard]] const Schema &GetSchema() const noexcept { return m_schema; }
	[[nodiscard]] size_t RowCount() const noexcept { return m_live; }
	/// Nombre d'emplacements (vivants + libres) : borne des RowId.
	[[nodiscard]] size_t SlotCount() const noexcept { return m_alive.size(); }
	[[nodiscard]] bool IsAlive(RowId r) const noexcept { return r < m_alive.size() && m_alive[r]; }

	/// Insère une ligne (valeurs converties au type des colonnes). Échec si clé dupliquée.
	[[nodiscard]] std::optional<RowId> Insert(const Row &row, std::string *error = nullptr);
	/// Insère ou remplace la ligne ayant la même clé primaire.
	RowId Upsert(const Row &row);
	bool Erase(RowId r);
	/// Modifie une cellule (met à jour les index).
	bool Set(RowId r, int column, const Value &v, std::string *error = nullptr);

	[[nodiscard]] Value Get(RowId r, int column) const;
	[[nodiscard]] Row GetRow(RowId r) const;
	/// Accès typé rapide (sans construire de `Value`).
	[[nodiscard]] int64_t GetInt(RowId r, int column) const noexcept;
	[[nodiscard]] std::string_view GetText(RowId r, int column) const noexcept;
	[[nodiscard]] bool IsNull(RowId r, int column) const noexcept;

	[[nodiscard]] RowId FindByKey(const Value &key) const;
	void CreateIndex(int column);
	[[nodiscard]] const BTreeIndex *Index(int column) const noexcept;

	void Clear();

	/// Appelle `fn(RowId)` pour chaque ligne vivante.
	template <typename Fn> void Scan(Fn &&fn) const {
		for (RowId r = 0; r < m_alive.size(); ++r)
			if (m_alive[r])
				if (!fn(r))
					return;
	}

private:
	using ColumnData = std::variant<std::vector<int64_t>, std::vector<double>, std::vector<std::string>, std::vector<Blob>>;
	struct Column {
		ColumnData data;
		std::vector<uint8_t> isNull;
		std::unique_ptr<BTreeIndex> index;
	};

	[[nodiscard]] static std::optional<Value> Coerce(const Value &v, ColumnType t);
	void WriteCell(RowId r, int c, const Value &v);
	void IndexInsert(RowId r);
	void IndexErase(RowId r);

	std::string m_name;
	Schema m_schema;
	std::vector<Column> m_columns;
	std::vector<uint8_t> m_alive;
	std::vector<RowId> m_free;
	size_t m_live = 0;
	int m_pk = -1;
	core::RobinMap<int64_t, RowId> m_intKeys;
	core::RobinMap<std::string, RowId, core::Hasher<std::string>> m_textKeys;
};

} // namespace sql
