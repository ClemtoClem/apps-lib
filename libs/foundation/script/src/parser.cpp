// Définitions du parseur du langage de script (construction de l'AST).
#include "internal.hpp"

#include <algorithm>
#include <format>

namespace script {

bool Parser::Accept(std::string_view s) {
	if (IsSym(s) || IsKw(s)) {
		++m_i;
		return true;
	}
	return false;
}

bool Parser::Fail(std::string msg) {
	if (!bad) {
		bad = true;
		err = {std::move(msg), m_prog.file, Peek().line, Peek().col};
	}
	return false;
}

NodePtr Parser::Make(NK k) {
	auto n = std::make_unique<Node>();
	n->kind = k;
	n->line = Peek().line;
	n->col = Peek().col;
	return n;
}

bool Parser::IsKeyword(std::string_view s) { return std::ranges::find(Keywords(), s) != Keywords().end(); }

bool Parser::ParseProgram() {
	while (Peek().kind != T::End && !bad) {
		NodePtr s = Statement();
		if (!s)
			return false;
		m_prog.statements.push_back(std::move(s));
	}
	return !bad;
}

NodePtr Parser::Block() {
	auto b = Make(NK::Block);
	if (!Expect("{"))
		return nullptr;
	while (!IsSym("}")) {
		if (Peek().kind == T::End) {
			Fail("'}' attendu avant la fin du fichier");
			return nullptr;
		}
		NodePtr s = Statement();
		if (!s)
			return nullptr;
		b->flag |= s->kind == NK::Let || s->kind == NK::FnDecl || s->kind == NK::ClassDecl || s->kind == NK::ImportStmt;
		b->kids.push_back(std::move(s));
	}
	++m_i;
	return b;
}

const FunctionDef *Parser::Function(std::string name, bool body, bool async) {
	auto def = std::make_unique<FunctionDef>();
	def->name = std::move(name);
	def->file = m_prog.file;
	def->async = async;
	if (!Expect("("))
		return nullptr;
	if (!IsSym(")")) {
		do {
			if (Peek().kind != T::Ident) {
				Fail("nom de paramètre attendu");
				return nullptr;
			}
			def->params.push_back(m_intern(Peek().text));
			++m_i;
		} while (Accept(","));
	}
	if (!Expect(")"))
		return nullptr;
	if (body) { // une méthode `abstract` n'a pas de corps
		def->body = Block();
		if (!def->body)
			return nullptr;
	}
	const FunctionDef *raw = def.get();
	m_prog.functions.push_back(std::move(def));
	return raw;
}

NodePtr Parser::Statement(bool abstract) {
	if (IsKw("let") || IsKw("var") || IsKw("const")) {
		auto n = Make(NK::Let);
		++m_i;
		if (Peek().kind != T::Ident) {
			Fail("nom de variable attendu");
			return nullptr;
		}
		n->sym = m_intern(Peek().text);
		++m_i;
		if (Accept("=")) {
			NodePtr e = Expression();
			if (!e)
				return nullptr;
			n->kids.push_back(std::move(e));
		}
		return n;
	}
	const bool asyncFn = IsKw("async") && IsKw("fn", 1) && Peek(2).kind == T::Ident;
	if ((IsKw("fn") || asyncFn) && Peek(1).kind == T::Ident) {
		if (asyncFn)
			++m_i; // `async`
		auto n = Make(NK::FnDecl);
		++m_i; // `fn`
		n->sym = m_intern(Peek().text);
		std::string name = Peek().text;
		++m_i;
		n->fn = Function(std::move(name), !abstract, asyncFn);
		return n->fn ? std::move(n) : nullptr;
	}
	if (IsKw("namespace") && Peek(1).kind == T::Ident) {
		auto n = Make(NK::NamespaceDecl);
		++m_i;
		n->text = Peek().text;
		n->sym = m_intern(Peek().text);
		++m_i;
		NodePtr body = Block();
		if (!body)
			return nullptr;
		n->kids.push_back(std::move(body));
		return n;
	}
	if (IsKw("if")) {
		auto n = Make(NK::If);
		++m_i;
		NodePtr cond = Expression();
		if (!cond)
			return nullptr;
		NodePtr then = Block();
		if (!then)
			return nullptr;
		n->kids.push_back(std::move(cond));
		n->kids.push_back(std::move(then));
		if (Accept("else")) {
			NodePtr other = IsKw("if") ? Statement() : Block();
			if (!other)
				return nullptr;
			n->kids.push_back(std::move(other));
		}
		return n;
	}
	if (IsKw("while")) {
		auto n = Make(NK::While);
		++m_i;
		NodePtr cond = Expression();
		NodePtr body = cond ? Block() : nullptr;
		if (!body)
			return nullptr;
		n->kids.push_back(std::move(cond));
		n->kids.push_back(std::move(body));
		return n;
	}
	if (IsKw("do")) {
		auto n = Make(NK::DoWhile);
		++m_i;
		NodePtr body = Block();
		if (!body || !Expect("while"))
			return nullptr;
		NodePtr cond = Expression();
		if (!cond)
			return nullptr;
		n->kids.push_back(std::move(body));
		n->kids.push_back(std::move(cond));
		return n;
	}
	if (IsKw("for")) {
		auto n = Make(NK::For);
		++m_i;
		const bool paren = Accept("(");
		if (Peek().kind != T::Ident) {
			Fail("variable de boucle attendue");
			return nullptr;
		}
		n->sym = m_intern(Peek().text);
		++m_i;
		if (!Expect("in"))
			return nullptr;
		NodePtr seq = Expression();
		if (!seq || (paren && !Expect(")")))
			return nullptr;
		NodePtr body = Block();
		if (!body)
			return nullptr;
		n->kids.push_back(std::move(seq));
		n->kids.push_back(std::move(body));
		return n;
	}
	if (IsKw("return")) {
		auto n = Make(NK::Return);
		const int line = Peek().line;
		++m_i;
		if (!IsSym("}") && Peek().kind != T::End && Peek().line == line) {
			NodePtr e = Expression();
			if (!e)
				return nullptr;
			n->kids.push_back(std::move(e));
		}
		return n;
	}
	if (IsKw("break")) {
		auto n = Make(NK::Break);
		++m_i;
		return n;
	}
	if (IsKw("continue")) {
		auto n = Make(NK::Continue);
		++m_i;
		return n;
	}
	if (IsKw("import")) { // ses définitions dans la portée courante (sans chargeur : sans effet)
		auto n = Make(NK::ImportStmt);
		++m_i;
		if (Peek().kind != T::String) {
			Fail("nom de module attendu après import");
			return nullptr;
		}
		n->text = Peek().text;
		++m_i;
		return n;
	}
	const bool isAbstract = IsKw("abstract") && (IsKw("class", 1) || IsKw("interface", 1));
	const bool isInterface = IsKw("interface") && Peek(1).kind == T::Ident;
	if (isAbstract)
		++m_i;
	if (isInterface || (IsKw("class") && Peek(1).kind == T::Ident)) {
		auto n = Make(isInterface ? NK::InterfaceDecl : NK::ClassDecl);
		n->flag = isAbstract;
		++m_i;
		n->text = Peek().text;
		n->sym = m_intern(Peek().text);
		++m_i;
		if (Accept("extends")) { // interface : super-interfaces ; classe : parent/bases natives
			do {
				NodePtr base = Postfix();
				if (!base)
					return nullptr;
				n->kids.push_back(std::move(base));
			} while (Accept(","));
		}
		n->number = double(n->kids.size()); // bases
		if (Accept("implements")) { // classe : interfaces (après les bases)
			do {
				NodePtr iface = Postfix();
				if (!iface)
					return nullptr;
				n->kids.push_back(std::move(iface));
			} while (Accept(","));
		}
		n->mod = uint8_t(n->kids.size() - size_t(n->number)); // nombre d'interfaces
		if (!Expect("{"))
			return nullptr;
		while (!IsSym("}")) {
			if (Peek().kind == T::End) {
				Fail("'}' attendu avant la fin du fichier (classe)");
				return nullptr;
			}
			if (Accept(";"))
				continue;
			uint8_t mods = 0; // static / factory / override / async
			for (;;) {
				if (Accept("static")) mods |= 1;
				else if (Accept("factory")) mods |= 2;
				else if (Accept("override")) mods |= 4;
				else if (Accept("async")) mods |= 8;
				else break;
			}
			const bool exported = Accept("export");
			const bool abstractMember = Accept("abstract");
			if (exported && !IsKw("let") && !IsKw("var")) {
				Fail("export : champ « let » attendu");
				return nullptr;
			}
			if (!IsKw("let") && !IsKw("var") && !IsKw("const") && !(IsKw("fn") && Peek(1).kind == T::Ident)) {
				Fail(std::format("champ (let) ou méthode (fn) attendu dans la classe {}, trouvé '{}'", n->text, Peek().text));
				return nullptr;
			}
			NodePtr member = Statement(abstractMember || isInterface); // interface / `abstract` : sans corps
			if (!member)
				return nullptr;
			member->flag = exported; // champ exporté (paramètre)
			member->mod = mods;
			n->kids.push_back(std::move(member));
		}
		++m_i;
		return n;
	}
	if (IsKw("enum") && Peek(1).kind == T::Ident) {
		auto n = Make(NK::EnumDecl);
		++m_i;
		n->text = Peek().text;
		n->sym = m_intern(Peek().text);
		++m_i;
		if (!Expect("{"))
			return nullptr;
		while (!IsSym("}")) {
			if (Peek().kind == T::End) {
				Fail("'}' attendu avant la fin du fichier (enum)");
				return nullptr;
			}
			if (Accept(";") || Accept(","))
				continue;
			if (Peek().kind != T::Ident) {
				Fail("nom de valeur d'enum attendu");
				return nullptr;
			}
			auto v = Make(NK::Ident);
			v->sym = m_intern(Peek().text);
			v->text = Peek().text;
			++m_i;
			if (Accept("=")) {
				NodePtr e = Expression();
				if (!e)
					return nullptr;
				v->kids.push_back(std::move(e));
			}
			n->kids.push_back(std::move(v));
		}
		++m_i;
		return n;
	}
	if (IsSym("{"))
		return Block();
	auto n = Make(NK::ExprStmt);
	NodePtr e = Expression();
	if (!e)
		return nullptr;
	n->kids.push_back(std::move(e));
	return n;
}

NodePtr Parser::Assignment() {
	NodePtr lhs = OrExpr();
	if (!lhs)
		return nullptr;
	static constexpr std::string_view ops[] = {"=", "+=", "-=", "*=", "/=", "%="};
	for (auto op : ops) {
		if (IsSym(op)) {
			if (lhs->kind != NK::Ident && lhs->kind != NK::Index && lhs->kind != NK::Member) {
				Fail("cible d'affectation invalide");
				return nullptr;
			}
			auto n = Make(NK::Assign);
			++m_i;
			n->text = std::string(op.substr(0, op.size() - 1)); // "" pour '=', sinon l'opérateur
			NodePtr rhs = Assignment();
			if (!rhs)
				return nullptr;
			n->kids.push_back(std::move(lhs));
			n->kids.push_back(std::move(rhs));
			return n;
		}
	}
	return lhs;
}

NodePtr Parser::Binary(NodePtr (Parser::*next)(), std::initializer_list<std::string_view> ops, NK kind) {
	NodePtr l = (this->*next)();
	while (l) {
		std::string_view hit;
		for (auto op : ops)
			if (IsSym(op) || IsKw(op))
				hit = op;
		if (hit.empty())
			break;
		auto n = Make(kind);
		++m_i;
		n->text = std::string(hit);
		NodePtr r = (this->*next)();
		if (!r)
			return nullptr;
		n->kids.push_back(std::move(l));
		n->kids.push_back(std::move(r));
		l = std::move(n);
	}
	return l;
}

NodePtr Parser::NotExpr() {
	if (IsKw("not")) {
		auto n = Make(NK::Unary);
		++m_i;
		n->text = "not";
		NodePtr e = NotExpr();
		if (!e)
			return nullptr;
		n->kids.push_back(std::move(e));
		return n;
	}
	return Equality();
}

NodePtr Parser::UnaryExpr() {
	if (IsSym("-")) {
		auto n = Make(NK::Unary);
		++m_i;
		n->text = "-";
		NodePtr e = UnaryExpr();
		if (!e)
			return nullptr;
		n->kids.push_back(std::move(e));
		return n;
	}
	if (IsKw("await")) {
		auto n = Make(NK::Await);
		++m_i;
		NodePtr e = UnaryExpr();
		if (!e)
			return nullptr;
		n->kids.push_back(std::move(e));
		return n;
	}
	return Power();
}

NodePtr Parser::Power() {
	NodePtr base = Postfix();
	if (base && IsSym("^")) {
		auto n = Make(NK::Binary);
		++m_i;
		n->text = "^";
		NodePtr exp = UnaryExpr(); // associatif à droite
		if (!exp)
			return nullptr;
		n->kids.push_back(std::move(base));
		n->kids.push_back(std::move(exp));
		return n;
	}
	return base;
}

bool Parser::Args(Node &call) {
	if (!IsSym(")")) {
		do {
			NodePtr a = Expression();
			if (!a)
				return false;
			call.kids.push_back(std::move(a));
		} while (Accept(","));
	}
	return Expect(")");
}

NodePtr Parser::Postfix() {
	NodePtr e = Primary();
	while (e) {
		if (IsSym("(")) {
			auto n = Make(NK::Call);
			++m_i;
			n->kids.push_back(std::move(e));
			if (!Args(*n))
				return nullptr;
			e = std::move(n);
		} else if (IsSym("[")) {
			auto n = Make(NK::Index);
			++m_i;
			NodePtr idx = Expression();
			if (!idx || !Expect("]"))
				return nullptr;
			n->kids.push_back(std::move(e));
			n->kids.push_back(std::move(idx));
			e = std::move(n);
		} else if (IsSym(".") && Peek(1).kind == T::Ident) {
			++m_i;
			std::string name = Peek().text;
			++m_i;
			if (IsSym("(")) {
				auto n = Make(NK::MethodCall);
				++m_i;
				n->text = std::move(name);
				n->kids.push_back(std::move(e));
				if (!Args(*n))
					return nullptr;
				e = std::move(n);
			} else {
				auto n = Make(NK::Member);
				n->text = std::move(name);
				n->kids.push_back(std::move(e));
				e = std::move(n);
			}
		} else {
			break;
		}
	}
	return e;
}

NodePtr Parser::Primary() {
	const Tok &t = Peek();
	if (t.kind == T::Number) {
		auto n = Make(NK::Number);
		n->number = t.number;
		++m_i;
		return n;
	}
	if (t.kind == T::String) {
		auto n = Make(NK::String);
		n->text = t.text;
		++m_i;
		return n;
	}
	if (t.kind == T::Ident) {
		if (t.text == "nil" || t.text == "null") {
			++m_i;
			return Make(NK::Nil);
		}
		if (t.text == "true" || t.text == "false") {
			auto n = Make(t.text == "true" ? NK::True : NK::False);
			++m_i;
			return n;
		}
		if (t.text == "import") { // expression : la table des définitions du module
			auto n = Make(NK::Import);
			++m_i;
			if (Peek().kind != T::String) {
				Fail("nom de module attendu après import");
				return nullptr;
			}
			n->text = Peek().text;
			++m_i;
			return n;
		}
		if (t.text == "this" || t.text == "super") {
			auto n = Make(NK::Ident);
			n->sym = m_intern(t.text);
			n->text = t.text;
			++m_i;
			return n;
		}
		if (t.text == "new") { // `new Classe(...)` : instanciation (appeler une classe instancie déjà)
			++m_i;
			return Primary();
		}
		const bool asyncLambda = t.text == "async" && Peek(1).kind == T::Ident && Peek(1).text == "fn";
		if (t.text == "fn" || asyncLambda) {
			if (asyncLambda)
				++m_i;
			auto n = Make(NK::Lambda);
			++m_i;
			n->fn = Function("<anonyme>", true, asyncLambda);
			return n->fn ? std::move(n) : nullptr;
		}
		if (IsKeyword(t.text)) {
			Fail(std::format("mot-clé '{}' inattendu", t.text));
			return nullptr;
		}
		auto n = Make(NK::Ident);
		n->sym = m_intern(t.text);
		n->text = t.text;
		++m_i;
		return n;
	}
	if (Accept("(")) {
		NodePtr e = Expression();
		if (!e || !Expect(")"))
			return nullptr;
		return e;
	}
	if (IsSym("[")) {
		auto n = Make(NK::ListLit);
		++m_i;
		while (!IsSym("]")) {
			NodePtr e = Expression();
			if (!e)
				return nullptr;
			n->kids.push_back(std::move(e));
			if (!Accept(","))
				break;
		}
		if (!Expect("]"))
			return nullptr;
		return n;
	}
	if (IsSym("{")) {
		auto n = Make(NK::MapLit);
		++m_i;
		while (!IsSym("}")) {
			std::string key;
			if (Peek().kind == T::Ident || Peek().kind == T::String)
				key = Peek().text;
			else if (Peek().kind == T::Number)
				key = NumberToString(Peek().number);
			else {
				Fail("clé de table attendue");
				return nullptr;
			}
			++m_i;
			if (!Accept(":") && !Expect("="))
				return nullptr;
			NodePtr v = Expression();
			if (!v)
				return nullptr;
			n->keys.push_back(std::move(key));
			n->kids.push_back(std::move(v));
			if (!Accept(","))
				break;
		}
		if (!Expect("}"))
			return nullptr;
		return n;
	}
	Fail(t.kind == T::End ? "expression attendue avant la fin du fichier" : std::format("expression attendue, trouvé '{}'", t.text));
	return nullptr;
}

} // namespace script
