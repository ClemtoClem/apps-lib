// example-render3d — rendu 3D sur SDL_GPU par sdl3 (wrapper sdl3 seulement).
//
//   Scène 1 « Cube »                    : un cube qui tourne, éclairage frontal, grille.
//   Scène 2 « Géométries et lumières »  : boîte, sphère métal, tore et nœud torique PBR sur une
//                                         racine tournante, deux lumières ponctuelles et un spot.
//   Scène 3 « Ombres et ciel »          : sol damier, ombres du soleil (PCF), ciel et réflexions,
//                                         verre, anneau d'instances, particules, repère filaire.
//
// Souris dans la vue : glisser = tourner, clic droit glissé = déplacer, molette = zoom.
// Clavier : 1 2 3 scènes, Espace pause, R vue initiale.
//
//   cpp-maker run example-render3d [--scene=N]
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <sdl3/render/render3d.hpp>

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr std::array<std::string_view, 3> kScenes = {"Cube", "Géométries et lumières", "Ombres et ciel"};

math::FMatrix4 Rotation(const math::FVector3 &axis, float angle) { return sdl3::RotationMatrix(math::FQuaternion::AxisAngle(axis, angle)); }

math::FMatrix4 At(const math::FVector3 &p, const math::FMatrix4 &rotation = math::FMatrix4::Identity(), float scale = 1.f) {
	return math::FMatrix4::Translation(p) * rotation * math::FMatrix4::Scale({scale, scale, scale});
}

