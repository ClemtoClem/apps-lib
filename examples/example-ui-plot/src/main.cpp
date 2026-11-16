// example-ui-plot — galerie de graphiques interactifs (ui::PlotView).
//
//   Rangée 1 : courbes + aire + barres d'erreur · barres + tiges · nuage de points sur deux axes
//   Rangée 2 : chandeliers OHLC · signal en direct · carte de chaleur (dessin au canvas)
//
// Sur chaque graphique : glisser = déplacer, molette = zoom (sur un axe si la souris est sur
// ses graduations), clic droit glissé = zoom rectangle, double-clic = ajuster, légende :
// survol = mise en avant, clic = masquer.
//
//   cpp-maker run example-ui-plot
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>

#include <app/app.hpp>
#include <ui/plot_view.hpp>

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

/// Générateur pseudo-aléatoire déterministe (mêmes données à chaque lancement).
struct Random {
	uint32_t state = 0x12345678u;
	float Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return float(state) / 4294967295.f;
	}
	float Gauss() { return (Next() + Next() + Next() + Next() - 2.f) * 1.7f; }
};

class PlotGallery final : public app::Application {
protected:
	bool OnInit() override {
		Random rng;
		// 1. Courbes, aire et mesures avec incertitude.
		m_curves.title = "Courbes, aire et mesures";
		m_curves.xLabel = "t (s)";
		ui::PlotLayer &area = m_curves.Series("enveloppe", ui::PlotStyle::Area, sdl3::Rgba(74, 144, 255, 200));
		ui::PlotLayer &sine = m_curves.Series("sin(t)", ui::PlotStyle::Line, sdl3::Rgba(96, 200, 140));
		for (int i = 0; i <= 120; ++i) {
			const float t = float(i) / 10.f;
			sine.x.push_back(t), sine.y.push_back(std::sin(t));
			area.x.push_back(t), area.y.push_back(0.5f + 0.4f * std::cos(t * 0.5f));
		}
		ui::PlotLayer &measures = m_curves.Series("mesures (± écart type)", ui::PlotStyle::ErrorBars, sdl3::Rgba(236, 180, 72));
		for (int i = 0; i <= 12; ++i) {
			const float t = float(i);
			measures.x.push_back(t), measures.y.push_back(std::sin(t) + rng.Gauss() * 0.1f), measures.err.push_back(0.1f + rng.Next() * 0.15f);
		}
		// 2. Barres et tiges : 12 mois.
		m_bars.title = "Ventes mensuelles et objectifs";
		m_bars.xLabel = "mois";
		ui::PlotLayer &sales = m_bars.Series("ventes", ui::PlotStyle::Bars);
		ui::PlotLayer &target = m_bars.Series("objectif", ui::PlotStyle::Stems, sdl3::Rgba(222, 96, 120));
		for (int m = 1; m <= 12; ++m) {
			sales.x.push_back(float(m)), sales.y.push_back(40.f + 25.f * std::sin(float(m) * 0.6f) + rng.Next() * 15.f);
			target.x.push_back(float(m)), target.y.push_back(55.f + float(m) * 1.5f);
		}
		// 3. Nuage de points (axe gauche) et moyenne glissante (axe droit, autre unité).
		m_scatter.title = "Corrélation (deux axes)";
		m_scatter.yLabel = "masse (kg)";
		m_scatter.y2Label = "IMC";
		ui::PlotLayer &points = m_scatter.Series("individus", ui::PlotStyle::Scatter, sdl3::Rgba(170, 120, 255));
		points.width = 2.f;
		for (int i = 0; i < 150; ++i) {
			const float h = 150.f + rng.Next() * 45.f;
			points.x.push_back(h), points.y.push_back((h - 100.f) * 0.9f + rng.Gauss() * 6.f);
		}
		std::ranges::sort(points.x); // abscisses triées pour la courbe secondaire
		ui::PlotLayer &bmi = m_scatter.Series("IMC moyen", ui::PlotStyle::Line, sdl3::Rgba(80, 200, 220));
		bmi.secondaryAxis = true;
		for (int h = 150; h <= 195; h += 5)
			bmi.x.push_back(float(h)), bmi.y.push_back(((float(h) - 100.f) * 0.9f) / std::pow(float(h) / 100.f, 2.f));
		// 4. Chandeliers : marche aléatoire sur 60 jours.
		m_candles.title = "Cours (OHLC)";
		m_candles.xLabel = "jour";
		ui::PlotLayer &price = m_candles.Series("action", ui::PlotStyle::Candles);
		float value = 100.f;
		for (int d = 0; d < 60; ++d) {
			const float open = value, close = std::max(10.f, value + rng.Gauss() * 2.5f);
			price.x.push_back(float(d)), price.open.push_back(open), price.close.push_back(close);
			price.high.push_back(std::max(open, close) + rng.Next() * 2.f), price.low.push_back(std::min(open, close) - rng.Next() * 2.f);
			value = close;
		}
		// 5. Signal en direct (alimenté chaque image).
		m_live.title = "Signal en direct (10 s)";
		m_live.xLabel = "t (s)";
		m_live.Series("capteur", ui::PlotStyle::Line, sdl3::Rgba(110, 200, 160));
		m_live.Series("moyenne", ui::PlotStyle::Line, sdl3::Rgba(236, 180, 72));
		return true;
	}

