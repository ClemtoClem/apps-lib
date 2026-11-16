// Définitions de data/json.hpp — valeur JSON (vue par valeur, `std::variant`) :
// le parsage et l'écriture du texte JSON sont délégués au format de document
// commun (`JsonDocument` sur l'arbre `dataio::Node`) ; il n'y a qu'un seul
// parseur et un seul écrivain JSON dans le module. L'édition en place
// (ReplaceJsonArrayElement / AppendJsonArrayElement) reste indépendante.
#include "dataio/json.hpp"

#include <cmath>
#include <format>
#include <fstream>

#include <core/string.hpp>
#include "dataio/documents/json.hpp"

namespace dataio {

namespace {

const Json kNullJson;
const JsonArray kEmptyArray;
const JsonObject kEmptyObject;

// ============================================================================
// Conversion Json (valeur) <-> Node (arbre commun)
//
// `dataio::Json` est une VUE par valeur, commode pour les consommateurs ;
// l'arbre `dataio::Node` est la représentation commune (partagée avec les
// autres formats : XML, YAML, TOML…).
// ============================================================================

/// Convertit la valeur `Json` vers l'arbre commun `Node`.
[[nodiscard]] NodePtr ToNode(const Json &j) {
	switch (j.Type()) {
	case JsonType::Null: return Node::MakeNone();
	case JsonType::Bool: return Node::MakeBool(j.AsBool());
	case JsonType::Number: {
		const double d = j.AsNumber();
		// Entier exact dans la plage : on garde le type entier (aller-retour stable).
		if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9.0e15)
			return Node::MakeInt(static_cast<int64_t>(d));
		return Node::MakeFloat(d);
	}
	case JsonType::String: return Node::MakeString(String(j.AsString()));
	case JsonType::Array: {
		auto arr = Node::MakeArray();
		for (const Json &e : j.AsArray())
			arr->Push(ToNode(e));
		return arr;
	}
	case JsonType::Object: {
		auto obj = Node::MakeObject();
		for (const auto &[k, v] : j.AsObject())
			obj->Set(String(k), ToNode(v));
		return obj;
	}
	}
	return Node::MakeNone();
}

/// Convertit un nœud de l'arbre commun vers la valeur `Json`.
[[nodiscard]] Json ToJson(const NodePtr &n) {
	if (!n)
		return Json();
	switch (n->type) {
	case NodeType::NONE: return Json();
	case NodeType::BOOL: return Json(n->boolValue);
	case NodeType::INT: return Json(static_cast<double>(n->intValue));
	case NodeType::FLOAT: return Json(n->floatValue);
	case NodeType::STRING: return Json(std::string(n->stringValue));
	case NodeType::ARRAY: {
		JsonArray a;
		for (size_t i = 0; i < n->GetSize(); ++i)
			a.push_back(ToJson(n->At(i)));
		return Json(std::move(a));
	}
	case NodeType::OBJECT: {
		JsonObject o;
		for (const String &k : n->Keys())
			o.emplace_back(std::string(k), ToJson(n->Get(k)));
		return Json(std::move(o));
	}
	}
	return Json();
}

// ============================================================================
// Édition en place (ReplaceJsonArrayElement / AppendJsonArrayElement)
// ============================================================================

/// Saute espaces et commentaires à partir de `i` ; retourne la position suivante.
size_t SkipBlank(std::string_view t, size_t i) {
	while (i < t.size()) {
		const char c = t[i];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			++i;
		} else if (c == '#' || (c == '/' && i + 1 < t.size() && t[i + 1] == '/')) {
			while (i < t.size() && t[i] != '\n')
				++i;
		} else if (c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
			const size_t e = t.find("*/", i + 2);
			i = e == std::string_view::npos ? t.size() : e + 2;
		} else {
			break;
		}
	}
	return i;
}

/// Fin (exclue) de la valeur qui commence en `i` (chaîne, conteneur ou littéral).
size_t ValueEnd(std::string_view t, size_t i) {
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

/// Texte de `value` indenté comme un élément commençant après `lineIndent`.
std::string IndentedJson(const Json &value, std::string_view lineIndent, bool tabs, int width) {
	const std::string raw = WriteJson(value, tabs ? 1 : std::max(1, width));
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
			out += lineIndent;
			lineStart = true;
		}
	}
	return out;
}

/// Indentation de la ligne où commence `pos` et style (tabulations ou largeur d'espaces).
std::string_view LineIndent(std::string_view t, size_t pos, bool &tabs, int &width) {
	size_t b = pos;
	while (b > 0 && t[b - 1] != '\n')
		--b;
	size_t e = b;
	while (e < pos && (t[e] == ' ' || t[e] == '\t'))
		++e;
	const std::string_view indent = t.substr(b, e - b);
	tabs = indent.find('\t') != std::string_view::npos;
	width = indent.empty() ? 2 : int(indent.size());
	return indent;
}

} // namespace

// ── Json : accès et mutation ─────────────────────────────────────────────────

