#include "ui/ui.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>

#include <core/hash.hpp>
#include <core/string.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/input/touch.hpp>

namespace ui {

namespace {
/// Format d'affichage : `std::format` classique, ou "%" pour un pourcentage (0..1 → 0..100 %).
std::string FormatValue(std::string_view format, float v) {
	if (format == "%")
		return std::format("{:.0f} %", double(v) * 100.0);
	return std::vformat(format, std::make_format_args(v));
}
/// Capitales Unicode pour le Latin-1 et « œ » (titres de section accentués).
std::string UpperUtf8(std::string_view in) {
	std::string out;
	out.reserve(in.size());
	for (size_t i = 0; i < in.size();) {
		char32_t cp = unicode::DecodeUtf8(in, i);
		if (cp >= U'a' && cp <= U'z')
			cp -= 32;
		else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)
			cp -= 32;
		else if (cp == U'œ')
			cp = U'Œ';
		else if (cp == U'ÿ')
			cp = U'Ÿ';
		unicode::AppendUtf8(out, cp);
	}
	return out;
}
} // namespace

bool Context::KeyPressed(sdl3::keyboard::Key k) const noexcept {
	return m_events->KeycodePressed(static_cast<SDL_Keycode>(k));
}

void icons::RegisterAll() {
	sdl3::Canvas::RegisterIcons({
		Codepoint(MaterialIcons::PLAY_ARROW), Codepoint(MaterialIcons::ADD), Codepoint(MaterialIcons::PUBLIC), Codepoint(MaterialIcons::EXTENSION),
		Codepoint(MaterialIcons::SETTINGS), Codepoint(MaterialIcons::VIEW_IN_AR), Codepoint(MaterialIcons::UNDO), Codepoint(MaterialIcons::ARROW_BACK),
		Codepoint(MaterialIcons::REFRESH), Codepoint(MaterialIcons::LIGHTBULB), Codepoint(MaterialIcons::DELETE), Codepoint(MaterialIcons::EDIT),
		Codepoint(MaterialIcons::CHECK), Codepoint(MaterialIcons::CLOSE), Codepoint(MaterialIcons::FOLDER), Codepoint(MaterialIcons::DESCRIPTION),
		Codepoint(MaterialIcons::MUSIC_NOTE), Codepoint(MaterialIcons::VOLUME_UP), Codepoint(MaterialIcons::TUNE), Codepoint(MaterialIcons::SPORTS_ESPORTS),
		Codepoint(MaterialIcons::PALETTE), Codepoint(MaterialIcons::SPEED), Codepoint(MaterialIcons::INFO), Codepoint(MaterialIcons::SAVE),
		Codepoint(MaterialIcons::TERRAIN), Codepoint(MaterialIcons::PETS), Codepoint(MaterialIcons::BOLT), Codepoint(MaterialIcons::CODE),
		Codepoint(MaterialIcons::WB_SUNNY), Codepoint(MaterialIcons::WARNING), Codepoint(MaterialIcons::BUILD), Codepoint(MaterialIcons::MEMORY),
		Codepoint(MaterialIcons::KEYBOARD), Codepoint(MaterialIcons::VISIBILITY), Codepoint(MaterialIcons::MOUSE), Codepoint(MaterialIcons::STORAGE),
		Codepoint(MaterialIcons::CHEVRON_LEFT), Codepoint(MaterialIcons::CHEVRON_RIGHT), Codepoint(MaterialIcons::SEARCH), Codepoint(MaterialIcons::SKIP_NEXT),
		Codepoint(MaterialIcons::PAUSE), Codepoint(MaterialIcons::EXIT_TO_APP), Codepoint(MaterialIcons::HOME), Codepoint(MaterialIcons::SCHEDULE),
		Codepoint(MaterialIcons::INSERT_DRIVE_FILE), Codepoint(MaterialIcons::TERMINAL), Codepoint(MaterialIcons::TABLE_CHART), Codepoint(MaterialIcons::GRID_ON),
		Codepoint(MaterialIcons::PERSON), Codepoint(MaterialIcons::DASHBOARD), Codepoint(MaterialIcons::REPLAY), Codepoint(MaterialIcons::EXPAND_MORE),
		Codepoint(MaterialIcons::ARROW_FORWARD), Codepoint(MaterialIcons::FACE), Codepoint(MaterialIcons::CHECKROOM), Codepoint(MaterialIcons::CIRCLE),
		Codepoint(MaterialIcons::FIBER_MANUAL_RECORD), Codepoint(MaterialIcons::LOCK), Codepoint(MaterialIcons::PUSH_PIN), Codepoint(MaterialIcons::ERROR),
		Codepoint(MaterialIcons::ARROW_DROP_UP), Codepoint(MaterialIcons::ARROW_DROP_DOWN), Codepoint(MaterialIcons::ARROW_LEFT), Codepoint(MaterialIcons::ARROW_RIGHT),
		Codepoint(MaterialIcons::OPEN_WITH), Codepoint(MaterialIcons::ROTATE_RIGHT), Codepoint(MaterialIcons::CENTER_FOCUS_STRONG), Codepoint(MaterialIcons::MY_LOCATION),
		Codepoint(MaterialIcons::LIST), Codepoint(MaterialIcons::CATEGORY), Codepoint(MaterialIcons::BRUSH), Codepoint(MaterialIcons::DELETE_OUTLINE),
		Codepoint(MaterialIcons::LAYERS), Codepoint(MaterialIcons::WORKSPACES), Codepoint(MaterialIcons::COLORIZE), Codepoint(MaterialIcons::FORMAT_COLOR_FILL),
		Codepoint(MaterialIcons::CLEANING_SERVICES), Codepoint(MaterialIcons::REDO), Codepoint(MaterialIcons::CROP_FREE), Codepoint(MaterialIcons::STRAIGHTEN),
		Codepoint(MaterialIcons::ANIMATION), Codepoint(MaterialIcons::TIMELINE), Codepoint(MaterialIcons::ADD_BOX), Codepoint(MaterialIcons::CREATE_NEW_FOLDER),
		Codepoint(MaterialIcons::CONTENT_COPY), Codepoint(MaterialIcons::VISIBILITY_OFF), Codepoint(MaterialIcons::OPEN_IN_NEW), Codepoint(MaterialIcons::TRANSFORM),
		Codepoint(MaterialIcons::ASPECT_RATIO), Codepoint(MaterialIcons::VIEW_AGENDA), Codepoint(MaterialIcons::VIEW_COLUMN)
	});
}


// ── Identifiants ──────────────────────────────────────────────────────────
void Context::PushId(std::string_view s) { m_idStack.push_back(MakeId(s)); }
void Context::PushId(uint64_t n) { m_idStack.push_back(core::HashCombine(m_idStack.empty() ? 0 : m_idStack.back(), n)); }
void Context::PopId() {
	if (!m_idStack.empty())
		m_idStack.pop_back();
}
Id Context::MakeId(std::string_view label) const {
	// « Libellé##id » : seule la partie après ## identifie (libellés dynamiques).
	const size_t hash = label.find("##");
	const std::string_view key = hash == std::string_view::npos ? label : label.substr(hash);
	return core::HashCombine(m_idStack.empty() ? 0x5ca1ab1eULL : m_idStack.back(), core::HashString(key)) | 1;
}

static std::string_view Visible(std::string_view label) {
	const size_t hash = label.find("##");
	return hash == std::string_view::npos ? label : label.substr(0, hash);
}

// ── Image ─────────────────────────────────────────────────────────────────
void Context::Begin(float dt) {
	if (dt >= 0) {
		m_time += double(dt);
		m_dt = dt;
	} else {
		// horloge murale par défaut (aucun `Update(dt)` ce cycle)
		static const auto start = std::chrono::steady_clock::now();
		const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		m_dt = m_time > 0 ? float(std::min(0.1, now - m_time)) : 0.f;
		m_time = now;
	}
	// Entrée de l'image : molette et glissé tactile sont « consommables » (membres
	// mutables remis à zéro ici puis consommés par les widgets au fil de l'image).
	m_wheelX = m_events->WheelX();
	m_wheelY = m_events->WheelY();
	m_panX = 0.f;
	m_panY = 0.f;
	m_touchDown = false;
	m_touchTravel = 0.f;
	const float cw = m_canvas->Width(), ch = m_canvas->Height();
	for (const sdl3::TouchFinger &f : m_events->TouchFingers()) {
		if (sdl3::touch::DeviceType(f.touch) != sdl3::TouchDeviceType::DIRECT)
			continue;
		if (!m_touchDown) {
			m_touchDown = true;
			m_touchFinger = uint64_t(f.finger);
		}
		if (uint64_t(f.finger) == m_touchFinger) {
			const float dx = f.dx * cw, dy = f.dy * ch;
			m_panX += dx;
			m_panY += dy;
			m_touchTravel += std::fabs(dx) + std::fabs(dy);
		}
	}
	m_top = m_topNext;
	m_topRect = m_topNextRect;
	m_topNext = 0;
	m_topSeen = false;
	m_hot = 0;
	m_wantsMouse = m_active != 0;
	m_clickedThisFrame = false;
	m_tooltip.clear();
	++m_frame;
	m_popupHover = (m_choicePopup.id != 0 && m_choicePopup.box.Contains(MouseX(), MouseY())) ||
				   (m_menuPopup.menu >= 0 && m_menuPopup.box.Contains(MouseX(), MouseY()));
	m_columns.clear();
	m_idStack.clear();
	m_staticPressed = false;
	m_staticSerial.Clear();
	if (MousePressed(sdl3::MouseButton::Left))
		m_focus = 0; // un clic hors d'un champ retire le focus (le champ cliqué le reprend)
	// Glissé tactile : au-delà d'un seuil, le doigt fait défiler au lieu d'actionner le widget.
	if (!m_touchDown)
		m_touchScrolling = false;
	else if (!m_touchScrolling && m_touchTravel > 10.f) {
		m_touchScrolling = true;
		m_active = 0;
	}
	m_scrollStack.clear();
}

void Context::End() {
	// Texte statique sélectionné : un appui ailleurs le désélectionne ; Ctrl+C le copie,
	// Ctrl+A sélectionne tout le texte (quand aucun champ n'a le focus).
	if (MousePressed(sdl3::MouseButton::Left) && !m_staticPressed)
		m_staticSel = StaticSelection{};
	if (!MouseDown(sdl3::MouseButton::Left))
		m_staticSel.dragging = false;
	if (m_staticSel.id != 0 && m_focus == 0 && Ctrl()) {
		if (KeycodePressed(SDLK_C) || KeycodePressed(SDLK_INSERT))
			(void)m_staticSel.edit.Copy(m_staticSel.text);
		else if (KeycodePressed(SDLK_A))
			m_staticSel.edit.SelectAll(m_staticSel.text);
	}
	if (!MouseDown(sdl3::MouseButton::Left))
		m_active = 0;
	DrawChoicePopup(); // premier plan : au-dessus de tous les widgets de l'image
	DrawMenuPopup();
	if (!m_tooltip.empty()) {
		// Infobulle multi-lignes (`\n`).
		sdl3::Canvas &c = *m_canvas;
		const float fs = m_theme.fontSize - 1, lh = c.LineHeight(fs);
		std::vector<std::string_view> lines;
		for (size_t start = 0;;) {
			const size_t nl = m_tooltip.find('\n', start);
			lines.push_back(std::string_view(m_tooltip).substr(start, nl == std::string::npos ? std::string::npos : nl - start));
			if (nl == std::string::npos)
				break;
			start = nl + 1;
		}
		float textW = 0.f;
		for (const std::string_view line : lines)
			textW = std::max(textW, c.MeasureText(line, fs));
		const float w = std::min(textW + 18, c.Width() - 8), h = lh * float(lines.size()) + 10;
		float x = MouseX() + 14, y = MouseY() + 18;
		x = std::max(4.f, std::min(x, c.Width() - w - 4));
		y = std::max(4.f, std::min(y, c.Height() - h - 4));
		c.PushClip({0, 0, c.Width(), c.Height()}, false);
		c.FillRoundRect({x, y, w, h}, 5, sdl3::WithAlpha(m_theme.panel, 250));
		c.StrokeRoundRect({x, y, w, h}, 5, m_theme.panelBorder);
		for (size_t i = 0; i < lines.size(); ++i)
			c.Text(x + 9, y + 5 + lh * float(i), lines[i], m_theme.text, fs);
		c.PopClip();
	}
}

