#pragma once
/**
 * @file database.hpp
 * @brief Base de données embarquée : tables en colonnes, SQL, requêtes préparées,
 *        constructeur de requêtes, instantané binaire (CRC-32), accès concurrent.
 *
 * Sous-ensemble SQL accepté :
 * ```
 * CREATE TABLE t (col INT|REAL|TEXT|BLOB [PRIMARY KEY] [INDEX], …)
 * CREATE INDEX ON t (col)
 * DROP TABLE t
 * INSERT [OR REPLACE] INTO t [(cols)] VALUES (…), (…)
 * SELECT * | COUNT(*) | col, … FROM t [WHERE e] [ORDER BY col [ASC|DESC]] [LIMIT n [OFFSET m]]
 * UPDATE t SET col = e, … [WHERE e]
 * DELETE FROM t [WHERE e]
 * ```
 * Expressions : `= != <> < <= > >= + - * / %`, `AND OR NOT`, `BETWEEN a AND b`,
 * `IN (…)`, `LIKE 'pré%'`, `IS [NOT] NULL`, paramètres `?` liés à l'exécution.
 *
 * Planificateur : un `WHERE` (conjonction) contenant `pk = c` utilise l'index de
 * hachage ; une égalité/plage sur une colonne indexée utilise le B+arbre ;
 * `ORDER BY` sur une colonne indexée lit le B+arbre dans l'ordre. Sinon balayage.
 *
 * Concurrence : lectures partagées (`std::shared_mutex`), écritures exclusives.
 */
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/result.hpp>

#include "table.hpp"
#include "value.hpp"

namespace sql {

struct ResultSet {
	std::vector<std::string> columns;
	std::vector<Row> rows;
	size_t affected = 0;
	/// Plan utilisé (« hash pk », « btree idx(col) », « scan ») : utile en débogage.
	std::string plan;

	[[nodiscard]] bool Empty() const noexcept { return rows.empty(); }
	[[nodiscard]] const Value &At(size_t row, size_t col) const { return rows[row][col]; }
};

struct Statement; // AST préparé (opaque)

class PreparedStatement {
public:
	PreparedStatement();
	~PreparedStatement();
	PreparedStatement(PreparedStatement &&) noexcept;
	PreparedStatement &operator=(PreparedStatement &&) noexcept;
	[[nodiscard]] bool Valid() const noexcept { return m_stmt != nullptr; }
	[[nodiscard]] size_t ParameterCount() const noexcept;

private:
	friend class Database;
	std::unique_ptr<Statement> m_stmt;
};

class Database {
public:
	Database();
	~Database();
	Database(const Database &) = delete;
	Database &operator=(const Database &) = delete;

	/// Analyse puis exécute une instruction ; `params` remplit les `?`.
	[[nodiscard]] Result<ResultSet, std::string> Execute(std::string_view sql, std::span<const Value> params = {});
	/// Analyse une fois, exécute ensuite sans coût d'analyse.
	[[nodiscard]] Result<PreparedStatement, std::string> Prepare(std::string_view sql);
	[[nodiscard]] Result<ResultSet, std::string> Execute(const PreparedStatement &stmt, std::span<const Value> params = {});
	/// Exécute un script de plusieurs instructions séparées par `;`.
	[[nodiscard]] Result<size_t, std::string> ExecuteScript(std::string_view sql);

	// Accès structuré (sans texte SQL)
	bool CreateTable(const std::string &name, Schema schema);
	bool DropTable(const std::string &name);
	[[nodiscard]] std::vector<std::string> TableNames() const;
	/// Accès direct à une table (l'appelant gère la synchronisation via `Lock*`).
	[[nodiscard]] Table *GetTable(std::string_view name);
	[[nodiscard]] const Table *GetTable(std::string_view name) const;

	[[nodiscard]] std::shared_lock<std::shared_mutex> LockRead() const { return std::shared_lock(m_mutex); }
	[[nodiscard]] std::unique_lock<std::shared_mutex> LockWrite() { return std::unique_lock(m_mutex); }

	// Persistance
	[[nodiscard]] bool SaveSnapshot(const std::string &path) const;
	[[nodiscard]] Result<bool, std::string> LoadSnapshot(const std::string &path);

private:
	Result<ResultSet, std::string> Run(const Statement &st, std::span<const Value> params);

	mutable std::shared_mutex m_mutex;
	std::map<std::string, std::unique_ptr<Table>, std::less<>> m_tables;
};

/// Constructeur fluide : `QueryBuilder("chunks").Select({"key"}).Where("y", ">", 3).OrderBy("key").Limit(10)`.
class QueryBuilder {
public:
	explicit QueryBuilder(std::string table) : m_table(std::move(table)) {}
	QueryBuilder &Select(std::vector<std::string> cols);
	QueryBuilder &Where(std::string column, std::string op, Value v);
	QueryBuilder &OrderBy(std::string column, bool descending = false);
	QueryBuilder &Limit(size_t n, size_t offset = 0);
	/// Texte SQL généré (valeurs passées en paramètres `?`).
	[[nodiscard]] std::string Sql() const;
	[[nodiscard]] std::span<const Value> Params() const noexcept { return m_params; }
	[[nodiscard]] Result<ResultSet, std::string> Execute(Database &db) const { return db.Execute(Sql(), m_params); }

private:
	std::string m_table;
	std::vector<std::string> m_columns;
	std::vector<std::string> m_conditions;
	std::vector<Value> m_params;
	std::string m_order;
	size_t m_limit = 0, m_offset = 0;
};

} // namespace sql
