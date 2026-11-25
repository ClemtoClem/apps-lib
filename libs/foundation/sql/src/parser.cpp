#include "parser.hpp"

#include <cctype>
#include <charconv>
#include <format>

namespace sql {
namespace {

enum class Tok : uint8_t { End, Ident, Number, String, BlobLit, Param, Symbol };

struct Token {
	Tok kind = Tok::End;
	std::string text; // identifiant en majuscules pour les mots-clés : voir `upper`
	std::string upper;
	Value number;
	size_t pos = 0;
};

class Lexer {
public:
	explicit Lexer(std::string_view s) : m_s(s) {}

	bool Next(Token &t, std::string &err) {
		Skip();
		t = Token{};
		t.pos = m_i;
		if (m_i >= m_s.size())
			return true;
		const char c = m_s[m_i];
		if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
			if ((c == 'x' || c == 'X') && m_i + 1 < m_s.size() && m_s[m_i + 1] == '\'')
				return Blob(t, err);
			const size_t b = m_i;
			while (m_i < m_s.size() && (std::isalnum(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '_'))
				++m_i;
			t.kind = Tok::Ident;
			t.text = std::string(m_s.substr(b, m_i - b));
			t.upper = t.text;
			for (char &ch : t.upper)
				ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
			return true;
		}
		if (c == '"' || c == '`') { // identifiant entre guillemets
			const char q = c;
			const size_t b = ++m_i;
			while (m_i < m_s.size() && m_s[m_i] != q)
				++m_i;
			if (m_i >= m_s.size()) {
				err = "identifiant non terminé";
				return false;
			}
			t.kind = Tok::Ident;
			t.text = std::string(m_s.substr(b, m_i - b));
			t.upper = "\x01"; // jamais un mot-clé
			++m_i;
			return true;
		}
		if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && m_i + 1 < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_i + 1]))))
			return Number(t, err);
		if (c == '\'') {
			std::string out;
			++m_i;
			for (;;) {
				if (m_i >= m_s.size()) {
					err = "chaîne non terminée";
					return false;
				}
				if (m_s[m_i] == '\'') {
					if (m_i + 1 < m_s.size() && m_s[m_i + 1] == '\'') {
						out.push_back('\'');
						m_i += 2;
						continue;
					}
					++m_i;
					break;
				}
				out.push_back(m_s[m_i++]);
			}
			t.kind = Tok::String;
			t.text = std::move(out);
			return true;
		}
		if (c == '?') {
			++m_i;
			t.kind = Tok::Param;
			return true;
		}
		static constexpr std::string_view two[] = {"<=", ">=", "<>", "!=", "=="};
		for (auto s2 : two) {
			if (m_s.substr(m_i, 2) == s2) {
				t.kind = Tok::Symbol;
				t.text = std::string(s2 == "==" ? "=" : s2);
				m_i += 2;
				return true;
			}
		}
		if (std::string_view("=<>(),*+-/%;.").find(c) != std::string_view::npos) {
			t.kind = Tok::Symbol;
			t.text = std::string(1, c);
			++m_i;
			return true;
		}
		err = std::format("caractère inattendu '{}' (position {})", c, m_i);
		return false;
	}

	[[nodiscard]] size_t Pos() const noexcept { return m_i; }