void Context::DrawChoicePopup() {
	ChoicePopup &p = m_choicePopup;
	if (p.id == 0)
		return;
	sdl3::Canvas &c = *m_canvas;
	const float itemH = m_theme.rowHeight - 4.f;
	const int visible = int(std::round(p.box.h / itemH));
	const int n = int(p.options.size());
	if (m_popupHover && WheelY() != 0.f) // défilement d'une longue liste
		p.first = std::clamp(p.first - int(WheelY()), 0, std::max(0, n - visible));
	c.PushClip({0, 0, c.Width(), c.Height()}, false);
	c.FillRoundRect({p.box.x + 2.f, p.box.y + 3.f, p.box.w, p.box.h}, m_theme.radius, sdl3::Rgba(0, 0, 0, 90));
	c.FillRoundRect(p.box, m_theme.radius, sdl3::WithAlpha(m_theme.panel, 252));
	c.StrokeRoundRect(p.box, m_theme.radius, m_theme.panelBorder);
	const bool pressed = MousePressed(sdl3::MouseButton::Left);
	int hovered = -1;
	c.PushClip(p.box);
	for (int i = p.first; i < std::min(n, p.first + visible); ++i) {
		const Rect row{p.box.x + 3.f, p.box.y + float(i - p.first) * itemH, p.box.w - 6.f, itemH};
		const bool over = row.Contains(MouseX(), MouseY());
		if (over)
			hovered = i;
		if (i == p.current)
			c.FillRoundRect(row.Inset(0, 2), m_theme.radius, sdl3::WithAlpha(m_theme.accent, over ? 255 : 200));
		else if (over)
			c.FillRoundRect(row.Inset(0, 2), m_theme.radius, m_theme.widgetHover);
		c.Text(row.x + 10.f, row.y + (itemH - c.LineHeight(m_theme.fontSize)) * 0.5f, c.Ellipsize(p.options[size_t(i)], row.w - 20.f, m_theme.fontSize),
			   i == p.current ? m_theme.accentText : m_theme.text, m_theme.fontSize);
	}
	c.PopClip();
	c.PopClip();
	// Fermeture : un choix, un clic ailleurs, Échap — jamais dans l'image d'ouverture.
	if (p.openedFrame == m_frame)
		return;
	if (pressed && hovered >= 0)
		p.picked = hovered; // appliqué par Choice à l'image suivante, puis la liste se ferme
	else if ((pressed && !m_popupHover) || MousePressed(sdl3::MouseButton::Right) || KeycodePressed(SDLK_ESCAPE))
		p.id = 0;
}

MenuChoice Context::MenuBar(const Rect &r, std::span<const BarMenu> menus) {
	sdl3::Canvas &c = *m_canvas;
	const Id bar = MakeId("##menubar");
	MenuPopup &p = m_menuPopup;
	MenuChoice result;
	if (p.bar == bar && p.picked.menu >= 0) { // choix fait dans le menu (image précédente)
		result = p.picked;
		p = MenuPopup{};
	}
	const float fs = m_theme.fontSize - 1.f;
	float x = r.x + 4.f;
	if (p.bar == bar)
		p.barRect = r;
	for (size_t m = 0; m < menus.size(); ++m) {
		const Rect title{x, r.y + 3.f, c.MeasureText(menus[m].title, fs) + 20.f, r.h - 6.f};
		x = title.Right() + 2.f;
		const bool open = p.bar == bar && p.menu == int(m);
		const bool over = !m_blocked && title.Contains(MouseX(), MouseY());
		if (open || over)
			c.FillRoundRect(title, m_theme.radius, open ? m_theme.widgetActive : m_theme.widgetHover);
		c.TextCentered(title, menus[m].title, m_theme.text, fs);
		const bool switchTo = over && p.bar == bar && p.menu >= 0 && !open; // survol d'un titre voisin
		if (over)
			m_wantsMouse = true;
		if ((over && MousePressed(sdl3::MouseButton::Left)) || switchTo) {
			if (open) {
				p = MenuPopup{};
				continue;
			}
			MenuPopup next;
			next.bar = bar;
			next.menu = int(m);
			next.barRect = r;
			next.openedFrame = m_frame;
			float w = title.w, h = 8.f;
			for (const MenuEntry &item : menus[m].items) {
				next.entries.push_back({std::string(item.label), std::string(item.shortcut), item.enabled, item.checked});
				w = std::max(w, c.MeasureText(item.label, fs) + c.MeasureText(item.shortcut, fs - 1.f) + 76.f);
				h += item.label.empty() ? 9.f : m_theme.rowHeight - 4.f;
			}
			next.box = {std::clamp(title.x, 4.f, std::max(4.f, c.Width() - w - 4.f)), r.Bottom() + 1.f, w, h};
			p = std::move(next);
		}
	}
	return result;
}

void Context::DrawMenuPopup() {
	MenuPopup &p = m_menuPopup;
	if (p.menu < 0)
		return;
	sdl3::Canvas &c = *m_canvas;
	const float fs = m_theme.fontSize - 1.f, itemH = m_theme.rowHeight - 4.f;
	c.PushClip({0, 0, c.Width(), c.Height()}, false);
	c.FillRoundRect({p.box.x + 3.f, p.box.y + 4.f, p.box.w, p.box.h}, m_theme.radius, sdl3::Rgba(0, 0, 0, 100));
	c.FillRoundRect(p.box, m_theme.radius, sdl3::WithAlpha(m_theme.panel, 252));
	c.StrokeRoundRect(p.box, m_theme.radius, m_theme.panelBorder);
	const bool pressed = MousePressed(sdl3::MouseButton::Left);
	int hovered = -1;
	float y = p.box.y + 4.f;
	for (size_t i = 0; i < p.entries.size(); ++i) {
		const MenuPopup::Entry &e = p.entries[i];
		if (e.label.empty()) {
			c.Line(p.box.x + 10.f, y + 4.5f, p.box.Right() - 10.f, y + 4.5f, m_theme.panelBorder);
			y += 9.f;
			continue;
		}
		const Rect row{p.box.x + 4.f, y, p.box.w - 8.f, itemH};
		y += itemH;
		const bool over = e.enabled && row.Contains(MouseX(), MouseY());
		if (over) {
			hovered = int(i);
			c.FillRoundRect(row, m_theme.radius, sdl3::WithAlpha(m_theme.accent, 230));
		}
		const sdl3::Color ink = over ? m_theme.accentText : e.enabled ? m_theme.text : m_theme.textDim;
		const float ty = row.y + (itemH - c.LineHeight(fs)) * 0.5f;
		if (e.checked)
			c.Icon({row.x + 4.f, row.y, 20.f, itemH}, icons::Codepoint(icons::MaterialIcons::CHECK), ink, 16.f);
		c.Text(row.x + 28.f, ty, e.label, ink, fs);
		if (!e.shortcut.empty())
			c.Text(row.Right() - 10.f - c.MeasureText(e.shortcut, fs - 1.f), ty + 1.f, e.shortcut, over ? ink : m_theme.textDim, fs - 1.f);
	}
	c.PopClip();
	if (p.openedFrame == m_frame)
		return;
	if (pressed && hovered >= 0)
		p.picked = {p.menu, hovered}; // rendu par MenuBar à l'image suivante
	else if ((pressed && !m_popupHover && !p.barRect.Contains(MouseX(), MouseY())) || MousePressed(sdl3::MouseButton::Right) || KeycodePressed(SDLK_ESCAPE))
		p = MenuPopup{};
}

bool Context::MouseIn(const Rect &r) const noexcept {
	if (m_blocked || m_popupHover)
		return false;
	if (!r.Contains(MouseX(), MouseY()))
		return false;
	for (const ScrollFrame &f : m_scrollStack)
		if (!f.view.Contains(MouseX(), MouseY()))
			return false;
	return true;
}

Context::Interaction Context::Interact(Id id, const Rect &r) {
	Interaction it;
	if (m_blocked || m_popupHover)
		return it; // arrière-plan d'un modal, ou sous une liste déroulante ouverte : aucune interaction
	const bool inside = MouseIn(r);
	if (inside) { // soumis après les autres : dessiné au-dessus d'eux
		m_topNext = id;
		m_topNextRect = r;
	}
	// Recouvert par le widget du dessus de l'image précédente (souris toujours sur lui), s'il
	// n'a pas encore été soumis à cette image — sinon il est dessous.
	if (id == m_top)
		m_topSeen = true;
	const bool covered = m_top != 0 && m_top != id && !m_topSeen && MouseIn(m_topRect);
	// Activé à cette image par un widget dessous : celui-ci, soumis après, le remplace.
	const bool takeover = m_active != 0 && m_activeFrame == m_frame && MousePressed(sdl3::MouseButton::Left);
	it.hovered = inside && !covered && (m_active == 0 || m_active == id || takeover);
	if (it.hovered) {
		m_hot = id;
		m_wantsMouse = true;
		if (MousePressed(sdl3::MouseButton::Left)) {
			m_active = id;
			m_activeFrame = m_frame;
		}
		if (MousePressed(sdl3::MouseButton::Right))
			it.rightClicked = true;
	}
	it.held = m_active == id && MouseDown(sdl3::MouseButton::Left);
	it.leftClicked = m_active == id && MouseReleased(sdl3::MouseButton::Left) && it.hovered;
	if (it.leftClicked) {
		it.doubleClicked = id == m_lastClickId && m_time - m_lastClickTime < 0.4;
		m_lastClickId = id;
		m_lastClickTime = m_time;
	}
	m_clickedThisFrame |= it.leftClicked;
	return it;
}

// ── Mise en page ──────────────────────────────────────────────────────────
void Context::BeginColumn(const Rect &area) { m_columns.push_back({area, area.y, {}, 0, area.x + area.w}); }
void Context::EndColumn() {
	if (!m_columns.empty())
		m_columns.pop_back();
}

Rect Context::Next(float h) {
	if (h <= 0)
		h = m_theme.rowHeight;
	if (m_columns.empty())
		BeginColumn({0, 0, m_canvas->Width(), m_canvas->Height()});
	Column &col = m_columns.back();
	if (col.rowIndex < col.rowCells.size())
		return col.rowCells[col.rowIndex++];
	Rect r{col.area.x, col.cursorY, col.area.w, h};
	col.cursorY += h + m_theme.spacing;
	return r;
}

void Context::ExtendContent(float right) {
	if (!m_columns.empty())
		m_columns.back().maxRight = std::max(m_columns.back().maxRight, right);
}

Rect Context::ScrollViewport() const {
	return m_scrollStack.empty() ? Rect{0, 0, m_canvas->Width(), m_canvas->Height()} : m_scrollStack.back().view;
}

void Context::Row(std::span<const float> weights, float h) {
	if (h <= 0)
		h = m_theme.rowHeight;
	const Rect line = Next(h);
	Column &col = m_columns.back();
	col.rowCells.clear();
	col.rowIndex = 0;
	if (weights.empty())
		return;
	float total = 0;
	for (float w : weights)
		total += w;
	const float gap = m_theme.spacing;
	const float usable = line.w - gap * float(weights.size() - 1);
	float x = line.x;
	for (float w : weights) {
		const float cw = usable * w / total;
		col.rowCells.push_back({x, line.y, cw, h});
		x += cw + gap;
	}
}

void Context::Row(std::initializer_list<float> weights, float h) { Row(std::span<const float>(weights.begin(), weights.end()), h); }

void Context::Space(float h) {
	if (!m_columns.empty())
		m_columns.back().cursorY += h;
}

Rect Context::Remaining() const {
	if (m_columns.empty())
		return {0, 0, m_canvas->Width(), m_canvas->Height()};
	const Column &c = m_columns.back();
	return {c.area.x, c.cursorY, c.area.w, std::max(0.f, c.area.y + c.area.h - c.cursorY)};
}

