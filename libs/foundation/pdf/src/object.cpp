#include "pdf/object.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace pdf {

namespace {

constexpr int kMaxDepth = 256;

bool IsWhite(uint8_t c) noexcept { return c == 0 || c == '\t' || c == '\n' || c == '\f' || c == '\r' || c == ' '; }
bool IsDelimiter(uint8_t c) noexcept {
	return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' || c == '%';
}
bool IsRegular(uint8_t c) noexcept { return !IsWhite(c) && !IsDelimiter(c); }
int HexValue(uint8_t c) noexcept {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

} // namespace

// ── Object ──

const Object *Object::Get(std::string_view key) const noexcept {
	if (!IsDictionary())
		return nullptr;
	for (size_t i = 0; i < keys.size(); ++i)
		if (keys[i] == key)
			return &items[i];
	return nullptr;
}

void Object::Set(std::string key, Object value) {
	for (size_t i = 0; i < keys.size(); ++i)
		if (keys[i] == key) {
			items[i] = std::move(value);
			return;
		}
	keys.push_back(std::move(key));
	items.push_back(std::move(value));
}

Object Object::MakeInteger(int64_t v) {
	Object o;
	o.kind = ObjectKind::Integer;
	o.integer = v;
	return o;
}

Object Object::MakeReal(double v) {
	Object o;
	o.kind = ObjectKind::Real;
	o.real = v;
	return o;
}

Object Object::MakeName(std::string name) {
	Object o;
	o.kind = ObjectKind::Name;
	o.text = std::move(name);
	return o;
}

Object Object::MakeString(std::string bytes) {
	Object o;
	o.kind = ObjectKind::String;
	o.text = std::move(bytes);
	return o;
}

Object Object::MakeReference(Reference r) {
	Object o;
	o.kind = ObjectKind::Reference;
	o.ref = r;
	return o;
}

// ── Parser ──

void Parser::SkipWhitespace() noexcept {
	while (m_pos < m_data.size()) {
		const uint8_t c = m_data[m_pos];
		if (IsWhite(c)) {
			++m_pos;
		} else if (c == '%') { // commentaire jusqu'à la fin de ligne
			while (m_pos < m_data.size() && m_data[m_pos] != '\n' && m_data[m_pos] != '\r')
				++m_pos;
		} else {
			break;
		}
	}
}

bool Parser::AtEnd() noexcept {
	SkipWhitespace();
	return m_pos >= m_data.size();
}

std::string_view Parser::Word() noexcept {
	const size_t start = m_pos;
	while (m_pos < m_data.size() && IsRegular(m_data[m_pos]))
		++m_pos;
	return {reinterpret_cast<const char *>(m_data.data()) + start, m_pos - start};
}

bool Parser::Expect(std::string_view keyword) {
	SkipWhitespace();
	const size_t saved = m_pos;
	if (Word() == keyword)
		return true;
	m_pos = saved;
	return false;
}

Result<Object, std::string> Parser::ParseObject() { return ParseValue(0); }

Result<Object, std::string> Parser::ParseValue(int depth) {
	if (depth > kMaxDepth)
		return Err(std::string("imbrication trop profonde"));
	SkipWhitespace();
	if (m_pos >= m_data.size())
		return Err(std::string("fin de données inattendue"));
	const uint8_t c = m_data[m_pos];
	if (c == '/')
		return ParseName();
	if (c == '(')
		return ParseLiteralString();
	if (c == '<' && m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '<') {
		m_pos += 2;
		Object dict;
		dict.kind = ObjectKind::Dictionary;
		for (;;) {
			SkipWhitespace();
			if (m_pos + 1 < m_data.size() && m_data[m_pos] == '>' && m_data[m_pos + 1] == '>') {
				m_pos += 2;
				break;
			}
			if (m_pos >= m_data.size())
				return Err(std::string("dictionnaire non terminé"));
			auto key = ParseValue(depth + 1);
			if (!key)
				return key;
			if (key.Value().kind != ObjectKind::Name)
				return Err(std::format("clé de dictionnaire invalide à {}", m_pos));
			auto value = ParseValue(depth + 1);
			if (!value)
				return value;
			dict.Set(std::move(key.Value().text), std::move(value.Value()));
		}
		const size_t saved = m_pos;
		if (Expect("stream"))
			return ParseStreamBody(std::move(dict));
		m_pos = saved;
		return Ok(std::move(dict));
	}
	if (c == '<')
		return ParseHexString();
	if (c == '[') {
		++m_pos;
		Object array;
		array.kind = ObjectKind::Array;
		for (;;) {
			SkipWhitespace();
			if (m_pos >= m_data.size())
				return Err(std::string("tableau non terminé"));
			if (m_data[m_pos] == ']') {
				++m_pos;
				break;
			}
			auto item = ParseValue(depth + 1);
			if (!item)
				return item;
			array.items.push_back(std::move(item.Value()));
		}
		return Ok(std::move(array));
	}
	if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
		auto number = ParseNumber();
		if (!number || number.Value().kind != ObjectKind::Integer || number.Value().integer < 0)
			return number;
		// « n g R » : référence indirecte.
		const size_t saved = m_pos;
		SkipWhitespace();
		if (m_pos < m_data.size() && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
			auto gen = ParseNumber();
			if (gen && gen.Value().kind == ObjectKind::Integer && Expect("R"))
				return Ok(Object::MakeReference({uint32_t(number.Value().integer), uint16_t(gen.Value().integer)}));
		}
		m_pos = saved;
		return number;
	}
	const size_t start = m_pos;
	const std::string_view word = Word();
	if (word == "true" || word == "false") {
		Object o;
		o.kind = ObjectKind::Bool;
		o.boolean = word == "true";
		return Ok(std::move(o));
	}
	if (word == "null")
		return Ok(Object{});
	if (word.empty())
		++m_pos; // délimiteur isolé (« ) », « > »…) : on avance pour ne pas boucler
	return Err(std::format("lexème inattendu « {} » à {}", word.empty() ? std::string(1, char(c)) : std::string(word), start));
}

