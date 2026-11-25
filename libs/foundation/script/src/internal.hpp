#pragma once
/**
 * @file internal.hpp
 * @brief Types internes du langage de script (jetons, lexer, AST, parseur),
 *        partagés entre les unités de traduction de `foundation.script`.
 *
 * Ne fait PAS partie de l'API publique (en-tête privé, dans `src/`).
 */
#include <cstdint>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "script/script.hpp"

namespace script {

/// Nombre → chaîne lisible (entiers exacts, sinon décimaux).
[[nodiscard]] std::string NumberToString(double d);

// ── Jetons ──────────────────────────────────────────────────────────────────
enum class T : uint8_t { End, Ident, Number, String, Sym };

struct Tok {
	T kind = T::End;
	std::string text;
	double number = 0;
	int line = 1, col = 1;
};

/// Lexer : découpe la source en jetons (aucune dépendance à l'interpréteur).
class Lexer {
public:
	explicit Lexer(std::string_view s) : m_s(s) {}
	[[nodiscard]] bool Run(std::vector<Tok> &out, ScriptError &err);

private:
	int Col() const { return static_cast<int>(m_i - m_lineStart) + 1; }
	void Skip();

	std::string_view m_s;
	size_t m_i = 0;
	int m_line = 1;
	size_t m_lineStart = 0;
};

// ── AST ────────────────────────────────────────────────────────────────────
enum class NK : uint8_t {
	// expressions
	Nil, True, False, Number, String, Ident, ListLit, MapLit, Lambda, Unary, Binary, And, Or, Index, Member, Call, MethodCall, Assign, Import, Await,
	// instructions
	Let, Block, If, While, DoWhile, For, Return, Break, Continue, ExprStmt, FnDecl, ClassDecl, InterfaceDecl, NamespaceDecl, EnumDecl, ImportStmt
};

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
	NK kind;
	int line = 0, col = 0;
	double number = 0;
	std::string text;	  // chaîne littérale / nom de membre / opérateur
	uint32_t sym = 0;	  // symbole (identifiant)
	bool flag = false;	  // Block : contient des `let` ; Assign : opérateur composé ; membre : `export`
	uint8_t mod = 0;	  // modificateur d'un membre de classe : 1 = static, 2 = factory, 4 = override, 8 = async
	const FunctionDef *fn = nullptr;
	std::vector<NodePtr> kids;
	std::vector<std::string> keys; // MapLit
};

struct FunctionDef {
	std::string name;
	std::vector<uint32_t> params;
	NodePtr body;
	std::string file;
	bool async = false; ///< `async fn` : l'appel rend un objet future (tâche)
};

struct Program {
	std::vector<NodePtr> statements;
	std::vector<std::unique_ptr<FunctionDef>> functions;
	std::string file;
};

struct Environment;
using EnvPtr = std::shared_ptr<Environment>;

struct Environment {
	std::vector<std::pair<uint32_t, Value>> vars;
	std::shared_ptr<Environment> parent;
	Value *Find(uint32_t sym) {
		for (Environment *e = this; e; e = e->parent.get())
			for (auto it = e->vars.rbegin(); it != e->vars.rend(); ++it) // les dernières déclarations masquent
				if (it->first == sym)
					return &it->second;
		return nullptr;
	}
};

// ── Parseur ────────────────────────────────────────────────────────────────
/// Parseur récursif descendant : construit l'AST d'un programme.
class Parser {
public:
	Parser(std::function<uint32_t(std::string_view)> intern, std::vector<Tok> toks, Program &prog)
		: m_intern(std::move(intern)), m_toks(std::move(toks)), m_prog(prog) {}

	[[nodiscard]] bool ParseProgram();
	ScriptError err;
	bool bad = false;

private:
	const Tok &Peek(size_t o = 0) const { return m_toks[std::min(m_i + o, m_toks.size() - 1)]; }
	bool IsSym(std::string_view s, size_t o = 0) const { return Peek(o).kind == T::Sym && Peek(o).text == s; }
	bool IsKw(std::string_view s, size_t o = 0) const { return Peek(o).kind == T::Ident && Peek(o).text == s; }
	bool Accept(std::string_view s);
	bool Fail(std::string msg);
	bool Expect(std::string_view s) { return Accept(s) || Fail(std::format("'{}' attendu, trouvé '{}'", s, Peek().text)); }
	NodePtr Make(NK k);
	static bool IsKeyword(std::string_view s);

	NodePtr Block();
	const FunctionDef *Function(std::string name, bool body = true, bool async = false);
	NodePtr Statement(bool abstract = false);
	NodePtr Expression() { return Assignment(); }
	NodePtr Assignment();
	NodePtr Binary(NodePtr (Parser::*next)(), std::initializer_list<std::string_view> ops, NK kind = NK::Binary);
	NodePtr OrExpr() { return Binary(&Parser::AndExpr, {"or"}, NK::Or); }
	NodePtr AndExpr() { return Binary(&Parser::NotExpr, {"and"}, NK::And); }
	NodePtr NotExpr();
	NodePtr Equality() { return Binary(&Parser::Comparison, {"==", "!="}); }
	NodePtr Comparison() { return Binary(&Parser::Concat, {"<", "<=", ">", ">=", "is", "as"}); }
	NodePtr Concat() { return Binary(&Parser::Additive, {".."}); }
	NodePtr Additive() { return Binary(&Parser::Multiplicative, {"+", "-"}); }
	NodePtr Multiplicative() { return Binary(&Parser::UnaryExpr, {"*", "/", "%"}); }
	NodePtr UnaryExpr();
	NodePtr Power();
	bool Args(Node &call);
	NodePtr Postfix();
	NodePtr Primary();

	std::function<uint32_t(std::string_view)> m_intern;
	std::vector<Tok> m_toks;
	size_t m_i = 0;
	Program &m_prog;
};

} // namespace script