float Context::ColumnY() const {
	return m_columns.empty() ? 0.f : m_columns.back().cursorY;
}

// ── Conteneurs ────────────────────────────────────────────────────────────
void Context::Card(const Rect &r, bool hovered, bool selected) {
	sdl3::Canvas &c = *m_canvas;
	c.FillRoundRect(r, m_theme.radius + 2, hovered ? sdl3::Rgba(34, 39, 50, 240) : sdl3::Rgba(28, 32, 41, 235));
	c.StrokeRoundRect(r, m_theme.radius + 2, selected ? m_theme.accent : (hovered ? sdl3::Rgba(76, 84, 102) : m_theme.panelBorder),
					  selected ? 2.f : 1.f);
}

void Context::BeginPanel(const Rect &r, std::string_view title) {
	sdl3::Canvas &c = *m_canvas;
	c.FillRoundRect(r, m_theme.radius + 4, m_theme.panel);
	c.StrokeRoundRect(r, m_theme.radius + 4, m_theme.panelBorder);
	if (MouseIn(r))
		m_wantsMouse = true;
	float top = r.y;
	if (!title.empty()) {
		const float th = m_theme.rowHeight + 4;
		c.Text(r.x + m_theme.padding, r.y + (th - c.LineHeight(m_theme.fontSize + 1, sdl3::CanvasFont::Bold)) * 0.5f + 2, title, m_theme.text,
			   m_theme.fontSize + 1, sdl3::CanvasFont::Bold);
		c.FillRect({r.x + 1, r.y + th, r.w - 2, 1}, m_theme.panelBorder);
		top += th;
	}
	const float p = m_theme.padding;
	BeginColumn({r.x + p, top + p, r.w - 2 * p, r.y + r.h - top - 2 * p});
}

void Context::EndPanel() { EndColumn(); }

bool Context::PageHeader(const Rect &r, std::string_view title, std::string_view subtitle) {
	sdl3::Canvas &c = *m_canvas;
	const bool back = IconButton({r.x, r.y + 8, 36, 36}, icons::Codepoint(icons::MaterialIcons::ARROW_BACK), "Retour (Échap)");
	c.Text(r.x + 48, r.y, title, m_theme.text, 26, sdl3::CanvasFont::Bold);
	if (!subtitle.empty())
		c.Text(r.x + 50, r.y + 36, subtitle, m_theme.textDim, 13);
	return back;
}

void Context::BeginScroll(std::string_view idText, const Rect &r, const ScrollOptions &o) {
	const Id id = MakeId(idText);
	ScrollState &st = m_scroll[id];
	const float t = o.bar.thickness;
	const bool allowV = (uint8_t(o.axes) & uint8_t(ScrollAxes::Vertical)) != 0;
	const bool allowH = (uint8_t(o.axes) & uint8_t(ScrollAxes::Horizontal)) != 0;
	// Barres nécessaires d'après le contenu de l'image précédente ; l'une peut entraîner l'autre.
	bool v = allowV && (o.bar.alwaysVisible || st.contentH > r.h + 0.5f);
	const bool h = allowH && (o.bar.alwaysVisible || st.overflowW > 0.5f); // mesuré par rapport à la colonne
	if (!v && allowV && h && st.contentH > r.h - t + 0.5f)
		v = true;
	const Rect view{r.x, r.y, std::max(0.f, r.w - (v ? t : 0)), std::max(0.f, r.h - (h ? t : 0))};
	st.offsetY = std::clamp(st.offsetY, 0.f, std::max(0.f, st.contentH - view.h));
	st.offsetX = std::clamp(st.offsetX, 0.f, std::max(0.f, st.overflowW));
	if (!allowH)
		st.offsetX = 0;
	if (MouseIn(r))
		m_wantsMouse = true;
	m_canvas->PushClip(view);
	m_scrollStack.push_back({id, r, view, v, h, o});
	BeginColumn({view.x - st.offsetX, view.y - st.offsetY, view.w, 1e9f});
}

void Context::EndScroll() {
	if (m_scrollStack.empty())
		return;
	const ScrollFrame f = m_scrollStack.back();
	m_scrollStack.pop_back();
	ScrollState &st = m_scroll[f.id];
	if (!m_columns.empty()) {
		const Column &col = m_columns.back();
		st.contentH = std::max(0.f, col.cursorY - m_theme.spacing - (f.view.y - st.offsetY));
		st.overflowW = std::max(0.f, col.maxRight - (col.area.x + col.area.w));
		EndColumn();
	}
	m_canvas->PopClip();
	const float contentW = f.view.w + st.overflowW;
	// Entrée après le contenu : une zone imbriquée a déjà consommé la molette qui la concerne.
	ScrollInput(f.outer, st.offsetX, st.offsetY, f.horizontal ? st.overflowW : 0.f, std::max(0.f, st.contentH - f.view.h), f.options.wheelStep);
	PushId(f.id);
	if (f.vertical)
		ScrollbarV("##v", {f.view.Right(), f.outer.y, f.options.bar.thickness, f.view.h}, st.offsetY, st.contentH, f.view.h, f.options.bar);
	if (f.horizontal)
		ScrollbarH("##h", {f.outer.x, f.view.Bottom(), f.view.w, f.options.bar.thickness}, st.offsetX, contentW, f.view.w, f.options.bar);
	PopId();
	if (f.vertical && f.horizontal)
		m_canvas->FillRect({f.view.Right(), f.view.Bottom(), f.options.bar.thickness, f.options.bar.thickness}, sdl3::Rgba(24, 27, 35));
}

bool Context::ScrollInput(const Rect &area, float &offsetX, float &offsetY, float maxX, float maxY, float wheelStep) {
	if (!MouseIn(area))
		return false;
	float wy = m_wheelY, wx = m_wheelX;
	if (Shift() && wx == 0) { // Maj + molette : horizontal
		wx = -wy;
		wy = 0;
	}
	if (maxY <= 0 && wx == 0) { // zone horizontale seulement (onglets) : la molette défile en X
		wx = -wy;
		wy = 0;
	}
	bool used = false;
	if (wy != 0 && maxY > 0) {
		offsetY = std::clamp(offsetY - wy * wheelStep, 0.f, maxY);
		m_wheelY = 0;
		used = true;
	}
	if (wx != 0 && maxX > 0) {
		offsetX = std::clamp(offsetX + wx * wheelStep, 0.f, maxX);
		m_wheelX = 0;
		if (maxY <= 0 || Shift())
			m_wheelY = 0;
		used = true;
	}
	if (m_touchScrolling && (m_panX != 0 || m_panY != 0)) {
		if (maxX > 0) {
			offsetX = std::clamp(offsetX - m_panX, 0.f, maxX);
			m_panX = 0;
			used = true;
		}
		if (maxY > 0) {
			offsetY = std::clamp(offsetY - m_panY, 0.f, maxY);
			m_panY = 0;
			used = true;
		}
	}
	if (used)
		m_wantsMouse = true;
	return used;
}

bool Context::ScrollbarV(std::string_view id, const Rect &bar, float &offset, float content, float viewport, const ScrollbarOptions &o) {
	return Scrollbar(MakeId(id), bar, true, offset, content, viewport, o);
}
bool Context::ScrollbarH(std::string_view id, const Rect &bar, float &offset, float content, float viewport, const ScrollbarOptions &o) {
	return Scrollbar(MakeId(id), bar, false, offset, content, viewport, o);
}

bool Context::Scrollbar(Id id, const Rect &bar, bool vertical, float &offset, float content, float viewport, const ScrollbarOptions &o) {
	sdl3::Canvas &c = *m_canvas;
	const float before = offset;
	const float maxOffset = std::max(0.f, content - viewport);
	offset = std::clamp(offset, 0.f, maxOffset);
	const float length = vertical ? bar.h : bar.w;
	const float thick = vertical ? bar.w : bar.h;
	const float arrow = o.arrows ? std::min(thick, length / 3.f) : 0.f;
	const float start = (vertical ? bar.y : bar.x) + arrow;
	const float trackLen = std::max(0.f, length - 2 * arrow);
	auto along = [&](float pos, float len) { // rectangle sur l'axe de la barre
		return vertical ? Rect{bar.x, pos, bar.w, len} : Rect{pos, bar.y, len, bar.h};
	};
	ScrollbarState &st = m_scrollbars[id];
	c.FillRect(bar, sdl3::Rgba(24, 27, 35));
	const bool enabled = maxOffset > 0;

	// Boutons fléchés : un pas au clic, puis répétition après 0,35 s de maintien.
	if (o.arrows) {
		const auto arrowButton = [&](const Rect &r, float dir, char32_t icon, uint64_t salt) {
			const auto it = Interact(core::HashCombine(id, salt), r);
			if (it.held && enabled) {
				if (MousePressed(sdl3::MouseButton::Left)) {
					offset += dir * o.step;
					st.repeat = 0.35f;
				} else if ((st.repeat -= m_dt) <= 0) {
					offset += dir * o.step;
					st.repeat = 0.04f;
				}
			}
			if (it.hovered && enabled)
				c.FillRect(r, it.held ? m_theme.widgetActive : m_theme.widgetHover);
			const Color col = !enabled ? sdl3::WithAlpha(m_theme.textDim, 80) : (it.held ? m_theme.accent : (it.hovered ? m_theme.text : m_theme.textDim));
			c.Icon(r, icon, col, thick + 8);
		};
		arrowButton(along(start - arrow, arrow), -1, vertical ? icons::Codepoint(icons::MaterialIcons::ARROW_DROP_UP) : icons::Codepoint(icons::MaterialIcons::ARROW_LEFT), 1);
		arrowButton(along(start + trackLen, arrow), 1, vertical ? icons::Codepoint(icons::MaterialIcons::ARROW_DROP_DOWN) : icons::Codepoint(icons::MaterialIcons::ARROW_RIGHT), 2);
	}

	// Piste et curseur : glisser le curseur, ou cliquer la piste pour l'y amener directement.
	const float thumbLen = enabled ? std::clamp(trackLen * viewport / std::max(content, 1.f), std::min(o.minThumb, trackLen), trackLen) : trackLen;
	auto thumbPos = [&] { return start + (trackLen - thumbLen) * (maxOffset > 0 ? offset / maxOffset : 0.f); };
	const Rect track = along(start, trackLen);
	const auto it = Interact(id, track);
	const float mouse = vertical ? MouseY() : MouseX();
	if (it.held && enabled) {
		if (MousePressed(sdl3::MouseButton::Left)) {
			const float p = thumbPos();
			st.grab = (mouse >= p && mouse < p + thumbLen) ? mouse - p : thumbLen * 0.5f; // hors du curseur : saut
		}
		const float p = std::clamp(mouse - st.grab - start, 0.f, std::max(0.f, trackLen - thumbLen));
		offset = trackLen > thumbLen ? p / (trackLen - thumbLen) * maxOffset : 0.f;
	}
	if (MouseIn(bar) && enabled) { // molette directement sur la barre
		float dummy = 0;
		if (vertical)
			ScrollInput(bar, dummy, offset, 0.f, maxOffset, o.step);
		else
			ScrollInput(bar, offset, dummy, maxOffset, 0.f, o.step);
	}
	offset = std::clamp(offset, 0.f, maxOffset);
	if (enabled) {
		const float p = thumbPos();
		const bool overThumb = it.hovered && mouse >= p && mouse < p + thumbLen;
		const float inset = std::max(2.f, thick * 0.2f);
		Rect thumb = along(p, thumbLen);
		thumb = vertical ? Rect{thumb.x + inset, thumb.y + 1, thumb.w - 2 * inset, thumb.h - 2} : Rect{thumb.x + 1, thumb.y + inset, thumb.w - 2, thumb.h - 2 * inset};
		const float radius = (vertical ? thumb.w : thumb.h) * 0.5f;
		c.FillRoundRect(thumb, radius, it.held ? m_theme.accent : (overThumb ? sdl3::Rgba(120, 130, 150) : sdl3::Rgba(78, 86, 104)));
	}
	return offset != before;
}