private:
	void Skip() {
		while (m_i < m_s.size()) {
			if (std::isspace(static_cast<unsigned char>(m_s[m_i]))) {
				++m_i;
			} else if (m_s.substr(m_i, 2) == "--") {
				while (m_i < m_s.size() && m_s[m_i] != '\n')
					++m_i;
			} else if (m_s.substr(m_i, 2) == "/*") {
				const size_t e = m_s.find("*/", m_i + 2);
				m_i = e == std::string_view::npos ? m_s.size() : e + 2;
			} else {
				break;
			}
		}
	}
	bool Number(Token &t, std::string &err) {
		const size_t b = m_i;
		bool real = false;
		while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '.' || m_s[m_i] == 'e' ||
									m_s[m_i] == 'E' || ((m_s[m_i] == '-' || m_s[m_i] == '+') && (m_s[m_i - 1] == 'e' || m_s[m_i - 1] == 'E')))) {
			real |= m_s[m_i] == '.' || m_s[m_i] == 'e' || m_s[m_i] == 'E';
			++m_i;
		}
		const char *first = m_s.data() + b, *last = m_s.data() + m_i;
		t.kind = Tok::Number;
		if (real) {
			double d = 0;
			if (std::from_chars(first, last, d).ec != std::errc{}) {
				err = "nombre invalide";
				return false;
			}
			t.number = Value(d);
		} else {
			int64_t v = 0;
			if (std::from_chars(first, last, v).ec != std::errc{}) {
				err = "entier invalide";
				return false;
			}
			t.number = Value(v);
		}
		return true;
	}
	bool Blob(Token &t, std::string &err) {
		m_i += 2;
		sql::Blob bytes;
		auto hex = [](char ch) {
			if (ch >= '0' && ch <= '9') return ch - '0';
			if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
			if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
			return -1;
		};
		while (m_i + 1 < m_s.size() && m_s[m_i] != '\'') {
			const int h = hex(m_s[m_i]), l = hex(m_s[m_i + 1]);
			if (h < 0 || l < 0) {
				err = "littéral blob invalide";
				return false;
			}
			bytes.push_back(static_cast<uint8_t>(h * 16 + l));
			m_i += 2;
		}
		if (m_i >= m_s.size() || m_s[m_i] != '\'') {
			err = "littéral blob non terminé";
			return false;
		}
		++m_i;
		t.kind = Tok::BlobLit;
		t.number = Value(std::move(bytes));
		return true;
	}

	std::string_view m_s;
	size_t m_i = 0;
};

class Parser {
public:
	explicit Parser(std::string_view s) : m_lex(s) {}

	Result<std::unique_ptr<Statement>, std::string> Run(size_t *consumed) {
		if (!Advance())
			return Err(m_err);
		auto st = std::make_unique<Statement>();
		m_stmt = st.get();
		bool ok = false;
		if (Kw("CREATE"))
			ok = Create();
		else if (Kw("DROP"))
			ok = Drop();
		else if (Kw("INSERT"))
			ok = Insert();
		else if (Kw("SELECT"))
			ok = Select();
		else if (Kw("UPDATE"))
			ok = Update();
		else if (Kw("DELETE"))
			ok = Delete();
		else
			return Err(std::string("instruction inconnue (CREATE, DROP, INSERT, SELECT, UPDATE, DELETE attendus)"));
		if (!ok)
			return Err(m_err);
		if (Sym(";")) {
		} else if (m_tok.kind != Tok::End) {
			return Err(std::format("élément inattendu '{}'", m_tok.text));
		}
		if (consumed)
			*consumed = m_tok.kind == Tok::End ? m_lex.Pos() : m_tok.pos;
		return Ok(std::move(st));
	}

private:
	bool Advance() { return m_lex.Next(m_tok, m_err); }
	bool Fail(std::string msg) {
		if (m_err.empty())
			m_err = std::move(msg);
		return false;
	}
	bool IsKw(std::string_view kw) const { return m_tok.kind == Tok::Ident && m_tok.upper == kw; }
	bool Kw(std::string_view kw) {
		if (!IsKw(kw))
			return false;
		Advance();
		return true;
	}
	bool ExpectKw(std::string_view kw) { return Kw(kw) || Fail(std::format("'{}' attendu", kw)); }
	bool IsSym(std::string_view s) const { return m_tok.kind == Tok::Symbol && m_tok.text == s; }
	bool Sym(std::string_view s) {
		if (!IsSym(s))
			return false;
		Advance();
		return true;
	}
	bool ExpectSym(std::string_view s) { return Sym(s) || Fail(std::format("'{}' attendu", s)); }
	bool Ident(std::string &out) {
		if (m_tok.kind != Tok::Ident)
			return Fail("identifiant attendu");
		out = m_tok.text;
		return Advance();
	}