Result<Object, std::string> Parser::ParseName() {
	++m_pos; // « / »
	std::string name;
	while (m_pos < m_data.size() && IsRegular(m_data[m_pos])) {
		const uint8_t c = m_data[m_pos++];
		if (c == '#' && m_pos + 1 < m_data.size()) {
			const int hi = HexValue(m_data[m_pos]), lo = HexValue(m_data[m_pos + 1]);
			if (hi >= 0 && lo >= 0) {
				name.push_back(char(hi * 16 + lo));
				m_pos += 2;
				continue;
			}
		}
		name.push_back(char(c));
	}
	return Ok(Object::MakeName(std::move(name)));
}

Result<Object, std::string> Parser::ParseLiteralString() {
	++m_pos; // « ( »
	std::string out;
	int nesting = 1;
	while (m_pos < m_data.size()) {
		const uint8_t c = m_data[m_pos++];
		if (c == '(') {
			++nesting;
		} else if (c == ')') {
			if (--nesting == 0)
				return Ok(Object::MakeString(std::move(out)));
		} else if (c == '\\' && m_pos < m_data.size()) {
			const uint8_t e = m_data[m_pos++];
			switch (e) {
			case 'n': out.push_back('\n'); continue;
			case 'r': out.push_back('\r'); continue;
			case 't': out.push_back('\t'); continue;
			case 'b': out.push_back('\b'); continue;
			case 'f': out.push_back('\f'); continue;
			case '\r': // continuation de ligne
				if (m_pos < m_data.size() && m_data[m_pos] == '\n')
					++m_pos;
				continue;
			case '\n': continue;
			default: break;
			}
			if (e >= '0' && e <= '7') {
				int v = e - '0';
				for (int k = 0; k < 2 && m_pos < m_data.size() && m_data[m_pos] >= '0' && m_data[m_pos] <= '7'; ++k)
					v = v * 8 + (m_data[m_pos++] - '0');
				out.push_back(char(v & 0xFF));
			} else {
				out.push_back(char(e)); // \( \) \\ et échappements inconnus : le caractère lui-même
			}
			continue;
		}
		out.push_back(char(c));
	}
	return Err(std::string("chaîne littérale non terminée"));
}

Result<Object, std::string> Parser::ParseHexString() {
	++m_pos; // « < »
	std::string out;
	int pending = -1;
	while (m_pos < m_data.size()) {
		const uint8_t c = m_data[m_pos++];
		if (c == '>') {
			if (pending >= 0)
				out.push_back(char(pending * 16)); // chiffre final implicite 0
			return Ok(Object::MakeString(std::move(out)));
		}
		if (IsWhite(c))
			continue;
		const int v = HexValue(c);
		if (v < 0)
			return Err(std::format("chiffre hexadécimal invalide à {}", m_pos - 1));
		if (pending < 0) {
			pending = v;
		} else {
			out.push_back(char(pending * 16 + v));
			pending = -1;
		}
	}
	return Err(std::string("chaîne hexadécimale non terminée"));
}

Result<Object, std::string> Parser::ParseNumber() {
	const size_t start = m_pos;
	if (m_pos < m_data.size() && (m_data[m_pos] == '+' || m_data[m_pos] == '-'))
		++m_pos;
	bool real = false;
	while (m_pos < m_data.size() && ((m_data[m_pos] >= '0' && m_data[m_pos] <= '9') || m_data[m_pos] == '.')) {
		real = real || m_data[m_pos] == '.';
		++m_pos;
	}
	std::string_view text(reinterpret_cast<const char *>(m_data.data()) + start, m_pos - start);
	if (!text.empty() && text.front() == '+')
		text.remove_prefix(1);
	if (text.empty() || text == "-" || text == "." || text == "-.")
		return Ok(Object::MakeInteger(0)); // tolérance des lecteurs courants
	if (!real) {
		int64_t v = 0;
		const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
		if (ec == std::errc())
			return Ok(Object::MakeInteger(v));
	}
	double v = 0;
	std::string normalized(text);
	if (normalized.front() == '.' || (normalized.size() > 1 && normalized[0] == '-' && normalized[1] == '.'))
		normalized.insert(normalized.front() == '-' ? 1 : 0, "0");
	const auto [ptr, ec] = std::from_chars(normalized.data(), normalized.data() + normalized.size(), v);
	if (ec != std::errc() || !std::isfinite(v))
		return Err(std::format("nombre invalide « {} »", text));
	return Ok(Object::MakeReal(v));
}

