#include "sdl3/render/renderer3d.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include <core/log.hpp>

#include "shaders_spirv.inc"

namespace sdl3 {

namespace {

struct FrameBlock {
	math::FMatrix4 viewProj;
	math::FMatrix4 lightViewProj;
};

struct MaterialBlock {
	math::FVector4 baseColor, params, emissive, uvScale;
};

struct SkyBlock {
	math::FVector4 right, up, forward;
};

sdl3::GpuShader MakeShader(SDL_GPUDevice *dev, const unsigned char *code, size_t size, SDL_GPUShaderStage stage, uint32_t samplers,
						   uint32_t uniforms) {
	SDL_GPUShaderCreateInfo info{};
	info.code = code;
	info.code_size = size;
	info.entrypoint = "main";
	info.format = SDL_GPU_SHADERFORMAT_SPIRV;
	info.stage = stage;
	info.num_samplers = samplers;
	info.num_uniform_buffers = uniforms;
	return sdl3::GpuShader(dev, info);
}

/// Tampon GPU rempli une fois (commande soumise aussitôt).
std::optional<sdl3::GpuBuffer> UploadBuffer(sdl3::GpuDevice &device, SDL_GPUBufferUsageFlags usage, const void *data, uint32_t size) {
	auto buffer = device.CreateBuffer(usage, size);
	auto staging = device.CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size);
	if (!buffer || !staging)
		return std::nullopt;
	{
		auto map = staging.Value().Map();
		if (!map)
			return std::nullopt;
		std::memcpy(map.GetData(), data, size);
	}
	auto cmd = device.AcquireCommandBuffer();
	{
		auto pass = cmd.BeginCopyPass();
		pass.UploadToBuffer({staging.Value().Get(), 0}, {buffer.Value().Get(), 0, size});
	}
	if (!cmd.Submit())
		return std::nullopt;
	return std::move(buffer.Value());
}

MaterialBlock MakeMaterial(const Material &m) {
	return {{m.color, m.opacity},
			{m.metallic, m.roughness, m.unlit ? 1.f : 0.f, m.receiveShadow ? 1.f : 0.f},
			{m.emissive, 0.f},
			{m.uvScale.x, m.uvScale.y, 0.f, 0.f}};
}

float Display(float linear) { return std::pow(std::clamp(linear, 0.f, 1.f), 1.f / 2.2f); }

} // namespace

LightBlock PackLights(const Lighting &l, const math::FVector3 &cameraPos, bool shadow, float shadowTexel) noexcept {
	LightBlock b{};
	const Environment &env = l.environment;
	const math::FVector3 toSun = (l.sun.direction * -1.f).Normalized();
	b.cameraPos = {cameraPos, l.exposure};
	b.sunDir = {toSun, l.sun.intensity};
	b.sunColor = {l.sun.color, shadow ? 1.f : 0.f};
	b.skyZenith = {env.zenith, env.intensity};
	b.skyHorizon = {env.horizon, shadowTexel};
	const size_t points = std::min(l.points.size(), kMaxPointLights), spots = std::min(l.spots.size(), kMaxSpotLights);
	b.groundColor = {env.ground, float(points)};
	b.fogColor = {env.fogColor, env.fogDensity};
	b.counts = {float(spots), env.showSun ? 1.f : 0.f, 0.f, 0.f};
	for (size_t i = 0; i < points; ++i) {
		const PointLight &p = l.points[i];
		b.pointPos[i] = {p.position, std::max(p.range, 0.01f)};
		b.pointColor[i] = {p.color * p.intensity, 0.f};
	}
	for (size_t i = 0; i < spots; ++i) {
		const SpotLight &s = l.spots[i];
		b.spotPos[i] = {s.position, std::max(s.range, 0.01f)};
		b.spotDir[i] = {s.direction.Normalized(), std::cos(s.angle)};
		b.spotColor[i] = {s.color * s.intensity, std::cos(s.angle * (1.f - std::clamp(s.penumbra, 0.f, 1.f)))};
	}
	return b;
}

// ── RenderTarget3D ─────────────────────────────────────────────────────────────