// ── Chargement ────────────────────────────────────────────────────────────
void Context::Spinner(const Rect &r, float size, Color col) {
	sdl3::Canvas &c = *m_canvas;
	const Color color = col.a ? col : m_theme.accent;
	const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
	const float thickness = std::max(2.5f, size * 0.11f);
	const float radius = size * 0.5f - thickness * 0.5f;
	const float t = float(m_time);
	constexpr float kPi = 3.14159265f;
	// Anneau de fond, puis arc dont la longueur « respire » pendant qu'il tourne.
	constexpr int kRing = 48;
	for (int i = 0; i < kRing; ++i) {
		const float a0 = 2 * kPi * float(i) / kRing, a1 = 2 * kPi * float(i + 1) / kRing;
		c.Line(cx + radius * std::cos(a0), cy + radius * std::sin(a0), cx + radius * std::cos(a1), cy + radius * std::sin(a1),
			   sdl3::WithAlpha(color, 34), thickness);
	}
	const float head = t * 4.6f;
	const float sweep = kPi * (0.35f + 0.55f * (0.5f + 0.5f * std::sin(t * 2.3f)));
	constexpr int kSegments = 28;
	for (int i = 0; i < kSegments; ++i) {
		const float f0 = float(i) / kSegments, f1 = float(i + 1) / kSegments;
		const float a0 = head - sweep * (1 - f0), a1 = head - sweep * (1 - f1) + 0.02f;
		c.Line(cx + radius * std::cos(a0), cy + radius * std::sin(a0), cx + radius * std::cos(a1), cy + radius * std::sin(a1),
			   sdl3::WithAlpha(color, uint8_t(60 + 195 * f1)), thickness);
	}
	c.FillCircle(cx + radius * std::cos(head), cy + radius * std::sin(head), thickness * 0.5f, color); // bout arrondi
}

void Context::Loader(std::string_view label, float h) {
	const Rect r = Next(h);
	const float lh = label.empty() ? 0.f : m_canvas->LineHeight(m_theme.fontSize - 2) + 6;
	Spinner({r.x, r.y, r.w, r.h - lh}, 34);
	if (!label.empty())
		m_canvas->TextCentered({r.x, r.Bottom() - lh, r.w, lh}, label, m_theme.textDim, m_theme.fontSize - 2);
}

// ── Widgets de base ───────────────────────────────────────────────────────
void Context::Label(std::string_view text, Color c, float size) {
	if (size <= 0)
		size = m_theme.fontSize;
	const float lh = m_canvas->LineHeight(size);
	const Rect r = Next(std::max(lh, m_theme.rowHeight * 0.75f));
	const TextLine line{0, text.size(), r.x, r.y + (r.h - lh) * 0.5f};
	SelectableText(text, std::span(&line, 1), size, sdl3::CanvasFont::Regular, c.a ? c : m_theme.text);
	ExtendContent(r.x + m_canvas->MeasureText(text, size));
}

void Context::Heading(std::string_view text) {
	const float size = m_theme.fontSize + 4;
	const Rect r = Next(m_canvas->LineHeight(size, sdl3::CanvasFont::Bold) + 4);
	const TextLine line{0, text.size(), r.x, r.y + 2};
	SelectableText(text, std::span(&line, 1), size, sdl3::CanvasFont::Bold, m_theme.text);
}

void Context::Wrapped(std::string_view text, Color c, float size) {
	if (size <= 0)
		size = m_theme.fontSize;
	if (m_columns.empty())
		BeginColumn({0, 0, m_canvas->Width(), m_canvas->Height()});
	const float maxW = m_columns.back().area.w;
	// Coupure aux espaces (et aux sauts de ligne) ; chaque ligne garde sa place dans le texte
	// d'origine, pour une sélection continue d'une ligne à l'autre.
	std::vector<TextLine> lines;
	size_t lineStart = 0, lineEnd = 0, pos = 0;
	const auto flush = [&] {
		const Rect r = Next(m_canvas->LineHeight(size) + 1);
		lines.push_back({lineStart, lineEnd - lineStart, r.x, r.y});
	};
	while (pos <= text.size()) {
		size_t wordEnd = text.find_first_of(" \n", pos);
		if (wordEnd == std::string_view::npos)
			wordEnd = text.size();
		if (lineEnd > lineStart && m_canvas->MeasureText(text.substr(lineStart, wordEnd - lineStart), size) > maxW) {
			flush();
			lineStart = pos;
		}
		lineEnd = wordEnd;
		if (wordEnd < text.size() && text[wordEnd] == '\n') {
			flush();
			lineStart = lineEnd = wordEnd + 1;
		}
		pos = wordEnd + 1;
	}
	if (lineEnd > lineStart)
		flush();
	SelectableText(text, lines, size, sdl3::CanvasFont::Regular, c.a ? c : m_theme.text);
}

void Context::Separator() {
	const Rect r = Next(6);
	m_canvas->FillRect({r.x, r.y + 2, r.w, 1}, m_theme.panelBorder);
}

void Context::Section(char32_t icon, std::string_view title) {
	Space(4);
	const Rect r = Next(24);
	sdl3::Canvas &c = *m_canvas;
	float x = r.x;
	if (icon && c.Icon({x, r.y, 18, r.h}, icon, m_theme.accent, 17))
		x += 24;
	const std::string upper = UpperUtf8(title);
	const float w = c.Text(x, r.y + (r.h - c.LineHeight(13, sdl3::CanvasFont::Bold)) * 0.5f, upper, m_theme.accent, 13, sdl3::CanvasFont::Bold);
	c.FillRect({x + w + 10, r.y + r.h * 0.5f, std::max(0.f, r.Right() - x - w - 10), 1}, m_theme.panelBorder);
}

void Context::Icon(const Rect &r, char32_t icon, Color c, float size) { m_canvas->Icon(r, icon, c.a ? c : m_theme.text, size); }

bool Context::IconButton(const Rect &r, char32_t icon, std::string_view tooltip, bool enabled, Color tint) {
	const Id id = MakeId(std::format("##icon{}{}", uint32_t(icon), tooltip));
	const Interaction it = enabled ? Interact(id, r) : Interaction{};
	if (it.hovered || it.held)
		m_canvas->FillRoundRect(r, m_theme.radius, it.held ? m_theme.widgetActive : m_theme.widgetHover);
	const Color c = !enabled ? sdl3::WithAlpha(m_theme.textDim, 90) : (tint.a ? tint : (it.hovered ? m_theme.text : m_theme.textDim));
	if (!m_canvas->Icon(r, icon, c, std::min(r.h - 6, 22.f)))
		m_canvas->TextCentered(r, "?", c, m_theme.fontSize);
	if (it.hovered && !tooltip.empty())
		Tooltip(tooltip);
	return it.leftClicked;
}

bool Context::ButtonAt(const Rect &r, std::string_view label, bool enabled, bool selected) {
	const Id id = MakeId(label);
	const Interaction it = enabled ? Interact(id, r) : Interaction{};
	Color bg = selected ? m_theme.accent : m_theme.widget;
	if (enabled && it.held)
		bg = selected ? m_theme.accentHover : m_theme.widgetActive;
	else if (enabled && it.hovered)
		bg = selected ? m_theme.accentHover : m_theme.widgetHover;
	m_canvas->FillRoundRect(r, m_theme.radius, bg);
	m_canvas->StrokeRoundRect(r, m_theme.radius, it.hovered && !selected ? sdl3::Rgba(86, 96, 118) : m_theme.panelBorder);
	m_canvas->TextCentered(r, Visible(label), !enabled ? m_theme.textDim : (selected ? m_theme.accentText : m_theme.text), m_theme.fontSize,
						   selected ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular);
	return it.leftClicked;
}

bool Context::Button(std::string_view label, bool enabled) { return ButtonAt(Next(), label, enabled); }

bool Context::PrimaryButton(const Rect &r, std::string_view label, bool enabled) {
	const Interaction it = enabled ? Interact(MakeId(label), r) : Interaction{};
	const Color bg = !enabled ? m_theme.widget : (it.held ? m_theme.accent : (it.hovered ? m_theme.accentHover : m_theme.accent));
	m_canvas->FillRoundRect(r, m_theme.radius, bg);
	if (enabled)
		m_canvas->StrokeRoundRect(r, m_theme.radius, sdl3::Rgba(150, 236, 150), 1.5f);
	m_canvas->TextCentered(r, Visible(label), enabled ? m_theme.accentText : m_theme.textDim, m_theme.fontSize, sdl3::CanvasFont::Bold);
	return it.leftClicked;
}

bool Context::DangerButton(const Rect &r, std::string_view label, bool enabled) {
	const Interaction it = enabled ? Interact(MakeId(label), r) : Interaction{};
	m_canvas->FillRoundRect(r, m_theme.radius, it.hovered ? sdl3::Rgba(90, 30, 34) : sdl3::Rgba(46, 24, 28));
	m_canvas->StrokeRoundRect(r, m_theme.radius, m_theme.danger, 1.5f);
	m_canvas->TextCentered(r, Visible(label), enabled ? sdl3::Rgba(255, 120, 120) : m_theme.textDim, m_theme.fontSize);
	return it.leftClicked;
}

bool Context::MenuButton(std::string_view idText, char32_t icon, std::string_view title, std::string_view subtitle, bool primary) {
	const Rect r = Next(primary ? 58 : 48);
	const Interaction it = Interact(MakeId(idText), r);
	sdl3::Canvas &c = *m_canvas;
	if (primary) {
		c.FillRoundRect(r, m_theme.radius + 1, it.hovered ? m_theme.accentHover : m_theme.accent);
		c.StrokeRoundRect(r, m_theme.radius + 1, sdl3::Rgba(160, 240, 160), 1.5f);
	} else {
		c.FillRoundRect(r, m_theme.radius + 1, it.held ? m_theme.widgetActive : (it.hovered ? sdl3::Rgba(40, 46, 58, 245) : sdl3::Rgba(28, 33, 42, 235)));
		c.StrokeRoundRect(r, m_theme.radius + 1, it.hovered ? sdl3::Rgba(90, 100, 122) : sdl3::Rgba(64, 72, 88));
	}
	const Color iconColor = primary ? m_theme.accentText : m_theme.accent;
	const Color titleColor = primary ? m_theme.accentText : m_theme.text;
	const Color subColor = primary ? sdl3::Rgba(24, 60, 24) : m_theme.textDim;
	c.Icon({r.x + 10, r.y, 32, r.h}, icon, iconColor, primary ? 26 : 22);
	const float tx = r.x + 52;
	const float titleSize = primary ? 18.f : 15.5f;
	const float th = c.LineHeight(titleSize, sdl3::CanvasFont::Bold), sh = c.LineHeight(12);
	const float ty = r.y + (r.h - th - (subtitle.empty() ? 0 : sh)) * 0.5f;
	c.Text(tx, ty, title, titleColor, titleSize, sdl3::CanvasFont::Bold);
	if (!subtitle.empty())
		TextIn({tx, ty + th - 1, r.Right() - tx - 10, sh}, subtitle, {subColor, 12, sdl3::CanvasFont::Regular, TextOverflow::MarqueeOnHover});
	return it.leftClicked;
}

bool Context::Checkbox(std::string_view label, bool &value) {
	const Rect r = Next();
	const Interaction it = Interact(MakeId(label), r);
	const float s = 18;
	const Rect box{r.x, r.y + (r.h - s) * 0.5f, s, s};
	m_canvas->FillRoundRect(box, 4, value ? m_theme.accent : (it.hovered ? m_theme.widgetHover : m_theme.widget));
	m_canvas->StrokeRoundRect(box, 4, value ? m_theme.accent : m_theme.panelBorder);
	if (value)
		m_canvas->Icon(box, icons::Codepoint(icons::MaterialIcons::CHECK), m_theme.accentText, 16);
	m_canvas->Text(r.x + s + 10, r.y + (r.h - m_canvas->LineHeight(m_theme.fontSize)) * 0.5f, Visible(label), m_theme.text, m_theme.fontSize);
	if (it.leftClicked)
		value = !value;
	return it.leftClicked;
}

