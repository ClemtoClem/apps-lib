// example-gpu-basics — l'API SDL_GPU brute, par le wrapper sdl3 seulement (remplace les
// exemples Vulkan / Wayland vk_triangle et vk_cube).
//
//   Triangle      : tampon de sommets colorés, pipeline sans profondeur, rotation en uniforme.
//   Cube texturé  : tampons de sommets et d'index, texture téléversée par un tampon de transfert
//                   (mipmaps générées par le GPU), trois échantillonneurs, profondeur recréée
//                   avec la cible, matrices en uniformes poussés.
// Les deux : pipeline plein ou filaire (FILLMODE_LINE), rendu dans une texture affichée sans
// copie par le renderer 2D du même device.
//
//   cpp-maker run example-gpu-basics [--cube]
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <app/app.hpp>

#include "shaders_spirv.inc"

namespace {

constexpr std::array<std::string_view, 2> kDemos = {"Triangle", "Cube texturé"};
constexpr std::array<std::string_view, 3> kSamplers = {"Au plus proche", "Linéaire + mipmaps", "Anisotrope ×16"};
constexpr SDL_GPUTextureFormat kColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

struct ColorVertex {
	float x, y, r, g, b;
};

struct CubeVertex {
	float position[3], normal[3], uv[2];
};

struct CubeUniforms {
	math::FMatrix4 mvp, model;
	math::FVector4 uvScale;
};

/// Shader SPIR-V embarqué (`tools/compile_shaders.sh`).
std::optional<sdl3::GpuShader> LoadShader(sdl3::GpuDevice &device, std::span<const unsigned char> code, SDL_GPUShaderStage stage, uint32_t samplers,
										  uint32_t uniforms) {
	SDL_GPUShaderCreateInfo info{};
	info.code = code.data();
	info.code_size = code.size();
	info.entrypoint = "main";
	info.format = SDL_GPU_SHADERFORMAT_SPIRV;
	info.stage = stage;
	info.num_samplers = samplers;
	info.num_uniform_buffers = uniforms;
	auto shader = device.CreateShader(info);
	if (!shader) {
		core::log::Error("shader : {}", shader.Error());
		return std::nullopt;
	}
	return std::move(shader.Value());
}

/// Tampon GPU rempli par un tampon de transfert et une passe de copie.
std::optional<sdl3::GpuBuffer> Upload(sdl3::GpuDevice &device, SDL_GPUBufferUsageFlags usage, std::span<const std::byte> data) {
	const auto size = uint32_t(data.size());
	auto buffer = device.CreateBuffer(usage, size);
	auto staging = device.CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size);
	if (!buffer || !staging || !staging.Value().WriteData(data))
		return std::nullopt;
	auto cmd = device.AcquireCommandBuffer();
	{
		auto pass = cmd.BeginCopyPass();
		pass.UploadToBuffer({staging.Value().Get(), 0}, {buffer.Value().Get(), 0, size});
	}
	if (!cmd.Submit())
		return std::nullopt;
	return std::move(buffer.Value());
}

/// Motif procédural 256² : damier, cercles concentriques, dégradé (les mipmaps se voient au loin).
std::vector<uint8_t> Pattern(uint32_t size) {
	std::vector<uint8_t> px(size_t(size) * size * 4);
	for (uint32_t y = 0; y < size; ++y)
		for (uint32_t x = 0; x < size; ++x) {
			const float u = float(x) / float(size), v = float(y) / float(size);
			const bool check = ((x / 32) + (y / 32)) % 2 == 0;
			const float ring = 0.5f + 0.5f * std::cos(std::hypot(u - 0.5f, v - 0.5f) * 60.f);
			uint8_t *p = &px[(size_t(y) * size + x) * 4];
			p[0] = uint8_t(check ? 40 + 180 * u : 230 * ring);
			p[1] = uint8_t(check ? 90 + 120 * v : 120 + 90 * ring);
			p[2] = uint8_t(check ? 210 : 255 - 120 * u);
			p[3] = 255;
			if (x % 64 < 2 || y % 64 < 2) // lignes fines : scintillent sans mipmaps
				p[0] = p[1] = p[2] = 250;
		}
	return px;
}

