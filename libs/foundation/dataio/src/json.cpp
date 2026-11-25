// Définitions de dataio/json.hpp — fonctions JSON de commodité sur dataio::Node.
// Le parsage et l'écriture du texte JSON sont délégués au codec `JsonDocument`
// (arbre `Node` commun) ; l'édition en place de tableaux reste indépendante.
#include "dataio/json.hpp"

#include <algorithm>
#include <format>

#include "dataio/documents/json.hpp"

namespace dataio {

namespace {

// ============================================================================
// Édition en place (ReplaceJsonArrayElement / AppendJsonArrayElement)
// ============================================================================

/// Saute espaces et commentaires à partir de `i` ; retourne la position suivante.
size_t SkipBlank(StringView t, size_t i) {
	while (i < t.size()) {
		const char c = t[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			++i;
		} else if (c == '#' || (c == '/' && i + 1 < t.size() && t[i + 1] == '/')) {
			while (i < t.size() && t[i] != '\n')
				++i;
		} else if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
			const size_t e = t.find("*/", i + 2);
			i = e == StringView::npos ? t.size() : e + 2;
		} else {
			break;
		}
	}
	return i;
}

/// Fin (exclue) de la valeur qui commence en `i` (chaîne, conteneur ou littéral).
size_t ValueEnd(StringView t, size_t i) {
	if (i >= t.size())
		return i;
	if (t[i] == '"') {
		for (++i; i < t.size(); ++i) {
			if (t[i] == '\\')
				++i;
			else if (t[i] == '"')
				return i + 1;
		}
		return t.size();
	}
	if (t[i] == '[' || t[i] == '{') {
		int depth = 0;
		while (i < t.size()) {
			const size_t j = SkipBlank(t, i);
			if (j != i) {
				i = j;
				continue;
			}
			const char c = t[i];
			if (c == '"') {
				i = ValueEnd(t, i);
				continue;
			}
			if (c == '[' || c == '{')
				++depth;
			else if (c == ']' || c == '}') {
				if (--depth == 0)
					return i + 1;
			}
			++i;
		}
		return t.size();
	}
	while (i < t.size() && t[i] != ',' && t[i] != ']' && t[i] != '}' && t[i] != ' ' && t[i] != '\n' && t[i] != '\t' && t[i] != '\r')
		++i;
	return i;
}

/// Texte JSON de `value` indenté comme un élément commençant après `lineIndent`.
String IndentedJson(const NodePtr &value, StringView lineIndent, bool tabs, int width) {
	const String raw = WriteJson(value, tabs ? 1 : std::max(1, width));
	std::string out;
	out.reserve(raw.size() + raw.size() / 4);
	bool lineStart = false;
	for (char c : raw) {
		if (lineStart && c == ' ' && tabs) {
			out.push_back('\t');
			continue;
		}
		if (lineStart && c != ' ')
			lineStart = false;
		out.push_back(c);
		if (c == '\n') {
			out += std::string(lineIndent);
			lineStart = true;
		}
	}
	return String(std::move(out));
}

/// Indentation de la ligne où commence `pos` et style (tabulations ou largeur d'espaces).
StringView LineIndent(StringView t, size_t pos, bool &tabs, int &width) {
	size_t b = pos;
	while (b > 0 && t[b - 1] != '\n')
		--b;
	size_t e = b;
	while (e < pos && (t[e] == ' ' || t[e] == '\t'))
		++e;
	const StringView indent = t.Substr(b, e - b);
	tabs = indent.find('\t') != StringView::npos;
	width = indent.empty() ? 2 : int(indent.size());
	return indent;
}

} // namespace

// ── Parse / écriture (délégués au format commun) ─────────────────────────────

Result<NodePtr, ParseError> ParseJson(StringView text) {
	JsonDocument doc;
	if (auto err = doc.DecodeStr(String(text)); err.IsSome())
		return Err(err.Value());
	return Ok(doc.GetRoot());
}

Result<NodePtr, ParseError> LoadJsonFile(const std::string &path) {
	auto content = String::ReadFile(path.c_str());
	if (!content)
		return Err(ParseError(String::Format("impossible de lire {}", path), 0, 0));
	auto r = ParseJson(*content);
	if (!r)
		return Err(ParseError(String::Format("{}: {}", path, r.Error().Format()), r.Error().line, r.Error().column));
	return r;
}

String WriteJson(const NodePtr &value, int indent) {
	return WriteJsonNode(value, indent);
}

bool SaveJsonFile(const std::string &path, const NodePtr &value, int indent) {
	return WriteJson(value, indent).WriteFile(path.c_str());
}

std::vector<std::pair<size_t, size_t>> JsonArrayElementSpans(StringView text) {
	std::vector<std::pair<size_t, size_t>> spans;
	size_t i = SkipBlank(text, 0);
	if (i >= text.size() || text[i] != '[')
		return spans;
	++i;
	while (true) {
		i = SkipBlank(text, i);
		if (i >= text.size() || text[i] == ']')
			break;
		if (text[i] == ',') {
			++i;
			continue;
		}
		const size_t end = ValueEnd(text, i);
		if (end <= i)
			break;
		spans.emplace_back(i, end);
		i = end;
	}
	return spans;
}

std::optional<String> ReplaceJsonArrayElement(StringView text, size_t index, const NodePtr &value) {
	const auto spans = JsonArrayElementSpans(text);
	if (index >= spans.size())
		return std::nullopt;
	const auto [b, e] = spans[index];
	bool tabs = false;
	int width = 2;
	const StringView indent = LineIndent(text, b, tabs, width);
	String out = String(text.Substr(0, b));
	out += IndentedJson(value, indent, tabs, width);
	out += String(text.Substr(e));
	return out;
}

std::optional<String> AppendJsonArrayElement(StringView text, const NodePtr &value) {
	const auto spans = JsonArrayElementSpans(text);
	size_t close = StringView::npos;
	if (!spans.empty()) {
		close = SkipBlank(text, spans.back().second);
	} else {
		const size_t open = SkipBlank(text, 0);
		if (open >= text.size() || text[open] != '[')
			return std::nullopt;
		close = SkipBlank(text, open + 1);
	}
	if (close >= text.size() || text[close] != ']')
		return std::nullopt;
	bool tabs = true;
	int width = 1;
	StringView indent = StringView("\t");
	if (!spans.empty())
		indent = LineIndent(text, spans.back().first, tabs, width);
	String out = String(text.Substr(0, spans.empty() ? close : spans.back().second));
	out += spans.empty() ? "\n" : ",\n";
	out += String(indent);
	out += IndentedJson(value, indent, tabs, width);
	out += "\n";
	out += String(text.Substr(close));
	return out;
}

} // namespace dataio
