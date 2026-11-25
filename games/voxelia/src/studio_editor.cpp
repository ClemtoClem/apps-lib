// Voxelia Studio — éditeur de code (phase 4). Cf. studio_editor.hpp.
#include "studio_editor.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

#include <sdl3/sdl3.hpp>

#include <core/string.hpp>
#include <dataio/json.hpp>
#include <script/script.hpp>

namespace voxelia::studio {

namespace icons = ui::icons;
using Rect = sdl3::FRect;
using sdl3::Rgba;

namespace {

constexpr std::string_view kKeywords[] = {"let", "var", "const", "fn", "if", "else", "while", "for", "in", "return", "break", "continue",
										  "and", "or", "not", "nil", "true", "false", "import"};
constexpr std::string_view kModules[] = {"voxel", "world", "entity", "game", "content", "sql", "math", "structure", "formula"};
constexpr size_t kMaxUndo = 200;

size_t Prev(const std::string &l, size_t p) {
	while (p > 0 && (static_cast<unsigned char>(l[--p]) & 0xC0) == 0x80) {
	}
	return p;
}
size_t Next(const std::string &l, size_t p) {
	if (p >= l.size())
		return p;
	++p;
	while (p < l.size() && (static_cast<unsigned char>(l[p]) & 0xC0) == 0x80)
		++p;
	return p;
}
bool WordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) & 0x80); }

struct Pos {
	size_t y, x;
	bool operator<(const Pos &o) const { return y != o.y ? y < o.y : x < o.x; }
	bool operator==(const Pos &o) const = default;
};
std::pair<Pos, Pos> Range(const CodeDocument &d) {
	Pos a{d.ay, d.ax}, b{d.cy, d.cx};
	if (b < a)
		std::swap(a, b);
	return {a, b};
}

std::string Lower(std::string_view s) {
	std::string out(s);
	for (char &c : out)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

/// Mot (identifiants et points) qui précède le curseur : contexte de complétion.
std::string WordBefore(const CodeDocument &d) {
	const std::string &l = d.lines[d.cy];
	size_t b = std::min(d.cx, l.size());
	while (b > 0 && (WordChar(l[b - 1]) || l[b - 1] == '.'))
		--b;
	return l.substr(b, std::min(d.cx, l.size()) - b);
}

} // namespace

size_t ColumnToByte(const std::string &line, size_t col) {
	size_t i = 0, c = 0;
	while (i < line.size() && c < col) {
		(void)unicode::DecodeUtf8(line, i);
		++c;
	}
	return i;
}
size_t ByteToColumn(const std::string &line, size_t byte) { return unicode::CodepointCount(std::string_view(line).substr(0, byte)); }

std::string CodeDocument::Text() const {
	std::string s;
	for (size_t i = 0; i < lines.size(); ++i) {
		if (i)
			s.push_back('\n');
		s += lines[i];
	}
	return s;
}

void CodeDocument::SetText(std::string_view text) {
	lines.clear();
	size_t start = 0;
	while (start <= text.size()) {
		const size_t nl = text.find('\n', start);
		std::string_view l = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
		if (!l.empty() && l.back() == '\r')
			l.remove_suffix(1);
		lines.emplace_back(l);
		if (nl == std::string_view::npos)
			break;
		start = nl + 1;
	}
	if (lines.empty())
		lines.emplace_back();
	cx = cy = ax = ay = 0;
	selecting = false;
	++version;
}

// ── Édition ─────────────────────────────────────────────────────────────────

void CodeEditor::Edit(CodeDocument &d, int kind) {
	const uint64_t now = sdl3::GetTicksMS();
	const bool coalesce = kind != 0 && kind == d.lastEdit && now - d.lastEditTicks < 1200;
	if (!coalesce) {
		d.undo.push_back({d.lines, d.cx, d.cy});
		if (d.undo.size() > kMaxUndo)
			d.undo.erase(d.undo.begin());
	}
	d.redo.clear();
	d.lastEdit = kind;
	d.lastEditTicks = now;
	d.dirty = true;
	++d.version;
}

void CodeEditor::Undo(CodeDocument &d) {
	if (d.undo.empty())
		return;
	d.redo.push_back({d.lines, d.cx, d.cy});
	d.lines = std::move(d.undo.back().lines);
	d.cx = d.undo.back().cx;
	d.cy = std::min(d.undo.back().cy, d.lines.size() - 1);
	d.undo.pop_back();
	d.selecting = false;
	d.lastEdit = 0;
	d.dirty = true;
	++d.version;
}

void CodeEditor::Redo(CodeDocument &d) {
	if (d.redo.empty())
		return;
	d.undo.push_back({d.lines, d.cx, d.cy});
	d.lines = std::move(d.redo.back().lines);
	d.cx = d.redo.back().cx;
	d.cy = std::min(d.redo.back().cy, d.lines.size() - 1);
	d.redo.pop_back();
	d.selecting = false;
	d.lastEdit = 0;
	d.dirty = true;
	++d.version;
}

