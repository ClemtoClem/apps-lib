#include "ui/window_frame.hpp"

#include <algorithm>
#include <format>

#include <SDL3/SDL.h>

namespace ui {

namespace {
constexpr float kGap = 2.f, kPad = 8.f;

/// Largeur des boutons de la fenêtre (réduire, agrandir, fermer) pour une barre de hauteur `h`.
float ControlWidth(float h) { return std::max(h * 1.15f, 34.f); }

/// Rectangle vide placé à l'abscisse `x` (bouton absent).
Rect Empty(float x, float y) { return {x, y, 0, 0}; }
} // namespace

FrameLayout ComputeFrameLayout(const Rect &window, float height, std::span<const float> leftWidths, std::span<const float> rightWidths,
							   bool hasIcon, bool minimizeButton, bool maximizeButton, bool closeButton) {
	FrameLayout l;
	l.window = window;
	height = std::clamp(height, 0.f, window.h);
	l.bar = {window.x, window.y, window.w, height};
	l.content = {window.x, window.y + height, window.w, window.h - height};

	// De droite à gauche : fermer, agrandir, réduire, puis les boutons personnalisés.
	float right = l.bar.Right();
	const float control = ControlWidth(height);
	auto takeControl = [&](bool present) {
		if (!present)
			return Empty(right, l.bar.y);
		right -= control;
		return Rect{right, l.bar.y, control, height};
	};
	l.close = takeControl(closeButton);
	l.maximize = takeControl(maximizeButton);
	l.minimize = takeControl(minimizeButton);
	const float inset = std::min(4.f, height * 0.12f);
	right -= rightWidths.empty() ? 0.f : kPad;
	l.right.resize(rightWidths.size());
	for (size_t i = rightWidths.size(); i-- > 0;) {
		right -= rightWidths[i];
		l.right[i] = {right, l.bar.y + inset, rightWidths[i], height - 2 * inset};
		right -= kGap;
	}

	// De gauche à droite : icône, boutons personnalisés.
	float left = l.bar.x + kPad;
	if (hasIcon) {
		const float size = std::max(height - 14.f, 0.f);
		l.icon = {left, l.bar.y + (height - size) * 0.5f, size, size};
		left += size + kPad;
	} else {
		l.icon = Empty(left, l.bar.y);
	}
	l.left.reserve(leftWidths.size());
	for (const float w : leftWidths) {
		// Un bouton qui ne tient plus avant la zone de droite est écarté (largeur nulle).
		const bool fits = left + w <= right;
		l.left.push_back(fits ? Rect{left, l.bar.y + inset, w, height - 2 * inset} : Empty(left, l.bar.y));
		if (fits)
			left += w + kGap;
	}
	l.title = {left + kPad, l.bar.y, std::max(0.f, right - left - 2 * kPad), height};
	return l;
}

FrameHit HitTestFrame(const FrameLayout &layout, float x, float y, bool maximized, float border) noexcept {
	const Rect &w = layout.window;
	if (!w.Contains(x, y))
		return FrameHit::Client;
	if (!maximized && border > 0.f) {
		// Les coins sont un peu plus larges que les bords : plus faciles à saisir.
		const float corner = border * 2.5f;
		const bool l = x < w.x + border, r = x >= w.Right() - border, t = y < w.y + border, b = y >= w.Bottom() - border;
		const bool nearL = x < w.x + corner, nearR = x >= w.Right() - corner, nearT = y < w.y + corner, nearB = y >= w.Bottom() - corner;
		if ((t && nearL) || (l && nearT))
			return FrameHit::TopLeft;
		if ((t && nearR) || (r && nearT))
			return FrameHit::TopRight;
		if ((b && nearL) || (l && nearB))
			return FrameHit::BottomLeft;
		if ((b && nearR) || (r && nearB))
			return FrameHit::BottomRight;
		if (l)
			return FrameHit::Left;
		if (r)
			return FrameHit::Right;
		if (t)
			return FrameHit::Top;
		if (b)
			return FrameHit::Bottom;
	}
	if (!layout.bar.Contains(x, y))
		return FrameHit::Client;
	for (const Rect *r : {&layout.minimize, &layout.maximize, &layout.close})
		if (r->Contains(x, y))
			return FrameHit::Client;
	for (const std::vector<Rect> *list : {&layout.left, &layout.right})
		for (const Rect &r : *list)
			if (r.Contains(x, y))
				return FrameHit::Client;
	return FrameHit::Caption;
}

WindowFrameResult WindowFrame(Context &u, const Rect &window, const WindowFrameOptions &options) {
	sdl3::Canvas &c = u.Canvas();
	const Theme &th = u.Style();
	const float fontSize = th.fontSize;

	// Largeur de chaque bouton personnalisé : icône carrée, texte mesuré, ou les deux.
	auto widthOf = [&](const FrameButton &b) {
		const float iconW = b.icon ? options.height - 10.f : 0.f;
		const float textW = b.label.empty() ? 0.f : c.MeasureText(b.label, fontSize) + 14.f;
		return std::max(iconW + textW, 24.f) + (b.separator ? 9.f : 0.f);
	};
	std::vector<float> leftW, rightW;
	for (const FrameButton &b : options.left)
		leftW.push_back(widthOf(b));
	for (const FrameButton &b : options.right)
		rightW.push_back(widthOf(b));

	WindowFrameResult result;
	result.layout = ComputeFrameLayout(window, options.height, leftW, rightW, options.icon != 0 || options.image != nullptr,
									   options.minimizeButton, options.maximizeButton, options.closeButton);
	const FrameLayout &l = result.layout;

	c.FillRect(l.bar, th.title);
	c.FillRect({l.bar.x, l.bar.Bottom() - 1, l.bar.w, 1}, th.panelBorder);
	const Color textColor = options.focused ? th.text : th.textDim;

	if (l.icon.w > 0) {
		if (options.image)
			c.Image(*options.image, l.icon);
		else
			(void)c.Icon(l.icon, options.icon, options.iconColor.a ? options.iconColor : th.accent, l.icon.h);
	}

	auto drawButtons = [&](std::span<const FrameButton> buttons, const std::vector<Rect> &rects) {
		for (size_t i = 0; i < buttons.size(); ++i) {
			const FrameButton &b = buttons[i];
			Rect r = rects[i];
			if (r.w <= 0)
				continue;
			if (b.separator) {
				c.FillRect({r.x + 3, r.y + 5, 1, r.h - 10}, th.panelBorder);
				r.x += 9;
				r.w -= 9;
			}
			const Context::Interaction it = b.enabled ? u.Interact(u.MakeId(std::format("##frame:{}", b.id)), r) : Context::Interaction{};
			if (b.active || it.hovered || it.held)
				c.FillRoundRect(r, th.radius, it.held ? th.widgetActive : (b.active && !it.hovered ? th.widget : th.widgetHover));
			const Color fg = !b.enabled ? sdl3::WithAlpha(th.textDim, 90) : textColor;
			float x = r.x;
			if (b.icon) {
				const float iconW = b.label.empty() ? r.w : options.height - 10.f;
				(void)c.Icon({x, r.y, iconW, r.h}, b.icon, !b.enabled ? fg : (b.tint.a ? b.tint : fg), std::min(r.h - 8.f, 20.f));
				x += iconW;
			}
			if (!b.label.empty())
				c.TextCentered({x, r.y, r.Right() - x, r.h}, b.label, fg, fontSize);
			if (it.hovered && !b.tooltip.empty())
				u.Tooltip(b.tooltip);
			if (it.leftClicked) {
				result.action = FrameAction::Custom;
				result.custom = b.id;
			}
		}
	};
	drawButtons(options.left, l.left);
	drawButtons(options.right, l.right);

	// Titre centré dans la fenêtre s'il y tient, sinon dans la zone libre.
	if (!options.title.empty() && l.title.w > 20) {
		const float w = c.MeasureText(options.title, fontSize, sdl3::CanvasFont::Bold);
		float x = l.bar.x + (l.bar.w - w) * 0.5f;
		if (x < l.title.x || x + w > l.title.Right())
			x = l.title.x + std::max(0.f, (l.title.w - w) * 0.5f);
		c.PushClip(l.title);
		c.Text(x, l.bar.y + (l.bar.h - c.LineHeight(fontSize, sdl3::CanvasFont::Bold)) * 0.5f, options.title, textColor, fontSize, sdl3::CanvasFont::Bold);
		c.PopClip();
	}

	// Boutons de la fenêtre : pictogrammes tracés (aucune police requise).
	auto control = [&](const Rect &r, std::string_view id, FrameAction action, bool danger) {
		if (r.w <= 0)
			return;
		const Context::Interaction it = u.Interact(u.MakeId(id), r);
		if (it.hovered || it.held)
			c.FillRect(r, danger ? th.danger : (it.held ? th.widgetActive : th.widgetHover));
		const Color fg = it.hovered && danger ? sdl3::Rgba(255, 255, 255) : textColor;
		const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f, s = 5.f;
		switch (action) {
		case FrameAction::Minimize: c.Line(cx - s, cy + s - 1, cx + s, cy + s - 1, fg, 1.5f); break;
		case FrameAction::ToggleMaximize:
			if (options.maximized) { // restaurer : deux carrés décalés
				c.StrokeRect({cx - s + 2, cy - s, 2 * s - 2, 2 * s - 2}, fg, 1.5f);
				c.FillRect({cx - s, cy - s + 2, 2 * s - 2, 2 * s - 2}, it.hovered ? th.widgetHover : th.title);
				c.StrokeRect({cx - s, cy - s + 2, 2 * s - 2, 2 * s - 2}, fg, 1.5f);
			} else {
				c.StrokeRect({cx - s, cy - s, 2 * s, 2 * s}, fg, 1.5f);
			}
			break;
		default:
			c.Line(cx - s, cy - s, cx + s, cy + s, fg, 1.5f);
			c.Line(cx + s, cy - s, cx - s, cy + s, fg, 1.5f);
			break;
		}
		if (it.leftClicked)
			result.action = action;
	};
	control(l.minimize, "##frame-minimize", FrameAction::Minimize, false);
	control(l.maximize, "##frame-maximize", FrameAction::ToggleMaximize, false);
	control(l.close, "##frame-close", FrameAction::Close, true);
	return result;
}

// ── WindowChrome ────────────────────────────────────────────────────────────

namespace {
SDL_HitTestResult SDLCALL ChromeHitTest(SDL_Window *, const SDL_Point *point, void *data) {
	const auto *chrome = static_cast<const WindowChrome *>(data);
	switch (chrome->HitTest(point->x, point->y)) {
	case FrameHit::Caption: return SDL_HITTEST_DRAGGABLE;
	case FrameHit::Left: return SDL_HITTEST_RESIZE_LEFT;
	case FrameHit::Right: return SDL_HITTEST_RESIZE_RIGHT;
	case FrameHit::Top: return SDL_HITTEST_RESIZE_TOP;
	case FrameHit::Bottom: return SDL_HITTEST_RESIZE_BOTTOM;
	case FrameHit::TopLeft: return SDL_HITTEST_RESIZE_TOPLEFT;
	case FrameHit::TopRight: return SDL_HITTEST_RESIZE_TOPRIGHT;
	case FrameHit::BottomLeft: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
	case FrameHit::BottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
	default: return SDL_HITTEST_NORMAL;
	}
}
} // namespace

WindowChrome::WindowChrome(SDL_Window *window) : m_window(window) {
	if (m_window)
		SDL_SetWindowHitTest(m_window, ChromeHitTest, this);
}

WindowChrome::~WindowChrome() {
	if (m_window)
		SDL_SetWindowHitTest(m_window, nullptr, nullptr);
}

void WindowChrome::Update(const FrameLayout &layout, float scale) {
	m_layout = layout;
	m_scale = scale > 0.f ? scale : 1.f;
}

FrameHit WindowChrome::HitTest(int x, int y) const noexcept {
	return HitTestFrame(m_layout, float(x) / m_scale, float(y) / m_scale, Maximized());
}

bool WindowChrome::Maximized() const { return m_window && (SDL_GetWindowFlags(m_window) & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)); }

bool WindowChrome::Focused() const { return m_window && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS); }

bool WindowChrome::Apply(FrameAction action) {
	if (!m_window)
		return action == FrameAction::Close;
	switch (action) {
	case FrameAction::Close: return true;
	case FrameAction::Minimize: SDL_MinimizeWindow(m_window); break;
	case FrameAction::ToggleMaximize:
		if (Maximized())
			SDL_RestoreWindow(m_window);
		else
			SDL_MaximizeWindow(m_window);
		break;
	default: break;
	}
	return false;
}

} // namespace ui
