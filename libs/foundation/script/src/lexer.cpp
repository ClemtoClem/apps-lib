// Définitions du lexer du langage de script.
#include "internal.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace script {

bool Lexer::Run(std::vector<Tok> &out, ScriptError &err) {
	for (;;) {
		Skip();
		Tok t;
		t.line = m_line;
		t.col = Col();
		if (m_i >= m_s.size()) {
			out.push_back(t);
			return true;
		}
		const char c = m_s[m_i];
		if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80) {
			const size_t b = m_i;
			while (m_i < m_s.size() && (std::isalnum(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '_' ||
										static_cast<unsigned char>(m_s[m_i]) >= 0x80))
				++m_i;
			t.kind = T::Ident;
			t.text = std::string(m_s.substr(b, m_i - b));
			if (t.text == "func")
				t.text = "fn"; // alias de `fn`
		} else if (std::isdigit(static_cast<unsigned char>(c))) {
			const size_t b = m_i;
			while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) ||
										(m_s[m_i] == '.' && m_i + 1 < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_i + 1]))) ||
										m_s[m_i] == 'e' || m_s[m_i] == 'E' || m_s[m_i] == 'x' || std::isxdigit(static_cast<unsigned char>(m_s[m_i]))))
				++m_i;
			const std::string_view lit = m_s.substr(b, m_i - b);
			t.kind = T::Number;
			if (lit.starts_with("0x")) {
				uint64_t v = 0;
				std::from_chars(lit.data() + 2, lit.data() + lit.size(), v, 16);
				t.number = static_cast<double>(v);
			} else if (std::from_chars(lit.data(), lit.data() + lit.size(), t.number).ec != std::errc{}) {
				err = {std::format("nombre invalide '{}'", lit), "", t.line, t.col};
				return false;
			}
		} else if (c == '"' || c == '\'') {
			const char q = c;
			++m_i;
			std::string s;
			for (;;) {
				if (m_i >= m_s.size()) {
					err = {"chaîne non terminée", "", t.line, t.col};
					return false;
				}
				const char ch = m_s[m_i++];
				if (ch == q)
					break;
				if (ch == '\n')
					++m_line, m_lineStart = m_i;
				if (ch != '\\' || m_i >= m_s.size()) {
					s.push_back(ch);
					continue;
				}
				const char e = m_s[m_i++];
				switch (e) {
				case 'n': s.push_back('\n'); break;
				case 't': s.push_back('\t'); break;
				case 'r': s.push_back('\r'); break;
				case '0': s.push_back('\0'); break;
				default: s.push_back(e); break;
				}
			}
			t.kind = T::String;
			t.text = std::move(s);
		} else {
			static constexpr std::string_view multi[] = {"==", "!=", "<=", ">=", "+=", "-=", "*=", "/=", "%=", "..", "&&", "||"};
			t.kind = T::Sym;
			for (auto m : multi)
				if (m_s.substr(m_i, 2) == m) {
					t.text = std::string(m);
					break;
				}
			if (t.text.empty())
				t.text = std::string(1, c);
			m_i += t.text.size();
			if (t.text == "&&")
				t = Tok{T::Ident, "and", 0, t.line, t.col};
			else if (t.text == "||")
				t = Tok{T::Ident, "or", 0, t.line, t.col};
			else if (t.text == "!")
				t = Tok{T::Ident, "not", 0, t.line, t.col};
		}
		out.push_back(std::move(t));
	}
}

void Lexer::Skip() {
	while (m_i < m_s.size()) {
		const char c = m_s[m_i];
		if (c == '\n') {
			++m_i;
			++m_line;
			m_lineStart = m_i;
		} else if (std::isspace(static_cast<unsigned char>(c)) || c == ';') {
			++m_i;
		} else if (c == '#' || m_s.substr(m_i, 2) == "//") {
			while (m_i < m_s.size() && m_s[m_i] != '\n')
				++m_i;
		} else if (m_s.substr(m_i, 2) == "/*") {
			m_i += 2;
			while (m_i < m_s.size() && m_s.substr(m_i, 2) != "*/") {
				if (m_s[m_i] == '\n')
					++m_line, m_lineStart = m_i + 1;
				++m_i;
			}
			m_i = std::min(m_s.size(), m_i + 2);
		} else {
			break;
		}
	}
}

} // namespace script