std::string CodeEditor::SelectedText(const CodeDocument &d) const {
	if (!d.selecting)
		return d.lines[d.cy] + "\n"; // sans sélection : la ligne entière
	const auto [a, b] = Range(d);
	if (a.y == b.y)
		return d.lines[a.y].substr(a.x, b.x - a.x);
	std::string out = d.lines[a.y].substr(a.x);
	for (size_t y = a.y + 1; y < b.y; ++y)
		out += "\n" + d.lines[y];
	out += "\n" + d.lines[b.y].substr(0, b.x);
	return out;
}

void CodeEditor::DeleteSelection(CodeDocument &d) {
	if (!d.selecting)
		return;
	const auto [a, b] = Range(d);
	d.lines[a.y] = d.lines[a.y].substr(0, a.x) + d.lines[b.y].substr(std::min(b.x, d.lines[b.y].size()));
	d.lines.erase(d.lines.begin() + std::ptrdiff_t(a.y) + 1, d.lines.begin() + std::ptrdiff_t(b.y) + 1);
	d.cy = a.y;
	d.cx = a.x;
	d.selecting = false;
}

void CodeEditor::Insert(CodeDocument &d, std::string_view text) {
	DeleteSelection(d);
	std::string &line = d.lines[d.cy];
	d.cx = std::min(d.cx, line.size());
	const std::string tail = line.substr(d.cx);
	line.erase(d.cx);
	size_t start = 0;
	bool first = true;
	while (true) {
		const size_t nl = text.find('\n', start);
		std::string_view part = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
		if (!part.empty() && part.back() == '\r')
			part.remove_suffix(1);
		if (!first) {
			d.lines.insert(d.lines.begin() + std::ptrdiff_t(d.cy) + 1, std::string());
			++d.cy;
			d.cx = 0;
		}
		d.lines[d.cy].append(part);
		d.cx = d.lines[d.cy].size();
		first = false;
		if (nl == std::string_view::npos)
			break;
		start = nl + 1;
	}
	d.lines[d.cy] += tail;
}

void CodeEditor::FindNext(CodeDocument &d, bool backwards) {
	if (m_find.empty())
		return;
	const std::string needle = Lower(m_find);
	const size_t n = d.lines.size();
	const auto [a, b] = d.selecting ? Range(d) : std::pair<Pos, Pos>{Pos{d.cy, d.cx}, Pos{d.cy, d.cx}};
	for (size_t step = 0; step <= n; ++step) {
		const size_t y = backwards ? (a.y + n - step) % n : (b.y + step) % n;
		const std::string hay = Lower(d.lines[y]);
		size_t x;
		if (!backwards) {
			x = hay.find(needle, step == 0 ? b.x : 0);
		} else {
			const size_t limit = step == 0 ? (a.x == 0 ? std::string::npos : a.x - 1) : std::string::npos;
			x = step == 0 && a.x == 0 ? std::string::npos : hay.rfind(needle, limit);
		}
		if (x != std::string::npos) {
			d.ay = y;
			d.ax = x;
			d.cy = y;
			d.cx = x + needle.size();
			d.selecting = true;
			return;
		}
	}
}

void CodeEditor::Goto(CodeDocument &d, int line, int column) {
	d.cy = size_t(std::clamp(line - 1, 0, int(d.lines.size()) - 1));
	d.cx = ColumnToByte(d.lines[d.cy], size_t(std::max(0, column - 1)));
	d.selecting = false;
	m_lastCx = m_lastCy = SIZE_MAX; // défilement vers le curseur
}

void CodeEditor::Check(CodeDocument &d) {
	d.checkedVersion = d.version;
	d.errorLine = -1;
	d.errorColumn = 0;
	d.errorMessage.clear();
	if (d.path.extension() == ".json") {
		auto r = dataio::ParseJson(d.Text());
		if (!r) {
			d.errorLine = r.Error().line - 1;
			d.errorColumn = r.Error().column;
			d.errorMessage = r.Error().Format();
		}
	} else if (d.IsScript()) {
		script::Interpreter vm;
		auto r = vm.Check(d.Text(), d.path.filename().string());
		if (!r) {
			d.errorLine = r.Error().line - 1;
			d.errorColumn = r.Error().column;
			d.errorMessage = r.Error().Format();
		}
	}
}

// ── Affichage et saisie ────────────────────────────────────────────────────

