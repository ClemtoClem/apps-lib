// example-gpu-instancing — des dizaines de milliers d'objets sur SDL_GPU (remplace gfx_scene,
// écrit pour Vulkan / Wayland, ici entièrement par le wrapper sdl3).
//
//  - objets STATIQUES (champ de colonnes) : rangés par cellule de grille et par matériau ; à
//    chaque image, les cellules hors du frustum sont écartées et les autres envoyées en un
//    appel instancié par matériau ;
//  - objets DYNAMIQUES (cubes en orbite) : matrices recalculées à chaque image, un appel ;
//  - quatre matériaux texturés, brouillard, ombres du soleil facultatives ;
//  - mesures : temps CPU de préparation, objets visibles, appels, triangles, images par seconde.
//
//   cpp-maker run example-gpu-instancing [--statics=N]
#include <algorithm>
#include <array>
#include <chrono>
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
constexpr int kMaterials = 4;
constexpr float kSpacing = 2.4f;
constexpr int kCellSize = 16; // colonnes par côté de cellule
constexpr std::array<int, 4> kCounts = {10'000, 30'000, 60'000, 100'000};
constexpr std::array<std::string_view, 4> kCountNames = {"10 000", "30 000", "60 000", "100 000"};

/// Générateur déterministe (même champ à chaque lancement).
struct Random {
	uint32_t state = 0x9e3779b9u;
	float Next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return float(state) / 4294967295.f;
	}
};

/// Texture procédurale 64² : `kind` 0 briques, 1 rayures, 2 points, 3 grain.
std::vector<uint8_t> MakeTexture(int kind) {
	constexpr uint32_t n = 64;
	std::vector<uint8_t> px(n * n * 4);
	Random rng;
	for (uint32_t y = 0; y < n; ++y)
		for (uint32_t x = 0; x < n; ++x) {
			float v = 1.f;
			switch (kind) {
			case 0: v = (y % 16 < 2 || ((x + (y / 16 % 2) * 16) % 32) < 2) ? 0.55f : 1.f; break;
			case 1: v = (x + y) % 16 < 8 ? 1.f : 0.75f; break;
			case 2: v = std::hypot(float(x % 16) - 7.5f, float(y % 16) - 7.5f) < 4.f ? 0.6f : 1.f; break;
			default: v = 0.8f + 0.2f * rng.Next(); break;
			}
			uint8_t *p = &px[(size_t(y) * n + x) * 4];
			p[0] = p[1] = p[2] = uint8_t(255.f * v);
			p[3] = 255;
		}
	return px;
}

struct Cell {
	math::FAABB bounds;
	std::array<std::vector<sdl3::Instance>, kMaterials> instances;
};

class InstancingDemo final : public app::Application {
public:
	explicit InstancingDemo(int countIndex) : m_countIndex(countIndex) {}

protected:
	bool OnInit() override {
		m_r3d = sdl3::Renderer3D::Create(*Gpu(), 2048);
		if (!m_r3d)
			return false;
		m_cube = m_r3d->Upload(sdl3::MeshData::Box(1.f, 1.f, 1.f));
		m_ground = m_r3d->Upload(sdl3::MeshData::Plane(1.f, 1.f));
		if (!m_cube || !m_ground)
			return false;
		static constexpr uint32_t palette[kMaterials] = {0xc8a27a, 0x7aa6c8, 0x9ac87a, 0xc87aa0};
		for (int i = 0; i < kMaterials; ++i) {
			const std::vector<uint8_t> tex = MakeTexture(i);
			m_materials[size_t(i)] = sdl3::Material::Plastic(sdl3::Srgb(palette[i]), 0.55f);
			m_materials[size_t(i)].texture = m_r3d->CreateTexture(tex, 64, 64);
			m_materials[size_t(i)].uvScale = {1.f, 2.f};
		}
		m_orbit.distance = 70.f;
		m_orbit.pitch = 0.5f;
		m_orbit.maxDistance = 400.f;
		m_lighting.sun.direction = {-0.45f, -1.f, -0.3f};
		m_lighting.sun.castShadow = false;
		m_lighting.sun.shadowExtent = 45.f;
		m_lighting.environment.fogDensity = 0.0025f;
		m_lighting.environment.fogColor = sdl3::Srgb(0xa9bdd3);
		BuildField(kCounts[size_t(m_countIndex)]);
		return true;
	}

	void OnFixedUpdate(double step) override { m_time += float(step); }