	bool Create() {
		if (Kw("INDEX")) {
			CreateIndexStmt s;
			if (IsKw("ON") == false && m_tok.kind == Tok::Ident) // nom d'index facultatif
				Advance();
			if (!ExpectKw("ON") || !Ident(s.table) || !ExpectSym("(") || !Ident(s.column) || !ExpectSym(")"))
				return false;
			m_stmt->body = std::move(s);
			return true;
		}
		if (!ExpectKw("TABLE"))
			return false;
		CreateTableStmt s;
		if (Kw("IF")) {
			if (!ExpectKw("NOT") || !ExpectKw("EXISTS"))
				return false;
			s.ifNotExists = true;
		}
		if (!Ident(s.table) || !ExpectSym("("))
			return false;
		do {
			ColumnDef col;
			if (!Ident(col.name))
				return false;
			if (m_tok.kind != Tok::Ident)
				return Fail("type de colonne attendu");
			const std::string &t = m_tok.upper;
			if (t == "INT" || t == "INTEGER" || t == "BIGINT" || t == "BOOL" || t == "BOOLEAN")
				col.type = ColumnType::Int;
			else if (t == "REAL" || t == "FLOAT" || t == "DOUBLE")
				col.type = ColumnType::Real;
			else if (t == "TEXT" || t == "STRING" || t == "VARCHAR")
				col.type = ColumnType::Text;
			else if (t == "BLOB")
				col.type = ColumnType::Blob;
			else
				return Fail(std::format("type inconnu '{}'", m_tok.text));
			Advance();
			for (;;) {
				if (Kw("PRIMARY")) {
					if (!ExpectKw("KEY"))
						return false;
					col.primaryKey = true;
				} else if (Kw("INDEX")) {
					col.indexed = true;
				} else {
					break;
				}
			}
			s.schema.columns.push_back(std::move(col));
		} while (Sym(","));
		int pk = 0;
		for (const auto &c : s.schema.columns)
			pk += c.primaryKey;
		if (pk > 1)
			return Fail("une seule clé primaire par table");
		if (!ExpectSym(")"))
			return false;
		m_stmt->body = std::move(s);
		return true;
	}

	bool Drop() {
		DropTableStmt s;
		if (!ExpectKw("TABLE"))
			return false;
		if (Kw("IF")) {
			if (!ExpectKw("EXISTS"))
				return false;
			s.ifExists = true;
		}
		if (!Ident(s.table))
			return false;
		m_stmt->body = std::move(s);
		return true;
	}

	bool Insert() {
		InsertStmt s;
		if (Kw("OR")) {
			if (!ExpectKw("REPLACE"))
				return false;
			s.replace = true;
		}
		if (!ExpectKw("INTO") || !Ident(s.table))
			return false;
		if (Sym("(")) {
			do {
				std::string c;
				if (!Ident(c))
					return false;
				s.columns.push_back(std::move(c));
			} while (Sym(","));
			if (!ExpectSym(")"))
				return false;
		}
		if (!ExpectKw("VALUES"))
			return false;
		do {
			if (!ExpectSym("("))
				return false;
			std::vector<ExprPtr> row;
			do {
				ExprPtr e = Expression();
				if (!e)
					return false;
				row.push_back(std::move(e));
			} while (Sym(","));
			if (!ExpectSym(")"))
				return false;
			s.rows.push_back(std::move(row));
		} while (Sym(","));
		m_stmt->body = std::move(s);
		return true;
	}

	bool Select() {
		SelectStmt s;
		if (Sym("*")) {
		} else if (IsKw("COUNT")) {
			Advance();
			if (!ExpectSym("(") || !ExpectSym("*") || !ExpectSym(")"))
				return false;
			s.countStar = true;
		} else {
			do {
				std::string c;
				if (!Ident(c))
					return false;
				s.columns.push_back(std::move(c));
			} while (Sym(","));
		}
		if (!ExpectKw("FROM") || !Ident(s.table))
			return false;
		if (Kw("WHERE") && !(s.where = Expression()))
			return false;
		if (Kw("ORDER")) {
			if (!ExpectKw("BY") || !Ident(s.orderBy))
				return false;
			if (Kw("DESC"))
				s.descending = true;
			else
				Kw("ASC");
		}
		if (Kw("LIMIT")) {
			if (m_tok.kind != Tok::Number)
				return Fail("nombre attendu après LIMIT");
			s.limit = static_cast<size_t>(m_tok.number.AsInt());
			Advance();
			if (Kw("OFFSET")) {
				if (m_tok.kind != Tok::Number)
					return Fail("nombre attendu après OFFSET");
				s.offset = static_cast<size_t>(m_tok.number.AsInt());
				Advance();
			}
		}
		m_stmt->body = std::move(s);
		return true;
	}

