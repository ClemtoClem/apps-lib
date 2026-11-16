// Visualisation de signaux de l'interface immédiate : oscilloscope, spectre, vumètre.
#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include "ui/ui.hpp"

namespace ui {

namespace {

/// Couleur explicite, sinon l'accent du thème (alpha nul : couleur par défaut).
Color Or(Color c, Color fallback) { return c.a == 0 ? fallback : c; }

} // namespace

void Context::Scope(const Rect &r, std::span<const float> samples, Color color, float gain) {
	sdl3::Canvas &c = *m_canvas;
	const Color line = Or(color, m_theme.accent);
	c.FillRoundRect(r, m_theme.radius, m_theme.widget);
	c.PushClip(r);
	// Grille : 8 divisions horizontales, 4 verticales, axe zéro plus marqué.
	const Color grid = sdl3::WithAlpha(m_theme.panelBorder, 120);
	for (int i = 1; i < 8; ++i)
		c.Line(r.x + r.w * float(i) / 8.f, r.y, r.x + r.w * float(i) / 8.f, r.Bottom(), grid);
	for (int i = 1; i < 4; ++i)
		c.Line(r.x, r.y + r.h * float(i) / 4.f, r.Right(), r.y + r.h * float(i) / 4.f, i == 2 ? m_theme.panelBorder : grid);
	if (samples.size() >= 2 && r.w >= 2.f) {
		const float mid = r.y + r.h * 0.5f, half = r.h * 0.5f / std::max(gain, 1e-6f);
		const auto y = [&](float s) { return std::clamp(mid - s * half, r.y, r.Bottom()); };
		const size_t columns = size_t(r.w);
		if (samples.size() > columns * 2) {
			// Plus d'échantillons que de pixels : enveloppe min / max par colonne.
			for (size_t col = 0; col < columns; ++col) {
				const size_t a = col * samples.size() / columns, b = std::max(a + 1, (col + 1) * samples.size() / columns);
				const auto [lo, hi] = std::minmax_element(samples.begin() + std::ptrdiff_t(a), samples.begin() + std::ptrdiff_t(b));
				const float x = r.x + float(col);
				c.Line(x, y(*hi), x, y(*lo) + 1.f, line, 1.f);
			}
		} else {
			std::vector<float> xy(samples.size() * 2);
			for (size_t i = 0; i < samples.size(); ++i) {
				xy[i * 2] = r.x + r.w * float(i) / float(samples.size() - 1);
				xy[i * 2 + 1] = y(samples[i]);
			}
			c.Polyline(xy.data(), samples.size(), line, 1.5f);
		}
	}
	c.PopClip();
	c.StrokeRoundRect(r, m_theme.radius, m_theme.panelBorder);
}

void Context::SpectrumView(const Rect &r, std::span<const float> freqs, std::span<const float> db, float minHz, float maxHz, float minDb,
						   float maxDb, Color color) {
	sdl3::Canvas &c = *m_canvas;
	const Color fill = Or(color, m_theme.accent);
	c.FillRoundRect(r, m_theme.radius, m_theme.widget);
	const Rect plot{r.x + 34.f, r.y + 6.f, std::max(1.f, r.w - 40.f), std::max(1.f, r.h - 24.f)};
	const float logMin = std::log10(std::max(minHz, 1.f)), logMax = std::log10(std::max(maxHz, minHz + 1.f));
	const auto xOf = [&](float hz) { return plot.x + plot.w * (std::log10(std::max(hz, 1.f)) - logMin) / (logMax - logMin); };
	const auto yOf = [&](float d) { return plot.y + plot.h * (1.f - std::clamp((d - minDb) / (maxDb - minDb), 0.f, 1.f)); };
	// Grille : décades et repères 2 / 5, paliers de 20 dB.
	const Color grid = sdl3::WithAlpha(m_theme.panelBorder, 120);
	const float small = m_theme.fontSize - 4.f;
	for (float decade = std::pow(10.f, std::floor(logMin)); decade <= maxHz; decade *= 10.f)
		for (const float m : {1.f, 2.f, 5.f}) {
			const float hz = decade * m;
			if (hz < minHz || hz > maxHz)
				continue;
			const float x = xOf(hz);
			c.Line(x, plot.y, x, plot.Bottom(), m == 1.f ? m_theme.panelBorder : grid);
			const std::string label = hz >= 1000.f ? std::format("{:g}k", hz / 1000.f) : std::format("{:g}", hz);
			const float lw = c.MeasureText(label, small); // étiquette gardée dans le cadre
			c.Text(std::clamp(x - lw * 0.5f, plot.x, r.Right() - lw - 4.f), plot.Bottom() + 3.f, label, m_theme.textDim, small);
		}
	for (float d = std::ceil(minDb / 20.f) * 20.f; d <= maxDb; d += 20.f) {
		const float yy = yOf(d);
		c.Line(plot.x, yy, plot.Right(), yy, grid);
		const std::string label = std::format("{:.0f}", d);
		c.Text(plot.x - 6.f - c.MeasureText(label, small), yy - c.LineHeight(small) * 0.5f, label, m_theme.textDim, small);
	}
	// Courbe : un point par pixel au plus (maximum des cases qui y tombent), aire remplie.
	c.PushClip(plot);
	const size_t n = std::min(freqs.size(), db.size());
	std::vector<float> column(size_t(plot.w) + 1, minDb - 1.f);
	for (size_t i = 0; i < n; ++i) {
		if (freqs[i] < minHz || freqs[i] > maxHz)
			continue;
		const size_t col = size_t(std::clamp(xOf(freqs[i]) - plot.x, 0.f, plot.w));
		column[col] = std::max(column[col], db[i]);
	}
	std::vector<float> xy;
	float last = minDb;
	for (size_t col = 0; col < column.size(); ++col) {
		if (column[col] >= minDb - 0.5f)
			last = column[col];
		const float x = plot.x + float(col), yy = yOf(last);
		c.Line(x, yy, x, plot.Bottom(), sdl3::WithAlpha(fill, 60));
		xy.push_back(x);
		xy.push_back(yy);
	}
	if (xy.size() >= 4)
		c.Polyline(xy.data(), xy.size() / 2, fill, 1.5f);
	c.PopClip();
	c.StrokeRoundRect(r, m_theme.radius, m_theme.panelBorder);
}

void Context::LevelMeter(const Rect &r, float rms, float peak, std::string_view label) {
	sdl3::Canvas &c = *m_canvas;
	Rect bar = r;
	if (!label.empty()) {
		c.Text(r.x, r.y + (r.h - c.LineHeight(m_theme.fontSize - 2.f)) * 0.5f, label, m_theme.textDim, m_theme.fontSize - 2.f);
		bar.x += 54.f;
		bar.w = std::max(0.f, bar.w - 54.f - 64.f);
	}
	const auto frac = [](float amplitude) {
		const float d = amplitude > 0.f ? 20.f * std::log10(amplitude) : -120.f;
		return std::clamp((d + 60.f) / 60.f, 0.f, 1.f);
	};
	c.FillRoundRect(bar, bar.h * 0.5f, m_theme.widget);
	// Dégradé par zones : vert jusqu'à −12 dB, jaune jusqu'à −3 dB, rouge au-delà.
	const float level = frac(rms);
	const struct {
		float from, to;
		Color color;
	} zones[] = {{0.f, 0.8f, m_theme.success}, {0.8f, 0.95f, m_theme.warning}, {0.95f, 1.f, m_theme.danger}};
	for (const auto &z : zones) {
		const float a = z.from, b = std::min(z.to, level);
		if (b > a)
			c.FillRect({bar.x + bar.w * a, bar.y + 1.f, bar.w * (b - a), bar.h - 2.f}, z.color);
	}
	const float px = bar.x + bar.w * frac(peak);
	c.FillRect({px - 1.f, bar.y, 2.f, bar.h}, m_theme.text);
	if (!label.empty()) {
		const float d = peak > 0.f ? 20.f * std::log10(peak) : -120.f;
		const std::string text = d <= -60.f ? std::string("−∞ dB") : std::format("{:.1f} dB", d);
		c.Text(r.Right() - c.MeasureText(text, m_theme.fontSize - 2.f), r.y + (r.h - c.LineHeight(m_theme.fontSize - 2.f)) * 0.5f, text,
			   m_theme.text, m_theme.fontSize - 2.f);
	}
}

} // namespace ui