	void OnOffscreen() override {
		const float scale = Canvas().Scale();
		if (m_view.w <= 0.f || !m_target.Resize(*m_r3d, &Renderer(), int(m_view.w * scale), int(m_view.h * scale)))
			return;
		m_orbit.Apply(m_camera);
		m_camera.zFar = 600.f;
		m_lighting.sun.shadowCenter = m_orbit.target;

		const auto start = std::chrono::steady_clock::now();
		const math::FFrustum frustum = math::FFrustum::FromMatrix(m_camera.ViewProjection(m_target.Aspect()));
		m_visibleCells = 0;
		m_visibleStatics = 0;
		for (auto &batch : m_batches)
			batch.clear();
		for (const Cell &cell : m_cells) {
			if (m_culling && !frustum.Intersects(cell.bounds))
				continue;
			++m_visibleCells;
			for (size_t m = 0; m < kMaterials; ++m) {
				m_batches[m].insert(m_batches[m].end(), cell.instances[m].begin(), cell.instances[m].end());
				m_visibleStatics += cell.instances[m].size();
			}
		}
		BuildDynamics();
		const float half = m_fieldHalf + 20.f;
		m_r3d->Draw(m_ground, math::FMatrix4::Scale({half * 2.f, 1.f, half * 2.f}), sdl3::Material::Plastic(sdl3::Srgb(0x55595f), 0.95f));
		for (size_t m = 0; m < kMaterials; ++m)
			m_r3d->DrawInstances(m_cube, m_materials[m], m_batches[m]);
		sdl3::Material orbiting = sdl3::Material::Metal(sdl3::Srgb(0xf0d08a), 0.25f);
		m_r3d->DrawInstances(m_cube, orbiting, m_dynamics);
		m_cpuMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
		std::shift_left(m_cpuHistory.begin(), m_cpuHistory.end(), 1); // le plus récent à droite
		m_cpuHistory.back() = m_cpuMs;

		auto cmd = Gpu()->AcquireCommandBuffer();
		m_r3d->Render(cmd, m_target, m_camera, m_lighting);
		(void)cmd.Submit();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		sdl3::Canvas &c = Canvas();
		constexpr float kPanel = 300.f;
		m_view = {area.x, area.y, area.w - kPanel, area.h};
		if (sdl3::Texture *tex = m_target.View())
			c.Image(*tex, m_view);
		(void)m_orbit.HandleMouse(u.MouseX(), u.MouseY(), u.MouseDown(), u.MouseDown(sdl3::MouseButton::Right) || u.MouseDown(sdl3::MouseButton::Middle),
								  u.WheelY(), u.MouseOver(m_view));
		if (m_autoRotate && !m_orbit.Dragging())
			m_orbit.yaw += FrameSeconds() * 0.08f;
		DrawStats(m_view.Inset(14));

		const sdl3::FRect side{m_view.Right(), area.y, kPanel, area.h};
		c.FillRect(side, th.panel);
		c.Line(side.x, side.y, side.x, side.Bottom(), th.panelBorder);
		u.BeginScroll("panel", side.Inset(14), {.axes = ui::ScrollAxes::Vertical});
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::LAYERS), "Objets statiques");
		int count = m_countIndex;
		if (u.Choice(u.Next(30), "count", count, kCountNames)) {
			m_countIndex = count;
			BuildField(kCounts[size_t(count)]);
		}
		u.Checkbox("Élimination par frustum", m_culling);
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::ANIMATION), "Objets dynamiques");
		u.SliderInt("En orbite", m_dynamicCount, 0, 20000);
		u.SliderFloat("Vitesse", m_orbitSpeed, 0.f, 3.f, "{:.1f}×");
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::WB_SUNNY), "Rendu");
		u.Checkbox("Ombres du soleil", m_lighting.sun.castShadow);
		u.SliderFloat("Brouillard", m_lighting.environment.fogDensity, 0.f, 0.03f, "{:.3f}");
		u.Checkbox("Rotation automatique", m_autoRotate);
		bool vsync = m_vsync;
		if (u.Checkbox("Synchronisation verticale", vsync)) {
			m_vsync = vsync;
			Renderer().SetVSync(sdl3::Renderer::VSync(m_vsync ? 1 : 0));
		}
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::SPEED), "Préparation CPU (ms)");
		u.Plot({}, m_cpuHistory, 0.f, 0.f, 70.f, th.accent);
		u.Wrapped("Élimination, regroupement par matériau et matrices des objets dynamiques, avant l'envoi au GPU.", th.textDim, 12.f);
		u.EndScroll();
	}

