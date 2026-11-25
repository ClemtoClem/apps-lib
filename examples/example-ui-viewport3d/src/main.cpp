// example-ui-viewport3d — vues 3D (sdl3) dans une interface ordinaire.
//
// La même scène est rendue deux fois par image, dans deux cibles de rapports d'aspect très
// différents : une large vue perspective (caméra orbitale) et une étroite vue de dessus.
// Panneau de gauche : liste des objets, ajout / suppression, propriétés de la sélection
// (position, rotation, échelle, matériau, couleur).
//
// Perspective : clic = sélectionner, glisser = tourner, clic droit glissé = déplacer, molette = zoom.
// Dessus      : clic = sélectionner, glisser la sélection = la déplacer au sol, molette = zoom.
//
//   cpp-maker run example-ui-viewport3d
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <app/app.hpp>
#include <sdl3/render/render3d.hpp>

namespace {

enum class Shape { Cube, Sphere, Torus, Cylinder, Cone };
enum class Finish { Plastic, Metal, Glass, Emissive };
constexpr std::array<std::string_view, 5> kShapes = {"Cube", "Sphère", "Tore", "Cylindre", "Cône"};
constexpr std::array<std::string_view, 4> kFinishes = {"Plastique", "Métal", "Verre", "Émissif"};

struct Object {
	std::string name;
	Shape shape = Shape::Cube;
	sdl3::Transform transform;
	float yaw = 0.f; // degrés (rotation autour de Y)
	sdl3::Color color = sdl3::Rgba(90, 150, 230);
	Finish finish = Finish::Plastic;
	float roughness = 0.4f;
};

/// Une vue : cible de rendu, caméra, rectangle à l'écran.
struct View {
	std::string_view title;
	sdl3::RenderTarget3D target;
	sdl3::Camera3D camera;
	sdl3::FRect rect{};
};

class ViewportDemo final : public app::Application {
protected:
	bool OnInit() override {
		m_r3d = sdl3::Renderer3D::Create(*Gpu(), 2048);
		if (!m_r3d)
			return false;
		using sdl3::MeshData;
		m_meshes = {m_r3d->Upload(MeshData::Cube()), m_r3d->Upload(MeshData::Sphere(0.5f, 40, 24)), m_r3d->Upload(MeshData::Torus(0.45f, 0.18f, 20, 48)),
					m_r3d->Upload(MeshData::Cylinder(0.5f, 0.5f, 1.f, 36)), m_r3d->Upload(MeshData::Cylinder(0.f, 0.5f, 1.f, 36))};
		m_ground = m_r3d->Upload(MeshData::Plane(1.f, 1.f));
		if (std::ranges::find(m_meshes, 0u) != m_meshes.end() || !m_ground)
			return false;
		Add(Shape::Cube, {-1.8f, 0.5f, 0.f}, sdl3::Rgba(220, 80, 70));
		Add(Shape::Sphere, {0.f, 0.5f, 0.f}, sdl3::Rgba(225, 225, 235), Finish::Metal);
		Add(Shape::Torus, {1.8f, 0.2f, 0.4f}, sdl3::Rgba(236, 180, 72), Finish::Metal);
		Add(Shape::Cylinder, {0.6f, 0.5f, -2.f}, sdl3::Rgba(80, 190, 130));
		Add(Shape::Cone, {-1.2f, 0.5f, -2.2f}, sdl3::Rgba(150, 110, 240));
		Add(Shape::Cube, {0.f, 0.5f, 2.f}, sdl3::Rgba(140, 200, 255), Finish::Glass);
		m_selected = 1;
		m_orbit.target = {0.f, 0.4f, 0.f};
		m_orbit.distance = 9.f;
		m_orbit.pitch = 0.42f;
		m_orbit.yaw = 0.7f;
		m_lighting.sun.direction = {-0.5f, -1.f, -0.35f};
		m_lighting.sun.shadowExtent = 7.f;
		m_perspective.title = "Perspective";
		m_top.title = "Dessus";
		m_top.camera.fovY = 40.f * math::DEG2RAD;
		return true;
	}

