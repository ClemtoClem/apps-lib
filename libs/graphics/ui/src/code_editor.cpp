#include "ui/code_editor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace ui {

namespace {

bool IsIdentStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool IsIdentChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

/// Débuts de ligne du texte.
std::vector<size_t> LineStarts(std::string_view text) {
	std::vector<size_t> starts{0};
	for (size_t i = 0; i < text.size(); ++i)
		if (text[i] == '\n')
			starts.push_back(i + 1);
	return starts;
}

/// Crochet apparié à celui en `at` (recherche simple, chaînes ignorées) ; npos sinon.
size_t MatchBracket(std::string_view text, size_t at) {
	static constexpr std::string_view open = "([{", close = ")]}";
	if (at >= text.size())
		return std::string_view::npos;
	const char c = text[at];
	if (const size_t o = open.find(c); o != std::string_view::npos) {
		int depth = 0;
		for (size_t i = at; i < text.size(); ++i) {
			if (text[i] == c)
				++depth;
			else if (text[i] == close[o] && --depth == 0)
				return i;
		}
	} else if (const size_t k = close.find(c); k != std::string_view::npos) {
		int depth = 0;
		for (size_t i = at + 1; i-- > 0;) {
			if (text[i] == c)
				++depth;
			else if (text[i] == open[k] && --depth == 0)
				return i;
		}
	}
	return std::string_view::npos;
}

} // namespace

sdl3::Color CodePalette::Of(TokenKind kind) const noexcept {
	switch (kind) {
	case TokenKind::Keyword: return keyword;
	case TokenKind::Builtin: return builtin;
	case TokenKind::Number: return number;
	case TokenKind::String: return string;
	case TokenKind::Comment: return comment;
	case TokenKind::Function: return function;
	case TokenKind::Operator: return operatorColor;
	default: return text;
	}
}