private:
	void BuildField(int count) {
		m_cells.clear();
		const int side = int(std::ceil(std::sqrt(float(count))));
		const int cellsPerSide = (side + kCellSize - 1) / kCellSize;
		m_fieldHalf = float(side) * kSpacing * 0.5f;
		m_cells.resize(size_t(cellsPerSide * cellsPerSide));
		for (Cell &cell : m_cells)
			cell.bounds = {{1e30f, 0.f, 1e30f}, {-1e30f, 0.f, -1e30f}};
		Random rng;
		m_staticCount = 0;
		for (int z = 0; z < side && m_staticCount < size_t(count); ++z)
			for (int x = 0; x < side && m_staticCount < size_t(count); ++x) {
				const float h = 0.6f + std::pow(rng.Next(), 3.f) * 7.f;
				const float w = 0.7f + rng.Next() * 0.9f;
				const math::FVector3 pos{float(x) * kSpacing - m_fieldHalf, h * 0.5f, float(z) * kSpacing - m_fieldHalf};
				sdl3::Instance inst;
				inst.model = math::FMatrix4::Translation(pos) * math::FMatrix4::Scale({w, h, w});
				const float shade = 0.75f + 0.25f * rng.Next();
				inst.tint = {shade, shade, shade, 1.f};
				Cell &cell = m_cells[size_t((z / kCellSize) * cellsPerSide + x / kCellSize)];
				cell.instances[size_t(rng.Next() * kMaterials) % kMaterials].push_back(inst);
				cell.bounds.min = {std::min(cell.bounds.min.x, pos.x - w), 0.f, std::min(cell.bounds.min.z, pos.z - w)};
				cell.bounds.max = {std::max(cell.bounds.max.x, pos.x + w), std::max(cell.bounds.max.y, h), std::max(cell.bounds.max.z, pos.z + w)};
				++m_staticCount;
			}
		m_orbit.target = {0.f, 2.f, 0.f};
	}

	void BuildDynamics() {
		m_dynamics.resize(size_t(m_dynamicCount));
		const float t = m_time * m_orbitSpeed;
		for (size_t i = 0; i < m_dynamics.size(); ++i) {
			const float k = float(i);
			const float ring = 12.f + std::fmod(k * 0.618f, 1.f) * (m_fieldHalf * 0.9f);
			const float a = k * 2.399963f + t * (6.f / ring);
			const float y = 10.f + 4.f * std::sin(k * 0.37f + t * 0.8f);
			const math::FMatrix4 spin = sdl3::RotationMatrix(math::FQuaternion::AxisAngle({std::sin(k), 1.f, std::cos(k)}, t * 2.f + k));
			m_dynamics[i].model = math::FMatrix4::Translation({std::cos(a) * ring, y, std::sin(a) * ring}) * spin * math::FMatrix4::Scale({0.6f, 0.6f, 0.6f});
		}
	}

	void DrawStats(const sdl3::FRect &r) {
		sdl3::Canvas &c = Canvas();
		const ui::Theme &th = Theme();
		const sdl3::RenderStats &s = m_r3d->Stats();
		const float culled = m_staticCount ? 100.f * (1.f - float(m_visibleStatics) / float(m_staticCount)) : 0.f;
		const std::array<std::string, 4> lines = {
			std::format("{} statiques · {} dynamiques", m_staticCount, m_dynamicCount),
			std::format("{} visibles ({:.0f} % écartés) · {}/{} cellules", m_visibleStatics, culled, m_visibleCells, m_cells.size()),
			std::format("{} appels · {:.2f} M triangles", s.drawCalls, double(s.triangles) / 1e6),
			std::format("CPU {:.2f} ms · {:.0f} i/s ({:.1f} ms)", m_cpuMs, Fps(), FrameSeconds() * 1000.f),
		};
		float w = 0.f;
		for (const std::string &l : lines)
			w = std::max(w, c.MeasureText(l, 12.f));
		const sdl3::FRect box{r.x, r.y, w + 24.f, 18.f * float(lines.size()) + 16.f};
		c.FillRoundRect(box, 8.f, sdl3::Rgba(10, 12, 18, 175));
		for (size_t i = 0; i < lines.size(); ++i)
			c.Text(box.x + 12.f, box.y + 8.f + 18.f * float(i), lines[i], i == 0 ? th.text : th.textDim, 12.f, i == 0 ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular);
		// Barre du temps CPU (16,7 ms = une image à 60 Hz).
		const sdl3::FRect bar{box.x, box.Bottom() + 6.f, box.w, 6.f};
		c.FillRoundRect(bar, 3.f, sdl3::Rgba(10, 12, 18, 175));
		c.FillRoundRect({bar.x, bar.y, bar.w * std::min(1.f, m_cpuMs / 16.7f), bar.h}, 3.f, m_cpuMs < 4.f ? th.accent : th.warning);
	}

	std::unique_ptr<sdl3::Renderer3D> m_r3d;
	sdl3::RenderTarget3D m_target;
	sdl3::Camera3D m_camera;
	sdl3::OrbitController m_orbit;
	sdl3::Lighting m_lighting;
	sdl3::MeshId m_cube = 0, m_ground = 0;
	std::array<sdl3::Material, kMaterials> m_materials;
	std::vector<Cell> m_cells;
	std::array<std::vector<sdl3::Instance>, kMaterials> m_batches;
	std::vector<sdl3::Instance> m_dynamics;
	std::array<float, 120> m_cpuHistory{};
	size_t m_staticCount = 0, m_visibleStatics = 0, m_visibleCells = 0;
	sdl3::FRect m_view{};
	int m_countIndex = 0, m_dynamicCount = 2000;
	float m_time = 0.f, m_orbitSpeed = 1.f, m_cpuMs = 0.f, m_fieldHalf = 0.f;
	bool m_culling = true, m_autoRotate = true, m_vsync = true;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "Instanciation massive";
	config.width = 1320;
	config.height = 780;
	config.customFrame = true;
	config.gpu = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::WORKSPACES);
	int countIndex = 0;
	for (const std::string &arg : config.ParseArgs(argc, argv))
		if (arg.starts_with("--statics=")) {
			const int n = std::atoi(arg.c_str() + 10);
			while (countIndex + 1 < int(kCounts.size()) && kCounts[size_t(countIndex)] < n)
				++countIndex;
		}
	return InstancingDemo(countIndex).Run(config);
}