bool Context::Toggle(const Rect &area, std::string_view id, bool &value) {
	const Rect r{area.Right() - 44, area.y + (area.h - 22) * 0.5f, 44, 22};
	const Interaction it = Interact(MakeId(id), r);
	if (it.leftClicked)
		value = !value;
	m_canvas->FillRoundRect(r, 11, value ? (it.hovered ? m_theme.accentHover : m_theme.accent) : (it.hovered ? m_theme.widgetActive : m_theme.widgetHover));
	m_canvas->FillCircle(value ? r.Right() - 11 : r.x + 11, r.y + 11, 8, value ? sdl3::Rgba(20, 40, 20) : sdl3::Rgba(214, 220, 230));
	return it.leftClicked;
}

bool Context::SliderAt(const Rect &r, std::string_view id, float &value, float min, float max) {
	const Rect hit{r.x - 6, r.y, r.w + 12, r.h};
	const Interaction it = Interact(MakeId(id), hit);
	bool changed = false;
	if (it.held && max > min) {
		const float t = std::clamp((MouseX() - r.x) / r.w, 0.f, 1.f);
		const float nv = min + (max - min) * t;
		changed = nv != value;
		value = nv;
	}
	// Molette : hors zone de défilement, ou avec Ctrl (sinon elle fait défiler la page).
	if (it.hovered && m_wheelY != 0 && max > min && (m_scrollStack.empty() || Ctrl())) {
		value = std::clamp(value + (max - min) * 0.02f * m_wheelY, min, max);
		m_wheelY = 0;
		changed = true;
	}
	const float t = max > min ? std::clamp((value - min) / (max - min), 0.f, 1.f) : 0.f;
	const float cy = r.y + r.h * 0.5f;
	m_canvas->FillRoundRect({r.x, cy - 2.5f, r.w, 5}, 2.5f, m_theme.widgetHover);
	m_canvas->FillRoundRect({r.x, cy - 2.5f, r.w * t, 5}, 2.5f, m_theme.accent);
	m_canvas->FillCircle(r.x + r.w * t, cy, it.held || it.hovered ? 8.f : 7.f, sdl3::Rgba(236, 240, 246));
	return changed;
}

bool Context::SliderFloat(std::string_view label, float &value, float min, float max, std::string_view format) {
	const Rect r = Next();
	const float labelW = std::min(r.w * 0.42f, 190.f);
	// Libellé borné à sa colonne : défilement au survol s'il est trop long.
	TextIn({r.x, r.y, labelW - 6, r.h}, Visible(label), {m_theme.text, m_theme.fontSize, sdl3::CanvasFont::Regular, TextOverflow::MarqueeOnHover});
	const std::string text = FormatValue(format, value);
	const float valueW = 64;
	m_canvas->Text(r.x + labelW, r.y + (r.h - m_canvas->LineHeight(m_theme.fontSize - 1)) * 0.5f, text, m_theme.textDim, m_theme.fontSize - 1);
	return SliderAt({r.x + labelW + valueW, r.y, r.w - labelW - valueW - 8, r.h}, label, value, min, max);
}

bool Context::SliderInt(std::string_view label, int &value, int min, int max) {
	float f = float(value);
	const bool changed = SliderFloat(label, f, float(min), float(max), "{:.0f}");
	const int nv = static_cast<int>(std::lround(f));
	const bool really = changed && nv != value;
	value = nv;
	return really;
}

bool Context::TextInput(std::string_view idText, std::string &value, bool *submitted, std::string_view placeholder) {
	return TextInputAt(Next(), idText, value, submitted, placeholder);
}

bool Context::DragNumber(const Rect &r, std::string_view idText, float &value, float speed, std::string_view format, Color accent, float min,
						 float max) {
	const Id id = MakeId(idText);
	sdl3::Canvas &c = *m_canvas;
	const float before = value;
	auto commit = [&] {
		float parsed = 0;
		std::string text = m_numberText;
		std::ranges::replace(text, ',', '.');
		const char *first = text.data();
		while (first < text.data() + text.size() && *first == ' ')
			++first;
		if (first < text.data() + text.size() && *first == '+')
			++first;
		const auto [ptr, ec] = std::from_chars(first, text.data() + text.size(), parsed);
		if (ec == std::errc() && ptr != first)
			value = std::clamp(parsed, min, max);
		m_numberEdit = 0;
	};
	if (m_numberEdit == id) { // saisie clavier
		PushId(id);
		const Id textId = MakeId("##numedit");
		if (KeyPressed(sdl3::keyboard::Key::ESCAPE)) {
			m_numberEdit = 0;
			m_focus = 0;
		} else if (m_focus != textId) {
			commit(); // clic ailleurs : la saisie est validée
		} else {
			bool submitted = false;
			TextInputAt(r, "##numedit", m_numberText, &submitted);
			if (submitted) {
				commit();
				m_focus = 0;
			}
		}
		PopId();
		if (m_numberEdit == id)
			return false;
	}
	const Interaction it = Interact(id, r);
	if (it.held && MousePressed(sdl3::MouseButton::Left)) {
		m_dragStartX = MouseX();
		m_dragStartValue = value;
		m_dragMoved = false;
	} else if (it.held) {
		const float dx = MouseX() - m_dragStartX;
		if (std::fabs(dx) > 2.f)
			m_dragMoved = true;
		if (m_dragMoved)
			value = std::clamp(m_dragStartValue + dx * speed * (Shift() ? 0.1f : 1.f), min, max);
	}
	if (it.leftClicked && !m_dragMoved) { // clic simple : saisie au clavier
		m_numberEdit = id;
		m_numberText = FormatValue(format, value);
		PushId(id);
		const Id textId = MakeId("##numedit");
		PopId();
		m_focus = textId;
		m_textEdits[textId].SelectAll(m_numberText); // la frappe remplace la valeur
	}
	c.FillRoundRect(r, m_theme.radius - 2, it.held ? m_theme.widgetActive : (it.hovered ? m_theme.widgetHover : m_theme.widget));
	if (accent.a) { // repère d'axe dans le coin supérieur droit
		c.FillRect({r.Right() - 8, r.y + 1, 7, 2}, accent);
		c.FillRect({r.Right() - 3, r.y + 1, 2, 7}, accent);
	}
	const std::string text = FormatValue(format, value);
	const float size = m_theme.fontSize - 2;
	c.PushClip(r);
	c.TextCentered(r, text, m_theme.text, size);
	c.PopClip();
	if (it.hovered && !it.held)
		Tooltip("glisser pour modifier (Maj : précis) · clic : saisir");
	if (it.held && m_dragMoved)
		m_wantsMouse = true;
	return value != before;
}

bool Context::DragFloats(std::string_view label, std::span<float> values, float speed, std::string_view format, float min, float max) {
	static constexpr Color kAxes[] = {Color{226, 72, 72, 255}, Color{86, 196, 92, 255}, Color{76, 140, 236, 255}, Color{150, 150, 160, 255}};
	PushId(label);
	const float lh = m_canvas->LineHeight(12);
	const Rect lr = Next(lh);
	m_canvas->Text(lr.x + 2, lr.y, Visible(label), m_theme.textDim, 12);
	const size_t n = std::min<size_t>(values.size(), 4);
	const Rect line = Next(26);
	const float gap = 4;
	const float w = (line.w - gap * float(n - 1)) / float(std::max<size_t>(n, 1));
	bool changed = false;
	for (size_t i = 0; i < n; ++i)
		changed |= DragNumber({line.x + float(i) * (w + gap), line.y, w, line.h}, std::format("##v{}", i), values[i], speed, format, kAxes[i], min, max);
	PopId();
	return changed;
}

bool Context::TextInputAt(const Rect &r, std::string_view idText, std::string &value, bool *submitted, std::string_view placeholder) {
	return EditText(r, MakeId(idText), value, false, submitted, placeholder);
}

bool Context::TextArea(const Rect &r, std::string_view idText, std::string &value, std::string_view placeholder) {
	return EditText(r, MakeId(idText), value, true, nullptr, placeholder);
}

TextEdit *Context::EditState(std::string_view idText) { return m_textEdits.Find(MakeId(idText)); }

CodePalette Context::CodeColors() const noexcept {
	CodePalette p;
	p.background = sdl3::Rgba(uint8_t(m_theme.background.r * 0.8f), uint8_t(m_theme.background.g * 0.8f), uint8_t(m_theme.background.b * 0.8f));
	p.text = m_theme.text;
	p.gutter = m_theme.title;
	p.gutterText = sdl3::WithAlpha(m_theme.textDim, 160);
	p.gutterCurrent = m_theme.text;
	p.selection = sdl3::WithAlpha(m_theme.accent, 90);
	p.caret = m_theme.accentHover;
	p.error = m_theme.danger;
	p.comment = sdl3::WithAlpha(m_theme.textDim, 200);
	return p;
}

bool Context::CodeEditor(const Rect &r, std::string_view idText, std::string &text, const CodeEditorOptions &options) {
	sdl3::Canvas &c = *m_canvas;
	const Id id = MakeId(idText);
	TextEdit &ed = m_textEdits[id];
	ed.multiline = true;
	ed.code = true;
	ed.readOnly = options.readOnly;
	ed.Clamp(text);
	const float size = options.fontSize > 0.f ? options.fontSize : m_theme.fontSize - 1.f;
	CodeLayout layout = LayoutCode(c, r, text, ed, options, size);
	const Interaction it = Interact(id, r);
	const bool inText = layout.TextArea().Contains(MouseX(), MouseY());
	if (it.hovered && MousePressed(sdl3::MouseButton::Left)) {
		const bool doubleClick = m_textClickId == id && m_time - m_textClickTime < 0.35;
		m_textClickId = id;
		m_textClickTime = doubleClick ? -1.0 : m_time;
		m_focus = id;
		if (inText)
			ed.MousePress(text, CodeHit(c, layout, text, MouseX(), MouseY()), Shift(), doubleClick);
		else { // clic dans la marge : la ligne entière
			const size_t at = CodeHit(c, layout, text, layout.TextArea().x + 1.f, MouseY());
			ed.SetCursor(TextEdit::LineStart(text, at), false);
			ed.cursor = std::min(text.size(), TextEdit::LineEnd(text, at) + 1);
		}
		m_caretTime = m_time;
	} else if (it.held && m_focus == id && inText) {
		ed.MouseDrag(CodeHit(c, layout, text, MouseX(), MouseY()));
	}
	const bool focused = m_focus == id;
	bool changed = false;
	if (focused) {
		const int pageLines = std::max(1, int((r.h - 8.f) / layout.lineHeight));
		const auto measure = [&](std::string_view s) { return c.MeasureText(s, size, sdl3::CanvasFont::Mono); };
		const TextEditResult res = ed.HandleKeys(text, *m_events, measure, pageLines);
		changed = res.changed;
		if (res.changed || res.moved) {
			m_caretTime = m_time;
			ScrollCodeToCaret(c, LayoutCode(c, r, text, ed, options, size), text, ed);
		}
		if (res.cancelled)
			m_focus = 0;
	}
	if (it.hovered && (WheelY() != 0.f || WheelX() != 0.f)) {
		size_t lines = 1;
		for (char ch : text)
			lines += ch == '\n' ? 1u : 0u;
		const float dy = Shift() ? 0.f : -WheelY() * layout.lineHeight * 3.f, dx = Shift() ? -WheelY() * 40.f : -WheelX() * 40.f;
		ed.scrollY = std::clamp(ed.scrollY + dy, 0.f, std::max(0.f, float(lines) * layout.lineHeight - r.h + 8.f));
		ed.scrollX = std::max(0.f, ed.scrollX + dx);
	}
	layout = LayoutCode(c, r, text, ed, options, size);
	c.PushClip(r);
	DrawCode(c, layout, text, ed, options, CodeColors(), focused, focused && std::fmod(m_time - m_caretTime, 1.0) < 0.6);
	c.PopClip();
	c.StrokeRoundRect(r, 3.f, focused ? m_theme.accent : m_theme.panelBorder, focused ? 1.5f : 1.f);
	return changed;
}

