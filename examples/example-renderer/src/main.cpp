// example-renderer — rendu 2D direct avec `sdl3::Renderer`.
//
// La scène est dessinée avec l'API brute du renderer (rectangles, lignes, cercles,
// géométrie colorée par sommet) dans une texture cible, en pixels physiques, puis
// affichée par le canvas ; le panneau latéral (couleurs, taille, mesures, journal des
// évènements) est une interface immédiate.
//
//   Flèches / WASD  déplacer le carré          R G B Y M  couleur du carré
//   Molette         taille du carré            Espace     fond aléatoire
//   Clic gauche     poser un cercle            Clic droit effacer les cercles
//   Déposer un fichier sur la fenêtre : il apparaît dans le journal.
//
//   cpp-maker run example-renderer
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <app/app.hpp>

namespace {

constexpr float kSpeed = 260.f;  ///< déplacement du carré (unités logiques / s)
constexpr float kPanelW = 300.f; ///< largeur du panneau latéral
constexpr size_t kLogLines = 14;

struct Swatch {
	std::string_view name;
	SDL_Keycode key;
	sdl3::Color color;
};
constexpr std::array kSwatches = {
	Swatch{"Rouge", SDLK_R, {235, 80, 80, 255}},	  Swatch{"Vert", SDLK_G, {90, 200, 120, 255}},
	Swatch{"Bleu", SDLK_B, {74, 144, 255, 255}},	  Swatch{"Jaune", SDLK_Y, {245, 200, 70, 255}},
	Swatch{"Magenta", SDLK_M, {220, 100, 220, 255}},
};

struct Circle {
	float x, y; ///< coordonnées logiques dans la scène
	sdl3::Color color;
};

class RendererDemo final : public app::Application {
protected:
	bool OnInit() override {
		// Journal : quelques types d'évènements, décrits en clair.
		for (const sdl3::EventType type : {sdl3::EventType::KEY_DOWN, sdl3::EventType::MOUSE_BUTTON_DOWN, sdl3::EventType::MOUSE_WHEEL,
										   sdl3::EventType::WINDOW_RESIZED, sdl3::EventType::WINDOW_FOCUS_GAINED,
										   sdl3::EventType::WINDOW_FOCUS_LOST, sdl3::EventType::GAMEPAD_ADDED, sdl3::EventType::GAMEPAD_REMOVED})
			(void)Events().Bind(type, [this](const sdl3::Event &e) { Log(Describe(e)); });
		return true;
	}

	void OnUpdate(double frameSeconds) override {
		sdl3::EventsManager &in = Events();
		const float dt = float(frameSeconds);
		const auto down = [&](SDL_Scancode a, SDL_Scancode b) { return in.KeyScan(a).IsPressed() || in.KeyScan(b).IsPressed(); };
		if (down(SDL_SCANCODE_LEFT, SDL_SCANCODE_A))
			m_x -= kSpeed * dt;
		if (down(SDL_SCANCODE_RIGHT, SDL_SCANCODE_D))
			m_x += kSpeed * dt;
		if (down(SDL_SCANCODE_UP, SDL_SCANCODE_W))
			m_y -= kSpeed * dt;
		if (down(SDL_SCANCODE_DOWN, SDL_SCANCODE_S))
			m_y += kSpeed * dt;
		if (m_scene.w > 0.f) { // taille de la scène connue après le premier affichage
			m_x = std::clamp(m_x, 0.f, std::max(0.f, m_scene.w - m_size));
			m_y = std::clamp(m_y, 0.f, std::max(0.f, m_scene.h - m_size));
		}

		for (const Swatch &s : kSwatches)
			if (in.Key(s.key).IsJustPressed())
				m_color = s.color;
		if (in.KeyScan(SDL_SCANCODE_SPACE).IsJustPressed())
			RandomBackground();
		if (in.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed())
			RequestQuit();
		for (const std::string &file : in.DroppedFiles())
			Log("Fichier déposé : " + file);
	}