bool RenderTarget3D::Resize(Renderer3D &renderer, sdl3::Renderer *view2d, int width, int height) {
	if (width <= 0 || height <= 0)
		return false;
	if (width == m_width && height == m_height && Ready() && (view2d == nullptr || m_view))
		return true;
	m_view.reset();
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = Renderer3D::kColorFormat;
	ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ci.width = uint32_t(width);
	ci.height = uint32_t(height);
	ci.layer_count_or_depth = 1;
	ci.num_levels = 1;
	auto color = renderer.Device().CreateTexture(ci);
	ci.format = renderer.DepthFormat();
	ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	auto depth = renderer.Device().CreateTexture(ci);
	if (!color || !depth) {
		core::log::Error("sdl3 : cible {}x{} : {}", width, height, color ? depth.Error() : color.Error());
		return false;
	}
	m_color = std::move(color.Value());
	m_depth = std::move(depth.Value());
	m_width = width;
	m_height = height;
	if (view2d) {
		auto view = sdl3::Texture::FromGpuTexture(*view2d, m_color.Get(), width, height, sdl3::PixelFormat::RGBA32);
		if (!view) {
			core::log::Error("sdl3 : vue 2D de la cible : {}", view.Error());
			return false;
		}
		m_view.emplace(std::move(view.Value()));
	}
	return true;
}

std::vector<uint8_t> RenderTarget3D::ReadPixels(sdl3::GpuDevice &device) {
	if (!Ready())
		return {};
	const uint32_t w = uint32_t(m_width), h = uint32_t(m_height), bytes = w * h * 4;
	auto staging = device.CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes);
	if (!staging)
		return {};
	auto cmd = device.AcquireCommandBuffer();
	{
		auto pass = cmd.BeginCopyPass();
		SDL_GPUTextureRegion src{};
		src.texture = m_color.Get();
		src.w = w;
		src.h = h;
		src.d = 1;
		SDL_GPUTextureTransferInfo dst{};
		dst.transfer_buffer = staging.Value().Get();
		dst.pixels_per_row = w;
		dst.rows_per_layer = h;
		pass.DownloadFromTexture(src, dst);
	}
	if (!cmd.Submit() || !device.WaitIdle())
		return {};
	auto map = staging.Value().Map();
	if (!map)
		return {};
	std::vector<uint8_t> pixels(bytes);
	std::memcpy(pixels.data(), map.GetData(), bytes);
	return pixels;
}

// ── Renderer3D ─────────────────────────────────────────────────────────────────

std::unique_ptr<Renderer3D> Renderer3D::Create(sdl3::GpuDevice &device, uint32_t shadowMapSize) {
	std::unique_ptr<Renderer3D> r(new Renderer3D());
	if (!r->Init(device, shadowMapSize))
		return nullptr;
	return r;
}

Renderer3D::~Renderer3D() = default;