bool Json::AsBool(bool def) const noexcept {
	if (const bool *b = std::get_if<bool>(&m_value))
		return *b;
	return def;
}
double Json::AsNumber(double def) const noexcept {
	if (const double *d = std::get_if<double>(&m_value))
		return *d;
	return def;
}
int64_t Json::AsInt(int64_t def) const noexcept {
	if (const double *d = std::get_if<double>(&m_value))
		return static_cast<int64_t>(*d);
	return def;
}
std::string_view Json::AsString(std::string_view def) const noexcept {
	if (const std::string *s = std::get_if<std::string>(&m_value))
		return *s;
	return def;
}
const JsonArray &Json::AsArray() const noexcept {
	if (const JsonArray *a = std::get_if<JsonArray>(&m_value))
		return *a;
	return kEmptyArray;
}
const JsonObject &Json::AsObject() const noexcept {
	if (const JsonObject *o = std::get_if<JsonObject>(&m_value))
		return *o;
	return kEmptyObject;
}
JsonArray &Json::MutArray() {
	if (!IsArray())
		m_value = JsonArray{};
	return std::get<JsonArray>(m_value);
}
JsonObject &Json::MutObject() {
	if (!IsObject())
		m_value = JsonObject{};
	return std::get<JsonObject>(m_value);
}
const Json *Json::Find(std::string_view key) const noexcept {
	if (const JsonObject *o = std::get_if<JsonObject>(&m_value))
		for (const auto &[k, v] : *o)
			if (k == key)
				return &v;
	return nullptr;
}
Json *Json::Find(std::string_view key) noexcept { return const_cast<Json *>(std::as_const(*this).Find(key)); }
const Json &Json::operator[](std::string_view key) const noexcept {
	const Json *j = Find(key);
	return j ? *j : kNullJson;
}
const Json &Json::operator[](size_t index) const noexcept {
	const JsonArray &a = AsArray();
	return index < a.size() ? a[index] : kNullJson;
}
const Json &Json::At(std::string_view path) const noexcept {
	const Json *cur = this;
	while (!path.empty() && cur) {
		const size_t dot = path.find('.');
		cur = cur->Find(path.substr(0, dot));
		path = dot == std::string_view::npos ? std::string_view{} : path.substr(dot + 1);
	}
	return cur ? *cur : kNullJson;
}
Json &Json::Set(std::string_view key, Json value) {
	if (Json *existing = Find(key)) {
		*existing = std::move(value);
		return *existing;
	}
	auto &o = MutObject();
	o.emplace_back(std::string(key), std::move(value));
	return o.back().second;
}
Json &Json::Push(Json value) {
	auto &a = MutArray();
	a.push_back(std::move(value));
	return a.back();
}
size_t Json::Size() const noexcept {
	if (IsArray())
		return AsArray().size();
	if (IsObject())
		return AsObject().size();
	return 0;
}
void Json::MergeFrom(const Json &other) {
	if (!IsObject() || !other.IsObject()) {
		*this = other;
		return;
	}
	for (const auto &[k, v] : other.AsObject()) {
		if (Json *mine = Find(k); mine && mine->IsObject() && v.IsObject())
			mine->MergeFrom(v);
		else
			Set(k, v);
	}
}

std::string JsonError::Format() const { return std::format("{}:{}: {}", line, column, message); }

// ── Parse / écriture (délégués au format commun) ─────────────────────────────

Result<Json, JsonError> ParseJson(std::string_view text) {
	JsonDocument doc;
	if (auto err = doc.DecodeStr(String(text)); err.IsSome()) {
		const ParseError pe = err.Value();
		return Err(JsonError{std::string(pe.message), pe.line, pe.column});
	}
	return Ok(ToJson(doc.GetRoot()));
}

Result<Json, JsonError> LoadJsonFile(const std::string &path) {
	auto content = String::ReadFile(path.c_str());
	if (!content)
		return Err(JsonError{"impossible de lire " + path, 0, 0});
	auto r = ParseJson(*content);
	if (!r)
		return Err(JsonError{path + ":" + r.Error().Format(), r.Error().line, r.Error().column});
	return r;
}

std::string WriteJson(const Json &value, int indent) {
	return std::string(WriteJsonNode(ToNode(value), indent));
}

bool SaveJsonFile(const std::string &path, const Json &value, int indent) {
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f)
		return false;
	const std::string s = WriteJson(value, indent);
	f.write(s.data(), static_cast<std::streamsize>(s.size()));
	return static_cast<bool>(f);
}

std::vector<std::pair<size_t, size_t>> JsonArrayElementSpans(std::string_view text) {
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

std::optional<std::string> ReplaceJsonArrayElement(std::string_view text, size_t index, const Json &value) {
	const auto spans = JsonArrayElementSpans(text);
	if (index >= spans.size())
		return std::nullopt;
	const auto [b, e] = spans[index];
	bool tabs = false;
	int width = 2;
	const std::string_view indent = LineIndent(text, b, tabs, width);
	std::string out(text.substr(0, b));
	out += IndentedJson(value, indent, tabs, width);
	out += text.substr(e);
	return out;
}

std::optional<std::string> AppendJsonArrayElement(std::string_view text, const Json &value) {
	const auto spans = JsonArrayElementSpans(text);
	size_t close = std::string_view::npos;
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
	std::string indent = "\t";
	if (!spans.empty())
		indent = std::string(LineIndent(text, spans.back().first, tabs, width));
	std::string out(text.substr(0, spans.empty() ? close : spans.back().second));
	out += spans.empty() ? "\n" : ",\n";
	out += indent;
	out += IndentedJson(value, indent, tabs, width);
	out += "\n";
	out += text.substr(close);
	return out;
}

} // namespace dataio