	void OnOffscreen() override {
		if (m_scene.w < 1.f || m_scene.h < 1.f)
			return;
		sdl3::Renderer &r = Renderer();
		const float s = Canvas().Scale();
		const int w = int(m_scene.w * s), h = int(m_scene.h * s);
		if (!m_target || m_targetW != w || m_targetH != h) {
			auto texture = r.CreateTexture(sdl3::PixelFormat::RGBA32, sdl3::TextureAccess::TARGET, w, h);
			if (texture.IsError())
				return;
			m_target.emplace(std::move(texture.Value()));
			m_targetW = w;
			m_targetH = h;
		}
		r.SetTarget(*m_target);
		r.SetDrawColor(m_background);
		r.Clear();

		// Dégradé par sommet (RenderGeometry) : lueur en bas de la scène.
		const sdl3::FColor clear{0.f, 0.f, 0.f, 0.f}, glow{0.29f, 0.56f, 1.f, 0.22f};
		const float fw = float(w), fh = float(h);
		const std::array<sdl3::Vertex, 4> quad = {sdl3::Vertex{{0, fh * 0.55f}, clear, {}}, sdl3::Vertex{{fw, fh * 0.55f}, clear, {}},
												  sdl3::Vertex{{fw, fh}, glow, {}}, sdl3::Vertex{{0, fh}, glow, {}}};
		const std::array<int, 6> indices = {0, 1, 2, 0, 2, 3};
		r.SetBlendMode(sdl3::BlendMode::BLEND);
		r.RenderGeometry(quad, indices);

		// Grille (DrawLines) tous les 50 unités logiques.
		r.SetDrawColor(sdl3::Color{255, 255, 255, 18});
		for (float x = 0.f; x < m_scene.w; x += 50.f)
			r.DrawLine(x * s, 0.f, x * s, fh);
		for (float y = 0.f; y < m_scene.h; y += 50.f)
			r.DrawLine(0.f, y * s, fw, y * s);

		// Cercles posés à la souris : disque translucide + contour.
		for (const Circle &c : m_circles) {
			r.SetDrawColor(sdl3::Color{c.color.r, c.color.g, c.color.b, 70});
			r.FillCircle({c.x * s, c.y * s}, 18.f * s);
			r.SetDrawColor(c.color);
			r.DrawCircle({c.x * s, c.y * s}, 18.f * s);
		}

		// Carré : remplissage, contour, croix centrale.
		const sdl3::FRect square{m_x * s, m_y * s, m_size * s, m_size * s};
		r.SetDrawColor(m_color);
		r.FillRect(square);
		r.SetDrawColor(sdl3::Color{245, 245, 245, 255});
		r.DrawRect(square);
		const float cx = square.x + square.w / 2.f, cy = square.y + square.h / 2.f;
		r.SetDrawColor(sdl3::Color{255, 240, 120, 255});
		r.DrawLine(cx - 10.f * s, cy, cx + 10.f * s, cy);
		r.DrawLine(cx, cy - 10.f * s, cx, cy + 10.f * s);
		r.ResetTarget();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		const sdl3::FRect panel{area.Right() - kPanelW, area.y, kPanelW, area.h};
		const sdl3::FRect frame{area.x + 14.f, area.y + 14.f, panel.x - area.x - 28.f, area.h - 28.f};
		m_scene = {0, 0, frame.w, frame.h};

		// Scène : texture cible affichée telle quelle (pixels physiques → logiques).
		c.FillRoundRect(frame.Inset(-1), 8.f, th.panelBorder);
		if (m_target)
			c.Image(*m_target, frame);
		HandleSceneMouse(frame);
		DrawPanel(panel);
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "clear", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::DELETE_OUTLINE), .tooltip = "Effacer les cercles (clic droit)",
						.enabled = !m_circles.empty()});
		left.push_back({.id = "background", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::FORMAT_COLOR_FILL), .tooltip = "Fond aléatoire (Espace)"});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "clear")
			m_circles.clear();
		else if (id == "background")
			RandomBackground();
	}