bool Renderer3D::Init(sdl3::GpuDevice &device, uint32_t shadowMapSize) {
	m_device = &device;
	SDL_GPUDevice *dev = device.Get();
	m_depthFormat = device.TextureSupportsFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
						? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
						: SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	const SDL_GPUTextureFormat shadowFormat =
		device.TextureSupportsFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
									 SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)
			? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
			: SDL_GPU_TEXTUREFORMAT_D16_UNORM;

	using namespace spirv;
	auto meshVs = MakeShader(dev, k_mesh_vert, sizeof k_mesh_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	auto meshFs = MakeShader(dev, k_mesh_frag, sizeof k_mesh_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);
	auto shadowVs = MakeShader(dev, k_shadow_vert, sizeof k_shadow_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	auto shadowFs = MakeShader(dev, k_shadow_frag, sizeof k_shadow_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
	auto skyVs = MakeShader(dev, k_sky_vert, sizeof k_sky_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
	auto skyFs = MakeShader(dev, k_sky_frag, sizeof k_sky_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 2);
	auto lineVs = MakeShader(dev, k_line_vert, sizeof k_line_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	auto lineFs = MakeShader(dev, k_line_frag, sizeof k_line_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0);
	if (!meshVs || !meshFs || !shadowVs || !shadowFs || !skyVs || !skyFs || !lineVs || !lineFs) {
		core::log::Error("sdl3 : shaders SPIR-V : {}", SDL_GetError());
		return false;
	}

	SDL_GPUColorTargetDescription opaque{};
	opaque.format = kColorFormat;
	SDL_GPUColorTargetDescription blended = opaque;
	blended.blend_state.enable_blend = true;
	blended.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	blended.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blended.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	blended.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blended.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blended.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

	// Maillages : sommets (emplacement 0) + instances (emplacement 1).
	const SDL_GPUVertexBufferDescription meshBuffers[2] = {{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
														   {1, sizeof(Instance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
	const SDL_GPUVertexAttribute meshAttrs[8] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},	{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24}, {3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
		{4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16}, {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 32},
		{6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 48}, {7, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 64},
	};
	SDL_GPUGraphicsPipelineCreateInfo pi{};
	pi.vertex_shader = meshVs.Get();
	pi.fragment_shader = meshFs.Get();
	pi.vertex_input_state.vertex_buffer_descriptions = meshBuffers;
	pi.vertex_input_state.num_vertex_buffers = 2;
	pi.vertex_input_state.vertex_attributes = meshAttrs;
	pi.vertex_input_state.num_vertex_attributes = 8;
	pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
	pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	pi.depth_stencil_state.enable_depth_test = true;
	pi.depth_stencil_state.enable_depth_write = true;
	pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	pi.target_info.color_target_descriptions = &opaque;
	pi.target_info.num_color_targets = 1;
	pi.target_info.has_depth_stencil_target = true;
	pi.target_info.depth_stencil_format = m_depthFormat;
	auto opaquePipe = device.CreateGraphicsPipeline(pi);
	pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	auto twoSidedPipe = device.CreateGraphicsPipeline(pi);
	pi.depth_stencil_state.enable_depth_write = false;
	pi.target_info.color_target_descriptions = &blended;
	auto transparentPipe = device.CreateGraphicsPipeline(pi);

	// Ombre : profondeur seule, biais de pente contre l'acné.
	SDL_GPUGraphicsPipelineCreateInfo si = pi;
	const SDL_GPUVertexAttribute shadowAttrs[5] = {meshAttrs[0], meshAttrs[3], meshAttrs[4], meshAttrs[5], meshAttrs[6]};
	si.vertex_shader = shadowVs.Get();
	si.fragment_shader = shadowFs.Get();
	si.vertex_input_state.vertex_attributes = shadowAttrs;
	si.vertex_input_state.num_vertex_attributes = 5;
	si.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	si.rasterizer_state.enable_depth_bias = true;
	si.rasterizer_state.depth_bias_constant_factor = 1.25f;
	si.rasterizer_state.depth_bias_slope_factor = 1.75f;
	si.depth_stencil_state.enable_depth_write = true;
	si.target_info.color_target_descriptions = nullptr;
	si.target_info.num_color_targets = 0;
	si.target_info.depth_stencil_format = shadowFormat;
	auto shadowPipe = device.CreateGraphicsPipeline(si);

	// Ciel : triangle plein écran, sans profondeur.
	SDL_GPUGraphicsPipelineCreateInfo ki{};
	ki.vertex_shader = skyVs.Get();
	ki.fragment_shader = skyFs.Get();
	ki.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	ki.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	ki.target_info.color_target_descriptions = &opaque;
	ki.target_info.num_color_targets = 1;
	ki.target_info.has_depth_stencil_target = true;
	ki.target_info.depth_stencil_format = m_depthFormat;
	auto skyPipe = device.CreateGraphicsPipeline(ki);

	// Segments : testés en profondeur, sans l'écrire, mélangés.
	const SDL_GPUVertexBufferDescription lineBuffer{0, sizeof(LineVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
	const SDL_GPUVertexAttribute lineAttrs[2] = {{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
												 {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 12}};
	SDL_GPUGraphicsPipelineCreateInfo li = pi;
	li.vertex_shader = lineVs.Get();
	li.fragment_shader = lineFs.Get();
	li.vertex_input_state.vertex_buffer_descriptions = &lineBuffer;
	li.vertex_input_state.num_vertex_buffers = 1;
	li.vertex_input_state.vertex_attributes = lineAttrs;
	li.vertex_input_state.num_vertex_attributes = 2;
	li.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
	auto linePipe = device.CreateGraphicsPipeline(li);

	if (!opaquePipe || !twoSidedPipe || !transparentPipe || !shadowPipe || !skyPipe || !linePipe) {
		core::log::Error("sdl3 : pipelines : {}", SDL_GetError());
		return false;
	}
	m_opaque = std::move(opaquePipe.Value());
	m_opaqueTwoSided = std::move(twoSidedPipe.Value());
	m_transparent = std::move(transparentPipe.Value());
	m_shadow = std::move(shadowPipe.Value());
	m_sky = std::move(skyPipe.Value());
	m_lines = std::move(linePipe.Value());

	SDL_GPUSamplerCreateInfo sci{};
	sci.min_filter = SDL_GPU_FILTER_LINEAR;
	sci.mag_filter = SDL_GPU_FILTER_LINEAR;
	sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	sci.address_mode_u = sci.address_mode_v = sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sci.enable_anisotropy = true;
	sci.max_anisotropy = 8.f;
	sci.max_lod = 16.f;
	auto linear = device.CreateSampler(sci);
	sci = {};
	sci.min_filter = SDL_GPU_FILTER_LINEAR; // filtrage des comparaisons : PCF matériel 2×2
	sci.mag_filter = SDL_GPU_FILTER_LINEAR;
	sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sci.address_mode_u = sci.address_mode_v = sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sci.enable_compare = true;
	sci.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	auto shadowSampler = device.CreateSampler(sci);
	SDL_GPUTextureCreateInfo tci{};
	tci.type = SDL_GPU_TEXTURETYPE_2D;
	tci.format = shadowFormat;
	tci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	tci.width = tci.height = std::max(shadowMapSize, 16u);
	tci.layer_count_or_depth = 1;
	tci.num_levels = 1;
	auto shadowMap = device.CreateTexture(tci);
	if (!linear || !shadowSampler || !shadowMap) {
		core::log::Error("sdl3 : échantillonneurs ou carte d'ombre : {}", SDL_GetError());
		return false;
	}
	m_linear = std::move(linear.Value());
	m_shadowSampler = std::move(shadowSampler.Value());
	m_shadowMap = std::move(shadowMap.Value());
	m_shadowSize = tci.width;

	static constexpr uint8_t white[4] = {255, 255, 255, 255};
	return CreateTexture(white, 1, 1) == 0; // TextureId 0
}

MeshId Renderer3D::Upload(const MeshData &mesh) {
	if (mesh.vertices.empty() || mesh.indices.empty())
		return 0;
	auto vertices = UploadBuffer(*m_device, SDL_GPU_BUFFERUSAGE_VERTEX, mesh.vertices.data(), uint32_t(mesh.vertices.size() * sizeof(Vertex)));
	auto indices = UploadBuffer(*m_device, SDL_GPU_BUFFERUSAGE_INDEX, mesh.indices.data(), uint32_t(mesh.indices.size() * sizeof(uint32_t)));
	if (!vertices || !indices) {
		core::log::Error("sdl3 : envoi d'un maillage : {}", SDL_GetError());
		return 0;
	}
	GpuMesh gpu{std::move(*vertices), std::move(*indices), uint32_t(mesh.indices.size()), mesh.Bounds()};
	for (size_t i = 0; i < m_meshes.size(); ++i)
		if (m_meshes[i].indexCount == 0) { // emplacement libéré : réutilisé
			m_meshes[i] = std::move(gpu);
			return MeshId(i + 1);
		}
	m_meshes.push_back(std::move(gpu));
	return MeshId(m_meshes.size());
}

bool Renderer3D::Replace(MeshId id, const MeshData &mesh) {
	if (id == 0 || id > m_meshes.size() || mesh.vertices.empty() || mesh.indices.empty())
		return false;
	auto vertices = UploadBuffer(*m_device, SDL_GPU_BUFFERUSAGE_VERTEX, mesh.vertices.data(), uint32_t(mesh.vertices.size() * sizeof(Vertex)));
	auto indices = UploadBuffer(*m_device, SDL_GPU_BUFFERUSAGE_INDEX, mesh.indices.data(), uint32_t(mesh.indices.size() * sizeof(uint32_t)));
	if (!vertices || !indices)
		return false;
	m_meshes[id - 1] = {std::move(*vertices), std::move(*indices), uint32_t(mesh.indices.size()), mesh.Bounds()};
	return true;
}

void Renderer3D::Release(MeshId id) {
	if (id == 0 || id > m_meshes.size())
		return;
	m_meshes[id - 1] = {}; // SDL_GPU diffère la libération tant qu'une image en vol l'utilise
	m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [id](const DrawItem &d) { return d.mesh == id; }), m_items.end());
}

math::FAABB Renderer3D::Bounds(MeshId mesh) const noexcept {
	return mesh > 0 && mesh <= m_meshes.size() ? m_meshes[mesh - 1].bounds : math::FAABB{};
}

TextureId Renderer3D::CreateTexture(std::span<const uint8_t> rgba, uint32_t width, uint32_t height) {
	const uint32_t bytes = width * height * 4;
	if (width == 0 || height == 0 || rgba.size() < bytes)
		return 0;
	const uint32_t levels = uint32_t(std::bit_width(std::max(width, height)));
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; // COLOR_TARGET : mipmaps
	ci.width = width;
	ci.height = height;
	ci.layer_count_or_depth = 1;
	ci.num_levels = levels;
	auto texture = m_device->CreateTexture(ci);
	auto staging = m_device->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes);
	if (!texture || !staging) {
		core::log::Error("sdl3 : texture {}x{} : {}", width, height, SDL_GetError());
		return 0;
	}
	{
		auto map = staging.Value().Map();
		if (!map)
			return 0;
		std::memcpy(map.GetData(), rgba.data(), bytes);
	}
	auto cmd = m_device->AcquireCommandBuffer();
	{
		auto pass = cmd.BeginCopyPass();
		SDL_GPUTextureTransferInfo src{};
		src.transfer_buffer = staging.Value().Get();
		src.pixels_per_row = width;
		src.rows_per_layer = height;
		SDL_GPUTextureRegion dst{};
		dst.texture = texture.Value().Get();
		dst.w = width;
		dst.h = height;
		dst.d = 1;
		pass.UploadToTexture(src, dst);
	}
	if (levels > 1)
		cmd.GenerateMipmaps(texture.Value());
	if (!cmd.Submit())
		return 0;
	m_textures.push_back(std::move(texture.Value()));
	return TextureId(m_textures.size() - 1);
}

void Renderer3D::Draw(MeshId mesh, const math::FMatrix4 &model, const Material &material, const math::FVector4 &tint) {
	const Instance instance{model, tint};
	DrawInstances(mesh, material, std::span(&instance, 1));
}

void Renderer3D::DrawInstances(MeshId mesh, const Material &material, std::span<const Instance> instances) {
	if (mesh == 0 || mesh > m_meshes.size() || m_meshes[mesh - 1].indexCount == 0 || instances.empty())
		return;
	m_items.push_back({mesh, material, uint32_t(m_instances.size()), uint32_t(instances.size()), 0.f});
	m_instances.insert(m_instances.end(), instances.begin(), instances.end());
}

void Renderer3D::Line(const math::FVector3 &a, const math::FVector3 &b, uint8_t r, uint8_t g, uint8_t bl, uint8_t alpha) {
	m_lineVertices.push_back({a, r, g, bl, alpha});
	m_lineVertices.push_back({b, r, g, bl, alpha});
}

void Renderer3D::DashedLine(const math::FVector3 &a, const math::FVector3 &b, float dash, float gap, uint8_t r, uint8_t g, uint8_t bl, uint8_t alpha) {
	const float length = a.Distance(b), period = dash + gap;
	if (length <= 0.f || dash <= 0.f || period <= 0.f)
		return;
	const math::FVector3 dir = (b - a) / length;
	const size_t count = std::min<size_t>(size_t(length / period) + 1, 4096); // borne : très longs axes
	const float step = std::max(period, length / float(count));
	for (float t = 0.f; t < length; t += step)
		Line(a + dir * t, a + dir * std::min(t + dash * step / period, length), r, g, bl, alpha);
}

void Renderer3D::Grid(float halfSize, float step, float y, uint8_t gray, uint8_t alpha) {
	const int n = int(std::floor(halfSize / step));
	for (int i = -n; i <= n; ++i) {
		const float k = float(i) * step;
		const uint8_t a = i == 0 ? uint8_t(std::min(255, alpha * 2)) : alpha;
		Line({k, y, -halfSize}, {k, y, halfSize}, gray, gray, gray, a);
		Line({-halfSize, y, k}, {halfSize, y, k}, gray, gray, gray, a);
	}
}

void Renderer3D::Axes(const math::FVector3 &origin, float length) {
	Line(origin, origin + math::FVector3{length, 0.f, 0.f}, 230, 80, 80);
	Line(origin, origin + math::FVector3{0.f, length, 0.f}, 90, 210, 110);
	Line(origin, origin + math::FVector3{0.f, 0.f, length}, 90, 140, 240);
}

void Renderer3D::WireBox(const math::FAABB &box, const math::FMatrix4 &model, uint8_t r, uint8_t g, uint8_t b) {
	math::FVector3 c[8];
	for (int i = 0; i < 8; ++i)
		c[i] = model.TransformPoint({i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y, i & 4 ? box.max.z : box.min.z});
	static constexpr int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	for (const auto &e : edges)
		Line(c[e[0]], c[e[1]], r, g, b);
}

void Renderer3D::Clear() noexcept {
	m_items.clear();
	m_instances.clear();
	m_lineVertices.clear();
}

bool Renderer3D::UploadFrameData(sdl3::GpuCommandBuffer &cmd) {
	const uint32_t instanceBytes = uint32_t(m_instances.size() * sizeof(Instance));
	const uint32_t lineBytes = uint32_t(m_lineVertices.size() * sizeof(LineVertex));
	if (instanceBytes + lineBytes == 0)
		return true;
	const auto grow = [&](sdl3::GpuBuffer &buffer, uint32_t &capacity, uint32_t needed, SDL_GPUBufferUsageFlags usage) {
		if (needed <= capacity)
			return true;
		const uint32_t size = std::bit_ceil(std::max(needed, 16384u));
		auto created = m_device->CreateBuffer(usage, size);
		if (!created)
			return false;
		buffer = std::move(created.Value());
		capacity = size;
		return true;
	};
	if (!grow(m_instanceBuffer, m_instanceCapacity, instanceBytes, SDL_GPU_BUFFERUSAGE_VERTEX) ||
		!grow(m_lineBuffer, m_lineCapacity, lineBytes, SDL_GPU_BUFFERUSAGE_VERTEX))
		return false;
	if (instanceBytes + lineBytes > m_stagingCapacity) {
		const uint32_t size = std::bit_ceil(instanceBytes + lineBytes);
		auto staging = m_device->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size);
		if (!staging)
			return false;
		m_staging = std::move(staging.Value());
		m_stagingCapacity = size;
	}
	{
		auto map = m_staging.Map(true); // cycle : l'image précédente peut encore le lire
		if (!map)
			return false;
		auto *dst = static_cast<uint8_t *>(map.GetData());
		if (instanceBytes)
			std::memcpy(dst, m_instances.data(), instanceBytes);
		if (lineBytes)
			std::memcpy(dst + instanceBytes, m_lineVertices.data(), lineBytes);
	}
	auto pass = cmd.BeginCopyPass();
	if (instanceBytes)
		pass.UploadToBuffer({m_staging.Get(), 0}, {m_instanceBuffer.Get(), 0, instanceBytes}, true);
	if (lineBytes)
		pass.UploadToBuffer({m_staging.Get(), instanceBytes}, {m_lineBuffer.Get(), 0, lineBytes}, true);
	return true;
}

void Renderer3D::DrawItems(sdl3::GpuRenderPass &pass, sdl3::GpuCommandBuffer &cmd, std::span<const DrawItem *const> items, bool shadowPass) {
	for (const DrawItem *item : items) {
		const GpuMesh &mesh = m_meshes[item->mesh - 1];
		const SDL_GPUBufferBinding buffers[2] = {{mesh.vertices.Get(), 0}, {m_instanceBuffer.Get(), item->firstInstance * uint32_t(sizeof(Instance))}};
		pass.BindVertexBuffers(0, buffers);
		pass.BindIndexBuffer({mesh.indices.Get(), 0}, SDL_GPU_INDEXELEMENTSIZE_32BIT);
		if (!shadowPass) {
			cmd.PushFragmentUniformData(0, MakeMaterial(item->material));
			const TextureId tex = item->material.texture < m_textures.size() ? item->material.texture : 0;
			const SDL_GPUTextureSamplerBinding samplers[2] = {{m_shadowMap.Get(), m_shadowSampler.Get()}, {m_textures[tex].Get(), m_linear.Get()}};
			pass.BindFragmentSamplers(0, samplers);
		}
		pass.DrawIndexedPrimitives(mesh.indexCount, item->instanceCount);
		++m_stats.drawCalls;
		if (!shadowPass) {
			m_stats.instances += item->instanceCount;
			m_stats.triangles += uint64_t(mesh.indexCount / 3) * item->instanceCount;
		}
	}
}

void Renderer3D::Render(sdl3::GpuCommandBuffer &cmd, RenderTarget3D &target, const Camera3D &camera, const Lighting &lighting) {
	m_stats = {};
	if (!target.Ready() || !cmd || !UploadFrameData(cmd)) {
		Clear();
		return;
	}
	m_stats.lines = uint32_t(m_lineVertices.size() / 2);

	// Tri des dessins : ombre, opaques (une ou deux faces), transparents de l'arrière vers l'avant.
	std::vector<const DrawItem *> casters, opaque, twoSided, transparent;
	for (DrawItem &item : m_items) {
		const math::FMatrix4 &m = m_instances[item.firstInstance].model;
		item.depth = (math::FVector3{m.m[3][0], m.m[3][1], m.m[3][2]} - camera.position).LengthSq();
		const Material &mat = item.material;
		if (mat.castShadow && !mat.unlit && mat.opacity >= 1.f)
			casters.push_back(&item);
		(mat.opacity < 1.f ? transparent : mat.doubleSided ? twoSided : opaque).push_back(&item);
	}
	std::ranges::sort(transparent, [](const DrawItem *a, const DrawItem *b) { return a->depth > b->depth; });

	const bool shadow = lighting.sun.castShadow && lighting.sun.intensity > 0.f && !casters.empty();
	const FrameBlock frame{camera.ViewProjection(target.Aspect()), SunViewProjection(lighting.sun)};
	const LightBlock lights = PackLights(lighting, camera.position, shadow, 1.f / float(m_shadowSize));

	if (shadow) {
		SDL_GPUDepthStencilTargetInfo depth{};
		depth.texture = m_shadowMap.Get();
		depth.clear_depth = 1.f;
		depth.load_op = SDL_GPU_LOADOP_CLEAR;
		depth.store_op = SDL_GPU_STOREOP_STORE;
		depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
		depth.cycle = true;
		auto pass = cmd.BeginRenderPass(std::span<const SDL_GPUColorTargetInfo>{}, &depth);
		pass.BindPipeline(m_shadow);
		cmd.PushVertexUniformData(0, frame);
		DrawItems(pass, cmd, casters, true);
	}

	const Environment &env = lighting.environment;
	SDL_GPUColorTargetInfo color{};
	color.texture = target.Color().Get();
	color.clear_color = {Display(env.background.x), Display(env.background.y), Display(env.background.z), 1.f};
	color.load_op = SDL_GPU_LOADOP_CLEAR;
	color.store_op = SDL_GPU_STOREOP_STORE;
	color.cycle = true;
	SDL_GPUDepthStencilTargetInfo depth{};
	depth.texture = target.Depth().Get();
	depth.clear_depth = 1.f;
	depth.load_op = SDL_GPU_LOADOP_CLEAR;
	depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth.cycle = true;
	auto pass = cmd.BeginRenderPass(color, &depth);
	cmd.PushVertexUniformData(0, frame);
	cmd.PushFragmentUniformData(1, lights);
	if (env.drawSky) {
		const math::FVector3 forward = (camera.target - camera.position).Normalized();
		const math::FVector3 right = forward.Cross(camera.up).Normalized(), up = right.Cross(forward);
		const float h = std::tan(camera.fovY * 0.5f);
		cmd.PushFragmentUniformData(0, SkyBlock{{right * (h * target.Aspect()), 0.f}, {up * h, 0.f}, {forward, 0.f}});
		pass.BindPipeline(m_sky);
		pass.DrawPrimitives(3);
	}
	if (!opaque.empty()) {
		pass.BindPipeline(m_opaque);
		DrawItems(pass, cmd, opaque, false);
	}
	if (!twoSided.empty()) {
		pass.BindPipeline(m_opaqueTwoSided);
		DrawItems(pass, cmd, twoSided, false);
	}
	if (!m_lineVertices.empty()) {
		pass.BindPipeline(m_lines);
		const SDL_GPUBufferBinding lines{m_lineBuffer.Get(), 0};
		pass.BindVertexBuffer(0, lines);
		pass.DrawPrimitives(uint32_t(m_lineVertices.size()));
	}
	if (!transparent.empty()) {
		pass.BindPipeline(m_transparent);
		DrawItems(pass, cmd, transparent, false);
	}
	pass.End();
	Clear();
}

} // namespace sdl3