class GpuBasics final : public app::Application {
public:
	explicit GpuBasics(int demo) : m_demo(demo) {}

protected:
	bool OnInit() override {
		sdl3::GpuDevice &dev = *Gpu();
		m_depthFormat = dev.TextureSupportsFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
							? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
							: SDL_GPU_TEXTUREFORMAT_D24_UNORM;
		using namespace shaders;
		auto triVs = LoadShader(dev, k_triangle_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
		auto triFs = LoadShader(dev, k_triangle_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
		auto cubeVs = LoadShader(dev, k_cube_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
		auto cubeFs = LoadShader(dev, k_cube_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
		if (!triVs || !triFs || !cubeVs || !cubeFs)
			return false;

		// ── Pipelines du triangle (plein, filaire) ──────────────────────────
		SDL_GPUColorTargetDescription color{};
		color.format = kColorFormat;
		const SDL_GPUVertexBufferDescription triBuffer{0, sizeof(ColorVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
		const SDL_GPUVertexAttribute triAttrs[2] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 0}, {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 8}};
		SDL_GPUGraphicsPipelineCreateInfo pi{};
		pi.vertex_shader = triVs->Get();
		pi.fragment_shader = triFs->Get();
		pi.vertex_input_state = {&triBuffer, 1, triAttrs, 2};
		pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		pi.target_info.color_target_descriptions = &color;
		pi.target_info.num_color_targets = 1;
		auto triFill = dev.CreateGraphicsPipeline(pi);
		pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
		auto triWire = dev.CreateGraphicsPipeline(pi);

		// ── Pipelines du cube (profondeur, faces arrière éliminées) ─────────
		const SDL_GPUVertexBufferDescription cubeBuffer{0, sizeof(CubeVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
		const SDL_GPUVertexAttribute cubeAttrs[3] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
													 {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
													 {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24}};
		pi.vertex_shader = cubeVs->Get();
		pi.fragment_shader = cubeFs->Get();
		pi.vertex_input_state = {&cubeBuffer, 1, cubeAttrs, 3};
		pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
		pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
		pi.depth_stencil_state.enable_depth_test = true;
		pi.depth_stencil_state.enable_depth_write = true;
		pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
		pi.target_info.has_depth_stencil_target = true;
		pi.target_info.depth_stencil_format = m_depthFormat;
		auto cubeFill = dev.CreateGraphicsPipeline(pi);
		pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_LINE;
		pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		auto cubeWire = dev.CreateGraphicsPipeline(pi);
		if (!triFill || !triWire || !cubeFill || !cubeWire) {
			core::log::Error("pipelines : {}", SDL_GetError());
			return false;
		}
		m_triFill = std::move(triFill.Value());
		m_triWire = std::move(triWire.Value());
		m_cubeFill = std::move(cubeFill.Value());
		m_cubeWire = std::move(cubeWire.Value());

		// ── Tampons ──────────────────────────────────────────────────────────
		constexpr std::array<ColorVertex, 3> triangle = {{{0.f, 0.8f, 1.f, 0.25f, 0.3f}, {-0.7f, -0.45f, 0.25f, 0.9f, 0.4f}, {0.7f, -0.45f, 0.3f, 0.45f, 1.f}}};
		std::vector<CubeVertex> vertices;
		std::vector<uint16_t> indices;
		BuildCube(vertices, indices);
		m_indexCount = uint32_t(indices.size());
		auto tri = Upload(dev, SDL_GPU_BUFFERUSAGE_VERTEX, std::as_bytes(std::span(triangle)));
		auto cubeV = Upload(dev, SDL_GPU_BUFFERUSAGE_VERTEX, std::as_bytes(std::span(vertices)));
		auto cubeI = Upload(dev, SDL_GPU_BUFFERUSAGE_INDEX, std::as_bytes(std::span(indices)));
		if (!tri || !cubeV || !cubeI)
			return false;
		m_triangle = std::move(*tri);
		m_cubeVertices = std::move(*cubeV);
		m_cubeIndices = std::move(*cubeI);
		return CreateTexture(dev) && CreateSamplers(dev);
	}

	void OnFixedUpdate(double step) override { m_angle += float(step) * m_speed; }

	void OnOffscreen() override {
		const float scale = Canvas().Scale();
		if (m_view.w <= 0.f || !EnsureTarget(int(m_view.w * scale), int(m_view.h * scale)))
			return;
		auto cmd = Gpu()->AcquireCommandBuffer();
		SDL_GPUColorTargetInfo color{};
		color.texture = m_color.Get();
		color.clear_color = {0.07f, 0.08f, 0.11f, 1.f};
		color.load_op = SDL_GPU_LOADOP_CLEAR;
		color.store_op = SDL_GPU_STOREOP_STORE;
		color.cycle = true;
		const float aspect = float(m_width) / float(m_height);
		if (m_demo == 0) {
			auto pass = cmd.BeginRenderPass(color);
			pass.BindPipeline(m_wireframe ? m_triWire : m_triFill);
			cmd.PushVertexUniformData(0, math::FVector4{std::cos(m_angle), std::sin(m_angle), 1.f / aspect, m_scale});
			pass.BindVertexBuffer(0, {m_triangle.Get(), 0});
			pass.DrawPrimitives(3);
		} else {
			SDL_GPUDepthStencilTargetInfo depth{};
			depth.texture = m_depth.Get();
			depth.clear_depth = 1.f;
			depth.load_op = SDL_GPU_LOADOP_CLEAR;
			depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
			depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
			depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
			depth.cycle = true;
			auto pass = cmd.BeginRenderPass(color, &depth);
			pass.BindPipeline(m_wireframe ? m_cubeWire : m_cubeFill);
			const math::FMatrix4 model = RotateY(m_angle) * RotateX(m_angle * 0.6f);
			const math::FMatrix4 view = math::FMatrix4::LookAt({0.f, 0.f, m_distance}, {}, {0.f, 1.f, 0.f});
			const math::FMatrix4 proj = math::FMatrix4::Perspective(55.f * math::DEG2RAD, aspect, 0.1f, 100.f);
			cmd.PushVertexUniformData(0, CubeUniforms{proj * view * model, model, {m_repeat, m_repeat, 0.f, 0.f}});
			pass.BindVertexBuffer(0, {m_cubeVertices.Get(), 0});
			pass.BindIndexBuffer({m_cubeIndices.Get(), 0}, SDL_GPU_INDEXELEMENTSIZE_16BIT);
			const SDL_GPUTextureSamplerBinding binding{m_texture.Get(), m_samplers[size_t(m_sampler)].Get()};
			pass.BindFragmentSamplers(0, std::span(&binding, 1));
			pass.DrawIndexedPrimitives(m_indexCount);
		}
		(void)cmd.Submit();
	}

	void OnRender(const sdl3::FRect &area, double) override {
		ui::Context &u = Ui();
		const ui::Theme &th = u.Style();
		sdl3::Canvas &c = Canvas();
		constexpr float kPanel = 300.f;
		m_view = {area.x, area.y, area.w - kPanel, area.h};
		if (m_viewReady && m_colorView)
			c.Image(*m_colorView, m_view);
		const std::string stats = std::format("{} · {}×{} · {:.2f} ms · {:.0f} i/s", kDemos[size_t(m_demo)], m_width, m_height, FrameSeconds() * 1000.f, Fps());
		c.FillRoundRect({m_view.x + 12.f, m_view.y + 12.f, c.MeasureText(stats, 12.f) + 20.f, 26.f}, 6.f, sdl3::Rgba(10, 12, 18, 170));
		c.Text(m_view.x + 22.f, m_view.y + 18.f, stats, th.text, 12.f);
		if (u.MouseOver(m_view) && u.WheelY() != 0.f)
			m_distance = std::clamp(m_distance * std::pow(0.9f, u.WheelY()), 1.6f, 30.f);

		const sdl3::FRect side{m_view.Right(), area.y, kPanel, area.h};
		c.FillRect(side, th.panel);
		c.Line(side.x, side.y, side.x, side.Bottom(), th.panelBorder);
		u.BeginScroll("panel", side.Inset(14), {.axes = ui::ScrollAxes::Vertical});
		m_demo = u.Tabs("demo", kDemos, m_demo);
		u.Space(6.f);
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), "Réglages");
		u.SliderFloat("Rad/s", m_speed, -3.f, 3.f, "{:+.1f}");
		u.Checkbox("Filaire (FILLMODE_LINE)", m_wireframe);
		if (m_demo == 0) {
			u.SliderFloat("Taille", m_scale, 0.2f, 1.2f);
		} else {
			u.SliderFloat("Distance", m_distance, 1.6f, 30.f, "{:.1f}");
			u.SliderFloat("Répétition", m_repeat, 1.f, 8.f, "{:.0f}×");
			u.Label("Échantillonneur", th.textDim, 13.f);
			u.Choice(u.Next(30), "sampler", m_sampler, kSamplers);
			u.Wrapped("Éloignez le cube avec une texture répétée : sans mipmaps, les lignes fines scintillent.", th.textDim, 12.f);
		}
		bool vsync = m_vsync;
		if (u.Checkbox("Synchronisation verticale", vsync)) {
			m_vsync = vsync;
			Renderer().SetVSync(sdl3::Renderer::VSync(m_vsync ? 1 : 0));
		}
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::MEMORY), "Device GPU");
		const auto info = [&](std::string_view key, const std::string &value) {
			const sdl3::FRect row = u.Next(20);
			c.Text(row.x, row.y + 2.f, key, th.textDim, 12.f);
			c.Text(row.x + 110.f, row.y + 2.f, value, th.text, 12.f);
		};
		const char *driver = Gpu()->Driver();
		info("Pilote", driver ? driver : "?");
		info("Shaders", "SPIR-V embarqué");
		info("Couleur", "R8G8B8A8_UNORM");
		info("Profondeur", m_depthFormat == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ? "D32_FLOAT" : "D24_UNORM");
		info("Texture", std::format("256² · {} niveaux", m_mipLevels));
		info("Sommets", m_demo == 0 ? std::string("3") : std::format("24 · {} index", m_indexCount));
		u.EndScroll();
	}

	void TitleButtons(std::vector<ui::FrameButton> &left, std::vector<ui::FrameButton> &) override {
		left.push_back({.id = "wire", .icon = ui::icons::Codepoint(ui::icons::MaterialIcons::GRID_ON), .label = "Filaire", .active = m_wireframe});
	}

	void OnTitleButton(std::string_view id) override {
		if (id == "wire")
			m_wireframe = !m_wireframe;
	}

private:
	static math::FMatrix4 RotateY(float a) {
		math::FMatrix4 r = math::FMatrix4::Identity();
		r.m[0][0] = std::cos(a), r.m[0][2] = -std::sin(a), r.m[2][0] = std::sin(a), r.m[2][2] = std::cos(a);
		return r;
	}
	static math::FMatrix4 RotateX(float a) {
		math::FMatrix4 r = math::FMatrix4::Identity();
		r.m[1][1] = std::cos(a), r.m[1][2] = std::sin(a), r.m[2][1] = -std::sin(a), r.m[2][2] = std::cos(a);
		return r;
	}

	/// 6 faces × 4 sommets (normales franches, UV 0..1 par face), sens direct vu de l'extérieur.
	static void BuildCube(std::vector<CubeVertex> &vertices, std::vector<uint16_t> &indices) {
		struct Face {
			float n[3], u[3], v[3]; // u × v = n
		};
		static constexpr Face faces[6] = {{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
										  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},	{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}}};
		static constexpr float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		for (const Face &f : faces) {
			const auto base = uint16_t(vertices.size());
			for (const auto &c : corners) {
				CubeVertex v{};
				for (int i = 0; i < 3; ++i) {
					v.position[i] = f.n[i] + f.u[i] * c[0] + f.v[i] * c[1];
					v.normal[i] = f.n[i];
				}
				v.uv[0] = (c[0] + 1.f) * 0.5f;
				v.uv[1] = (1.f - c[1]) * 0.5f;
				vertices.push_back(v);
			}
			indices.insert(indices.end(), {base, uint16_t(base + 1), uint16_t(base + 2), base, uint16_t(base + 2), uint16_t(base + 3)});
		}
	}

	bool CreateTexture(sdl3::GpuDevice &dev) {
		constexpr uint32_t kSize = 256;
		const std::vector<uint8_t> pixels = Pattern(kSize);
		m_mipLevels = 9; // 256 → 1
		SDL_GPUTextureCreateInfo ci{};
		ci.type = SDL_GPU_TEXTURETYPE_2D;
		ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; // COLOR_TARGET : GenerateMipmaps
		ci.width = ci.height = kSize;
		ci.layer_count_or_depth = 1;
		ci.num_levels = m_mipLevels;
		auto texture = dev.CreateTexture(ci);
		auto staging = dev.CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(pixels.size()));
		if (!texture || !staging || !staging.Value().WriteData(std::span<const uint8_t>(pixels)))
			return false;
		auto cmd = dev.AcquireCommandBuffer();
		{
			auto pass = cmd.BeginCopyPass();
			SDL_GPUTextureTransferInfo src{};
			src.transfer_buffer = staging.Value().Get();
			src.pixels_per_row = kSize;
			src.rows_per_layer = kSize;
			SDL_GPUTextureRegion dst{};
			dst.texture = texture.Value().Get();
			dst.w = dst.h = kSize;
			dst.d = 1;
			pass.UploadToTexture(src, dst);
		}
		cmd.GenerateMipmaps(texture.Value());
		if (!cmd.Submit())
			return false;
		m_texture = std::move(texture.Value());
		return true;
	}

