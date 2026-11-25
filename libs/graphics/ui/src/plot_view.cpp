// Définitions de ui/plot_view.hpp — graphique 2D interactif.
#include "ui/plot_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>

#include "ui/ui.hpp"

namespace ui {

namespace {

constexpr std::array kPalette = {sdl3::Rgba(74, 144, 255), sdl3::Rgba(96, 200, 140), sdl3::Rgba(236, 180, 72), sdl3::Rgba(222, 96, 120),
								 sdl3::Rgba(170, 120, 255), sdl3::Rgba(80, 200, 220)};

/// Pas « rond » (1, 2, 5 × 10ⁿ) donnant environ `count` graduations sur [lo, hi].
float NiceStep(float lo, float hi, int count) {
	const float raw = std::max((hi - lo) / float(std::max(count, 1)), 1e-12f);
	const float p = std::pow(10.f, std::floor(std::log10(raw)));
	const float f = raw / p;
	return (f < 1.5f ? 1.f : f < 3.5f ? 2.f : f < 7.5f ? 5.f : 10.f) * p;
}

std::string TickLabel(float v, float step) {
	const int decimals = std::clamp(int(-std::floor(std::log10(step))), 0, 6);
	if (std::abs(v) < step * 1e-3f)
		v = 0.f;
	return std::format("{:.{}f}", v, decimals);
}

/// Bornes [lo, hi] d'une plage, élargies de 5 % (et d'une unité si elle est vide).
void Pad(float &lo, float &hi) {
	if (!(lo <= hi)) {
		lo = 0.f;
		hi = 1.f;
		return;
	}
	if (hi - lo < 1e-9f) {
		lo -= 1.f;
		hi += 1.f;
		return;
	}
	const float m = (hi - lo) * 0.05f;
	lo -= m;
	hi += m;
}

} // namespace

PlotLayer &PlotView::Series(std::string name, PlotStyle style, sdl3::Color color) {
	PlotLayer layer;
	layer.name = std::move(name);
	layer.style = style;
	layer.color = color;
	m_layers.push_back(std::move(layer));
	return m_layers.back();
}

void PlotView::AutoFit() {
	constexpr float inf = std::numeric_limits<float>::infinity();
	float x0 = inf, x1 = -inf, y0 = inf, y1 = -inf, s0 = inf, s1 = -inf;
	for (const PlotLayer &l : m_layers) {
		if (!l.visible)
			continue;
		float &lo = l.secondaryAxis ? s0 : y0, &hi = l.secondaryAxis ? s1 : y1;
		const size_t n = l.style == PlotStyle::Candles ? l.close.size() : l.y.size();
		for (size_t i = 0; i < n; ++i) {
			const float x = i < l.x.size() ? l.x[i] : float(i);
			x0 = std::min(x0, x);
			x1 = std::max(x1, x);
			if (l.style == PlotStyle::Candles) {
				lo = std::min(lo, i < l.low.size() ? l.low[i] : l.close[i]);
				hi = std::max(hi, i < l.high.size() ? l.high[i] : l.close[i]);
				continue;
			}
			const float e = i < l.err.size() ? std::abs(l.err[i]) : 0.f;
			lo = std::min(lo, l.y[i] - e);
			hi = std::max(hi, l.y[i] + e);
			if (l.style == PlotStyle::Bars || l.style == PlotStyle::Stems || l.style == PlotStyle::Area)
				lo = std::min(lo, 0.f), hi = std::max(hi, 0.f); // barres et aires partent de 0
		}
		if (l.style == PlotStyle::Bars || l.style == PlotStyle::Candles)
			x0 -= 0.5f, x1 += 0.5f; // demi-barre de part et d'autre
	}
	xMin = x0, xMax = x1, yMin = y0, yMax = y1, y2Min = s0, y2Max = s1;
	if (!(x0 <= x1))
		xMin = 0.f, xMax = 1.f;
	else if (x1 - x0 < 1e-9f)
		xMin -= 1.f, xMax += 1.f;
	Pad(yMin, yMax);
	Pad(y2Min, y2Max);
	m_fitted = true;
}

bool PlotView::Draw(Context &u, const sdl3::FRect &r) {
	sdl3::Canvas &c = u.Canvas();
	const Theme &th = u.Style();
	m_time += double(u.Dt());
	if (!m_fitted)
		AutoFit();
	const bool hasSecondary = std::ranges::any_of(m_layers, [](const PlotLayer &l) { return l.secondaryAxis && l.visible; });
	const float small = th.fontSize - 3.f;
	// Zones : titre, graduations, légende.
	const float top = r.y + (title.empty() ? 8.f : 26.f);
	const sdl3::FRect plot{r.x + 52.f, top, std::max(10.f, r.w - 52.f - (hasSecondary ? 52.f : 14.f)), std::max(10.f, r.Bottom() - top - 34.f)};
	const sdl3::FRect xBand{plot.x, plot.Bottom(), plot.w, 30.f}, yBand{r.x, plot.y, 52.f, plot.h};
	const auto sx = [&](float x) { return plot.x + (x - xMin) / (xMax - xMin) * plot.w; };
	const auto syOf = [&](float y, bool second) {
		const float lo = second ? y2Min : yMin, hi = second ? y2Max : yMax;
		return plot.Bottom() - (y - lo) / (hi - lo) * plot.h;
	};
	const auto gx = [&](float px) { return xMin + (px - plot.x) / plot.w * (xMax - xMin); };
	const auto gy = [&](float py) { return yMin + (plot.Bottom() - py) / plot.h * (yMax - yMin); };

	// ── Souris ─────────────────────────────────────────────────────────────
	bool changed = false;
	const float mx = u.MouseX(), my = u.MouseY();
	const bool over = u.MouseOver(r);
	const bool inPlot = plot.Contains(mx, my);
	// Légende : rectangles calculés avant la gestion des clics.
	std::vector<sdl3::FRect> legendRects;
	if (legend) {
		float lx = plot.x + 8.f;
		for (const PlotLayer &l : m_layers) {
			const float w = c.MeasureText(l.name, small) + 26.f;
			legendRects.push_back({lx, plot.y + 6.f, w, 18.f});
			lx += w + 6.f;
		}
	}
	m_highlight = -1;
	for (size_t i = 0; i < legendRects.size(); ++i)
		if (legendRects[i].Contains(mx, my))
			m_highlight = int(i);
	if (over && m_drag == Drag::None) {
		if (u.WheelY() != 0.f) {
			const float k = std::pow(0.85f, u.WheelY());
			const bool onlyX = xBand.Contains(mx, my), onlyY = yBand.Contains(mx, my);
			if (!onlyY) { // zoom horizontal autour de la souris
				const float cx = gx(std::clamp(mx, plot.x, plot.Right()));
				xMin = cx + (xMin - cx) * k;
				xMax = cx + (xMax - cx) * k;
			}
			if (!onlyX) {
				const float cy = gy(std::clamp(my, plot.y, plot.Bottom())), t = (cy - yMin) / (yMax - yMin);
				yMin = cy + (yMin - cy) * k;
				yMax = cy + (yMax - cy) * k;
				const float c2 = y2Min + t * (y2Max - y2Min);
				y2Min = c2 + (y2Min - c2) * k;
				y2Max = c2 + (y2Max - c2) * k;
			}
			changed = true;
		}
		if (u.MousePressed(sdl3::MouseButton::Left)) {
			if (m_highlight >= 0) {
				m_layers[size_t(m_highlight)].visible = !m_layers[size_t(m_highlight)].visible;
				changed = true;
			} else if (inPlot) {
				if (m_time - m_lastClick < 0.35) {
					AutoFit();
					changed = true;
				} else
					m_drag = Drag::Pan;
				m_lastClick = m_time;
			}
		} else if (inPlot && u.MousePressed(sdl3::MouseButton::Right))
			m_drag = Drag::Box;
		m_startX = m_lastX = mx;
		m_startY = m_lastY = my;
	}
	if (m_drag == Drag::Pan) {
		const float dx = (mx - m_lastX) / plot.w * (xMax - xMin), dy = (my - m_lastY) / plot.h * (yMax - yMin);
		const float dy2 = (my - m_lastY) / plot.h * (y2Max - y2Min);
		xMin -= dx, xMax -= dx, yMin += dy, yMax += dy, y2Min += dy2, y2Max += dy2;
		changed |= dx != 0.f || dy != 0.f;
		if (!u.MouseDown())
			m_drag = Drag::None;
	} else if (m_drag == Drag::Box && !u.MouseDown(sdl3::MouseButton::Right)) {
		if (std::abs(mx - m_startX) > 4.f && std::abs(my - m_startY) > 4.f) {
			const float ax = gx(std::min(mx, m_startX)), bx = gx(std::max(mx, m_startX));
			const float ay = gy(std::max(my, m_startY)), by = gy(std::min(my, m_startY));
			const float t0 = (ay - yMin) / (yMax - yMin), t1 = (by - yMin) / (yMax - yMin);
			const float s0 = y2Min + t0 * (y2Max - y2Min), s1 = y2Min + t1 * (y2Max - y2Min);
			xMin = ax, xMax = bx, yMin = ay, yMax = by, y2Min = s0, y2Max = s1;
			changed = true;
		}
		m_drag = Drag::None;
	}
	m_lastX = mx;
	m_lastY = my;

	// ── Fond, grille, graduations ──────────────────────────────────────────
	c.FillRoundRect(r, th.radius, th.widget);
	if (!title.empty())
		c.Text(r.x + (r.w - c.MeasureText(title, th.fontSize - 1.f, sdl3::CanvasFont::Bold)) * 0.5f, r.y + 5.f, title, th.text, th.fontSize - 1.f,
			   sdl3::CanvasFont::Bold);
	const sdl3::Color grid = sdl3::WithAlpha(th.panelBorder, 140);
	const float xs = NiceStep(xMin, xMax, std::max(2, int(plot.w / 80.f)));
	for (float x = std::ceil(xMin / xs) * xs; x <= xMax; x += xs) {
		const float px = sx(x);
		c.Line(px, plot.y, px, plot.Bottom(), grid);
		const std::string label = TickLabel(x, xs);
		c.Text(px - c.MeasureText(label, small) * 0.5f, plot.Bottom() + 4.f, label, th.textDim, small);
	}
	const float ys = NiceStep(yMin, yMax, std::max(2, int(plot.h / 50.f)));
	for (float y = std::ceil(yMin / ys) * ys; y <= yMax; y += ys) {
		const float py = syOf(y, false);
		c.Line(plot.x, py, plot.Right(), py, grid);
		const std::string label = TickLabel(y, ys);
		c.Text(plot.x - 6.f - c.MeasureText(label, small), py - c.LineHeight(small) * 0.5f, label, th.textDim, small);
	}
	if (hasSecondary) {
		const float s = NiceStep(y2Min, y2Max, std::max(2, int(plot.h / 50.f)));
		for (float y = std::ceil(y2Min / s) * s; y <= y2Max; y += s) {
			const std::string label = TickLabel(y, s);
			c.Text(plot.Right() + 6.f, syOf(y, true) - c.LineHeight(small) * 0.5f, label, th.textDim, small);
		}
	}
	if (!xLabel.empty())
		c.Text(plot.Right() - c.MeasureText(xLabel, small), plot.Bottom() + 17.f, xLabel, th.textDim, small);
	if (!yLabel.empty())
		c.Text(r.x + 4.f, plot.y - 16.f + (title.empty() ? 6.f : 0.f), yLabel, th.textDim, small);
	if (hasSecondary && !y2Label.empty())
		c.Text(r.Right() - 4.f - c.MeasureText(y2Label, small), plot.y - 16.f + (title.empty() ? 6.f : 0.f), y2Label, th.textDim, small);

	// ── Séries ─────────────────────────────────────────────────────────────
	c.PushClip(plot);
	float bestDist = 14.f;
	std::string tip;
	for (size_t li = 0; li < m_layers.size(); ++li) {
		const PlotLayer &l = m_layers[li];
		if (!l.visible)
			continue;
		sdl3::Color col = l.color.a ? l.color : kPalette[li % kPalette.size()];
		if (m_highlight >= 0 && size_t(m_highlight) != li)
			col = sdl3::WithAlpha(col, 70);
		const bool second = l.secondaryAxis;
		const size_t n = l.style == PlotStyle::Candles ? l.close.size() : l.y.size();
		const auto xAt = [&](size_t i) { return i < l.x.size() ? l.x[i] : float(i); };
		const float zero = syOf(std::clamp(0.f, second ? y2Min : yMin, second ? y2Max : yMax), second);
		const float slot = n > 1 ? std::abs(sx(xAt(1)) - sx(xAt(0))) : plot.w * 0.5f;
		std::vector<float> xy;
		for (size_t i = 0; i < n; ++i) {
			const float px = sx(xAt(i));
			const float vy = l.style == PlotStyle::Candles ? l.close[i] : l.y[i];
			const float py = syOf(vy, second);
			switch (l.style) {
			case PlotStyle::Line:
			case PlotStyle::Area:
				xy.insert(xy.end(), {px, py});
				break;
			case PlotStyle::Scatter: c.FillCircle(px, py, std::max(2.f, l.width + 1.f), col); break;
			case PlotStyle::Bars: {
				const float bw = slot * std::clamp(l.width / 3.f, 0.2f, 0.9f);
				c.FillRect({px - bw * 0.5f, std::min(py, zero), bw, std::max(1.f, std::abs(zero - py))}, col);
				break;
			}
			case PlotStyle::Stems:
				c.Line(px, zero, px, py, col, 1.5f);
				c.FillCircle(px, py, 3.5f, col);
				break;
			case PlotStyle::ErrorBars: {
				const float e = i < l.err.size() ? std::abs(l.err[i]) : 0.f;
				const float a = syOf(vy - e, second), b = syOf(vy + e, second);
				c.Line(px, a, px, b, col, 1.2f);
				c.Line(px - 4.f, a, px + 4.f, a, col, 1.2f);
				c.Line(px - 4.f, b, px + 4.f, b, col, 1.2f);
				c.FillCircle(px, py, 3.f, col);
				break;
			}
			case PlotStyle::Candles: {
				const float o = l.open[i], h = l.high[i], lo = l.low[i], cl = l.close[i];
				const sdl3::Color candle = cl >= o ? th.success : th.danger;
				const sdl3::Color body = m_highlight >= 0 && size_t(m_highlight) != li ? sdl3::WithAlpha(candle, 70) : candle;
				const float bw = slot * 0.6f;
				c.Line(px, syOf(h, second), px, syOf(lo, second), body, 1.2f);
				const float y0 = syOf(std::max(o, cl), second), y1 = syOf(std::min(o, cl), second);
				c.FillRect({px - bw * 0.5f, y0, bw, std::max(1.5f, y1 - y0)}, body);
				break;
			}
			}
			// Infobulle : point le plus proche de la souris.
			if (inPlot && m_drag == Drag::None) {
				const float d = std::hypot(px - mx, py - my);
				if (d < bestDist) {
					bestDist = d;
					tip = l.style == PlotStyle::Candles
							  ? std::format("{} · x = {:g}\nouverture {:g} · haut {:g}\nbas {:g} · clôture {:g}", l.name, xAt(i), l.open[i], l.high[i], l.low[i], l.close[i])
							  : std::format("{}\nx = {:g} · y = {:g}", l.name, xAt(i), vy);
				}
			}
		}
		if (l.style == PlotStyle::Area) // surface continue : deux triangles par segment jusqu'à y = 0
			for (size_t i = 2; i + 1 < xy.size(); i += 2) {
				const sdl3::Color fill = sdl3::WithAlpha(col, uint8_t(col.a / 3));
				c.FillTriangle(xy[i - 2], xy[i - 1], xy[i], xy[i + 1], xy[i], zero, fill);
				c.FillTriangle(xy[i - 2], xy[i - 1], xy[i], zero, xy[i - 2], zero, fill);
			}
		if (xy.size() >= 4)
			c.Polyline(xy.data(), xy.size() / 2, col, l.width);
	}
	if (m_drag == Drag::Box) {
		const sdl3::FRect box{std::min(mx, m_startX), std::min(my, m_startY), std::abs(mx - m_startX), std::abs(my - m_startY)};
		c.FillRect(box, sdl3::WithAlpha(th.accent, 40));
		c.StrokeRect(box, th.accent);
	}
	c.PopClip();
	c.StrokeRect(plot, th.panelBorder);

	// ── Légende ────────────────────────────────────────────────────────────
	for (size_t i = 0; i < legendRects.size(); ++i) {
		const PlotLayer &l = m_layers[i];
		const sdl3::FRect lr = legendRects[i];
		const sdl3::Color col = l.color.a ? l.color : kPalette[i % kPalette.size()];
		c.FillRoundRect(lr, 4.f, sdl3::WithAlpha(th.panel, int(i) == m_highlight ? 250 : 200));
		c.FillRoundRect({lr.x + 6.f, lr.y + 5.f, 12.f, 8.f}, 2.f, l.visible ? col : sdl3::WithAlpha(col, 60));
		c.Text(lr.x + 22.f, lr.y + (lr.h - c.LineHeight(small)) * 0.5f, l.name, l.visible ? th.text : th.textDim, small);
	}
	if (!tip.empty())
		u.Tooltip(tip);
	return changed;
}

} // namespace ui