private:
	/// Description courte d'un évènement pour le journal.
	static std::string Describe(const sdl3::Event &e) {
		switch (sdl3::EventType(e.Type())) {
		case sdl3::EventType::KEY_DOWN: return std::format("Touche {}", SDL_GetKeyName(e.Key().key));
		case sdl3::EventType::MOUSE_BUTTON_DOWN: {
			static constexpr std::array<std::string_view, 6> kButtons = {"?", "gauche", "milieu", "droit", "X1", "X2"};
			const SDL_MouseButtonEvent &b = e.MouseButton();
			return std::format("Clic {} en {:.0f} × {:.0f}", kButtons[std::min<size_t>(b.button, 5)], b.x, b.y);
		}
		case sdl3::EventType::MOUSE_WHEEL: return std::format("Molette {:+.0f}", e.MouseWheel().y);
		case sdl3::EventType::WINDOW_RESIZED: return std::format("Fenêtre {} × {}", e.Window().data1, e.Window().data2);
		case sdl3::EventType::WINDOW_FOCUS_GAINED: return "Fenêtre active";
		case sdl3::EventType::WINDOW_FOCUS_LOST: return "Fenêtre inactive";
		case sdl3::EventType::GAMEPAD_ADDED: return "Manette connectée";
		case sdl3::EventType::GAMEPAD_REMOVED: return "Manette déconnectée";
		default: return std::string(e.Describe().c_str());
		}
	}

	/// Fond sombre aléatoire (générateur congruentiel linéaire).
	void RandomBackground() {
		m_seed = m_seed * 1664525u + 1013904223u;
		m_background = {uint8_t(16 + (m_seed >> 8) % 48), uint8_t(16 + (m_seed >> 16) % 48), uint8_t(24 + (m_seed >> 24) % 56), 255};
	}

	void Log(std::string line) {
		m_log.push_front(std::move(line));
		if (m_log.size() > kLogLines)
			m_log.pop_back();
		++m_events;
	}

	void HandleSceneMouse(const sdl3::FRect &frame) {
		ui::Context &u = Ui();
		if (!frame.Contains(u.MouseX(), u.MouseY()))
			return;
		const float mx = u.MouseX() - frame.x, my = u.MouseY() - frame.y;
		if (u.MousePressed(sdl3::MouseButton::Left)) {
			// Teinte selon la position : rouge vers la droite, bleu vers le bas.
			m_circles.push_back({mx, my, sdl3::Color{uint8_t(80 + mx / frame.w * 175), 120, uint8_t(80 + my / frame.h * 175), 255}});
		}
		if (u.MousePressed(sdl3::MouseButton::Right))
			m_circles.clear();
		if (u.WheelY() != 0.f)
			m_size = std::clamp(m_size + u.WheelY() * 6.f, 12.f, 220.f);
	}

	void DrawPanel(const sdl3::FRect &panel) {
		sdl3::Canvas &c = Canvas();
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		c.FillRect(panel, th.title);
		c.Line(panel.x, panel.y, panel.x, panel.Bottom(), th.panelBorder);
		u.BeginColumn(panel.Inset(16));

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE), "Carré");
		const sdl3::FRect swatchRow = u.Next(34);
		const float sw = (swatchRow.w - 4 * 8.f) / float(kSwatches.size());
		for (size_t i = 0; i < kSwatches.size(); ++i) {
			const Swatch &s = kSwatches[i];
			const sdl3::FRect r{swatchRow.x + float(i) * (sw + 8.f), swatchRow.y, sw, swatchRow.h};
			const auto it = u.Interact(u.MakeId(s.name), r);
			const bool selected = m_color == s.color;
			c.FillRoundRect(r, 6.f, sdl3::Rgba(s.color.r, s.color.g, s.color.b));
			if (selected || it.hovered)
				c.StrokeRoundRect(r.Inset(-2), 8.f, selected ? th.text : th.textDim, 2.f);
			if (it.hovered)
				u.Tooltip(std::format("{} ({})", s.name, SDL_GetKeyName(s.key)));
			if (it.leftClicked)
				m_color = s.color;
		}
		u.SliderFloat("Taille", m_size, 12.f, 220.f, "{:.0f}");

		u.Space(6);
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::SPEED), "Mesures");
		const std::array<std::pair<std::string_view, std::string>, 5> stats = {{
			{"Images / s", std::format("{:.0f}", Fps())},
			{"Position", std::format("{:.0f} × {:.0f}", m_x, m_y)},
			{"Cercles", std::format("{}", m_circles.size())},
			{"Évènements", std::format("{}", m_events)},
			{"Texture cible", std::format("{} × {} px", m_targetW, m_targetH)},
		}};
		for (const auto &[label, value] : stats) {
			const sdl3::FRect row = u.Next(20);
			c.Text(row.x, row.y + 2, label, th.textDim, 13);
			c.Text(row.Right() - c.MeasureText(value, 13, sdl3::CanvasFont::Bold), row.y + 2, value, th.text, 13, sdl3::CanvasFont::Bold);
		}

		u.Space(6);
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::LIST), "Journal");
		for (const std::string &line : m_log) {
			const sdl3::FRect row = u.Next(18);
			c.Text(row.x, row.y, c.Ellipsize(line, row.w, 12, sdl3::CanvasFont::Mono), th.textDim, 12, sdl3::CanvasFont::Mono);
		}
		if (m_log.empty())
			u.Wrapped("Touches, clics, molette, fenêtre et fichiers déposés apparaissent ici.", th.textDim, 12);
		u.EndColumn();
	}

	sdl3::FRect m_scene{};
	std::optional<sdl3::Texture> m_target;
	int m_targetW = 0, m_targetH = 0;
	float m_x = 120.f, m_y = 120.f, m_size = 64.f;
	sdl3::Color m_color = kSwatches[2].color;
	sdl3::Color m_background{24, 28, 40, 255};
	std::vector<Circle> m_circles;
	std::deque<std::string> m_log;
	uint64_t m_events = 0;
	uint32_t m_seed = 12345u;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Rendu 2D — sdl3::Renderer";
	config.width = 1100;
	config.height = 700;
	config.customFrame = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::BRUSH);
	config.ParseArgs(argc, argv);
	return RendererDemo().Run(config);
}