void CodeEditor::Draw(ui::Context &u, const Rect &area, CodeDocument &doc, const EditorEnv &env, bool &focus) {
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	CodeDocument &d = doc;
	if (d.lines.empty())
		d.lines.emplace_back();
	d.cy = std::min(d.cy, d.lines.size() - 1);
	d.cx = std::min(d.cx, d.lines[d.cy].size());
	m_charW = c.MeasureText("M", 14, sdl3::CanvasFont::Mono);
	const float lineH = c.LineHeight(14, sdl3::CanvasFont::Mono) + 2;
	const float gutter = 60;
	u.PushId("codeeditor");

	// ── Barre de recherche / remplacement / aller à la ligne ──
	float barH = 0;
	if (m_findOpen || m_gotoOpen) {
		barH = m_replaceOpen ? 66 : 34;
		const Rect bar{area.x, area.y, area.w, barH};
		c.FillRect(bar, Rgba(22, 25, 32));
		c.FillRect({bar.x, bar.Bottom() - 1, bar.w, 1}, th.panelBorder);
		if (m_gotoOpen) {
			bool submitted = false;
			c.Text(bar.x + 10, bar.y + 9, "Aller à la ligne", th.textDim, 13);
			if (m_focusField) {
				u.ClearFocus();
				m_focusField = false;
			}
			u.TextInputAt({bar.x + 130, bar.y + 4, 120, 26}, "##goto", m_goto, &submitted, "n° de ligne");
			if (submitted) {
				Goto(d, std::atoi(m_goto.c_str()));
				m_gotoOpen = false;
				focus = true;
			}
		} else {
			bool submitted = false;
			u.TextInputAt({bar.x + 8, bar.y + 4, std::min(320.f, bar.w * 0.45f), 26}, "##find", m_find, &submitted, "Rechercher (Entrée : suivant)");
			if (submitted) {
				FindNext(d, u.Shift());
				focus = false;
			}
			// Nombre d'occurrences.
			size_t count = 0;
			if (!m_find.empty()) {
				const std::string needle = Lower(m_find);
				for (const std::string &l : d.lines)
					for (size_t p = Lower(l).find(needle); p != std::string::npos; p = Lower(l).find(needle, p + needle.size()))
						++count;
			}
			float x = bar.x + 16 + std::min(320.f, bar.w * 0.45f);
			c.Text(x, bar.y + 9, std::format("{} résultat(s)", count), th.textDim, 12);
			x += 96;
			if (u.IconButton({x, bar.y + 4, 26, 26}, icons::Codepoint(icons::MaterialIcons::ARROW_DROP_UP), "Précédent (Maj+Entrée)"))
				FindNext(d, true);
			if (u.IconButton({x + 28, bar.y + 4, 26, 26}, icons::Codepoint(icons::MaterialIcons::ARROW_DROP_DOWN), "Suivant (Entrée)"))
				FindNext(d, false);
			if (u.IconButton({x + 56, bar.y + 4, 26, 26}, icons::Codepoint(icons::MaterialIcons::EDIT), "Remplacer (Ctrl+H)", true, m_replaceOpen ? th.accent : sdl3::Color{}))
				m_replaceOpen = !m_replaceOpen;
			if (m_replaceOpen) {
				u.TextInputAt({bar.x + 8, bar.y + 36, std::min(320.f, bar.w * 0.45f), 26}, "##replace", m_replace, nullptr, "Remplacer par");
				const float rx = bar.x + 16 + std::min(320.f, bar.w * 0.45f);
				if (u.ButtonAt({rx, bar.y + 36, 96, 26}, "Remplacer") && d.selecting && Lower(SelectedText(d)) == Lower(m_find)) {
					Edit(d, 0);
					Insert(d, m_replace);
					FindNext(d, false);
				}
				if (u.ButtonAt({rx + 102, bar.y + 36, 70, 26}, "Tout") && !m_find.empty()) {
					Edit(d, 0);
					const std::string needle = Lower(m_find);
					for (std::string &l : d.lines)
						for (size_t p = Lower(l).find(needle); p != std::string::npos; p = Lower(l).find(needle, p + m_replace.size()))
							l.replace(p, needle.size(), m_replace);
					d.selecting = false;
				}
			}
		}
		if (u.IconButton({bar.Right() - 30, bar.y + 4, 26, 26}, icons::Codepoint(icons::MaterialIcons::CLOSE), "Fermer (Échap)")) {
			m_findOpen = m_gotoOpen = false;
			focus = true;
		}
	}

	const ui::ScrollbarOptions bars{};
	const Rect editor{area.x, area.y + barH, area.w, area.h - barH};
	const Rect view{editor.x, editor.y, editor.w - bars.thickness, editor.h - bars.thickness};
	const float textX = view.x + gutter + 6;
	const int visible = std::max(1, int((view.h - 4) / lineH));
	auto posAt = [&](float mx, float my) {
		const size_t line = std::min(d.lines.size() - 1, size_t(std::max(0.f, (my - view.y - 4 + d.scrollY) / lineH)));
		const size_t col = size_t(std::max(0.f, std::round((mx - textX + d.scrollX) / m_charW)));
		return Pos{line, ColumnToByte(d.lines[line], col)};
	};

	// ── Souris : curseur, sélection par glissé, double-clic = mot ──
	const auto it = u.Interact(u.MakeId("##text"), view);
	if (u.MousePressed(sdl3::MouseButton::Left) && !u.MouseIn(area))
		focus = false;
	if (it.held && u.MousePressed(sdl3::MouseButton::Left)) {
		focus = true;
		m_completing = false;
		const Pos p = posAt(u.MouseX(), u.MouseY());
		const uint64_t now = sdl3::GetTicksMS();
		if (now - m_lastClick < 350 && p.y == d.cy) { // double-clic : mot entier
			const std::string &l = d.lines[p.y];
			size_t b = std::min(p.x, l.size()), e = b;
			while (b > 0 && WordChar(l[b - 1]))
				--b;
			while (e < l.size() && WordChar(l[e]))
				++e;
			d.ay = p.y;
			d.ax = b;
			d.cy = p.y;
			d.cx = e;
			d.selecting = e > b;
			m_dragging = false;
		} else {
			if (!u.Shift()) {
				d.ay = p.y;
				d.ax = p.x;
			}
			d.cy = p.y;
			d.cx = p.x;
			d.selecting = u.Shift() && !(Pos{d.ay, d.ax} == Pos{d.cy, d.cx});
			m_dragging = true;
		}
		m_lastClick = now;
		d.lastEdit = 0;
	} else if (m_dragging && u.MouseDown(sdl3::MouseButton::Left)) {
		const Pos p = posAt(u.MouseX(), u.MouseY());
		d.cy = p.y;
		d.cx = p.x;
		d.selecting = !(Pos{d.ay, d.ax} == Pos{d.cy, d.cx});
	}
	if (!u.MouseDown(sdl3::MouseButton::Left))
		m_dragging = false;

	// ── Clavier ──
	if (focus && !u.WantsKeyboard()) {
		auto &L = d.lines;
		auto moveTo = [&](size_t y, size_t x) {
			if (u.Shift()) {
				if (!d.selecting) {
					d.ay = d.cy;
					d.ax = d.cx;
				}
				d.cy = y;
				d.cx = x;
				d.selecting = !(Pos{d.ay, d.ax} == Pos{d.cy, d.cx});
			} else {
				d.cy = y;
				d.cx = x;
				d.selecting = false;
			}
			d.lastEdit = 0;
		};
		bool completionKeyUsed = false;
		if (m_completing && !m_items.empty()) {
			for (sdl3::Keycode code : u.Events().PressedKeycodes()) { const sdl3::keyboard::Key k = static_cast<sdl3::keyboard::Key>(code);
				if (k == sdl3::keyboard::Key::UP) {
					m_item = (m_item + int(m_items.size()) - 1) % int(m_items.size());
					completionKeyUsed = true;
				} else if (k == sdl3::keyboard::Key::DOWN) {
					m_item = (m_item + 1) % int(m_items.size());
					completionKeyUsed = true;
				} else if (k == sdl3::keyboard::Key::RETURN || k == sdl3::keyboard::Key::TAB) {
					const size_t dot = m_prefix.rfind('.');
					const std::string last = dot == std::string::npos ? m_prefix : m_prefix.substr(dot + 1);
					Edit(d, 0);
					d.cx -= std::min(d.cx, last.size());
					d.lines[d.cy].erase(d.cx, last.size());
					d.selecting = false;
					Insert(d, m_items[size_t(m_item)]);
					m_completing = false;
					completionKeyUsed = true;
				} else if (k == sdl3::keyboard::Key::ESCAPE) {
					m_completing = false;
					completionKeyUsed = true;
				}
			}
		}
		if (u.Ctrl()) {
			for (sdl3::Keycode kc : u.Events().PressedKeycodes()) {
				switch (kc) {
				case SDLK_Z: u.Shift() ? Redo(d) : Undo(d); break;
				case SDLK_Y: Redo(d); break;
				case SDLK_A:
					d.ay = d.ax = 0;
					d.cy = L.size() - 1;
					d.cx = L.back().size();
					d.selecting = true;
					break;
				case SDLK_C: sdl3::clipboard::SetText(SelectedText(d)); break;
				case SDLK_X:
					sdl3::clipboard::SetText(SelectedText(d));
					Edit(d, 0);
					if (d.selecting)
						DeleteSelection(d);
					else if (L.size() > 1) {
						L.erase(L.begin() + std::ptrdiff_t(d.cy));
						d.cy = std::min(d.cy, L.size() - 1);
						d.cx = 0;
					} else {
						L[0].clear();
						d.cx = 0;
					}
					break;
				case SDLK_V: {
					const String clip = sdl3::clipboard::GetText();
					if (!clip.empty()) {
						Edit(d, 0);
						Insert(d, clip.CStr());
					}
					break;
				}
				case SDLK_F:
					m_findOpen = true;
					m_gotoOpen = false;
					if (d.selecting && Range(d).first.y == Range(d).second.y)
						m_find = SelectedText(d);
					break;
				case SDLK_H:
					m_findOpen = m_replaceOpen = true;
					m_gotoOpen = false;
					break;
				case SDLK_G:
					m_gotoOpen = true;
					m_findOpen = false;
					m_goto = std::to_string(d.cy + 1);
					break;
				case SDLK_D: // dupliquer la ligne
					Edit(d, 0);
					L.insert(L.begin() + std::ptrdiff_t(d.cy) + 1, L[d.cy]);
					++d.cy;
					d.selecting = false;
					break;
				case SDLK_SLASH: { // commenter / décommenter
					Edit(d, 0);
					const std::string mark = d.path.extension() == ".json" ? "//" : "#";
					const auto [a, b] = d.selecting ? Range(d) : std::pair<Pos, Pos>{Pos{d.cy, 0}, Pos{d.cy, 0}};
					bool all = true;
					for (size_t y = a.y; y <= b.y; ++y) {
						const size_t f = L[y].find_first_not_of(" \t");
						all &= f == std::string::npos || L[y].compare(f, mark.size(), mark) == 0;
					}
					for (size_t y = a.y; y <= b.y; ++y) {
						const size_t f = L[y].find_first_not_of(" \t");
						if (f == std::string::npos)
							continue;
						if (all)
							L[y].erase(f, mark.size() + (L[y].compare(f + mark.size(), 1, " ") == 0 ? 1 : 0));
						else
							L[y].insert(f, mark + " ");
					}
					d.cx = std::min(d.cx, L[d.cy].size());
					d.ax = std::min(d.ax, L[d.ay].size());
					break;
				}
				case SDLK_SPACE: m_completing = true; break;
				default: break;
				}
			}
		}
		if (!u.TextInput().empty() && !u.Ctrl()) {
			Edit(d, 1);
			Insert(d, u.TextInput());
			const char last = u.TextInput().back();
			m_completing = last == '.' || (WordChar(last) && WordBefore(d).size() >= 2) || (m_completing && WordChar(last));
		}
		for (sdl3::Keycode code : u.Events().PressedKeycodes()) { const sdl3::keyboard::Key k = static_cast<sdl3::keyboard::Key>(code);
			if (completionKeyUsed && (k == sdl3::keyboard::Key::UP || k == sdl3::keyboard::Key::DOWN || k == sdl3::keyboard::Key::RETURN || k == sdl3::keyboard::Key::TAB || k == sdl3::keyboard::Key::ESCAPE))
				continue;
			std::string &line = L[d.cy];
			d.cx = std::min(d.cx, line.size());
			switch (k) {
			case sdl3::keyboard::Key::RETURN: {
				Edit(d, 0);
				DeleteSelection(d);
				std::string &cur = L[d.cy];
				std::string indent;
				for (char ch : cur) {
					if (ch != ' ' && ch != '\t')
						break;
					indent.push_back(ch);
				}
				if (d.cx > 0 && (cur[d.cx - 1] == '{' || cur[d.cx - 1] == '[' || cur[d.cx - 1] == '('))
					indent += env.tabMode ? std::string("\t") : std::string(size_t(env.tabSize), ' ');
				std::string rest = indent + cur.substr(d.cx);
				cur.erase(d.cx);
				L.insert(L.begin() + std::ptrdiff_t(d.cy) + 1, std::move(rest));
				++d.cy;
				d.cx = indent.size();
				m_completing = false;
				break;
			}
			case sdl3::keyboard::Key::TAB: {
				Edit(d, 0);
				const std::string unit = env.tabMode ? std::string("\t") : std::string(size_t(env.tabSize), ' ');
				if (d.selecting && Range(d).first.y != Range(d).second.y) { // indenter / désindenter les lignes
					const auto [a, b] = Range(d);
					for (size_t y = a.y; y <= b.y; ++y) {
						if (u.Shift()) {
							size_t n = 0;
							while (n < L[y].size() && n < unit.size() && (L[y][n] == ' ' || L[y][n] == '\t'))
								++n;
							L[y].erase(0, n);
						} else {
							L[y].insert(0, unit);
						}
					}
					d.cx = std::min(d.cx, L[d.cy].size());
					d.ax = std::min(d.ax, L[d.ay].size());
				} else if (!u.Shift()) {
					DeleteSelection(d);
					L[d.cy].insert(d.cx, unit);
					d.cx += unit.size();
				}
				break;
			}
			case sdl3::keyboard::Key::BACKSPACE:
				Edit(d, 2);
				if (d.selecting) {
					DeleteSelection(d);
				} else if (d.cx > 0) {
					const size_t p = Prev(line, d.cx);
					line.erase(p, d.cx - p);
					d.cx = p;
				} else if (d.cy > 0) {
					d.cx = L[d.cy - 1].size();
					L[d.cy - 1] += line;
					L.erase(L.begin() + std::ptrdiff_t(d.cy));
					--d.cy;
				}
				if (m_completing && WordBefore(d).empty())
					m_completing = false;
				break;
			case sdl3::keyboard::Key::DELETE:
				Edit(d, 2);
				if (d.selecting) {
					DeleteSelection(d);
				} else if (d.cx < line.size()) {
					line.erase(d.cx, Next(line, d.cx) - d.cx);
				} else if (d.cy + 1 < L.size()) {
					line += L[d.cy + 1];
					L.erase(L.begin() + std::ptrdiff_t(d.cy) + 1);
				}
				break;
			case sdl3::keyboard::Key::Left: {
				if (d.selecting && !u.Shift()) {
					const Pos a = Range(d).first;
					moveTo(a.y, a.x);
				} else if (d.cx > 0) {
					size_t p = Prev(line, d.cx);
					if (u.Ctrl()) // mot précédent
						while (p > 0 && WordChar(line[p - 1]))
							--p;
					moveTo(d.cy, p);
				} else if (d.cy > 0) {
					moveTo(d.cy - 1, L[d.cy - 1].size());
				}
				m_completing = false;
				break;
			}
			case sdl3::keyboard::Key::Right: {
				if (d.selecting && !u.Shift()) {
					const Pos b = Range(d).second;
					moveTo(b.y, b.x);
				} else if (d.cx < line.size()) {
					size_t p = Next(line, d.cx);
					if (u.Ctrl())
						while (p < line.size() && WordChar(line[p]))
							++p;
					moveTo(d.cy, p);
				} else if (d.cy + 1 < L.size()) {
					moveTo(d.cy + 1, 0);
				}
				m_completing = false;
				break;
			}
			case sdl3::keyboard::Key::UP:
			case sdl3::keyboard::Key::DOWN:
			case sdl3::keyboard::Key::PAGEUP:
			case sdl3::keyboard::Key::PAGEDOWN: {
				const size_t col = ByteToColumn(line, d.cx);
				const int delta = k == sdl3::keyboard::Key::UP ? -1 : k == sdl3::keyboard::Key::DOWN ? 1 : k == sdl3::keyboard::Key::PAGEUP ? -visible : visible;
				const size_t y = size_t(std::clamp<int64_t>(int64_t(d.cy) + delta, 0, int64_t(L.size()) - 1));
				moveTo(y, ColumnToByte(L[y], col));
				break;
			}
			case sdl3::keyboard::Key::HOME: {
				const size_t first = line.find_first_not_of(" \t");
				moveTo(d.cy, (first == std::string::npos || d.cx == first) ? 0 : first);
				break;
			}
			case sdl3::keyboard::Key::End: moveTo(d.cy, line.size()); break;
			case sdl3::keyboard::Key::ESCAPE:
				m_completing = false;
				m_findOpen = m_gotoOpen = false;
				d.selecting = false;
				break;
			default: break;
			}
		}
		if (m_findOpen && !m_find.empty() && u.KeycodePressed(SDLK_F3))
			FindNext(d, u.Shift());
	}
	if (m_findOpen && u.KeycodePressed(SDLK_ESCAPE)) {
		m_findOpen = m_gotoOpen = false;
		focus = true;
	}

	// ── Diagnostic en direct (0,4 s après la dernière frappe) ──
	if (d.version != d.checkedVersion && sdl3::GetTicksMS() - d.lastEditTicks > 400)
		Check(d);

	// ── Défilement : barres, molette, curseur toujours visible ──
	size_t maxColumns = 0;
	for (const std::string &l : d.lines)
		if (l.size() > maxColumns)
			maxColumns = std::max(maxColumns, unicode::CodepointCount(l));
	const float contentH = float(d.lines.size() + size_t(visible) - 1) * lineH + 8;
	const float contentW = gutter + 6 + float(maxColumns + 4) * m_charW;
	const float maxY = std::max(0.f, contentH - view.h), maxX = std::max(0.f, contentW - view.w);
	if (d.cy != m_lastCy || d.cx != m_lastCx) {
		const float cyPx = float(d.cy) * lineH;
		d.scrollY = std::clamp(d.scrollY, cyPx - float(visible - 1) * lineH, cyPx);
		const float cxPx = float(ByteToColumn(d.lines[d.cy], d.cx)) * m_charW;
		d.scrollX = std::clamp(d.scrollX, cxPx - (view.w - gutter - 6) + 3 * m_charW, std::max(0.f, cxPx - 2 * m_charW));
		m_lastCy = d.cy;
		m_lastCx = d.cx;
	}
	u.ScrollInput(view, d.scrollX, d.scrollY, maxX, maxY, lineH * 3);
	u.ScrollbarV("##v", {view.Right(), view.y, bars.thickness, view.h}, d.scrollY, contentH, view.h, bars);
	u.ScrollbarH("##h", {view.x + gutter, view.Bottom(), view.w - gutter, bars.thickness}, d.scrollX, contentW - gutter, view.w - gutter, bars);
	c.FillRect({view.x, view.Bottom(), gutter, bars.thickness}, Rgba(18, 20, 26));
	c.FillRect({view.Right(), view.Bottom(), bars.thickness, bars.thickness}, Rgba(24, 27, 35));
	d.scrollY = std::clamp(d.scrollY, 0.f, maxY);
	d.scrollX = std::clamp(d.scrollX, 0.f, maxX);

	// ── Rendu ──
	c.FillRect(view, Rgba(14, 16, 21));
	const size_t first = size_t(d.scrollY / lineH);
	const float y0 = view.y + 4 - (d.scrollY - float(first) * lineH);
	const auto [sa, sb] = Range(d);
	const std::string needle = m_findOpen ? Lower(m_find) : std::string();
	auto colX = [&](const std::string &l, size_t byte) { return textX - d.scrollX + m_charW * float(ByteToColumn(l, std::min(byte, l.size()))); };
	c.PushClip({view.x + gutter, view.y, view.w - gutter, view.h});
	for (size_t i = first; i < d.lines.size() && y0 + float(i - first) * lineH < view.Bottom(); ++i) {
		const float y = y0 + float(i - first) * lineH;
		const std::string &line = d.lines[i];
		if (i == d.cy && focus && !d.selecting)
			c.FillRect({view.x + gutter, y - 1, view.w - gutter, lineH}, Rgba(30, 34, 44));
		if (int(i) == d.errorLine)
			c.FillRect({view.x + gutter, y - 1, view.w - gutter, lineH}, Rgba(120, 30, 34, 90));
		if (d.selecting && i >= sa.y && i <= sb.y) { // sélection
			const float x0 = i == sa.y ? colX(line, sa.x) : textX - d.scrollX;
			const float x1 = i == sb.y ? colX(line, sb.x) : colX(line, line.size()) + m_charW * 0.6f;
			c.FillRect({x0, y - 1, std::max(2.f, x1 - x0), lineH}, Rgba(50, 90, 160, 150));
		}
		if (!needle.empty()) { // occurrences de la recherche
			const std::string low = Lower(line);
			for (size_t p = low.find(needle); p != std::string::npos; p = low.find(needle, p + needle.size()))
				c.StrokeRect({colX(line, p), y - 1, colX(line, p + needle.size()) - colX(line, p), lineH}, Rgba(236, 180, 72), 1.f);
		}
		// Coloration syntaxique.
		float x = textX - d.scrollX;
		size_t p = 0;
		const bool json = d.path.extension() == ".json";
		while (p < line.size()) {
			size_t e = p + 1;
			sdl3::Color col = th.text;
			const char ch = line[p];
			if ((ch == '#' && !json) || line.compare(p, 2, "//") == 0) {
				e = line.size();
				col = Rgba(110, 130, 110);
			} else if (ch == '"' || ch == '\'') {
				while (e < line.size() && line[e] != ch)
					e += line[e] == '\\' ? 2u : 1u;
				e = std::min(e + 1, line.size());
				col = Rgba(214, 170, 110);
			} else if (std::isdigit(static_cast<unsigned char>(ch))) {
				while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '.'))
					++e;
				col = Rgba(180, 206, 140);
			} else if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
				while (e < line.size() && (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '_'))
					++e;
				const std::string_view word(line.data() + p, e - p);
				if (std::ranges::find(kKeywords, word) != std::end(kKeywords))
					col = Rgba(198, 120, 221);
				else if (std::ranges::find(kModules, word) != std::end(kModules))
					col = Rgba(86, 182, 194);
				else if (e < line.size() && line[e] == '(')
					col = Rgba(97, 175, 239);
			} else if (std::string_view("{}()[]").find(ch) != std::string_view::npos) {
				col = Rgba(160, 168, 186);
			}
			const std::string_view token(line.data() + p, e - p);
			if (x < view.Right() && x + m_charW * float(token.size()) > view.x)
				c.Text(x, y, token, col, 14, sdl3::CanvasFont::Mono);
			x += m_charW * float(unicode::CodepointCount(token));
			p = e;
		}
		if (int(i) == d.errorLine) { // soulignement ondulé
			const size_t from = std::min(line.find_first_not_of(" \t"), line.size());
			float wx = colX(line, from);
			const float end = std::max(wx + m_charW * 3, colX(line, line.size()));
			std::vector<float> pts;
			for (int k = 0; wx <= end; ++k, wx += 3.f) {
				pts.push_back(wx);
				pts.push_back(y + lineH - 3 + (k % 2 ? 2.f : 0.f));
			}
			if (pts.size() >= 4)
				c.Polyline(pts.data(), pts.size() / 2, th.danger, 1.f);
		}
		if (i == d.cy && focus)
			c.FillRect({colX(line, d.cx), y, 2, lineH - 2}, th.accentHover);
	}
	c.PopClip();
	// Gouttière : numéros, marqueur d'erreur.
	c.PushClip(view);
	c.FillRect({view.x, view.y, gutter, view.h}, Rgba(18, 20, 26));
	for (size_t i = first; i < d.lines.size() && y0 + float(i - first) * lineH < view.Bottom(); ++i) {
		const float y = y0 + float(i - first) * lineH;
		const std::string num = std::to_string(i + 1);
		c.Text(view.x + gutter - 10 - c.MeasureText(num, 12, sdl3::CanvasFont::Mono), y + 1, num, i == d.cy ? th.text : Rgba(90, 98, 116), 12, sdl3::CanvasFont::Mono);
		if (int(i) == d.errorLine)
			c.FillCircle(view.x + 9, y + lineH * 0.5f, 4, th.danger);
	}
	c.PopClip();

	// ── Complétion ──
	if (m_completing && focus && env.complete) {
		m_prefix = WordBefore(d);
		m_items = env.complete(m_prefix);
		if (m_items.empty() || (m_items.size() == 1 && m_prefix.ends_with(m_items[0])))
			m_completing = m_items.empty() ? false : m_completing;
		if (!m_items.empty()) {
			m_item = std::clamp(m_item, 0, int(m_items.size()) - 1);
			const size_t shown = std::min<size_t>(m_items.size(), 9);
			const float px = colX(d.lines[d.cy], d.cx), py = y0 + float(d.cy - first + 1) * lineH;
			const float w = 280;
			const Rect pop{std::min(px, view.Right() - w), py + 2, w, float(shown) * 22 + 26};
			c.FillRoundRect(pop, 6, Rgba(24, 27, 35, 250));
			c.StrokeRoundRect(pop, 6, th.panelBorder);
			const size_t start = size_t(std::max(0, m_item - int(shown) + 1));
			for (size_t k = 0; k < shown && start + k < m_items.size(); ++k) {
				const Rect row{pop.x + 4, pop.y + 4 + float(k) * 22, pop.w - 8, 22};
				const bool sel = int(start + k) == m_item;
				if (sel)
					c.FillRoundRect(row, 4, Rgba(38, 92, 170));
				const std::string &item = m_items[start + k];
				const bool isModule = std::ranges::find(kModules, item) != std::end(kModules);
				u.Icon({row.x + 2, row.y, 18, row.h}, isModule ? icons::Codepoint(icons::MaterialIcons::EXTENSION) : icons::Codepoint(icons::MaterialIcons::CODE), isModule ? Rgba(86, 182, 194) : Rgba(97, 175, 239), 13);
				c.Text(row.x + 24, row.y + 3, item, th.text, 13, sdl3::CanvasFont::Mono);
				if (u.Interact(u.MakeId(std::format("##comp{}", k)), row).leftClicked) {
					m_item = int(start + k);
					const size_t dot = m_prefix.rfind('.');
					const std::string last = dot == std::string::npos ? m_prefix : m_prefix.substr(dot + 1);
					Edit(d, 0);
					d.cx -= std::min(d.cx, last.size());
					d.lines[d.cy].erase(d.cx, last.size());
					Insert(d, item);
					m_completing = false;
				}
			}
			c.Text(pop.x + 8, pop.Bottom() - 18, "flèches · Entrée / Tab · Échap", th.textDim, 11);
		}
	}

	// ── Message d'erreur ──
	if (!d.errorMessage.empty()) {
		const Rect err{view.x + gutter + 10, view.Bottom() - 34, view.w - gutter - 20, 28};
		const auto eit = u.Interact(u.MakeId("##err"), err);
		c.FillRoundRect(err, 6, eit.hovered ? Rgba(90, 28, 32, 240) : Rgba(70, 22, 26, 235));
		u.Icon({err.x + 4, err.y, 24, err.h}, icons::Codepoint(icons::MaterialIcons::WARNING), th.danger, 18);
		c.Text(err.x + 32, err.y + (err.h - c.LineHeight(13)) * 0.5f, c.Ellipsize(d.errorMessage, err.w - 40, 13), Rgba(255, 190, 190), 13);
		if (eit.leftClicked) {
			Goto(d, d.errorLine + 1, d.errorColumn);
			focus = true;
		}
		if (eit.hovered)
			u.Tooltip("clic : aller à l'erreur");
	}
	u.PopId();
}

} // namespace voxelia::studio