/// Damier RGBA (sol), deux gris.
std::vector<uint8_t> Checker(uint32_t size, uint32_t cells) {
	std::vector<uint8_t> px(size_t(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y)
		for (uint32_t x = 0; x < size; ++x) {
			const bool dark = ((x * cells / size) + (y * cells / size)) % 2 == 0;
			const uint8_t v = dark ? 150 : 205;
			uint8_t *p = &px[(size_t(y) * size + x) * 4];
			p[0] = v, p[1] = v, p[2] = uint8_t(v + 6), p[3] = 255;
		}
	return px;
}

/// Teinte (0..1) → couleur linéaire saturée.
sdl3::LinearColor Hue(float h) {
	const auto channel = [h](float offset) { return 0.5f + 0.5f * std::cos(2.f * kPi * (h + offset)); };
	return {channel(0.f), channel(-1.f / 3.f), channel(1.f / 3.f)};
}

class Render3dDemo final : public app::Application {
public:
	explicit Render3dDemo(int scene) : m_scene(std::clamp(scene, 0, 2)) {}

protected:
	bool OnInit() override {
		m_r3d = sdl3::Renderer3D::Create(*Gpu());
		if (!m_r3d)
			return false;
		using sdl3::MeshData;
		m_cube = m_r3d->Upload(MeshData::Cube());
		m_sphere = m_r3d->Upload(MeshData::Sphere(0.5f, 48, 32));
		m_torus = m_r3d->Upload(MeshData::Torus(0.8f, 0.3f, 24, 64));
		m_knot = m_r3d->Upload(MeshData::TorusKnot(0.6f, 0.22f, 160, 16, 2, 3));
		m_plane = m_r3d->Upload(MeshData::Plane(1.f, 1.f));
		m_cylinder = m_r3d->Upload(MeshData::Cylinder(0.5f, 0.5f, 1.f, 40));
		m_cone = m_r3d->Upload(MeshData::Cylinder(0.f, 0.6f, 1.2f, 40));
		const std::vector<uint8_t> checker = Checker(256, 8);
		m_checker = m_r3d->CreateTexture(checker, 256, 256);
		if (!m_cube || !m_sphere || !m_torus || !m_knot || !m_plane || !m_cylinder || !m_cone)
			return false;
		LoadScene(m_scene);
		return true;
	}

	void OnFixedUpdate(double step) override {
		if (!m_paused)
			m_time += float(step) * m_speed;
	}

	void OnUpdate(double) override {
		ui::Context &u = Ui();
		if (u.WantsKeyboard())
			return;
		for (int i = 0; i < 3; ++i)
			if (u.KeycodePressed(SDL_Keycode(SDLK_1 + unsigned(i))))
				LoadScene(i);
		if (u.KeycodePressed(SDLK_SPACE))
			m_paused = !m_paused;
		if (u.KeycodePressed(SDLK_R))
			ResetView();
	}

	void OnOffscreen() override {
		const float scale = Canvas().Scale();
		if (m_view.w <= 0.f || !m_target.Resize(*m_r3d, &Renderer(), int(m_view.w * scale), int(m_view.h * scale)))
			return;
		m_orbit.Apply(m_camera);
		UpdateLighting();
		switch (m_scene) {
		case 0: DrawCubeScene(); break;
		case 1: DrawShowcaseScene(); break;
		default: DrawShadowScene(); break;
		}
		if (m_grid)
			m_r3d->Grid(10.f, 1.f, m_scene == 2 ? 0.005f : -1.2f);
		if (m_axes)
			m_r3d->Axes({0.f, m_scene == 2 ? 0.01f : -1.19f, 0.f}, 1.5f);
		auto cmd = Gpu()->AcquireCommandBuffer();
		m_r3d->Render(cmd, m_target, m_camera, m_lighting);
		(void)cmd.Submit();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		constexpr float kPanel = 300.f;
		m_view = {area.x, area.y, area.w - kPanel, area.h};
		sdl3::Canvas &c = Canvas();
		if (sdl3::Texture *view = m_target.View())
			c.Image(*view, m_view);
		(void)m_orbit.HandleMouse(u.MouseX(), u.MouseY(), u.MouseDown(), u.MouseDown(sdl3::MouseButton::Right) || u.MouseDown(sdl3::MouseButton::Middle),
								  u.WheelY(), u.MouseOver(m_view));
		DrawOverlay(m_view.Inset(14));

		const sdl3::FRect side{m_view.Right(), area.y, kPanel, area.h};
		c.FillRect(side, th.panel);
		c.Line(side.x, side.y, side.x, side.Bottom(), th.panelBorder);
		u.BeginScroll("panel", side.Inset(14), {.axes = ui::ScrollAxes::Vertical});
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR), "Scène");
		int scene = m_scene;
		if (u.Choice(u.Next(30), "scene", scene, kScenes))
			LoadScene(scene);
		u.Checkbox("Animation en pause (Espace)", m_paused);
		u.SliderFloat("Vitesse", m_speed, 0.f, 3.f, "{:.1f}×");

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE), "Objet principal");
		u.SliderFloat("Métal", m_metallic, 0.f, 1.f);
		u.SliderFloat("Rugosité", m_roughness, 0.f, 1.f);

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::WB_SUNNY), "Lumières");
		u.SliderFloat("Azimut", m_sunAzimuth, -180.f, 180.f, "{:.0f}°");
		u.SliderFloat("Hauteur", m_sunElevation, 5.f, 90.f, "{:.0f}°");
		u.SliderFloat("Soleil", m_lighting.sun.intensity, 0.f, 6.f, "{:.1f}");
		u.Checkbox("Ombres du soleil", m_lighting.sun.castShadow);
		u.Checkbox("Lumières ponctuelles et spot", m_extraLights);

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TERRAIN), "Environnement");
		u.Checkbox("Ciel", m_lighting.environment.drawSky);
		u.SliderFloat("Ambiance", m_lighting.environment.intensity, 0.f, 2.f);
		u.SliderFloat("Exposition", m_lighting.exposure, 0.2f, 3.f, "{:.2f}");

		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::GRID_ON), "Affichage");
		u.Checkbox("Grille", m_grid);
		u.Checkbox("Axes", m_axes);
		u.Checkbox("Boîtes englobantes", m_bounds);
		u.EndScroll();
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "reset", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::CENTER_FOCUS_STRONG), .label = "Vue initiale", .tooltip = "Replace la caméra (R)"});
		left.push_back({.id = "pause", .icon = m_paused ? ui::icons::Codepoint(ui::icons::MaterialIcons::PLAY_ARROW) : ui::icons::Codepoint(ui::icons::MaterialIcons::PAUSE), .label = m_paused ? "Reprendre" : "Pause",
						.active = m_paused, .separator = true});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "reset")
			ResetView();
		else if (id == "pause")
			m_paused = !m_paused;
	}