	void OnFixedUpdate(double step) override {
		// 60 mesures par seconde, fenêtre glissante de 10 s.
		m_t += float(step);
		ui::PlotLayer &raw = m_live.Layers()[0], &mean = m_live.Layers()[1];
		const float sample = std::sin(m_t * 2.f) + 0.4f * std::sin(m_t * 7.3f) + m_rng.Gauss() * 0.15f;
		raw.x.push_back(m_t), raw.y.push_back(sample);
		m_ema = m_ema * 0.95f + sample * 0.05f;
		mean.x.push_back(m_t), mean.y.push_back(m_ema);
		for (ui::PlotLayer *l : {&raw, &mean})
			while (!l->x.empty() && l->x.front() < m_t - 10.f) {
				l->x.erase(l->x.begin());
				l->y.erase(l->y.begin());
			}
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const sdl3::FRect body = area.Inset(12);
		const float w = (body.w - 24.f) / 3.f, h = (body.h - 12.f) / 2.f;
		const auto cell = [&](int col, int row) { return sdl3::FRect{body.x + float(col) * (w + 12.f), body.y + float(row) * (h + 12.f), w, h}; };
		m_curves.Draw(u, cell(0, 0));
		m_bars.Draw(u, cell(1, 0));
		m_scatter.Draw(u, cell(2, 0));
		m_candles.Draw(u, cell(0, 1));
		if (m_follow) { // le direct suit la dernière seconde tant qu'on ne déplace pas la vue
			m_live.xMin = std::max(0.f, m_t - 10.f);
			m_live.xMax = std::max(10.f, m_t);
			m_live.yMin = -2.f;
			m_live.yMax = 2.f;
		}
		if (m_live.Draw(u, cell(1, 1)))
			m_follow = false;
		DrawHeatmap(cell(2, 1));
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "fit", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CENTER_FOCUS_STRONG), .label = "Tout ajuster", .tooltip = "Ajuste les six graphiques"});
		left.push_back({.id = "follow", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW), .label = "Suivre le direct", .active = m_follow, .separator = true});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "fit")
			for (ui::PlotView *p : {&m_curves, &m_bars, &m_scatter, &m_candles})
				p->AutoFit();
		else if (id == "follow")
			m_follow = !m_follow;
	}

private:
	/// Carte de chaleur d'une fonction 2D, palette « magma » simplifiée et échelle.
	void DrawHeatmap(const sdl3::FRect &r) {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		c.FillRoundRect(r, th.radius, th.widget);
		c.Text(r.x + (r.w - c.MeasureText("Carte de chaleur", th.fontSize - 1.f, sdl3::CanvasFont::Bold)) * 0.5f, r.y + 5.f, "Carte de chaleur", th.text,
			   th.fontSize - 1.f, sdl3::CanvasFont::Bold);
		const sdl3::FRect map{r.x + 14.f, r.y + 28.f, r.w - 70.f, r.h - 42.f};
		constexpr int cols = 48, rows = 30;
		const auto palette = [](float t) {
			t = std::clamp(t, 0.f, 1.f);
			static constexpr std::array stops = {sdl3::Rgba(12, 8, 38), sdl3::Rgba(90, 22, 110), sdl3::Rgba(200, 50, 90), sdl3::Rgba(250, 150, 60),
												 sdl3::Rgba(252, 240, 170)};
			const float f = t * float(stops.size() - 1);
			const size_t i = std::min(stops.size() - 2, size_t(f));
			const float k = f - float(i);
			const auto mix = [k](uint8_t a, uint8_t b) { return uint8_t(float(a) + (float(b) - float(a)) * k); };
			return sdl3::Rgba(mix(stops[i].r, stops[i + 1].r), mix(stops[i].g, stops[i + 1].g), mix(stops[i].b, stops[i + 1].b));
		};
		const float cw = map.w / cols, ch = map.h / rows, phase = m_t * 0.6f;
		for (int y = 0; y < rows; ++y)
			for (int x = 0; x < cols; ++x) {
				const float u = float(x) / cols * 2.f * kPi, v = float(y) / rows * 2.f * kPi;
				const float value = 0.5f + 0.25f * std::sin(u + phase) * std::cos(v) + 0.25f * std::sin(0.5f * (u + v) - phase);
				c.FillRect({map.x + float(x) * cw, map.y + float(y) * ch, cw + 0.5f, ch + 0.5f}, palette(value));
			}
		c.StrokeRect(map, th.panelBorder);
		// Échelle de couleurs.
		const sdl3::FRect bar{map.Right() + 12.f, map.y, 12.f, map.h};
		for (int i = 0; i < 40; ++i)
			c.FillRect({bar.x, bar.y + bar.h * float(i) / 40.f, bar.w, bar.h / 40.f + 0.5f}, palette(1.f - float(i) / 39.f));
		c.StrokeRect(bar, th.panelBorder);
		c.Text(bar.Right() + 4.f, bar.y - 2.f, "1", th.textDim, 11);
		c.Text(bar.Right() + 4.f, bar.Bottom() - 12.f, "0", th.textDim, 11);
	}

	ui::PlotView m_curves, m_bars, m_scatter, m_candles, m_live;
	Random m_rng;
	float m_t = 0.f, m_ema = 0.f;
	bool m_follow = true;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Galerie de graphiques";
	config.width = 1320;
	config.height = 800;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::TABLE_CHART);
	config.ParseArgs(argc, argv);
	return PlotGallery().Run(config);
}