bool Context::EditText(const Rect &r, Id id, std::string &value, bool multiline, bool *submitted, std::string_view placeholder) {
	sdl3::Canvas &c = *m_canvas;
	const float size = m_theme.fontSize, lh = c.LineHeight(size);
	TextEdit &ed = m_textEdits[id];
	ed.multiline = multiline;
	ed.Clamp(value);
	const auto measure = [&](std::string_view s) { return c.MeasureText(s, size); };
	const float padX = 10.f, padY = multiline ? 6.f : (r.h - lh) * 0.5f;
	const auto hit = [&] { return TextEdit::HitTest(value, MouseX() - (r.x + padX - ed.scrollX), MouseY() - (r.y + padY - ed.scrollY), lh, measure); };
	const Interaction it = Interact(id, r);
	if (it.hovered && MousePressed(sdl3::MouseButton::Left)) {
		// Appui : focus et curseur sous la souris (Maj : étend) ; double-clic : le mot.
		const bool doubleClick = m_textClickId == id && m_time - m_textClickTime < 0.35;
		m_textClickId = id;
		m_textClickTime = doubleClick ? -1.0 : m_time;
		m_focus = id;
		ed.MousePress(value, hit(), Shift(), doubleClick);
		m_caretTime = m_time;
	} else if (it.held && m_focus == id) {
		ed.MouseDrag(hit()); // glissé : sélection
	}
	const bool focused = m_focus == id;
	bool changed = false;
	if (submitted)
		*submitted = false;
	if (focused) {
		const int pageLines = std::max(1, int((r.h - 2.f * padY) / lh));
		const TextEditResult res = ed.HandleKeys(value, *m_events, measure, pageLines);
		changed = res.changed;
		if (res.changed || res.moved)
			m_caretTime = m_time;
		if (res.submitted && submitted)
			*submitted = true;
		if (res.cancelled)
			m_focus = 0;
		// Le curseur reste visible : défilement horizontal (et vertical en multi-ligne).
		const auto [cx, cy] = ed.CaretPosition(value, lh, measure);
		const float viewW = r.w - 2.f * padX, viewH = r.h - 2.f * padY;
		ed.scrollX = std::clamp(ed.scrollX, std::max(0.f, cx - viewW + 2.f), cx);
		if (multiline)
			ed.scrollY = std::clamp(ed.scrollY, std::max(0.f, cy + lh - viewH), cy);
	} else if (!multiline) {
		ed.scrollX = 0.f;
		ed.Collapse();
	}
	if (multiline && it.hovered && WheelY() != 0.f) {
		size_t lines = 1;
		for (char ch : value)
			lines += ch == '\n' ? 1 : 0;
		ed.scrollY = std::clamp(ed.scrollY - WheelY() * lh * 3.f, 0.f, std::max(0.f, float(lines) * lh - (r.h - 2.f * padY)));
	}
	c.FillRoundRect(r, m_theme.radius, focused ? sdl3::Rgba(14, 16, 21) : (it.hovered ? sdl3::Rgba(20, 23, 30) : sdl3::Rgba(16, 18, 24)));
	c.StrokeRoundRect(r, m_theme.radius, focused ? m_theme.accent : m_theme.panelBorder, focused ? 1.5f : 1.f);
	c.PushClip(r.Inset(2));
	if (value.empty() && !focused)
		c.Text(r.x + padX, r.y + padY, placeholder, m_theme.textDim, size);
	else {
		const bool caretOn = focused && std::fmod(m_time - m_caretTime, 1.0) < 0.6; // clignotement, fixe pendant la frappe
		ed.Draw(c, value, r.x + padX - ed.scrollX, r.y + padY - ed.scrollY, size, sdl3::CanvasFont::Regular, m_theme.text, sdl3::WithAlpha(m_theme.accent, focused ? 110 : 60),
				m_theme.accentHover, caretOn);
	}
	c.PopClip();
	return changed || (submitted && *submitted);
}

void Context::SelectableText(std::string_view text, std::span<const TextLine> lines, float size, sdl3::CanvasFont font, Color color) {
	sdl3::Canvas &c = *m_canvas;
	const float lh = c.LineHeight(size, font);
	const auto measure = [&](std::string_view s) { return c.MeasureText(s, size, font); };
	// Identité : le texte et son rang parmi les textes identiques de l'image.
	const uint64_t textHash = core::HashString(text);
	int &serial = m_staticSerial[textHash];
	const Id id = core::HashCombine(MakeId("##static"), core::HashCombine(textHash, uint64_t(serial++)));
	StaticSelection &sel = m_staticSel;
	// Position dans le texte du point de la souris (ligne la plus proche).
	const auto hit = [&]() -> size_t {
		size_t best = 0;
		float bestD = 1e30f;
		for (size_t i = 0; i < lines.size(); ++i) {
			const float d = std::fabs(MouseY() - (lines[i].y + lh * 0.5f));
			if (d < bestD)
				bestD = d, best = i;
		}
		const TextLine &l = lines[best];
		return l.start + TextEdit::HitTest(text.substr(l.start, l.length), MouseX() - l.x, 0.f, lh, measure);
	};
	if (!lines.empty() && MousePressed(sdl3::MouseButton::Left) && m_active == 0 && !m_staticPressed) {
		bool inside = false;
		for (const TextLine &l : lines)
			inside |= MouseIn({l.x - 2.f, l.y, measure(text.substr(l.start, l.length)) + 4.f, lh});
		if (inside) {
			const bool doubleClick = sel.id == id && m_textClickId == id && m_time - m_textClickTime < 0.35;
			m_textClickId = id;
			m_textClickTime = doubleClick ? -1.0 : m_time;
			if (sel.id != id) {
				sel.id = id;
				sel.edit = TextEdit{};
				sel.edit.readOnly = true;
				sel.edit.multiline = lines.size() > 1;
			}
			sel.text.assign(text);
			sel.edit.MousePress(sel.text, hit(), Shift(), doubleClick);
			sel.dragging = true;
			m_staticPressed = true;
		}
	}
	if (sel.id == id) {
		sel.text.assign(text);
		sel.edit.Clamp(sel.text);
		if (sel.dragging && MouseDown())
			sel.edit.MouseDrag(hit());
		const auto [a, b] = sel.edit.Range();
		for (const TextLine &l : lines) { // fond de la sélection, ligne par ligne
			const size_t s = std::max(a, l.start), e = std::min(b, l.start + l.length);
			if (s < e) {
				const float xa = measure(text.substr(l.start, s - l.start)), xb = measure(text.substr(l.start, e - l.start));
				c.FillRect({l.x + xa, l.y, xb - xa, lh}, sdl3::WithAlpha(m_theme.accent, 110));
			}
		}
	}
	for (const TextLine &l : lines)
		c.Text(l.x, l.y, text.substr(l.start, l.length), color, size, font);
}

bool Context::Selectable(std::string_view label, bool selected, Color color) {
	const Rect r = Next(m_theme.rowHeight - 2);
	const Interaction it = Interact(MakeId(label), r);
	if (selected)
		m_canvas->FillRoundRect(r, m_theme.radius, sdl3::Rgba(36, 72, 40, 230));
	else if (it.hovered)
		m_canvas->FillRoundRect(r, m_theme.radius, m_theme.widgetHover);
	const float w = m_canvas->Text(r.x + 10, r.y + (r.h - m_canvas->LineHeight(m_theme.fontSize)) * 0.5f, Visible(label),
								   color.a ? color : m_theme.text, m_theme.fontSize);
	ExtendContent(r.x + 10 + w + 10);
	return it.leftClicked;
}

int Context::Tabs(std::string_view idText, std::span<const std::string_view> labels, int selected) {
	const Rect line = Next();
	PushId(idText);
	float x = line.x;
	for (size_t i = 0; i < labels.size(); ++i) {
		const float w = m_canvas->MeasureText(labels[i], m_theme.fontSize) + 28;
		if (ButtonAt({x, line.y, w, line.h}, std::format("{}##tab{}", labels[i], i), true, int(i) == selected))
			selected = int(i);
		x += w + 4;
	}
	PopId();
	return selected;
}

bool Context::Choice(const Rect &r, std::string_view id, int &index, std::span<const std::string_view> options) {
	if (options.empty())
		return false;
	PushId(id);
	sdl3::Canvas &c = *m_canvas;
	c.FillRoundRect(r, m_theme.radius, sdl3::Rgba(16, 18, 24));
	c.StrokeRoundRect(r, m_theme.radius, m_theme.panelBorder);
	bool changed = false;
	const int n = int(options.size());
	if (IconButton({r.x + 2, r.y + 2, r.h - 4, r.h - 4}, icons::Codepoint(icons::MaterialIcons::CHEVRON_LEFT), "")) {
		index = (index + n - 1) % n;
		changed = true;
	}
	if (IconButton({r.Right() - r.h + 2, r.y + 2, r.h - 4, r.h - 4}, icons::Codepoint(icons::MaterialIcons::CHEVRON_RIGHT), "")) {
		index = (index + 1) % n;
		changed = true;
	}
	index = std::clamp(index, 0, n - 1);
	const Id self = MakeId("##choice");
	ChoicePopup &popup = m_choicePopup;
	if (popup.id == self && popup.picked >= 0) { // choix fait dans la liste (image précédente)
		changed |= popup.picked != index;
		index = std::clamp(popup.picked, 0, n - 1);
		popup.id = 0;
	}
	const Rect value{r.x + r.h, r.y, r.w - 2 * r.h, r.h};
	const Interaction it = Interact(self, value);
	if (it.hovered)
		c.FillRoundRect(value.Inset(0, 3), m_theme.radius, sdl3::WithAlpha(m_theme.widgetHover, 160));
	c.TextCentered(value, options[size_t(index)], m_theme.text, m_theme.fontSize);
	if (it.leftClicked) {
		if (popup.id == self)
			popup.id = 0; // second clic : referme
		else {
			const float itemH = m_theme.rowHeight - 4.f;
			const int visible = std::min(n, 12);
			float w = r.w;
			for (const std::string_view o : options)
				w = std::max(w, m_canvas->MeasureText(o, m_theme.fontSize) + 28.f);
			Rect box{r.x, r.Bottom() + 2.f, std::min(w, m_canvas->Width() - 8.f), float(visible) * itemH};
			if (box.Bottom() > m_canvas->Height() - 4.f) // pas de place dessous : au-dessus
				box.y = r.y - 2.f - box.h;
			box.x = std::clamp(box.x, 4.f, std::max(4.f, m_canvas->Width() - box.w - 4.f));
			popup = ChoicePopup{self, box, std::vector<std::string>(options.begin(), options.end()), index, -1,
								std::clamp(index - visible / 2, 0, std::max(0, n - visible)), m_frame};
		}
	}
	PopId();
	return changed;
}

void Context::ProgressBar(float fraction, std::string_view overlay) {
	const Rect r = Next(m_theme.rowHeight - 8);
	m_canvas->FillRoundRect(r, r.h * 0.5f, m_theme.widget);
	m_canvas->FillRoundRect({r.x, r.y, r.w * std::clamp(fraction, 0.f, 1.f), r.h}, r.h * 0.5f, m_theme.accent);
	if (!overlay.empty())
		m_canvas->TextCentered(r, overlay, m_theme.text, m_theme.fontSize - 2);
}

void Context::Plot(std::string_view label, std::span<const float> values, float min, float max, float h, Color color) {
	const Rect r = Next(h);
	sdl3::Canvas &c = *m_canvas;
	c.FillRoundRect(r, m_theme.radius, sdl3::Rgba(12, 14, 19, 230));
	c.StrokeRoundRect(r, m_theme.radius, m_theme.panelBorder);
	if (values.size() >= 2) {
		if (min == max) {
			auto [lo, hi] = std::ranges::minmax(values);
			min = std::min(0.f, lo);
			max = hi > min ? hi * 1.1f : min + 1.f;
		}
		std::vector<float> pts;
		pts.reserve(values.size() * 2);
		for (size_t i = 0; i < values.size(); ++i) {
			const float t = std::clamp((values[i] - min) / (max - min), 0.f, 1.f);
			pts.push_back(r.x + 2 + (r.w - 4) * float(i) / float(values.size() - 1));
			pts.push_back(r.y + r.h - 3 - (r.h - 6) * t);
		}
		c.Polyline(pts.data(), values.size(), color.a ? color : m_theme.accent, 1.5f);
	}
	if (!label.empty())
		c.Text(r.x + 8, r.y + 4, label, m_theme.textDim, m_theme.fontSize - 2);
}