private:
	void ResetView() {
		m_orbit = {};
		m_orbit.target = m_scene == 2 ? math::FVector3{0.f, 1.f, 0.f} : math::FVector3{};
		m_orbit.distance = m_scene == 0 ? 4.5f : m_scene == 1 ? 10.f : 12.f;
		m_orbit.pitch = m_scene == 0 ? 0.35f : 0.3f;
		m_orbit.yaw = m_scene == 2 ? 0.5f : 0.f;
	}

	/// Réglages de départ de chaque scène (repris des exemples d'origine).
	void LoadScene(int scene) {
		m_scene = scene;
		m_lighting = {};
		sdl3::Environment &env = m_lighting.environment;
		switch (scene) {
		case 0:
			m_sunAzimuth = 35.f, m_sunElevation = 40.f;
			m_lighting.sun.intensity = 2.6f;
			m_lighting.sun.castShadow = false;
			env.drawSky = false;
			env.background = sdl3::Srgb(0x14161e);
			env.intensity = 0.35f;
			m_metallic = 0.f, m_roughness = 0.4f;
			m_grid = true;
			break;
		case 1:
			m_sunAzimuth = -20.f, m_sunElevation = 35.f;
			m_lighting.sun.intensity = 0.8f; // discret : les lumières ponctuelles portent la scène
			m_lighting.sun.castShadow = false;
			env.drawSky = false;
			env.background = sdl3::Srgb(0x0a0a0e);
			env.intensity = 0.15f;
			m_metallic = 0.9f, m_roughness = 0.25f;
			m_grid = true;
			break;
		default:
			m_sunAzimuth = 35.f, m_sunElevation = 42.f;
			m_lighting.sun.intensity = 2.8f;
			m_lighting.sun.shadowExtent = 9.f;
			m_metallic = 1.f, m_roughness = 0.12f;
			m_grid = false;
			break;
		}
		m_extraLights = true;
		ResetView();
	}

	void UpdateLighting() {
		const float az = m_sunAzimuth * math::DEG2RAD, el = m_sunElevation * math::DEG2RAD;
		m_lighting.sun.direction = math::FVector3{std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)} * -1.f;
		m_lighting.points.clear();
		m_lighting.spots.clear();
		if (!m_extraLights || m_scene == 0)
			return;
		const float t = m_time;
		if (m_scene == 1) {
			m_lighting.points.push_back({.position = {-4.f + std::sin(t) * 0.8f, 2.f, 2.f}, .color = {1.f, 0.15f, 0.1f}, .intensity = 26.f, .range = 14.f});
			m_lighting.points.push_back({.position = {4.f, 2.f + std::cos(t) * 0.6f, 2.f}, .color = {0.15f, 0.3f, 1.f}, .intensity = 26.f, .range = 14.f});
			m_lighting.spots.push_back({.position = {0.f, 6.f, 4.f}, .direction = {0.f, -1.f, -0.4f}, .intensity = 60.f, .range = 18.f, .angle = 0.42f, .penumbra = 0.4f});
		} else {
			m_lighting.points.push_back({.position = {-3.f, 2.2f + 0.4f * std::sin(t * 1.3f), 2.f}, .color = {1.f, 0.3f, 0.2f}, .intensity = 18.f, .range = 9.f});
			m_lighting.spots.push_back({.position = {3.f, 5.f, 3.f}, .direction = {-0.35f, -1.f, -0.45f}, .color = {1.f, 0.95f, 0.85f}, .intensity = 50.f, .range = 14.f, .angle = 0.5f, .penumbra = 0.35f});
		}
	}

	void DrawObject(sdl3::MeshId mesh, const math::FMatrix4 &model, const sdl3::Material &material) {
		m_r3d->Draw(mesh, model, material);
		if (m_bounds)
			m_r3d->WireBox(m_r3d->Bounds(mesh), model, 255, 210, 90);
	}

	/// Petites sphères émissives aux positions des lumières ponctuelles.
	void DrawLightMarkers() {
		for (const sdl3::PointLight &p : m_lighting.points)
			m_r3d->Draw(m_sphere, At(p.position, math::FMatrix4::Identity(), 0.18f), sdl3::Material::Emissive(p.color, 3.f));
		for (const sdl3::SpotLight &s : m_lighting.spots)
			m_r3d->Draw(m_cone, At(s.position, math::FMatrix4::Identity(), 0.3f), sdl3::Material::Emissive(s.color, 2.f));
	}

	void DrawCubeScene() {
		const math::FMatrix4 spin = Rotation({0.f, 1.f, 0.f}, m_time * 0.8f) * Rotation({1.f, 0.f, 0.f}, m_time * 0.48f);
		DrawObject(m_cube, At({}, spin, 1.4f), sdl3::Material::Pbr(sdl3::Srgb(80, 140, 220), m_metallic, m_roughness));
	}

	void DrawShowcaseScene() {
		const math::FMatrix4 root = Rotation({0.f, 1.f, 0.f}, m_time * 0.25f);
		const float spin = m_time * 1.2f;
		const auto child = [&](float x, const math::FMatrix4 &rotation) { return root * math::FMatrix4::Translation({x, 0.f, 0.f}) * rotation; };
		DrawObject(m_cube, child(-4.5f, Rotation({0.f, 1.f, 0.f}, spin)) * math::FMatrix4::Scale({1.4f, 1.4f, 1.4f}),
				   sdl3::Material::Plastic(sdl3::Srgb(80, 140, 220)));
		DrawObject(m_sphere, child(-1.5f, Rotation({1.f, 0.f, 0.f}, spin)) * math::FMatrix4::Scale({1.8f, 1.8f, 1.8f}),
				   sdl3::Material::Metal(sdl3::Srgb(220, 220, 230), 0.2f));
		DrawObject(m_torus, child(1.5f, Rotation({1.f, 1.f, 0.f}, spin)),
				   sdl3::Material::Pbr(sdl3::Srgb(230, 180, 60), m_metallic, m_roughness));
		DrawObject(m_knot, child(4.5f, Rotation({0.f, 1.f, 0.f}, -spin)), sdl3::Material::Pbr(sdl3::Srgb(200, 60, 200), 0.2f, 0.4f));
		DrawLightMarkers();
	}

	void DrawShadowScene() {
		sdl3::Material ground = sdl3::Material::Plastic(sdl3::Srgb(0xd8d8de), 0.85f);
		ground.texture = m_checker;
		ground.uvScale = {5.f, 5.f};
		m_r3d->Draw(m_plane, math::FMatrix4::Scale({20.f, 1.f, 20.f}), ground);
		DrawObject(m_cube, At({-2.2f, 0.75f, 1.f}, Rotation({0.f, 1.f, 0.f}, 0.5f), 1.5f), sdl3::Material::Pbr(sdl3::Srgb(200, 60, 60), 0.1f, 0.4f));
		DrawObject(m_sphere, At({1.4f, 1.f + 0.25f * std::sin(m_time * 1.5f), 1.2f}, math::FMatrix4::Identity(), 1.8f),
				   sdl3::Material::Pbr(sdl3::Srgb(225, 225, 232), m_metallic, m_roughness));
		DrawObject(m_cylinder, At({0.f, 0.9f, -2.2f}, math::FMatrix4::Identity(), 1.f) * math::FMatrix4::Scale({1.f, 1.8f, 1.f}),
				   sdl3::Material::Pbr(sdl3::Srgb(60, 170, 120), 0.f, 0.3f));
		DrawObject(m_cone, At({-3.6f, 0.6f, -2.f}), sdl3::Material::Plastic(sdl3::Srgb(240, 180, 60)));
		DrawObject(m_torus, At({3.4f, 0.32f, -1.4f}, Rotation({0.f, 1.f, 0.f}, m_time * 0.4f), 1.f),
				   sdl3::Material::Metal(sdl3::Srgb(235, 190, 120), 0.3f));
		// Verre : transparent, trié et dessiné après les opaques.
		m_r3d->Draw(m_cube, At({0.2f, 0.6f, 3.4f}, Rotation({0.f, 1.f, 0.f}, -0.3f), 1.2f), sdl3::Material::Glass(sdl3::Srgb(150, 210, 255), 0.3f));

		// Anneau de 24 cubes instanciés (un seul appel), teintes par instance.
		std::array<sdl3::Instance, 24> ring;
		for (size_t i = 0; i < ring.size(); ++i) {
			const float a = float(i) / float(ring.size()) * 2.f * kPi + m_time * 0.3f;
			ring[i].model = At({std::cos(a) * 6.5f, 0.25f, std::sin(a) * 6.5f}, Rotation({0.f, 1.f, 0.f}, -a), 0.5f);
			const sdl3::LinearColor hue = Hue(float(i) / float(ring.size()));
			ring[i].tint = {hue.x, hue.y, hue.z, 1.f};
		}
		m_r3d->DrawInstances(m_cube, sdl3::Material::Plastic({1.f, 1.f, 1.f}, 0.35f), ring);

		// Particules : spirale de petits cubes émissifs.
		std::array<sdl3::Instance, 40> sparks;
		for (size_t i = 0; i < sparks.size(); ++i) {
			const float k = float(i), a = k * 0.55f + m_time * 1.5f;
			sparks[i].model = At({4.6f + 0.5f * std::cos(a), 0.6f + 0.07f * k, 2.6f + 0.5f * std::sin(a)}, Rotation({1.f, 1.f, 0.f}, a), 0.07f);
		}
		m_r3d->DrawInstances(m_cube, sdl3::Material::Emissive(sdl3::Srgb(255, 220, 90), 5.f), sparks);

		// Repère filaire au sol.
		for (const auto &[a, b] : std::array<std::pair<math::FVector3, math::FVector3>, 4>{
				 {{{-5.f, 0.01f, 4.5f}, {5.f, 0.01f, 4.5f}}, {{-5.f, 0.01f, -4.5f}, {5.f, 0.01f, -4.5f}}, {{-5.f, 0.01f, 4.5f}, {-5.f, 0.01f, -4.5f}}, {{5.f, 0.01f, 4.5f}, {5.f, 0.01f, -4.5f}}}})
			m_r3d->Line(a, b, 80, 220, 240);
		DrawLightMarkers();
	}

	void DrawOverlay(const sdl3::FRect &r) {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		const sdl3::RenderStats &s = m_r3d->Stats();
		const std::string line1 = std::string(kScenes[size_t(m_scene)]);
		const std::string line2 = std::format("{} appels · {} instances · {} triangles · {:.0f} i/s", s.drawCalls, s.instances, s.triangles, Fps());
		const float w = std::max(c.MeasureText(line1, 15.f, sdl3::CanvasFont::Bold), c.MeasureText(line2, 12.f)) + 24.f;
		const sdl3::FRect box{r.x, r.y, w, 50.f};
		c.FillRoundRect(box, 8.f, sdl3::Rgba(10, 12, 18, 170));
		c.Text(box.x + 12.f, box.y + 8.f, line1, th.text, 15.f, sdl3::CanvasFont::Bold);
		c.Text(box.x + 12.f, box.y + 29.f, line2, th.textDim, 12.f);
		const std::string_view help = "Glisser : tourner · clic droit : déplacer · molette : zoom";
		c.Text(r.x, r.Bottom() - 16.f, help, sdl3::Rgba(230, 232, 240, 170), 12.f);
	}

	std::unique_ptr<sdl3::Renderer3D> m_r3d;
	sdl3::RenderTarget3D m_target;
	sdl3::Camera3D m_camera;
	sdl3::OrbitController m_orbit;
	sdl3::Lighting m_lighting;
	sdl3::MeshId m_cube = 0, m_sphere = 0, m_torus = 0, m_knot = 0, m_plane = 0, m_cylinder = 0, m_cone = 0;
	sdl3::TextureId m_checker = 0;
	sdl3::FRect m_view{};
	int m_scene = 0;
	float m_time = 0.f, m_speed = 1.f;
	float m_metallic = 0.f, m_roughness = 0.4f;
	float m_sunAzimuth = 30.f, m_sunElevation = 40.f;
	bool m_paused = false, m_extraLights = true, m_grid = true, m_axes = false, m_bounds = false;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Rendu 3D";
	config.width = 1280;
	config.height = 760;
	config.customFrame = true;
	config.gpu = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR);
	int scene = 0;
	for (const std::string &arg : config.ParseArgs(argc, argv))
		if (arg.starts_with("--scene="))
			scene = std::atoi(arg.c_str() + 8) - 1;
	return Render3dDemo(scene).Run(config);
}