	bool CreateSamplers(sdl3::GpuDevice &dev) {
		for (size_t i = 0; i < m_samplers.size(); ++i) {
			SDL_GPUSamplerCreateInfo si{};
			const bool nearest = i == 0;
			si.min_filter = si.mag_filter = nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
			si.mipmap_mode = nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
			si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
			si.max_lod = nearest ? 0.f : 16.f; // au plus proche : niveau 0 seulement (aliasing visible)
			si.enable_anisotropy = i == 2;
			si.max_anisotropy = 16.f;
			auto sampler = dev.CreateSampler(si);
			if (!sampler)
				return false;
			m_samplers[i] = std::move(sampler.Value());
		}
		return true;
	}

	/// Cibles couleur + profondeur à la taille de la vue, et leur vue 2D.
	bool EnsureTarget(int w, int h) {
		if (w <= 0 || h <= 0)
			return false;
		if (w == m_width && h == m_height && m_viewReady)
			return true;
		m_viewReady = false;
		m_colorView.reset();
		sdl3::GpuDevice &dev = *Gpu();
		SDL_GPUTextureCreateInfo ci{};
		ci.type = SDL_GPU_TEXTURETYPE_2D;
		ci.format = kColorFormat;
		ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		ci.width = uint32_t(w);
		ci.height = uint32_t(h);
		ci.layer_count_or_depth = 1;
		ci.num_levels = 1;
		auto color = dev.CreateTexture(ci);
		ci.format = m_depthFormat;
		ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
		auto depth = dev.CreateTexture(ci);
		if (!color || !depth)
			return false;
		m_color = std::move(color.Value());
		m_depth = std::move(depth.Value());
		auto view = sdl3::Texture::FromGpuTexture(Renderer(), m_color.Get(), w, h);
		if (!view)
			return false;
		m_colorView.emplace(std::move(view.Value()));
		m_width = w;
		m_height = h;
		m_viewReady = true;
		return true;
	}