void Context::Badge(const Rect &r, std::string_view text, Color col) {
	m_canvas->FillRoundRect(r, r.h * 0.5f, sdl3::WithAlpha(col, 40));
	m_canvas->StrokeRoundRect(r, r.h * 0.5f, sdl3::WithAlpha(col, 200));
	m_canvas->TextCentered(r, text, col, 12, sdl3::CanvasFont::Bold);
}

void Context::Tooltip(std::string_view text) { m_tooltip = std::string(text); }

// ── Texte borné (débordement : coupe, « … », défilement) ──────────────────
void Context::TextIn(const Rect &r, std::string_view text, const TextStyle &style) {
	sdl3::Canvas &c = *m_canvas;
	const float size = style.size > 0 ? style.size : m_theme.fontSize;
	const Color color = style.color.a ? style.color : m_theme.text;
	const float y = r.y + (r.h - c.LineHeight(size, style.font)) * 0.5f;
	const float width = c.MeasureText(text, size, style.font);
	if (width <= r.w || r.w <= 4) {
		const TextLine line{0, text.size(), r.x, y};
		SelectableText(text, std::span(&line, 1), size, style.font, color);
		return;
	}
	const bool hovered = MouseIn(r);
	TextOverflow mode = style.overflow;
	const uint64_t key = core::HashCombine(core::HashString(text), core::HashCombine(uint64_t(r.x * 4), uint64_t(r.y * 4)));
	if (mode == TextOverflow::MarqueeOnHover) {
		if (hovered) {
			mode = TextOverflow::Marquee;
			m_hoverStart.TryEmplace(key, m_time);
		} else {
			m_hoverStart.Erase(key);
			mode = TextOverflow::Ellipsis;
		}
	}
	switch (mode) {
	case TextOverflow::Ellipsis:
		c.Text(r.x, y, c.Ellipsize(text, r.w, size, style.font), color, size, style.font);
		if (hovered)
			Tooltip(text);
		return;
	case TextOverflow::Clip:
		c.PushClip(r);
		c.Text(r.x, y, text, color, size, style.font);
		c.PopClip();
		return;
	default: break;
	}
	// Défilement aller-retour : pause, glissement vers la fin, pause, retour.
	constexpr double kPause = 1.2, kSpeed = 38.0; // secondes, pixels logiques / s
	const double overflow = double(width - r.w) + 6.0;
	const double travel = overflow / kSpeed;
	const double cycle = 2.0 * (kPause + travel);
	const double* started = m_hoverStart.Find(key);
	const double t = std::fmod(m_time - (started ? *started : 0.0), cycle);
	double offset;
	if (t < kPause)
		offset = 0;
	else if (t < kPause + travel)
		offset = (t - kPause) * kSpeed;
	else if (t < 2 * kPause + travel)
		offset = overflow;
	else
		offset = overflow - (t - 2 * kPause - travel) * kSpeed;
	c.PushClip(r);
	c.Text(r.x - float(offset), y, text, color, size, style.font);
	c.PopClip();
}

// ── Barre d'onglets ───────────────────────────────────────────────────────
TabBarResult Context::TabBar(std::string_view idText, const Rect &area, std::span<const TabItem> tabs, int active, const TabBarOptions &opt) {
	TabBarResult result;
	sdl3::Canvas &c = *m_canvas;
	const Id barId = MakeId(idText);
	TabBarState &st = m_tabBars[barId];
	const float th = opt.tabHeight;
	const Rect strip{area.x, area.y, area.w, th};
	c.FillRect({area.x, area.y, area.w, area.h}, sdl3::Rgba(20, 23, 30));
	if (tabs.empty()) {
		st = {};
		return result;
	}
	const float pad = 10, iconW = 20, stateW = 16, buttonW = 22, gap = 2;
	const size_t n = tabs.size();
	auto labelSize = [&](const TabItem &t) { return t.labelStyle.size > 0 ? t.labelStyle.size : m_theme.fontSize - 2; };
	auto stateCount = [&](const TabItem &t) { return t.statusIcons.size() + ((opt.modifiedDot && t.modified) ? 1 : 0); };
	auto chromeW = [&](const TabItem &t) {
		return pad * 2 + (t.icon ? iconW + 4 : 0) + float(stateCount(t)) * stateW + float(t.buttons.size()) * buttonW + 4;
	};
	std::vector<float> widths(n), pos(n);
	float total = 0;
	for (size_t i = 0; i < n; ++i) {
		const float natural = chromeW(tabs[i]) + c.MeasureText(tabs[i].label, labelSize(tabs[i]), tabs[i].labelStyle.font);
		widths[i] = std::clamp(natural, opt.minTabWidth, opt.maxTabWidth);
		pos[i] = total;
		total += widths[i] + gap;
	}
	total -= gap;
	const float maxScroll = std::max(0.f, total - area.w);
	const bool overflowing = maxScroll > 0;

	// Défilement : molette / pavé tactile / glissé tactile sur la barre, onglet actif ramené
	// dans la vue quand il change.
	{
		float unused = 0;
		ScrollInput(area, st.scroll, unused, maxScroll, 0.f, opt.scrollbar.step);
	}
	if (active >= 0 && size_t(active) < n && tabs[size_t(active)].id != st.lastActive) {
		st.lastActive = tabs[size_t(active)].id;
		const float a0 = pos[size_t(active)], a1 = a0 + widths[size_t(active)];
		if (a0 < st.scroll)
			st.scroll = a0;
		else if (a1 > st.scroll + area.w)
			st.scroll = a1 - area.w;
	}

	// Glisser-déposer : l'onglet saisi suit la souris, les autres s'écartent.
	if (st.pressIndex >= int(n))
		st = {st.scroll, st.lastActive};
	if (st.pressIndex >= 0 && MouseDown(sdl3::MouseButton::Left) && opt.reorderable && !st.dragging && std::fabs(MouseX() - st.pressX) > 6.f)
		st.dragging = true;
	int target = -1;
	float dragX = 0;
	if (st.dragging) {
		// Défilement automatique près des bords.
		if (MouseX() < area.x + 28)
			st.scroll -= 400.f * m_dt;
		else if (MouseX() > area.Right() - 28)
			st.scroll += 400.f * m_dt;
		st.scroll = std::clamp(st.scroll, 0.f, maxScroll);
		const size_t d = size_t(st.pressIndex);
		dragX = std::clamp(MouseX() - st.grabOffset, area.x - widths[d] * 0.5f, area.Right() - widths[d] * 0.5f);
		const float centre = dragX - area.x + st.scroll + widths[d] * 0.5f;
		target = 0;
		for (size_t i = 0; i < n; ++i)
			if (i != d && pos[i] + widths[i] * 0.5f < centre)
				++target;
	}
	st.scroll = std::clamp(st.scroll, 0.f, maxScroll);

	auto drawTab = [&](size_t i, float x, bool floating) {
		const TabItem &t = tabs[i];
		const Rect r{x, area.y + 2, widths[i], th - 2};
		const bool isActive = int(i) == active;
		const bool hovered = MouseIn(r) && !st.dragging;
		PushId(t.id);
		// Fond, puis boutons (ils captent le clic avant l'onglet).
		Color bg = isActive ? sdl3::Rgba(14, 16, 21) : (hovered ? m_theme.widgetHover : sdl3::Rgba(28, 32, 41));
		if (floating)
			bg = sdl3::Rgba(44, 50, 64, 245);
		c.FillRoundRect({r.x, r.y, r.w, r.h + 4}, 5, bg);
		if (isActive)
			c.FillRect({r.x + 2, r.y, r.w - 4, 2}, m_theme.accent);
		if (floating)
			c.StrokeRoundRect({r.x, r.y, r.w, r.h + 4}, 5, m_theme.accent);
		float right = r.Right() - pad + 4;
		for (size_t b = t.buttons.size(); b-- > 0;) {
			right -= buttonW;
			const Rect br{right, r.y + (r.h - buttonW) * 0.5f, buttonW, buttonW};
			if (!floating && IconButton(br, t.buttons[b].icon, t.buttons[b].tooltip, true, isActive || hovered ? Color{} : sdl3::WithAlpha(m_theme.textDim, 160))) {
				result.buttonTab = int(i);
				result.button = int(b);
			}
		}
		const auto drawState = [&](char32_t icon, Color col) {
			right -= stateW;
			c.Icon({right, r.y, stateW, r.h}, icon, col, icon == icons::Codepoint(icons::MaterialIcons::CIRCLE) || icon == icons::Codepoint(icons::MaterialIcons::FIBER_MANUAL_RECORD) ? 10.f : 14.f);
		};
		for (size_t k = t.statusIcons.size(); k-- > 0;)
			drawState(t.statusIcons[k], m_theme.textDim);
		if (opt.modifiedDot && t.modified)
			drawState(icons::Codepoint(icons::MaterialIcons::CIRCLE), isActive ? m_theme.text : m_theme.textDim);
		float left = r.x + pad;
		if (t.icon) {
			c.Icon({left, r.y, iconW, r.h}, t.icon, t.iconColor.a ? t.iconColor : m_theme.info, 16);
			left += iconW + 4;
		}
		TextStyle style = t.labelStyle;
		if (!style.color.a)
			style.color = isActive ? m_theme.text : m_theme.textDim;
		if (style.size <= 0)
			style.size = labelSize(t);
		TextIn({left, r.y, std::max(0.f, right - left - 4), r.h}, t.label, style);
		if (!floating) {
			const auto it = Interact(MakeId("##tab"), r);
			if (it.held && MousePressed(sdl3::MouseButton::Left)) {
				st.pressIndex = int(i);
				st.pressX = MouseX();
				st.grabOffset = MouseX() - r.x;
			}
			if (it.leftClicked && !st.dragging)
				result.activated = int(i);
			if (hovered && !t.tooltip.empty())
				Tooltip(t.tooltip);
		}
		PopId();
	};

	c.PushClip(strip);
	const float base = area.x - st.scroll;
	if (st.dragging) {
		const size_t d = size_t(st.pressIndex);
		float x = 0;
		int slot = 0;
		for (size_t i = 0; i < n; ++i) {
			if (i == d)
				continue;
			if (slot == target)
				x += widths[d] + gap; // place libre pour l'onglet déposé
			drawTab(i, base + x, false);
			x += widths[i] + gap;
			++slot;
		}
		drawTab(d, dragX, true);
		Interact(MakeId("##drag"), {area.x, area.y, area.w, area.h}); // conserve la capture de la souris
		m_wantsMouse = true;
	} else {
		for (size_t i = 0; i < n; ++i)
			if (base + pos[i] + widths[i] >= area.x && base + pos[i] <= area.Right())
				drawTab(i, base + pos[i], false);
	}
	c.PopClip();

	// Fin du glisser : déplacement demandé à l'appelant.
	if (!MouseDown(sdl3::MouseButton::Left)) {
		if (st.dragging && target >= 0) {
			result.moveFrom = st.pressIndex;
			result.moveTo = target;
			st.lastActive.clear(); // l'onglet actif peut avoir bougé : le garder visible
		}
		st.dragging = false;
		st.pressIndex = -1;
	}

	// Barre de défilement horizontale sous les onglets (widget commun `ScrollbarH`).
	if (overflowing) {
		PushId(barId);
		ScrollbarH("##tabscroll", {area.x, area.y + th + 1, area.w, opt.scrollbar.thickness}, st.scroll, total, area.w, opt.scrollbar);
		PopId();
	}
	return result;
}