Result<Object, std::string> Parser::ParseStreamBody(Object dict) {
	// Après « stream » : CRLF ou LF (un CR seul est toléré).
	if (m_pos < m_data.size() && m_data[m_pos] == '\r')
		++m_pos;
	if (m_pos < m_data.size() && m_data[m_pos] == '\n')
		++m_pos;
	const size_t start = m_pos;
	std::optional<int64_t> length;
	if (const Object *len = dict.Get("Length")) {
		if (len->kind == ObjectKind::Integer)
			length = len->integer;
		else if (len->kind == ObjectKind::Reference && m_resolveLength)
			length = m_resolveLength(len->ref);
	}
	const auto endsWithKeyword = [&](size_t end) {
		Parser probe(m_data, end);
		return probe.Expect("endstream");
	};
	size_t end = 0;
	if (length && *length >= 0 && start + size_t(*length) <= m_data.size() && endsWithKeyword(start + size_t(*length))) {
		end = start + size_t(*length);
	} else {
		// /Length absent ou faux : recherche de « endstream ».
		constexpr std::string_view kEnd = "endstream";
		const auto it = std::search(m_data.begin() + std::ptrdiff_t(start), m_data.end(), kEnd.begin(), kEnd.end());
		if (it == m_data.end())
			return Err(std::string("flux sans « endstream »"));
		end = size_t(it - m_data.begin());
		if (end > start && m_data[end - 1] == '\n')
			--end;
		if (end > start && m_data[end - 1] == '\r')
			--end;
	}
	dict.kind = ObjectKind::Stream;
	dict.data.assign(m_data.begin() + std::ptrdiff_t(start), m_data.begin() + std::ptrdiff_t(end));
	m_pos = end;
	if (!Expect("endstream"))
		return Err(std::string("« endstream » attendu"));
	return Ok(std::move(dict));
}

Result<std::pair<Reference, Object>, std::string> Parser::ParseIndirect() {
	SkipWhitespace();
	auto number = ParseNumber();
	if (!number || number.Value().kind != ObjectKind::Integer)
		return Err(std::string("numéro d'objet attendu"));
	SkipWhitespace();
	auto gen = ParseNumber();
	if (!gen || gen.Value().kind != ObjectKind::Integer)
		return Err(std::string("génération attendue"));
	if (!Expect("obj"))
		return Err(std::string("« obj » attendu"));
	auto value = ParseValue(0);
	if (!value)
		return Err(value.Error());
	(void)Expect("endobj"); // absent dans certains fichiers : toléré
	return Ok(std::pair{Reference{uint32_t(number.Value().integer), uint16_t(gen.Value().integer)}, std::move(value.Value())});
}

Result<Parser::Token, std::string> Parser::NextToken() {
	SkipWhitespace();
	if (m_pos >= m_data.size())
		return Err(std::string("fin du flux"));
	const uint8_t c = m_data[m_pos];
	if (IsRegular(c) && !((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) {
		const size_t saved = m_pos;
		const std::string_view word = Word();
		if (word != "true" && word != "false" && word != "null") {
			Token t;
			t.keyword = true;
			t.value.text = std::string(word);
			if (word == "BI") { // image en ligne : sautée jusqu'à « EI »
				constexpr std::string_view kEi = "EI";
				for (;;) {
					const auto it = std::search(m_data.begin() + std::ptrdiff_t(m_pos), m_data.end(), kEi.begin(), kEi.end());
					if (it == m_data.end()) {
						m_pos = m_data.size();
						break;
					}
					const size_t at = size_t(it - m_data.begin());
					m_pos = at + 2;
					if (at > 0 && IsWhite(m_data[at - 1]) && (m_pos >= m_data.size() || IsWhite(m_data[m_pos])))
						break;
				}
			}
			return Ok(std::move(t));
		}
		m_pos = saved;
	}
	if (c == ')' || c == '>' || c == ']' || c == '}' || c == '{') { // délimiteurs isolés des flux de contenu
		++m_pos;
		Token t;
		t.keyword = true;
		t.value.text = std::string(1, char(c));
		return Ok(std::move(t));
	}
	// Opérande : pas de référence « n g R » dans un flux de contenu.
	if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
		auto n = ParseNumber();
		if (!n)
			return Err(n.Error());
		return Ok(Token{std::move(n.Value()), false});
	}
	auto v = ParseValue(0);
	if (!v)
		return Err(v.Error());
	return Ok(Token{std::move(v.Value()), false});
}

} // namespace pdf
