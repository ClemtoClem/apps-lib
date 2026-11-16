// example-platform — une application complète sur `app::Application` (graphics.app).
//
// Des carrés rebondissent dans la fenêtre. La simulation avance par pas fixes (réglables
// de 10 à 240 Hz) ; le rendu interpole entre les deux derniers états, si bien que le
// mouvement reste fluide même avec une simulation lente : désactiver l'interpolation (I)
// à 10 Hz montre la différence. Un panneau affiche les mesures en direct.
//
//   ↑ / ↓  fréquence de simulation       I  interpolation       Espace  couleurs
//   Échap  quitter
//
//   cpp-maker run example-platform
//   build/host/debug/bin/example-platform --frames=120 --screenshot=capture.png
#include <algorithm>
#include <array>
#include <cmath>
#include <format>

#include <app/app.hpp>

namespace {

constexpr std::array kRates = {10.0, 20.0, 30.0, 60.0, 120.0, 240.0};
constexpr float kSize = 56.f;
constexpr float kHelpHeight = 30.f;

struct Body {
	float x, y, vx, vy; ///< état simulé (coordonnées logiques)
	float px, py;       ///< état au pas précédent (interpolation)
	sdl3::Color cold, warm;
};

class BouncingSquares final : public app::Application {
protected:
	bool OnInit() override {
		m_bodies = {
			Body{80, 90, 260, 190, 80, 90, sdl3::Rgba(74, 144, 255), sdl3::Rgba(255, 150, 70)},
			Body{420, 260, -210, 150, 420, 260, sdl3::Rgba(96, 200, 160), sdl3::Rgba(240, 90, 120)},
			Body{260, 380, 170, -240, 260, 380, sdl3::Rgba(170, 120, 255), sdl3::Rgba(250, 210, 80)},
		};
		SetFixedStep(1.0 / kRates[m_rate]);
		return true;
	}

	void OnUpdate(double) override {
		sdl3::EventsManager &in = Events();
		if (in.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed())
			RequestQuit();
		if (in.KeyScan(SDL_SCANCODE_SPACE).IsJustPressed())
			m_warm = !m_warm;
		if (in.KeyScan(SDL_SCANCODE_I).IsJustPressed())
			m_interpolate = !m_interpolate;
		const size_t before = m_rate;
		if (in.KeyScan(SDL_SCANCODE_UP).IsJustPressed())
			m_rate = std::min(m_rate + 1, kRates.size() - 1);
		if (in.KeyScan(SDL_SCANCODE_DOWN).IsJustPressed() && m_rate > 0)
			--m_rate;
		if (m_rate != before) {
			SetFixedStep(1.0 / kRates[m_rate]);
			Ui().Notify(std::format("Simulation à {:.0f} Hz", kRates[m_rate]));
		}
	}

	void OnFixedUpdate(double step) override {
		const float dt = float(step);
		for (Body &b : m_bodies) {
			b.px = b.x;
			b.py = b.y;
			b.x += b.vx * dt;
			b.y += b.vy * dt;
			// Rebond sur les bords de la zone de jeu (vitesse inversée, position ramenée).
			if (b.x < 0.f || b.x + kSize > m_area.w) {
				b.vx = -b.vx;
				b.x = std::clamp(b.x, 0.f, std::max(0.f, m_area.w - kSize));
			}
			if (b.y < 0.f || b.y + kSize > m_area.h) {
				b.vy = -b.vy;
				b.y = std::clamp(b.y, 0.f, std::max(0.f, m_area.h - kSize));
			}
		}
		++m_simulated;
	}