	void OnOffscreen() override {
		m_orbit.Apply(m_perspective.camera);
		m_top.camera.position = {0.f, m_topHeight, 0.001f};
		m_top.camera.target = {};
		m_top.camera.up = {0.f, 0.f, -1.f}; // haut de la vue = -Z
		const float scale = Canvas().Scale();
		auto cmd = Gpu()->AcquireCommandBuffer();
		for (View *v : {&m_perspective, &m_top}) {
			if (v->rect.w < 8.f || !v->target.Resize(*m_r3d, &Renderer(), int(v->rect.w * scale), int(v->rect.h * scale)))
				continue;
			DrawScene(v == &m_top);
			m_r3d->Render(cmd, v->target, v->camera, m_lighting);
		}
		(void)cmd.Submit();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		sdl3::Canvas &c = Canvas();
		constexpr float kSide = 290.f, kTop = 240.f, kGap = 10.f;
		const sdl3::FRect side{area.x, area.y, kSide, area.h};
		const sdl3::FRect views{side.Right() + kGap, area.y + kGap, area.w - kSide - 2.f * kGap, area.h - 2.f * kGap};
		m_top.rect = {views.Right() - kTop, views.y, kTop, views.h};			   // haute et étroite
		m_perspective.rect = {views.x, views.y, views.w - kTop - kGap, views.h}; // large

		c.FillRect(side, th.panel);
		c.Line(side.Right(), side.y, side.Right(), side.Bottom(), th.panelBorder);
		DrawPanel(side.Inset(14));
		HandlePerspective();
		HandleTop();
		for (View *v : {&m_perspective, &m_top})
			DrawViewFrame(*v);
	}

private:
	void Add(Shape shape, const math::FVector3 &position, sdl3::Color color, Finish finish = Finish::Plastic) {
		Object o;
		o.shape = shape;
		o.name = std::format("{} {}", kShapes[size_t(shape)], ++m_counter);
		o.transform.position = position;
		o.color = color;
		o.finish = finish;
		o.roughness = finish == Finish::Metal ? 0.2f : 0.4f;
		m_objects.push_back(std::move(o));
	}

	[[nodiscard]] static sdl3::Material MaterialOf(const Object &o) {
		const sdl3::LinearColor c = sdl3::Srgb(o.color.r, o.color.g, o.color.b);
		switch (o.finish) {
		case Finish::Metal: return sdl3::Material::Metal(c, o.roughness);
		case Finish::Glass: return sdl3::Material::Glass(c, 0.35f);
		case Finish::Emissive: return sdl3::Material::Emissive(c, 3.f);
		default: return sdl3::Material::Plastic(c, o.roughness);
		}
	}

	[[nodiscard]] math::FMatrix4 ModelOf(Object &o) const {
		o.transform.rotation = math::FQuaternion::AxisAngle({0.f, 1.f, 0.f}, o.yaw * math::DEG2RAD);
		return o.transform.Matrix();
	}

	void DrawScene(bool top) {
		m_r3d->Draw(m_ground, math::FMatrix4::Scale({14.f, 1.f, 14.f}), sdl3::Material::Plastic(sdl3::Srgb(0x3a3f4a), 0.9f));
		m_r3d->Grid(7.f, 1.f, 0.004f, 150, 60);
		for (size_t i = 0; i < m_objects.size(); ++i) {
			Object &o = m_objects[i];
			const math::FMatrix4 model = ModelOf(o);
			const sdl3::MeshId mesh = m_meshes[size_t(o.shape)];
			m_r3d->Draw(mesh, model, MaterialOf(o));
			if (int(i) == m_selected)
				m_r3d->WireBox(m_r3d->Bounds(mesh), model, 255, 210, 90);
		}
		if (!top)
			m_r3d->Axes({-6.9f, 0.01f, 6.9f}, 1.f);
	}

	/// Objet sous le rayon (le plus proche), -1 sinon.
	[[nodiscard]] int Pick(const math::FRay &ray) {
		int best = -1;
		float bestT = 1e30f;
		for (size_t i = 0; i < m_objects.size(); ++i) {
			Object &o = m_objects[i];
			(void)ModelOf(o); // rotation à jour
			// Rayon dans le repère de l'objet : le paramètre t reste celui du monde.
			const math::FRay local{o.transform.InverseTransformPoint(ray.origin), o.transform.InverseTransformDirection(ray.dir)};
			if (const auto t = sdl3::IntersectRayAabb(local, m_r3d->Bounds(m_meshes[size_t(o.shape)])); t && *t < bestT) {
				bestT = *t;
				best = int(i);
			}
		}
		return best;
	}

	[[nodiscard]] math::FRay RayAt(const View &v) {
		const ui::Context &u = Ui();
		return v.camera.ScreenRay((u.MouseX() - v.rect.x) / v.rect.w, (u.MouseY() - v.rect.y) / v.rect.h, v.target.Aspect());
	}

	void HandlePerspective() {
		ui::Context &u = Ui();
		const bool over = u.MouseOver(m_perspective.rect);
		if (over && u.MousePressed())
			m_pressX = u.MouseX(), m_pressY = u.MouseY();
		// Clic sans glissé : sélection.
		if (over && u.MouseReleased() && std::abs(u.MouseX() - m_pressX) + std::abs(u.MouseY() - m_pressY) < 4.f)
			m_selected = Pick(RayAt(m_perspective));
		(void)m_orbit.HandleMouse(u.MouseX(), u.MouseY(), u.MouseDown(), u.MouseDown(sdl3::MouseButton::Right) || u.MouseDown(sdl3::MouseButton::Middle),
								  u.WheelY(), over);
	}

