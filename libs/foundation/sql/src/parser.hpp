#pragma once
// AST et analyseur SQL (interne au module).
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <core/result.hpp>

#include "sql/table.hpp"
#include "sql/value.hpp"

namespace sql {

enum class Op : uint8_t { Eq, Ne, Lt, Le, Gt, Ge, Add, Sub, Mul, Div, Mod, And, Or, Not, Neg };

struct Expr {
	enum class Kind : uint8_t { Literal, Param, Column, Unary, Binary, Between, In, Like, IsNull };
	Kind kind = Kind::Literal;
	Op op = Op::Eq;
	bool negated = false; // NOT BETWEEN / NOT IN / NOT LIKE / IS NOT NULL
	Value literal;
	int param = 0;		  // index du paramètre `?`
	std::string column;	  // nom de colonne
	int columnSlot = -1;  // emplacement de résolution (par exécution)
	std::vector<std::unique_ptr<Expr>> args;
};
using ExprPtr = std::unique_ptr<Expr>;

struct CreateTableStmt {
	std::string table;
	Schema schema;
	bool ifNotExists = false;
};
struct CreateIndexStmt {
	std::string table, column;
};
struct DropTableStmt {
	std::string table;
	bool ifExists = false;
};
struct InsertStmt {
	std::string table;
	std::vector<std::string> columns;
	std::vector<std::vector<ExprPtr>> rows;
	bool replace = false;
};
struct SelectStmt {
	std::string table;
	std::vector<std::string> columns; // vide = *
	bool countStar = false;
	ExprPtr where;
	std::string orderBy;
	bool descending = false;
	size_t limit = SIZE_MAX, offset = 0;
};
struct UpdateStmt {
	std::string table;
	std::vector<std::pair<std::string, ExprPtr>> sets;
	ExprPtr where;
};
struct DeleteStmt {
	std::string table;
	ExprPtr where;
};

struct Statement {
	std::variant<CreateTableStmt, CreateIndexStmt, DropTableStmt, InsertStmt, SelectStmt, UpdateStmt, DeleteStmt> body;
	int paramCount = 0;
	int columnSlots = 0;
};

/// Analyse une instruction ; `consumed` reçoit la position après le `;` éventuel.
[[nodiscard]] Result<std::unique_ptr<Statement>, std::string> ParseStatement(std::string_view sql, size_t *consumed = nullptr);

} // namespace sql