	void OnRender(const sdl3::FRect &area, double alpha) override {
		sdl3::Canvas &c = Canvas();
		m_area = {0, 0, area.w, area.h - kHelpHeight}; // les carrés restent au-dessus du bandeau d'aide
		c.FillGradientV(area, sdl3::Rgba(18, 24, 38), sdl3::Rgba(10, 13, 22));
		// Grille légère : repère du mouvement.
		for (float x = area.x; x < area.Right(); x += 40.f)
			c.Line(x, area.y, x, area.Bottom(), sdl3::Rgba(255, 255, 255, 10));
		for (float y = area.y; y < area.Bottom(); y += 40.f)
			c.Line(area.x, y, area.Right(), y, sdl3::Rgba(255, 255, 255, 10));

		const float a = m_interpolate ? float(alpha) : 1.f;
		for (const Body &b : m_bodies) {
			const float x = area.x + b.px + (b.x - b.px) * a, y = area.y + b.py + (b.y - b.py) * a;
			const sdl3::Color color = m_warm ? b.warm : b.cold;
			// Traînée : positions passées extrapolées le long de la vitesse.
			for (int i = 4; i >= 1; --i) {
				const float t = float(i) * 0.018f;
				c.FillRoundRect({x - b.vx * t, y - b.vy * t, kSize, kSize}, 12.f, sdl3::WithAlpha(color, uint8_t(18 * (5 - i))));
			}
			c.FillRoundRect({x, y, kSize, kSize}, 12.f, color);
			c.StrokeRoundRect({x, y, kSize, kSize}, 12.f, sdl3::Rgba(255, 255, 255, 70), 1.5f);
		}
		DrawStats(area);
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "slower", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CHEVRON_LEFT), .tooltip = "Simulation plus lente (↓)", .enabled = m_rate > 0});
		left.push_back({.id = "faster", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CHEVRON_RIGHT), .tooltip = "Simulation plus rapide (↑)",
						.enabled = m_rate + 1 < kRates.size()});
		left.push_back({.id = "interp", .label = "Interpolation", .tooltip = "Rendu interpolé entre deux pas (I)", .active = m_interpolate,
						.separator = true});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "slower" && m_rate > 0)
			SetFixedStep(1.0 / kRates[--m_rate]);
		else if (id == "faster" && m_rate + 1 < kRates.size())
			SetFixedStep(1.0 / kRates[++m_rate]);
		else if (id == "interp")
			m_interpolate = !m_interpolate;
	}

private:
	void DrawStats(const sdl3::FRect &area) {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		const sdl3::FRect card{area.Right() - 250.f, area.y + 16.f, 234.f, 150.f};
		c.FillRoundRect(card, 10.f, sdl3::WithAlpha(th.panel, 225));
		c.StrokeRoundRect(card, 10.f, th.panelBorder);
		c.Text(card.x + 14, card.y + 12, "MESURES", th.textDim, 12, sdl3::CanvasFont::Bold);
		const std::array<std::pair<std::string, std::string>, 5> rows = {{
			{"Affichage", std::format("{:.0f} img/s", Fps())},
			{"Simulation", std::format("{:.0f} Hz", kRates[m_rate])},
			{"Pas simulés", std::format("{}", m_simulated)},
			{"Temps abandonné", std::format("{:.1f} ms", Clock().DroppedSeconds() * 1000.0)},
			{"Interpolation", m_interpolate ? "active" : "désactivée"},
		}};
		float y = card.y + 36;
		for (const auto &[label, value] : rows) {
			c.Text(card.x + 14, y, label, th.textDim, 13);
			c.Text(card.Right() - 14 - c.MeasureText(value, 13, sdl3::CanvasFont::Bold), y, value, th.text, 13, sdl3::CanvasFont::Bold);
			y += 21;
		}
		const sdl3::FRect strip{area.x, area.Bottom() - kHelpHeight, area.w, kHelpHeight};
		c.FillRect(strip, sdl3::WithAlpha(th.title, 235));
		c.Line(strip.x, strip.y, strip.Right(), strip.y, th.panelBorder);
		const std::string_view help = "↑ ↓ fréquence · I interpolation · Espace couleurs · Échap quitter";
		c.Text(strip.x + 16, strip.y + 8, help, th.textDim, 12);
	}

	std::array<Body, 3> m_bodies{};
	sdl3::FRect m_area{0, 0, 960, 600};
	size_t m_rate = 3; // 60 Hz
	uint64_t m_simulated = 0;
	bool m_warm = false, m_interpolate = true;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Pas fixe et rendu interpolé";
	config.width = 960;
	config.height = 640;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::SPEED);
	config.ParseArgs(argc, argv);
	return BouncingSquares().Run(config);
}