	void HandleTop() {
		ui::Context &u = Ui();
		const bool over = u.MouseOver(m_top.rect);
		if (over && u.WheelY() != 0.f)
			m_topHeight = std::clamp(m_topHeight * std::pow(0.88f, u.WheelY()), 4.f, 40.f);
		if (over && u.MousePressed()) {
			m_selected = Pick(RayAt(m_top));
			m_dragging = m_selected >= 0;
		}
		if (m_dragging && !u.MouseDown())
			m_dragging = false;
		if (m_dragging && m_selected >= 0) { // déplacement au sol : intersection rayon / plan horizontal
			Object &o = m_objects[size_t(m_selected)];
			const math::FRay ray = RayAt(m_top);
			if (std::abs(ray.dir.y) > 1e-4f) {
				const math::FVector3 hit = ray.At((o.transform.position.y - ray.origin.y) / ray.dir.y);
				o.transform.position.x = std::clamp(hit.x, -6.5f, 6.5f);
				o.transform.position.z = std::clamp(hit.z, -6.5f, 6.5f);
			}
		}
	}

	void DrawViewFrame(View &v) {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		if (sdl3::Texture *tex = v.target.View())
			c.Image(*tex, v.rect);
		const bool hot = Ui().MouseOver(v.rect) || (&v == &m_perspective && m_orbit.Dragging()) || (&v == &m_top && m_dragging);
		c.StrokeRoundRect(v.rect, 6.f, hot ? th.accent : th.panelBorder, hot ? 2.f : 1.f);
		const std::string label = &v == &m_top ? std::format("{} · {}×{}", v.title, v.target.Width(), v.target.Height())
											   : std::format("{} · {}×{} · {} triangles", v.title, v.target.Width(), v.target.Height(), m_r3d->Stats().triangles);
		const float w = c.MeasureText(label, 12.f) + 16.f;
		c.FillRoundRect({v.rect.x + 8.f, v.rect.y + 8.f, w, 22.f}, 6.f, sdl3::Rgba(10, 12, 18, 175));
		c.Text(v.rect.x + 16.f, v.rect.y + 12.f, label, th.text, 12.f);
	}

	void DrawPanel(const sdl3::FRect &r) {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		u.BeginScroll("panel", r, {.axes = ui::ScrollAxes::Vertical});
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::LAYERS), "Objets");
		for (size_t i = 0; i < m_objects.size(); ++i)
			if (u.Selectable(m_objects[i].name, int(i) == m_selected))
				m_selected = int(i);
		u.Row({1.f, 1.f}, 30.f);
		int shape = m_newShape;
		if (u.Choice(u.Next(), "new-shape", shape, kShapes))
			m_newShape = shape;
		if (u.Button("Ajouter")) {
			Add(Shape(m_newShape), {0.f, 0.5f, 0.f}, sdl3::Rgba(120, 170, 240));
			m_selected = int(m_objects.size()) - 1;
		}
		if (m_selected < 0 || m_selected >= int(m_objects.size())) {
			u.Space(8.f);
			u.Wrapped("Cliquez un objet dans une vue ou dans la liste pour modifier ses propriétés.", th.textDim, 13.f);
			u.EndScroll();
			return;
		}
		Object &o = m_objects[size_t(m_selected)];
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TRANSFORM), "Transformation");
		u.SliderFloat("X", o.transform.position.x, -6.5f, 6.5f);
		u.SliderFloat("Hauteur", o.transform.position.y, 0.f, 3.f);
		u.SliderFloat("Z", o.transform.position.z, -6.5f, 6.5f);
		u.SliderFloat("Rotation", o.yaw, -180.f, 180.f, "{:.0f}°");
		float scale = o.transform.scale.x;
		if (u.SliderFloat("Échelle", scale, 0.2f, 3.f))
			o.transform.scale = {scale, scale, scale};
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::PALETTE), "Matériau");
		int finish = int(o.finish);
		if (u.Choice(u.Next(30), "finish", finish, kFinishes))
			o.finish = Finish(finish);
		if (o.finish == Finish::Plastic || o.finish == Finish::Metal)
			u.SliderFloat("Rugosité", o.roughness, 0.02f, 1.f);
		u.ColorPicker("color", o.color, 150.f);
		u.Space(6.f);
		if (u.Button("Supprimer l'objet")) {
			m_objects.erase(m_objects.begin() + m_selected);
			m_selected = -1;
		}
		u.EndScroll();
	}

	std::unique_ptr<sdl3::Renderer3D> m_r3d;
	std::array<sdl3::MeshId, 5> m_meshes{};
	sdl3::MeshId m_ground = 0;
	sdl3::Lighting m_lighting;
	sdl3::OrbitController m_orbit;
	View m_perspective, m_top;
	std::vector<Object> m_objects;
	int m_selected = -1, m_newShape = 0, m_counter = 0;
	float m_topHeight = 16.f, m_pressX = 0.f, m_pressY = 0.f;
	bool m_dragging = false;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Vues 3D dans l'interface";
	config.width = 1320;
	config.height = 780;
	config.customFrame = true;
	config.gpu = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::VIEW_IN_AR);
	config.ParseArgs(argc, argv);
	return ViewportDemo().Run(config);
}
