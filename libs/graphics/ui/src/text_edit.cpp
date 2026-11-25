#include "ui/text_edit.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include <sdl3/core/events_manager.hpp>
#include <sdl3/core/misc.hpp>

namespace ui {

namespace {

bool IsWordByte(unsigned char c) noexcept { return c >= 0x80 || std::isalnum(c) || c == '_'; }

} // namespace

size_t TextEdit::PrevChar(std::string_view text, size_t p) noexcept {
	p = std::min(p, text.size());
	while (p > 0 && (static_cast<unsigned char>(text[--p]) & 0xC0) == 0x80) {
	}
	return p;
}

size_t TextEdit::NextChar(std::string_view text, size_t p) noexcept {
	if (p >= text.size())
		return text.size();
	++p;
	while (p < text.size() && (static_cast<unsigned char>(text[p]) & 0xC0) == 0x80)
		++p;
	return p;
}

size_t TextEdit::LineStart(std::string_view text, size_t p) noexcept {
	p = std::min(p, text.size());
	const size_t nl = p == 0 ? std::string_view::npos : text.rfind('\n', p - 1);
	return nl == std::string_view::npos ? 0 : nl + 1;
}

size_t TextEdit::LineEnd(std::string_view text, size_t p) noexcept {
	const size_t nl = text.find('\n', std::min(p, text.size()));
	return nl == std::string_view::npos ? text.size() : nl;
}

size_t TextEdit::CharCount(std::string_view text, size_t p) noexcept {
	size_t n = 0;
	for (size_t i = 0; i < std::min(p, text.size()); ++i)
		n += (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 ? 1u : 0u;
	return n;
}

size_t TextEdit::ByteOfChar(std::string_view text, size_t n) noexcept {
	size_t p = 0;
	for (size_t i = 0; i < n && p < text.size(); ++i)
		p = NextChar(text, p);
	return p;
}

std::string_view TextEdit::Selected(std::string_view text) const noexcept {
	const auto [a, b] = Range();
	return a < b && b <= text.size() ? text.substr(a, b - a) : std::string_view{};
}

void TextEdit::Clamp(std::string_view text) noexcept {
	cursor = std::min(cursor, text.size());
	anchor = std::min(anchor, text.size());
	while (cursor > 0 && cursor < text.size() && (static_cast<unsigned char>(text[cursor]) & 0xC0) == 0x80)
		--cursor;
	while (anchor > 0 && anchor < text.size() && (static_cast<unsigned char>(text[anchor]) & 0xC0) == 0x80)
		--anchor;
}

void TextEdit::SelectAll(std::string_view text) noexcept {
	anchor = 0;
	cursor = text.size();
	m_goalX = -1.f;
}

void TextEdit::SelectWord(std::string_view text, size_t at) noexcept {
	at = std::min(at, text.size());
	if (text.empty())
		return;
	const size_t probe = at < text.size() ? at : PrevChar(text, at);
	const bool word = IsWordByte(static_cast<unsigned char>(text[probe]));
	const auto same = [&](size_t i) { return text[i] != '\n' && IsWordByte(static_cast<unsigned char>(text[i])) == word; };
	size_t a = probe, b = probe;
	while (a > 0 && same(PrevChar(text, a)))
		a = PrevChar(text, a);
	while (b < text.size() && same(b))
		b = NextChar(text, b);
	anchor = a;
	cursor = std::max(b, NextChar(text, probe));
	m_goalX = -1.f;
}

void TextEdit::SetCursor(size_t at, bool extend) noexcept {
	cursor = at;
	if (!extend)
		anchor = at;
	m_goalX = -1.f;
}

bool TextEdit::DeleteSelection(std::string &text) {
	if (!HasSelection() || readOnly)
		return false;
	const auto [a, b] = Range();
	text.erase(a, b - a);
	cursor = anchor = a;
	return true;
}

bool TextEdit::Insert(std::string &text, std::string_view s) {
	if (readOnly)
		return false;
	std::string clean;
	clean.reserve(s.size());
	for (char c : s) {
		const auto u = static_cast<unsigned char>(c);
		if (c == '\r')
			continue;
		if (c == '\n' && !multiline) {
			clean += ' '; // collage de plusieurs lignes dans un champ d'une ligne
			continue;
		}
		if (u < 0x20 && c != '\n' && c != '\t')
			continue;
		clean += c;
	}
	if (clean.empty())
		return DeleteSelection(text);
	Clamp(text);
	DeleteSelection(text);
	text.insert(cursor, clean);
	cursor += clean.size();
	anchor = cursor;
	m_goalX = -1.f;
	return true;
}

bool TextEdit::Backspace(std::string &text, bool word) {
	if (readOnly)
		return false;
	if (DeleteSelection(text))
		return true;
	if (cursor == 0)
		return false;
	const size_t end = cursor;
	if (word)
		Move(text, Motion::WordLeft, false);
	else
		cursor = PrevChar(text, cursor);
	text.erase(cursor, end - cursor);
	anchor = cursor;
	return true;
}

std::vector<size_t> TextEdit::SelectedLineStarts(std::string_view text) const {
	auto [a, b] = Range();
	if (b > a && b > 0 && text[b - 1] == '\n')
		--b; // sélection finissant en début de ligne : cette ligne n'est pas touchée
	std::vector<size_t> starts;
	for (size_t s = LineStart(text, a);;) {
		starts.push_back(s);
		const size_t end = LineEnd(text, s);
		if (end >= b || end >= text.size())
			break;
		s = end + 1;
	}
	return starts;
}

bool TextEdit::ToggleComment(std::string &text, std::string_view prefix) {
	if (readOnly || prefix.empty())
		return false;
	Clamp(text);
	const std::vector<size_t> starts = SelectedLineStarts(text);
	const auto indentEnd = [&](size_t s) {
		while (s < text.size() && (text[s] == ' ' || text[s] == '\t'))
			++s;
		return s;
	};
	// Décommenter si toutes les lignes non vides commencent par le préfixe ; sinon commenter
	// à l'indentation la plus faible (les lignes vides restent intactes).
	bool uncomment = true, any = false;
	size_t column = std::string::npos;
	for (const size_t s : starts) {
		const size_t e = indentEnd(s);
		if (e >= text.size() || text[e] == '\n')
			continue;
		any = true;
		uncomment &= std::string_view(text).substr(e).starts_with(prefix);
		column = std::min(column, e - s);
	}
	if (!any)
		return false;
	const bool single = starts.size() == 1 && !HasSelection();
	const size_t caret = cursor, first = starts.front();
	long offset = 0, caretShift = 0;
	size_t lastStart = first;
	for (const size_t start : starts) {
		const size_t s = size_t(long(start) + offset);
		lastStart = s;
		const size_t e = indentEnd(s);
		if (e >= text.size() || text[e] == '\n')
			continue;
		if (uncomment) {
			size_t n = prefix.size();
			if (e + n < text.size() && text[e + n] == ' ')
				++n;
			text.erase(e, n);
			offset -= long(n);
			caretShift = -long(n);
		} else {
			const std::string mark = std::string(prefix) + ' ';
			text.insert(s + column, mark);
			offset += long(mark.size());
			caretShift = long(mark.size());
		}
	}
	if (single)
		cursor = anchor = size_t(std::max(long(first), long(caret) + caretShift));
	else {
		anchor = first;
		cursor = LineEnd(text, lastStart);
	}
	m_goalX = -1.f;
	return true;
}

bool TextEdit::IndentLines(std::string &text, bool outdent) {
	if (readOnly)
		return false;
	Clamp(text);
	const std::vector<size_t> starts = SelectedLineStarts(text);
	const size_t first = starts.front();
	const bool single = starts.size() == 1 && !HasSelection();
	const size_t caret = cursor;
	bool changed = false;
	long offset = 0; // décalage cumulé des lignes déjà traitées
	size_t lastStart = first, firstDelta = 0;
	for (size_t i = 0; i < starts.size(); ++i) {
		const size_t s = size_t(long(starts[i]) + offset);
		lastStart = s;
		size_t n = 0;
		if (!outdent) {
			text.insert(s, indentUnit);
			n = indentUnit.size();
			offset += long(n);
		} else {
			if (s < text.size() && text[s] == '\t')
				n = 1;
			else
				while (n < 4 && s + n < text.size() && text[s + n] == ' ')
					++n;
			text.erase(s, n);
			offset -= long(n);
		}
		changed |= n > 0;
		if (i == 0)
			firstDelta = n;
	}
	if (single) { // une seule ligne sans sélection : le curseur suit le texte
		cursor = anchor = outdent ? std::max(first, caret >= firstDelta ? caret - firstDelta : first) : caret + firstDelta;
	} else {
		anchor = first;
		cursor = LineEnd(text, lastStart);
	}
	m_goalX = -1.f;
	return changed;
}

bool TextEdit::NewLine(std::string &text) {
	Clamp(text);
	const size_t start = LineStart(text, Range().first);
	size_t ws = start;
	while (ws < text.size() && (text[ws] == ' ' || text[ws] == '\t') && ws < Range().first)
		++ws;
	std::string indent = "\n" + text.substr(start, ws - start);
	size_t before = Range().first;
	while (before > start && (text[before - 1] == ' ' || text[before - 1] == '\t'))
		--before;
	if (before > start && (text[before - 1] == '{' || text[before - 1] == '(' || text[before - 1] == '['))
		indent += indentUnit; // bloc ouvert : un cran de plus
	return Insert(text, indent);
}

bool TextEdit::Delete(std::string &text, bool word) {
	if (readOnly)
		return false;
	if (DeleteSelection(text))
		return true;
	if (cursor >= text.size())
		return false;
	const size_t start = cursor;
	size_t end = NextChar(text, cursor);
	if (word) {
		Move(text, Motion::WordRight, false);
		end = std::max(end, cursor);
	}
	text.erase(start, end - start);
	cursor = anchor = start;
	return true;
}

void TextEdit::Move(std::string_view text, Motion motion, bool extend, const MeasureFn &measure, int pageLines) {
	Clamp(text);
	const bool vertical = motion == Motion::Up || motion == Motion::Down || motion == Motion::PageUp || motion == Motion::PageDown;
	if (!vertical)
		m_goalX = -1.f;
	// Sélection sans Maj : ← et → la referment sur son bord.
	if (HasSelection() && !extend && (motion == Motion::Left || motion == Motion::Right)) {
		const auto [a, b] = Range();
		cursor = anchor = motion == Motion::Left ? a : b;
		return;
	}
	size_t p = cursor;
	switch (motion) {
	case Motion::Left: p = PrevChar(text, p); break;
	case Motion::Right: p = NextChar(text, p); break;
	case Motion::WordLeft:
		while (p > 0 && !IsWordByte(static_cast<unsigned char>(text[PrevChar(text, p)])))
			p = PrevChar(text, p);
		while (p > 0 && IsWordByte(static_cast<unsigned char>(text[PrevChar(text, p)])))
			p = PrevChar(text, p);
		break;
	case Motion::WordRight:
		while (p < text.size() && IsWordByte(static_cast<unsigned char>(text[p])))
			p = NextChar(text, p);
		while (p < text.size() && !IsWordByte(static_cast<unsigned char>(text[p])) && text[p] != '\n')
			p = NextChar(text, p);
		if (p == cursor && p < text.size())
			p = NextChar(text, p); // saut de ligne
		break;
	case Motion::LineStart: p = multiline ? LineStart(text, p) : 0; break;
	case Motion::LineEnd: p = multiline ? LineEnd(text, p) : text.size(); break;
	case Motion::Start: p = 0; break;
	case Motion::End: p = text.size(); break;
	case Motion::Up:
	case Motion::Down:
	case Motion::PageUp:
	case Motion::PageDown: {
		if (!multiline) {
			p = motion == Motion::Up || motion == Motion::PageUp ? 0 : text.size();
			break;
		}
		const size_t start = LineStart(text, p);
		if (m_goalX < 0.f)
			m_goalX = measure ? measure(text.substr(start, p - start)) : float(CharCount(text.substr(start), p - start));
		int lines = motion == Motion::Up || motion == Motion::Down ? 1 : std::max(1, pageLines);
		const bool up = motion == Motion::Up || motion == Motion::PageUp;
		size_t line = start;
		for (; lines > 0; --lines) {
			if (up) {
				if (line == 0)
					break;
				line = LineStart(text, line - 1);
			} else {
				const size_t end = LineEnd(text, line);
				if (end >= text.size())
					break;
				line = end + 1;
			}
		}
		if (line == start && lines > 0 && ((up && start == 0) || (!up && LineEnd(text, start) >= text.size()))) {
			p = up ? 0 : text.size(); // déjà sur la première / dernière ligne : bout du texte
			break;
		}
		const std::string_view row = text.substr(line, LineEnd(text, line) - line);
		if (measure)
			p = line + HitTest(row, m_goalX, 0.f, 1.f, measure);
		else
			p = line + ByteOfChar(row, size_t(m_goalX));
		break;
	}
	}
	cursor = p;
	if (!extend)
		anchor = p;
}

bool TextEdit::Copy(std::string_view text) const {
	if (!HasSelection() || secret)
		return false;
	return sdl3::clipboard::SetText(String(Selected(text)));
}

bool TextEdit::Cut(std::string &text) {
	if (readOnly || secret || !Copy(text))
		return false;
	return DeleteSelection(text);
}

bool TextEdit::Paste(std::string &text) {
	if (readOnly)
		return false;
	// Lecture directe : HasText peut répondre faux quand l'application possède elle-même le
	// presse-papiers (X11), alors que le texte est disponible.
	const String pasted = sdl3::clipboard::GetText();
	return !pasted.empty() && Insert(text, pasted);
}

TextEditResult TextEdit::HandleKeys(std::string &text, const sdl3::EventsManager &events, float dt, const MeasureFn &measure, int pageLines) {
	TextEditResult r;
	Clamp(text);
	const size_t beforeCursor = cursor, beforeAnchor = anchor;
	const bool ctrl = events.Ctrl(), shift = events.Shift();
	const bool numLock = (events.Mods() & SDL_KMOD_NUM) != 0;

	// Touches de l'image, dédupliquées. La répétition du système est remplacée plus
	// bas par une cadence propre à l'application (elle-même pilotée par `dt`).
	const std::span<const sdl3::Keycode> pressed = events.PressedKeycodes();
	std::vector<SDL_Keycode> keys;
	keys.reserve(pressed.size());
	for (const SDL_Keycode key : pressed)
		if (std::find(keys.begin(), keys.end(), key) == keys.end())
			keys.push_back(key);
	bool anyJustPressed = false;
	for (const SDL_Keycode key : keys)
		anyJustPressed |= events.KeycodeJustPressed(key);

	// Une touche « répétable » (retour arrière, flèches…) est appliquée au premier
	// appui, puis à cadence réglée tant qu'elle reste enfoncée. Une touche
	// « ponctuelle » (Entrée, Échap, raccourcis) n'est appliquée qu'au premier appui.
	const auto fire = [&](SDL_Keycode key) -> bool {
		if (dt <= 0.f)
			return true; // pas de cadence : l'appui (dédupliqué) est appliqué tel quel
		if (events.KeycodeJustPressed(key)) {
			m_repeatKey = int(key);
			m_repeatTimer = repeatDelay;
			return true;
		}
		if (events.Key(key).IsPressed() && m_repeatKey == int(key)) {
			m_repeatTimer -= dt;
			if (m_repeatTimer <= 0.f) {
				m_repeatTimer = repeatInterval;
				return true;
			}
		}
		return false;
	};
	const auto once = [&](SDL_Keycode key) -> bool { return dt <= 0.f || events.KeycodeJustPressed(key); };

	// Saisie de texte : le premier caractère est inséré immédiatement ; la répétition
	// d'une même touche est cadencée comme ci-dessus (le système peut en produire
	// plusieurs en quelques millisecondes, trop vite pour l'utilisateur).
	if (const std::string_view typed = events.TextInput(); !typed.empty() && !(ctrl && !events.Alt())) {
		const bool repeatChar = dt > 0.f && typed.size() == 1 && !anyJustPressed && typed == m_lastTyped;
		bool insertNow = !repeatChar;
		if (repeatChar) {
			m_repeatTimer -= dt;
			if (m_repeatTimer <= 0.f) {
				m_repeatTimer = repeatInterval;
				insertNow = true;
			}
		}
		if (insertNow) {
			// Code : « } » tapé sur une ligne vide d'indentation retire un cran.
			if (code && typed == "}" && !HasSelection()) {
				const size_t start = LineStart(text, cursor);
				const std::string_view before = std::string_view(text).substr(start, cursor - start);
				if (!before.empty() && before.find_first_not_of(" \t") == std::string_view::npos) {
					const size_t n = before.back() == '\t' ? 1 : std::min<size_t>(4, before.size());
					text.erase(cursor - n, n);
					cursor = anchor = cursor - n;
				}
			}
			r.changed |= Insert(text, typed);
			if (dt > 0.f && typed.size() == 1) {
				m_repeatKey = 0; // la saisie prend le pas sur la répétition de touche
				m_repeatTimer = repeatDelay;
			}
		}
		m_lastTyped = std::string(typed);
	} else if (typed.empty() && m_lastTyped.size() == 1) {
		// La touche de saisie a été relâchée : une prochaine frappe du même caractère
		// ne sera pas confondue avec une répétition.
		m_lastTyped.clear();
	}

	for (const SDL_Keycode key : keys) {
		SDL_Keycode k = key;
		if (!numLock) { // pavé numérique sans Verr. num. : touches de déplacement
			switch (k) {
			case SDLK_KP_4: k = SDLK_LEFT; break;
			case SDLK_KP_6: k = SDLK_RIGHT; break;
			case SDLK_KP_8: k = SDLK_UP; break;
			case SDLK_KP_2: k = SDLK_DOWN; break;
			case SDLK_KP_7: k = SDLK_HOME; break;
			case SDLK_KP_1: k = SDLK_END; break;
			case SDLK_KP_9: k = SDLK_PAGEUP; break;
			case SDLK_KP_3: k = SDLK_PAGEDOWN; break;
			case SDLK_KP_0: k = SDLK_INSERT; break;
			case SDLK_KP_PERIOD: k = SDLK_DELETE; break;
			default: break;
			}
		}
		switch (k) {
		case SDLK_LEFT:
			if (fire(key))
				Move(text, ctrl ? Motion::WordLeft : Motion::Left, shift, measure, pageLines);
			break;
		case SDLK_RIGHT:
			if (fire(key))
				Move(text, ctrl ? Motion::WordRight : Motion::Right, shift, measure, pageLines);
			break;
		case SDLK_HOME:
			if (fire(key))
				Move(text, ctrl ? Motion::Start : Motion::LineStart, shift, measure, pageLines);
			break;
		case SDLK_END:
			if (fire(key))
				Move(text, ctrl ? Motion::End : Motion::LineEnd, shift, measure, pageLines);
			break;
		case SDLK_UP:
			if (multiline && fire(key))
				Move(text, Motion::Up, shift, measure, pageLines);
			break;
		case SDLK_DOWN:
			if (multiline && fire(key))
				Move(text, Motion::Down, shift, measure, pageLines);
			break;
		case SDLK_PAGEUP:
			if (multiline && fire(key))
				Move(text, ctrl ? Motion::Start : Motion::PageUp, shift, measure, pageLines);
			break;
		case SDLK_PAGEDOWN:
			if (multiline && fire(key))
				Move(text, ctrl ? Motion::End : Motion::PageDown, shift, measure, pageLines);
			break;
		case SDLK_BACKSPACE:
			if (fire(key))
				r.changed |= Backspace(text, ctrl);
			break;
		case SDLK_DELETE:
			if (fire(key)) {
				if (shift && !ctrl)
					r.changed |= Cut(text);
				else
					r.changed |= Delete(text, ctrl);
			}
			break;
		case SDLK_INSERT:
			if (once(key)) {
				if (ctrl)
					(void)Copy(text);
				else if (shift)
					r.changed |= Paste(text);
			}
			break;
		case SDLK_A:
			if (ctrl && once(key))
				SelectAll(text);
			break;
		case SDLK_C:
			if (ctrl && once(key))
				(void)Copy(text);
			break;
		case SDLK_X:
			if (ctrl && once(key))
				r.changed |= Cut(text);
			break;
		case SDLK_V:
			if (ctrl && once(key))
				r.changed |= Paste(text);
			break;
		case SDLK_RETURN:
		case SDLK_KP_ENTER:
			if (multiline && !readOnly) {
				if (fire(key))
					r.changed |= code ? NewLine(text) : Insert(text, "\n");
			} else if (once(key)) {
				r.submitted = true;
			}
			break;
		case SDLK_TAB:
			if (code && !readOnly && once(key)) {
				const auto [a, b] = Range();
				const bool block = std::string_view(text).substr(a, b - a).find('\n') != std::string_view::npos;
				if (shift || block)
					r.changed |= IndentLines(text, shift);
				else
					r.changed |= Insert(text, indentUnit);
			}
			break;
		case SDLK_ESCAPE:
			if (once(key))
				r.cancelled = true;
			break;
		default: break;
		}
	}
	r.moved = cursor != beforeCursor || anchor != beforeAnchor;
	return r;
}

size_t TextEdit::HitTest(std::string_view text, float x, float y, float lineHeight, const MeasureFn &measure) {
	size_t line = 0;
	if (lineHeight > 0.f && y > 0.f)
		for (int n = int(y / lineHeight); n > 0; --n) {
			const size_t end = LineEnd(text, line);
			if (end >= text.size())
				break;
			line = end + 1;
		}
	const size_t end = LineEnd(text, line);
	const std::string_view row = text.substr(line, end - line);
	if (x <= 0.f || row.empty())
		return line;
	// Limites de caractères ; la largeur croît avec elles : recherche dichotomique de la
	// dernière limite à gauche du point, puis la plus proche des deux voisines.
	std::vector<size_t> bounds{0};
	for (size_t p = 0; p < row.size();)
		bounds.push_back(p = NextChar(row, p));
	size_t lo = 0, hi = bounds.size() - 1;
	while (lo < hi) {
		const size_t mid = (lo + hi + 1) / 2;
		if (measure(row.substr(0, bounds[mid])) <= x)
			lo = mid;
		else
			hi = mid - 1;
	}
	if (lo + 1 < bounds.size()) {
		const float a = measure(row.substr(0, bounds[lo])), b = measure(row.substr(0, bounds[lo + 1]));
		if (x - a > b - x)
			++lo;
	}
	return line + bounds[lo];
}

void TextEdit::MousePress(std::string_view text, size_t at, bool extend, bool doubleClick) noexcept {
	if (doubleClick)
		SelectWord(text, at);
	else
		SetCursor(at, extend);
}

std::pair<float, float> TextEdit::CaretPosition(std::string_view text, float lineHeight, const MeasureFn &measure) const {
	const size_t c = std::min(cursor, text.size());
	const size_t start = LineStart(text, c);
	size_t lineIndex = 0;
	for (size_t i = 0; i < start; ++i)
		lineIndex += text[i] == '\n' ? 1u : 0u;
	return {measure(text.substr(start, c - start)), float(lineIndex) * lineHeight};
}

void TextEdit::Draw(sdl3::Canvas &canvas, std::string_view text, float x, float y, float size, sdl3::CanvasFont font, sdl3::Color textColor, sdl3::Color selectionColor,
					sdl3::Color caretColor, bool showCaret) const {
	const float lh = canvas.LineHeight(size, font);
	const auto measure = [&](std::string_view s) { return canvas.MeasureText(s, size, font); };
	const auto [selA, selB] = Range();
	size_t line = 0;
	float ly = y;
	while (true) {
		const size_t end = LineEnd(text, line);
		const std::string_view row = text.substr(line, end - line);
		if (selA < selB && selA <= end && selB > line) { // part de la sélection sur cette ligne
			const size_t a = std::max(selA, line) - line, b = std::min(selB, end) - line;
			const float xa = measure(row.substr(0, a)), xb = measure(row.substr(0, b));
			const float extra = selB > end && end < text.size() ? size * 0.35f : 0.f; // saut de ligne compris
			canvas.FillRect({x + xa, ly, xb - xa + extra, lh}, selectionColor);
		}
		if (!row.empty())
			canvas.Text(x, ly, row, textColor, size, font);
		if (end >= text.size())
			break;
		line = end + 1;
		ly += lh;
	}
	if (showCaret) {
		const auto [cx, cy] = CaretPosition(text, lh, measure);
		canvas.FillRect({x + cx, y + cy, 1.5f, lh}, caretColor);
	}
}

} // namespace ui