	bool Update() {
		UpdateStmt s;
		if (!Ident(s.table) || !ExpectKw("SET"))
			return false;
		do {
			std::string c;
			if (!Ident(c) || !ExpectSym("="))
				return false;
			ExprPtr e = Expression();
			if (!e)
				return false;
			s.sets.emplace_back(std::move(c), std::move(e));
		} while (Sym(","));
		if (Kw("WHERE") && !(s.where = Expression()))
			return false;
		m_stmt->body = std::move(s);
		return true;
	}

	bool Delete() {
		DeleteStmt s;
		if (!ExpectKw("FROM") || !Ident(s.table))
			return false;
		if (Kw("WHERE") && !(s.where = Expression()))
			return false;
		m_stmt->body = std::move(s);
		return true;
	}

	// ── Expressions (descente récursive par précédence) ────────────────────
	static ExprPtr MakeBinary(Op op, ExprPtr a, ExprPtr b) {
		auto e = std::make_unique<Expr>();
		e->kind = Expr::Kind::Binary;
		e->op = op;
		e->args.push_back(std::move(a));
		e->args.push_back(std::move(b));
		return e;
	}

	ExprPtr Expression() { return OrExpr(); }
	ExprPtr OrExpr() {
		ExprPtr l = AndExpr();
		while (l && Kw("OR")) {
			ExprPtr r = AndExpr();
			if (!r)
				return nullptr;
			l = MakeBinary(Op::Or, std::move(l), std::move(r));
		}
		return l;
	}
	ExprPtr AndExpr() {
		ExprPtr l = NotExpr();
		while (l && Kw("AND")) {
			ExprPtr r = NotExpr();
			if (!r)
				return nullptr;
			l = MakeBinary(Op::And, std::move(l), std::move(r));
		}
		return l;
	}
	ExprPtr NotExpr() {
		if (Kw("NOT")) {
			ExprPtr inner = NotExpr();
			if (!inner)
				return nullptr;
			auto e = std::make_unique<Expr>();
			e->kind = Expr::Kind::Unary;
			e->op = Op::Not;
			e->args.push_back(std::move(inner));
			return e;
		}
		return Comparison();
	}
	ExprPtr Comparison() {
		ExprPtr l = Additive();
		if (!l)
			return nullptr;
		if (Kw("IS")) {
			auto e = std::make_unique<Expr>();
			e->kind = Expr::Kind::IsNull;
			e->negated = Kw("NOT");
			if (!ExpectKw("NULL"))
				return nullptr;
			e->args.push_back(std::move(l));
			return e;
		}
		const bool neg = Kw("NOT");
		if (Kw("BETWEEN")) {
			ExprPtr lo = Additive();
			if (!lo || !ExpectKw("AND"))
				return nullptr;
			ExprPtr hi = Additive();
			if (!hi)
				return nullptr;
			auto e = std::make_unique<Expr>();
			e->kind = Expr::Kind::Between;
			e->negated = neg;
			e->args.push_back(std::move(l));
			e->args.push_back(std::move(lo));
			e->args.push_back(std::move(hi));
			return e;
		}
		if (Kw("IN")) {
			auto e = std::make_unique<Expr>();
			e->kind = Expr::Kind::In;
			e->negated = neg;
			e->args.push_back(std::move(l));
			if (!ExpectSym("("))
				return nullptr;
			do {
				ExprPtr v = Additive();
				if (!v)
					return nullptr;
				e->args.push_back(std::move(v));
			} while (Sym(","));
			if (!ExpectSym(")"))
				return nullptr;
			return e;
		}
		if (Kw("LIKE")) {
			ExprPtr pat = Additive();
			if (!pat)
				return nullptr;
			auto e = std::make_unique<Expr>();
			e->kind = Expr::Kind::Like;
			e->negated = neg;
			e->args.push_back(std::move(l));
			e->args.push_back(std::move(pat));
			return e;
		}
		if (neg) {
			Fail("BETWEEN, IN ou LIKE attendu après NOT");
			return nullptr;
		}
		static constexpr std::pair<std::string_view, Op> ops[] = {{"=", Op::Eq}, {"!=", Op::Ne}, {"<>", Op::Ne}, {"<", Op::Lt},
																  {"<=", Op::Le}, {">", Op::Gt}, {">=", Op::Ge}};
		for (auto [s, op] : ops) {
			if (Sym(s)) {
				ExprPtr r = Additive();
				if (!r)
					return nullptr;
				return MakeBinary(op, std::move(l), std::move(r));
			}
		}
		return l;
	}
	ExprPtr Additive() {
		ExprPtr l = Multiplicative();
		while (l) {
			Op op;
			if (Sym("+"))
				op = Op::Add;
			else if (Sym("-"))
				op = Op::Sub;
			else
				break;
			ExprPtr r = Multiplicative();
			if (!r)
				return nullptr;
			l = MakeBinary(op, std::move(l), std::move(r));
		}
		return l;
	}
	ExprPtr Multiplicative() {
		ExprPtr l = UnaryExpr();
		while (l) {
			Op op;
			if (Sym("*"))
				op = Op::Mul;
			else if (Sym("/"))
				op = Op::Div;
			else if (Sym("%"))
				op = Op::Mod;
			else
				break;
			ExprPtr r = UnaryExpr();
			if (!r)
				return nullptr;
			l = MakeBinary(op, std::move(l), std::move(r));
		}
		return l;
	}
	ExprPtr UnaryExpr() {
		if (Sym("-")) {
			ExprPtr inner = UnaryExpr();
			if (!inner)
				return nullptr;
			if (inner->kind == Expr::Kind::Literal && inner->literal.IsNumeric()) {
				if (inner->literal.Type() == ColumnType::Int)
					inner->literal = Value(-inner->literal.AsInt());
				else
					inner->literal = Value(-inner->literal.AsReal());
				return inner;
			}
			auto e = std::make_unique<Expr>();
			e->kind = Expr::Kind::Unary;
			e->op = Op::Neg;
			e->args.push_back(std::move(inner));
			return e;
		}
		return Primary();
	}
	ExprPtr Primary() {
		auto e = std::make_unique<Expr>();
		switch (m_tok.kind) {
		case Tok::Number:
		case Tok::BlobLit:
			e->literal = m_tok.number;
			Advance();
			return e;
		case Tok::String:
			e->literal = Value(m_tok.text);
			Advance();
			return e;
		case Tok::Param:
			e->kind = Expr::Kind::Param;
			e->param = m_stmt->paramCount++;
			Advance();
			return e;
		case Tok::Ident:
			if (m_tok.upper == "NULL") {
				Advance();
				return e;
			}
			if (m_tok.upper == "TRUE" || m_tok.upper == "FALSE") {
				e->literal = Value(m_tok.upper == "TRUE");
				Advance();
				return e;
			}
			e->kind = Expr::Kind::Column;
			e->column = m_tok.text;
			e->columnSlot = m_stmt->columnSlots++;
			Advance();
			return e;
		case Tok::Symbol:
			if (Sym("(")) {
				ExprPtr inner = Expression();
				if (!inner || !ExpectSym(")"))
					return nullptr;
				return inner;
			}
			[[fallthrough]];
		default:
			Fail(m_tok.kind == Tok::End ? "expression attendue" : std::format("expression attendue près de '{}'", m_tok.text));
			return nullptr;
		}
	}

	Lexer m_lex;
	Token m_tok;
	std::string m_err;
	Statement *m_stmt = nullptr;
};

} // namespace

Result<std::unique_ptr<Statement>, std::string> ParseStatement(std::string_view sql, size_t *consumed) {
	return Parser(sql).Run(consumed);
}

} // namespace sql