	sdl3::GpuGraphicsPipeline m_triFill, m_triWire, m_cubeFill, m_cubeWire;
	sdl3::GpuBuffer m_triangle, m_cubeVertices, m_cubeIndices;
	sdl3::GpuTexture m_texture, m_color, m_depth;
	std::array<sdl3::GpuSampler, 3> m_samplers;
	std::optional<sdl3::Texture> m_colorView;
	SDL_GPUTextureFormat m_depthFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	sdl3::FRect m_view{};
	int m_demo = 0, m_sampler = 1, m_width = 0, m_height = 0;
	uint32_t m_indexCount = 0, m_mipLevels = 1;
	float m_angle = 0.f, m_speed = 0.8f, m_scale = 0.9f, m_distance = 4.5f, m_repeat = 1.f;
	bool m_wireframe = false, m_vsync = true, m_viewReady = false;
};

} // namespace

int main(int argc, char **argv) {
	app::AppConfig config;
	config.title = "SDL_GPU : premiers pas";
	config.width = 1100;
	config.height = 680;
	config.customFrame = true;
	config.gpu = true;
	config.icon = ui::icons::Codepoint(ui::icons::MaterialIcons::MEMORY);
	int demo = 0;
	for (const std::string &arg : config.ParseArgs(argc, argv))
		if (arg == "--cube")
			demo = 1;
	return GpuBasics(demo).Run(config);
}
