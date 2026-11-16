// Définitions de data/documents/json.hpp — codec JSON (RFC 8259) sur l'arbre
// dataio::Node commun. Parseur tolérant (commentaires `//`, `#`, `/* */` et
// virgules finales) et écrivain compact ou indenté (`WriteJsonNode`).
#include "dataio/documents/json.hpp"

#include <charconv>
#include <cstdio>

namespace dataio {

namespace {

/// Saute espaces et commentaires (//, #, /* */) — JSON « tolérant » (JSONC),
/// même tolérance que l'ancien parseur dataio::Json (commentaires de ligne et
/// de bloc, dièse), indispensable pour lire les fichiers de configuration.
void SkipWsComments(Reader &r) {
	for (;;) {
		r.SkipWs();
		auto c = r.Peek();
		if (!c)
			return;
		if (*c == '#') {
			do { r.Get(); } while (r.Peek().IsSome() && *r.Peek() != '\n');
		} else if (*c == '/' && r.PeekAt(1).IsSome() && *r.PeekAt(1) == '/') {
			do { r.Get(); } while (r.Peek().IsSome() && *r.Peek() != '\n');
		} else if (*c == '/' && r.PeekAt(1).IsSome() && *r.PeekAt(1) == '*') {
			r.Get();
			r.Get(); // "/*"
			for (;;) {
				auto a = r.Peek();
				if (!a)
					return;
				if (*a == '*' && r.PeekAt(1).IsSome() && *r.PeekAt(1) == '/') {
					r.Get();
					r.Get();
					break;
				}
				r.Get();
			}
		} else {
			return;
		}
	}
}

void EncodeString(String &out, const String &s) {
	out.Append('"');
	for (const char cChar : s) {
		const auto c = static_cast<unsigned char>(cChar);
		switch (c) {
			case '"':
				out.Append("\\\"");
				break;
			case '\\':
				out.Append("\\\\");
				break;
			case '\b':
				out.Append("\\b");
				break;
			case '\f':
				out.Append("\\f");
				break;
			case '\n':
				out.Append("\\n");
				break;
			case '\r':
				out.Append("\\r");
				break;
			case '\t':
				out.Append("\\t");
				break;
			default:
				if (c < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", int(c));
					out.Append(buf);
				} else {
					out.Append(static_cast<char>(c));
				}
		}
	}
	out.Append('"');
}

void WriteIndent(String &out, int level, int width) {
	for (int i = 0; i < level * width; ++i)
		out.Append(' ');
}

void EncodeNode(String &out, const NodePtr &node, int level, int width) {
	if (!node || node->IsNone()) {
		out.Append("null");
		return;
	}
	const bool pretty = width >= 0;
	auto nl = [&](int l) {
		if (!pretty)
			return;
		out.Append('\n');
		WriteIndent(out, l, width);
	};
	switch (node->type) {
		case NodeType::OBJECT: {
			out.Append('{');
			auto &keys = node->Keys();
			for (size_t i = 0; i < keys.size(); ++i) {
				nl(level + 1);
				EncodeString(out, keys[i]);
				out.Append(pretty ? ": " : ":");
				EncodeNode(out, node->Get(keys[i]), level + 1, width);
				if (i + 1 < keys.size())
					out.Append(',');
			}
			if (!keys.empty())
				nl(level);
			out.Append('}');
			return;
		}
		case NodeType::ARRAY: {
			out.Append('[');
			size_t n = node->GetSize();
			for (size_t i = 0; i < n; ++i) {
				nl(level + 1);
				EncodeNode(out, node->At(i), level + 1, width);
				if (i + 1 < n)
					out.Append(',');
			}
			if (n > 0)
				nl(level);
			out.Append(']');
			return;
		}
		case NodeType::STRING:
			EncodeString(out, node->stringValue);
			return;
		case NodeType::BOOL:
			out.Append(node->boolValue ? "true" : "false");
			return;
		case NodeType::INT:
			out.Append(String::From(node->intValue));
			return;
		case NodeType::FLOAT: {
			// Représentation la PLUS COURTE qui se relit à l'identique
			// (std::to_chars sans précision) ; on force le point décimal pour
			// que le type FLOAT survive à l'aller-retour.
			char buffer[64];
			const auto written = std::to_chars(buffer, buffer + sizeof(buffer), node->floatValue);
			String text = written.ec == std::errc{}
							  ? String(StringView(buffer, size_t(written.ptr - buffer)))
							  : String::FromStream(node->floatValue);
			bool needsDecimalPoint = true;
			for (size_t i = 0; i < text.GetSize(); ++i) {
				char c = text.CharAt(i);
				if (c == '.' || c == 'e' || c == 'E' || c == 'n' || c == 'i') {
					needsDecimalPoint = false;
					break;
				}
			}
			if (needsDecimalPoint)
				text.Concat(".0");
			out.Append(text);
			return;
		}
		default:
			out.Append("null");
			return;
	}
}

} // namespace

// ── JsonDocument ─────────────────────────────────────────────────────────────

String JsonDocument::EncodeStr() const {
	return WriteJsonNode(GetRoot(), 2);
}

String WriteJsonNode(const NodePtr &node, int indent) {
	String out;
	EncodeNode(out, node, 0, indent);
	return out;
}

Option<ParseError> JsonDocument::DecodeImpl(const String &content) {
	Reader r(content);
	auto res = ParseNode(r);
	if (res.IsError())
		return Some(res.Error());
	SkipWsComments(r);
	if (r.Peek().IsSome())
		return Some(ParseError("JSON: contenu inattendu après la valeur", r.Line()));
	SetRoot(res.Unwrap());
	return NONE;
}

Result<NodePtr, ParseError> JsonDocument::FailNode(const Reader &r, const String &msg) {
	return Result<NodePtr, ParseError>(Err<ParseError>(ParseError(String("JSON: ") + msg, r.Line())));
}

Result<String, ParseError> JsonDocument::FailStr(const Reader &r, const String &msg) {
	return Result<String, ParseError>(Err<ParseError>(ParseError(String("JSON: ") + msg, r.Line())));
}

Result<String, ParseError> JsonDocument::ParseStringLiteral(Reader &r) {
	r.Get(); // consomme le '"' ouvrant
	String value;
	while (true) {
		auto oc = r.Peek();
		if (!oc)
			return FailStr(r, "chaîne non terminée");
		if (*oc == '"')
			break;
		char c = *r.Get();
		if (c == '\\') {
			auto on = r.Get();
			if (!on)
				return FailStr(r, "échappement non terminé");
			char nx = *on;
			switch (nx) {
				case '"':
					value += '"';
					break;
				case '\\':
					value += '\\';
					break;
				case '/':
					value += '/';
					break;
				case 'b':
					value += '\b';
					break;
				case 'f':
					value += '\f';
					break;
				case 'n':
					value += '\n';
					break;
				case 'r':
					value += '\r';
					break;
				case 't':
					value += '\t';
					break;
				case 'u':
					for (int i = 0; i < 4 && !r.Eof(); ++i)
						r.Get(); // \uXXXX non décodé en UTF-8 ici
					break;
				default:
					value += nx;
			}
		} else {
			value += c;
		}
	}
	r.Get(); // '"' fermant
	return Ok<String>(value);
}

Result<NodePtr, ParseError> JsonDocument::ParseLiteral(Reader &r) {
	String token;
	while (true) {
		auto oc = r.Peek();
		if (!oc)
			break;
		char c = *oc;
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.')
			token += *r.Get();
		else
			break;
	}
	if (token.IsEmpty())
		return FailNode(r, "valeur attendue");
	if (token == "true")
		return Ok<NodePtr>(Node::MakeBool(true));
	if (token == "false")
		return Ok<NodePtr>(Node::MakeBool(false));
	if (token == "null")
		return Ok<NodePtr>(Node::MakeNone());
	if (auto i = token.TryParseInt())
		return Ok<NodePtr>(Node::MakeInt(*i));
	if (auto f = token.TryParseDouble())
		return Ok<NodePtr>(Node::MakeFloat(*f));
	return FailNode(r, "littéral invalide: " + token);
}

Result<NodePtr, ParseError> JsonDocument::ParseArray(Reader &r) {
	r.Get(); // '['
	auto arr = Node::MakeArray();
	SkipWsComments(r);
	if (r.PeekIs(']')) {
		r.Get();
		return Ok<NodePtr>(arr);
	}
	while (true) {
		auto item = ParseNode(r);
		if (item.IsError())
			return item;
		arr->Push(item.Unwrap());
		SkipWsComments(r);
		auto oc = r.Get();
		if (!oc)
			return FailNode(r, "',' ou ']' attendu dans un tableau");
		if (*oc == ']')
			break;
		if (*oc != ',')
			return FailNode(r, "',' ou ']' attendu dans un tableau");
		SkipWsComments(r);
		if (r.PeekIs(']')) { // virgule finale tolérée
			r.Get();
			break;
		}
	}
	return Ok<NodePtr>(arr);
}

Result<NodePtr, ParseError> JsonDocument::ParseObject(Reader &r) {
	r.Get(); // '{'
	auto obj = Node::MakeObject();
	SkipWsComments(r);
	if (r.PeekIs('}')) {
		r.Get();
		return Ok<NodePtr>(obj);
	}
	while (true) {
		SkipWsComments(r);
		if (!r.PeekIs('"'))
			return FailNode(r, "clé de type chaîne attendue");
		auto keyRes = ParseStringLiteral(r);
		if (keyRes.IsError())
			return Result<NodePtr, ParseError>(Err<ParseError>(keyRes.Error()));
		String key = keyRes.Unwrap();
		SkipWsComments(r);
		auto colon = r.Get();
		if (!colon || *colon != ':')
			return FailNode(r, "':' attendu après la clé");
		SkipWsComments(r);
		auto val = ParseNode(r);
		if (val.IsError())
			return val;
		obj->Set(key, val.Unwrap());
		SkipWsComments(r);
		auto oc = r.Get();
		if (!oc)
			return FailNode(r, "',' ou '}' attendu dans un objet");
		if (*oc == '}')
			break;
		if (*oc != ',')
			return FailNode(r, "',' ou '}' attendu dans un objet");
		SkipWsComments(r);
		if (r.PeekIs('}')) { // virgule finale tolérée
			r.Get();
			break;
		}
	}
	return Ok<NodePtr>(obj);
}

Result<NodePtr, ParseError> JsonDocument::ParseNode(Reader &r) {
	SkipWsComments(r);
	auto oc = r.Peek();
	if (!oc)
		return FailNode(r, "valeur attendue (fin de fichier)");
	switch (*oc) {
		case '{':
			return ParseObject(r);
		case '[':
			return ParseArray(r);
		case '"': {
			auto sres = ParseStringLiteral(r);
			if (sres.IsError())
				return Result<NodePtr, ParseError>(Err<ParseError>(sres.Error()));
			return Ok<NodePtr>(Node::MakeString(sres.Unwrap()));
		}
		default:
			return ParseLiteral(r);
	}
}

} // namespace dataio