void Tokenize(std::string_view line, const SyntaxRules &rules, bool &inBlock, std::vector<Token> &out) {
	out.clear();
	size_t i = 0;
	const auto push = [&](size_t start, size_t end, TokenKind kind) {
		if (end > start)
			out.push_back({start, end - start, kind});
	};
	while (i < line.size()) {
		if (inBlock) { // suite d'un commentaire bloc
			const size_t close = rules.blockClose.empty() ? std::string_view::npos : line.find(rules.blockClose, i);
			const size_t end = close == std::string_view::npos ? line.size() : close + rules.blockClose.size();
			push(i, end, TokenKind::Comment);
			inBlock = close == std::string_view::npos;
			i = end;
			continue;
		}
		const auto c = static_cast<unsigned char>(line[i]);
		if (c == ' ' || c == '\t') {
			++i;
			continue;
		}
		if (std::ranges::any_of(rules.lineComments, [&](const std::string &p) { return !p.empty() && line.substr(i).starts_with(p); })) {
			push(i, line.size(), TokenKind::Comment);
			break;
		}
		if (!rules.blockOpen.empty() && line.substr(i).starts_with(rules.blockOpen)) {
			inBlock = true;
			continue;
		}
		if (rules.quotes.find(char(c)) != std::string::npos) { // chaîne jusqu'au guillemet non échappé
			size_t j = i + 1;
			while (j < line.size() && line[j] != char(c))
				j += line[j] == '\\' ? 2u : 1u;
			const size_t end = std::min(line.size(), j + 1);
			push(i, end, TokenKind::String);
			i = end;
			continue;
		}
		if (std::isdigit(c) || (c == '.' && i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
			size_t j = i;
			while (j < line.size() && (IsIdentChar(static_cast<unsigned char>(line[j])) || line[j] == '.'))
				++j;
			push(i, j, TokenKind::Number);
			i = j;
			continue;
		}
		if (IsIdentStart(c)) {
			size_t j = i;
			while (j < line.size() && IsIdentChar(static_cast<unsigned char>(line[j])))
				++j;
			const std::string_view word = line.substr(i, j - i);
			TokenKind kind = TokenKind::Text;
			if (std::ranges::find(rules.keywords, word) != rules.keywords.end())
				kind = TokenKind::Keyword;
			else if (std::ranges::find(rules.builtins, word) != rules.builtins.end())
				kind = TokenKind::Builtin;
			else {
				size_t k = j;
				while (k < line.size() && line[k] == ' ')
					++k;
				if (k < line.size() && line[k] == '(')
					kind = TokenKind::Function; // appel ou définition
			}
			push(i, j, kind);
			i = j;
			continue;
		}
		push(i, TextEdit::NextChar(line, i), TokenKind::Operator);
		i = TextEdit::NextChar(line, i);
	}
}

CodeLayout LayoutCode(sdl3::Canvas &canvas, const sdl3::FRect &box, std::string_view text, const TextEdit &edit, const CodeEditorOptions &options, float size) {
	CodeLayout l;
	l.box = box;
	l.size = size;
	l.lineHeight = canvas.LineHeight(size, sdl3::CanvasFont::Mono);
	size_t lines = 1;
	for (char ch : text)
		lines += ch == '\n' ? 1u : 0u;
	const size_t digits = std::max<size_t>(2, std::to_string(lines).size());
	l.gutter = options.lineNumbers ? canvas.MeasureText(std::string(digits, '0'), size, sdl3::CanvasFont::Mono) + 18.f : 6.f;
	l.originX = box.x + l.gutter + 6.f - edit.scrollX;
	l.originY = box.y + 4.f - edit.scrollY;
	return l;
}

void ScrollCodeToCaret(sdl3::Canvas &canvas, const CodeLayout &l, std::string_view text, TextEdit &edit) {
	const auto measure = [&](std::string_view s) { return canvas.MeasureText(s, l.size, sdl3::CanvasFont::Mono); };
	const auto [cx, cy] = edit.CaretPosition(text, l.lineHeight, measure);
	const float viewW = l.box.w - l.gutter - 18.f, viewH = l.box.h - 8.f;
	edit.scrollX = std::clamp(edit.scrollX, std::max(0.f, cx - viewW), cx);
	edit.scrollY = std::clamp(edit.scrollY, std::max(0.f, cy + l.lineHeight - viewH), cy);
}

size_t CodeHit(sdl3::Canvas &canvas, const CodeLayout &l, std::string_view text, float x, float y) {
	const auto measure = [&](std::string_view s) { return canvas.MeasureText(s, l.size, sdl3::CanvasFont::Mono); };
	return TextEdit::HitTest(text, x - l.originX, y - l.originY, l.lineHeight, measure);
}

void DrawCode(sdl3::Canvas &c, const CodeLayout &l, std::string_view text, const TextEdit &edit, const CodeEditorOptions &options, const CodePalette &p,
			  bool focused, bool caretOn) {
	const float lh = l.lineHeight, size = l.size;
	const auto measure = [&](std::string_view s) { return c.MeasureText(s, size, sdl3::CanvasFont::Mono); };
	c.FillRect(l.box, p.background);
	const std::vector<size_t> starts = LineStarts(text);
	const size_t caretLine = size_t(std::upper_bound(starts.begin(), starts.end(), std::min(edit.cursor, text.size())) - starts.begin()) - 1;
	const size_t first = size_t(std::max(0.f, (edit.scrollY - 4.f) / lh));
	const size_t last = std::min(starts.size(), first + size_t(l.box.h / lh) + 2);
	const sdl3::FRect area = l.TextArea();
	c.PushClip(area);
	// Placeholder d'un éditeur vide.
	if (text.empty() && !focused && !options.placeholder.empty())
		c.Text(l.originX, l.originY, options.placeholder, p.comment, size, sdl3::CanvasFont::Mono);
	// Commentaire bloc ouvert avant la première ligne visible.
	bool inBlock = false;
	std::vector<Token> tokens;
	if (options.syntax)
		for (size_t i = 0; i < first && i < starts.size(); ++i) {
			const size_t end = i + 1 < starts.size() ? starts[i + 1] - 1 : text.size();
			Tokenize(text.substr(starts[i], end - starts[i]), *options.syntax, inBlock, tokens);
		}
	const auto [selA, selB] = edit.Range();
	// Crochets appariés autour du curseur.
	size_t bracketA = std::string_view::npos, bracketB = std::string_view::npos;
	for (const size_t probe : {edit.cursor, edit.cursor > 0 ? edit.cursor - 1 : std::string_view::npos})
		if (probe != std::string_view::npos && bracketB == std::string_view::npos)
			if (const size_t m = MatchBracket(text, probe); m != std::string_view::npos)
				bracketA = probe, bracketB = m;
	const auto charRect = [&](size_t pos, size_t lineStart, float y) {
		const float x0 = measure(text.substr(lineStart, pos - lineStart)), x1 = measure(text.substr(lineStart, TextEdit::NextChar(text, pos) - lineStart));
		return sdl3::FRect{l.originX + x0, y, x1 - x0, lh};
	};
	for (size_t i = first; i < last; ++i) {
		const size_t start = starts[i], end = i + 1 < starts.size() ? starts[i + 1] - 1 : text.size();
		const std::string_view row = text.substr(start, end - start);
		const float y = l.originY + float(i) * lh;
		if (options.errorLine == int(i) + 1)
			c.FillRect({area.x, y, area.w, lh}, sdl3::WithAlpha(p.error, 50));
		else if (focused && i == caretLine && selA == selB)
			c.FillRect({area.x, y, area.w, lh}, p.currentLine);
		if (selA < selB && selA <= end && selB > start) { // sélection sur cette ligne
			const size_t a = std::max(selA, start) - start, b = std::min(selB, end) - start;
			const float xa = measure(row.substr(0, a)), xb = measure(row.substr(0, b));
			c.FillRect({l.originX + xa, y, xb - xa + (selB > end && end < text.size() ? size * 0.5f : 0.f), lh}, p.selection);
		}
		for (const size_t b : {bracketA, bracketB})
			if (b != std::string_view::npos && b >= start && b < end)
				c.FillRect(charRect(b, start, y), p.bracket);
		if (options.syntax) {
			Tokenize(row, *options.syntax, inBlock, tokens);
			for (const Token &t : tokens)
				c.Text(l.originX + measure(row.substr(0, t.start)), y, row.substr(t.start, t.length), p.Of(t.kind), size, sdl3::CanvasFont::Mono);
		} else if (!row.empty()) {
			c.Text(l.originX, y, row, p.text, size, sdl3::CanvasFont::Mono);
		}
	}
	if (caretOn) {
		const auto [cx, cy] = edit.CaretPosition(text, lh, measure);
		c.FillRect({l.originX + cx, l.originY + cy, 2.f, lh}, p.caret);
	}
	c.PopClip();
	// Marge des numéros (par-dessus : le texte défile dessous sans la recouvrir).
	if (options.lineNumbers) {
		const sdl3::FRect gutter{l.box.x, l.box.y, l.gutter, l.box.h};
		c.FillRect(gutter, p.gutter);
		c.PushClip(gutter);
		for (size_t i = first; i < last; ++i) {
			const std::string number = std::to_string(i + 1);
			const float y = l.originY + float(i) * lh;
			const sdl3::Color ink = options.errorLine == int(i) + 1 ? p.error : i == caretLine ? p.gutterCurrent : p.gutterText;
			c.Text(gutter.Right() - 10.f - measure(number), y, number, ink, size, sdl3::CanvasFont::Mono);
		}
		c.PopClip();
	}
}

} // namespace ui