// ── Arborescence ───────────────────────────────────────────────────────────
TreeResult Context::Tree(std::string_view idText, const std::vector<TreeNode> &roots, std::set<std::string> &expanded, std::string &selected,
						 const TreeOptions &opt) {
	sdl3::Canvas &c = *m_canvas;
	TreeResult result;
	PushId(idText);
	const Id treeId = MakeId("##tree");

	// F2 : renommer le nœud sélectionné.
	if (KeyPressed(sdl3::keyboard::Key::F2) && !selected.empty())
		result.renameTarget = selected;

	TreeMenuState &menu = m_treeMenus[treeId];

	auto render = [&](auto &self, const std::vector<TreeNode> &nodes, int depth) -> void {
		for (const TreeNode &node : nodes) {
			const Rect row = Next(opt.rowHeight);
			const Rect item{row.x + float(depth) * opt.indent, row.y, std::max(0.f, row.w - float(depth) * opt.indent), row.h};
			const auto it = Interact(MakeId(node.id), item);

			if (it.leftClicked) { // clic gauche : sélection (focus)
				selected = node.id;
				result.selected = node.id;
				menu.target.clear();
			}
			if (it.doubleClicked) { // double-clic : déplier/plier (répertoire) ou ouvrir (fichier)
				result.doubleClicked = node.id;
				result.doubleIsDir = node.isDir;
				if (node.isDir) {
					if (!expanded.erase(node.id))
						expanded.insert(node.id);
				}
				menu.target.clear();
			} else if (it.rightClicked) { // clic droit : menu contextuel
				selected = node.id;
				menu.target = node.id;
				menu.anchor = item;
			}

			const bool isSelected = node.id == selected;
			if (isSelected)
				c.FillRoundRect(item, 4, m_theme.widgetActive);
			else if (it.hovered)
				c.FillRoundRect(item, 4, m_theme.widgetHover);

			const float x = item.x;
			if (node.isDir) {
				const bool open = expanded.contains(node.id);
				Icon({x, row.y, 14, row.h}, open ? icons::Codepoint(icons::MaterialIcons::EXPAND_MORE) : icons::Codepoint(icons::MaterialIcons::CHEVRON_RIGHT), m_theme.textDim, 15);
				Icon({x + 14, row.y, 20, row.h}, node.icon ? node.icon : icons::Codepoint(icons::MaterialIcons::FOLDER),
					 node.iconColor.a ? node.iconColor : m_theme.warning, 16);
				TextIn({x + 36, row.y, std::max(0.f, item.Right() - x - 36), row.h}, node.label,
					   {m_theme.text, 13, sdl3::CanvasFont::Regular, TextOverflow::MarqueeOnHover});
				if (open && !node.children.empty())
					self(self, node.children, depth + 1);
			} else {
				const bool json = node.label.ends_with(".json");
				Icon({x + 14, row.y, 20, row.h}, node.icon ? node.icon : (json ? icons::Codepoint(icons::MaterialIcons::DESCRIPTION) : icons::Codepoint(icons::MaterialIcons::CODE)),
					 node.iconColor.a ? node.iconColor : (json ? m_theme.warning : m_theme.info), 16);
				TextIn({x + 36, row.y, std::max(0.f, item.Right() - x - 36), row.h}, node.label,
					   {m_theme.text, 13, sdl3::CanvasFont::Regular, TextOverflow::MarqueeOnHover});
			}
		}
	};
	render(render, roots, 0);

	// ── Menu contextuel (ouvrir / renommer / dupliquer / supprimer) ────────
	if (!menu.target.empty()) {
		struct Item { const char *label; char32_t icon; };
		static constexpr Item kItems[] = {{"Ouvrir", icons::Codepoint(icons::MaterialIcons::OPEN_IN_NEW)}, {"Renommer", icons::Codepoint(icons::MaterialIcons::EDIT)},
										  {"Dupliquer", icons::Codepoint(icons::MaterialIcons::CONTENT_COPY)}, {"Supprimer", icons::Codepoint(icons::MaterialIcons::DELETE)}};
		constexpr size_t kCount = sizeof(kItems) / sizeof(kItems[0]);
		const float mw = 150.f, mh = float(kCount) * 26.f + 8.f;
		Rect mr{menu.anchor.x, menu.anchor.Bottom(), mw, mh};
		if (mr.Right() > c.Width())
			mr.x = menu.anchor.Right() - mw;
		if (mr.Bottom() > c.Height())
			mr.y = menu.anchor.y - mh;
		c.FillRect(mr, m_theme.panel);
		c.StrokeRect(mr, m_theme.panelBorder, 1.f);
		for (size_t i = 0; i < kCount; ++i) {
			const Rect ir{mr.x + 4, mr.y + 4 + float(i) * 26.f, mw - 8, 26.f};
			const auto mi = Interact(MakeId(std::format("menu{}", i)), ir);
			if (mi.hovered)
				c.FillRoundRect(ir, 4, m_theme.widgetHover);
			Icon({ir.x + 6, ir.y, 20, ir.h}, kItems[i].icon, m_theme.textDim, 15);
			c.Text(ir.x + 30, ir.y + (ir.h - c.LineHeight(13)) * 0.5f, kItems[i].label, m_theme.text, 13);
			if (mi.leftClicked) {
				result.menuAction = i == 0 ? "open" : i == 1 ? "rename" : i == 2 ? "duplicate" : "delete";
				result.menuTarget = menu.target;
				menu.target.clear();
			}
		}
		if (MousePressed(sdl3::MouseButton::Left) && !MouseIn(mr))
			menu.target.clear(); // clic ailleurs : fermer
	}

	PopId();
	return result;
}

// ── Paramètres ────────────────────────────────────────────────────────────
Rect Context::SettingRow(std::string_view label, std::string_view description, bool modified, bool &resetClicked,
						 std::string_view defaultText) {
	sdl3::Canvas &c = *m_canvas;
	const bool hasDesc = !description.empty();
	const Rect r = Next(hasDesc ? 48 : 34);
	const bool hover = MouseIn(r);
	c.FillRoundRect(r, m_theme.radius, hover ? sdl3::Rgba(36, 41, 52, 200) : m_theme.row);
	const float controlW = std::min(r.w * 0.46f, 380.f);
	const float labelW = r.w - controlW - 50;
	const float lh = c.LineHeight(m_theme.fontSize);
	const float ly = hasDesc ? r.y + 6 : r.y + (r.h - lh) * 0.5f;
	TextIn({r.x + 10, ly, labelW, lh}, Visible(label), {m_theme.text, m_theme.fontSize, sdl3::CanvasFont::Regular, settingOverflow});
	if (hasDesc)
		TextIn({r.x + 10, ly + lh, labelW, c.LineHeight(12)}, description, {m_theme.textDim, 12, sdl3::CanvasFont::Regular, settingOverflow});
	// Bouton « valeur par défaut » : flèche en demi-tour (Material « undo »).
	const Rect reset{r.Right() - 36, r.y + (r.h - 30) * 0.5f, 30, 30};
	const std::string tip = std::format("Remettre la valeur par défaut ({})", defaultText);
	resetClicked = IconButton(reset, icons::Codepoint(icons::MaterialIcons::UNDO), modified ? std::string_view(tip) : std::string_view("Valeur par défaut"), modified,
							  modified ? m_theme.accent : Color{});
	return {r.Right() - 44 - controlW, r.y + (r.h - 28) * 0.5f, controlW, 28};
}

bool Context::SettingFloat(std::string_view label, std::string_view description, float &value, float min, float max, float def,
						   std::string_view format) {
	PushId(label);
	bool reset = false;
	const bool modified = std::fabs(value - def) > (max - min) * 1e-4f;
	const std::string defText = FormatValue(format, def);
	const Rect ctl = SettingRow(label, description, modified, reset, defText);
	const std::string text = FormatValue(format, value);
	const float valueW = 70;
	m_canvas->Text(ctl.x + valueW - 10 - m_canvas->MeasureText(text, m_theme.fontSize - 1), ctl.y + (ctl.h - m_canvas->LineHeight(m_theme.fontSize - 1)) * 0.5f,
				   text, modified ? m_theme.text : m_theme.textDim, m_theme.fontSize - 1);
	bool changed = SliderAt({ctl.x + valueW + 4, ctl.y, ctl.w - valueW - 12, ctl.h}, "slider", value, min, max);
	if (reset) {
		value = def;
		changed = true;
	}
	PopId();
	return changed;
}

bool Context::SettingInt(std::string_view label, std::string_view description, int &value, int min, int max, int def, std::string_view unit) {
	PushId(label);
	bool reset = false;
	const std::string defText = unit.empty() ? std::to_string(def) : std::format("{} {}", def, unit);
	const Rect ctl = SettingRow(label, description, value != def, reset, defText);
	const std::string text = unit.empty() ? std::to_string(value) : std::format("{} {}", value, unit);
	const float valueW = 84;
	m_canvas->Text(ctl.x + valueW - 10 - m_canvas->MeasureText(text, m_theme.fontSize - 1), ctl.y + (ctl.h - m_canvas->LineHeight(m_theme.fontSize - 1)) * 0.5f,
				   text, value != def ? m_theme.text : m_theme.textDim, m_theme.fontSize - 1);
	float f = float(value);
	bool changed = false;
	if (SliderAt({ctl.x + valueW + 4, ctl.y, ctl.w - valueW - 12, ctl.h}, "slider", f, float(min), float(max))) {
		const int nv = static_cast<int>(std::lround(f));
		changed = nv != value;
		value = nv;
	}
	if (reset) {
		value = def;
		changed = true;
	}
	PopId();
	return changed;
}

bool Context::SettingToggle(std::string_view label, std::string_view description, bool &value, bool def) {
	PushId(label);
	bool reset = false;
	const Rect ctl = SettingRow(label, description, value != def, reset, def ? "activé" : "désactivé");
	bool changed = Toggle(ctl, "toggle", value);
	if (reset) {
		value = def;
		changed = true;
	}
	PopId();
	return changed;
}

bool Context::SettingChoice(std::string_view label, std::string_view description, int &index, std::span<const std::string_view> options,
							int def) {
	PushId(label);
	bool reset = false;
	const std::string_view defText = def >= 0 && size_t(def) < options.size() ? options[size_t(def)] : std::string_view("?");
	const Rect ctl = SettingRow(label, description, index != def, reset, defText);
	const float w = std::min(ctl.w, 240.f);
	bool changed = Choice({ctl.Right() - w, ctl.y, w, ctl.h}, "choice", index, options);
	if (reset) {
		index = def;
		changed = true;
	}
	PopId();
	return changed;
}

bool Context::SettingText(std::string_view label, std::string_view description, std::string &value, std::string_view def,
						  std::string_view placeholder) {
	PushId(label);
	bool reset = false;
	const Rect ctl = SettingRow(label, description, value != def, reset, def.empty() ? std::string_view("vide") : def);
	bool changed = TextInputAt(ctl, "text", value, nullptr, placeholder);
	if (reset) {
		value = std::string(def);
		changed = true;
	}
	PopId();
	return changed;
}

// ── Notifications ─────────────────────────────────────────────────────────
void Context::Notify(std::string text, Color c, float seconds) {
	m_toasts.push_back({std::move(text), c.a ? c : m_theme.accent, seconds});
	if (m_toasts.size() > 6)
		m_toasts.erase(m_toasts.begin());
}

void Context::DrawNotifications(float dt, float topOffset) {
	sdl3::Canvas &c = *m_canvas;
	float y = topOffset;
	for (Toast &t : m_toasts) {
		t.remaining -= dt;
		const float a = std::clamp(t.remaining, 0.f, 1.f);
		const std::string text = c.Ellipsize(t.text, c.Width() * 0.6f, m_theme.fontSize);
		const float w = c.MeasureText(text, m_theme.fontSize) + 32;
		const Rect r{c.Width() - w - 14, y, w, m_theme.rowHeight + 6};
		c.FillRoundRect(r, m_theme.radius, sdl3::WithAlpha(m_theme.panel, uint8_t(240 * a)));
		c.FillRoundRect({r.x, r.y, 4, r.h}, 2, sdl3::WithAlpha(t.color, uint8_t(255 * a)));
		c.Text(r.x + 16, r.y + (r.h - c.LineHeight(m_theme.fontSize)) * 0.5f, text, sdl3::WithAlpha(m_theme.text, uint8_t(255 * a)), m_theme.fontSize);
		y += r.h + 6;
	}
	std::erase_if(m_toasts, [](const Toast &t) { return t.remaining <= 0; });
}

} // namespace ui
