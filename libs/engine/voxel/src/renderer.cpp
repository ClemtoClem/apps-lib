#include "voxel/renderer.hpp"

#include <algorithm>
#include <cstring>

#include <core/log.hpp>
#include <diagnostics/trace.hpp>

#include "shaders_spirv.inc"

namespace voxel {
namespace {

struct FrameUniform {
	math::FMatrix4 viewProj;
	math::FVector4 cameraPos;
	math::FVector4 fog;
};
struct FragUniform {
	math::FVector4 fogColor;
	math::FVector4 params;	 // x = seuil alpha, y = lumière du jour, z = opacité min, w = interval LOD (blocs)
	math::FVector4 cameraPos; // xyz = position de la caméra (calcul du niveau de détail)
	math::FVector4 extra;	  // x = rebonds de réflexion (intensité spéculaire)
};
struct LightsUniform {
	math::FVector4 count;
	math::FVector4 posRange[kMaxPointLights]; // xyz = position, w = portée
	math::FVector4 colorInt[kMaxPointLights]; // rgb = couleur, w = intensité
	math::FVector4 dirType[kMaxPointLights];  // xyz = direction, w = type (0 point, 1 spot, 2 directionnelle)
	math::FVector4 cone[kMaxPointLights];	  // x = cos intérieur, y = cos extérieur, z = rayon de faisceau
	math::FVector4 shadowInfo;				  // xyz = centre du volume d'ombres, w = taille (0 : désactivé)
};

LightsUniform MakeLights(const SceneParams &params, const math::FVector3 &shadowCenter, uint32_t shadowSize) {
	LightsUniform u{};
	const size_t n = std::min(params.lights.size(), kMaxPointLights);
	u.count = {float(n), 0, 0, 0};
	for (size_t i = 0; i < n; ++i) {
		const PointLight &l = params.lights[i];
		u.posRange[i] = {l.position, l.range};
		u.colorInt[i] = {l.color, l.intensity};
		u.dirType[i] = {l.direction, float(static_cast<uint32_t>(l.type))};
		u.cone[i] = {l.innerCos, l.outerCos, l.radius, 0.f};
	}
	u.shadowInfo = {shadowCenter, float(shadowSize)};
	return u;
}

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

struct CubeVertex {
	float x, y, z, nx, ny, nz;
};

void BuildCube(std::vector<CubeVertex> &out) {
	// 6 faces × 2 triangles, sens trigonométrique vu de l'extérieur.
	const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (int f = 0; f < 6; ++f) {
		const int d = f / 2, u = (d + 1) % 3, v = (d + 2) % 3;
		const float s = (f % 2) ? -1.f : 1.f;
		float c[4][3];
		const float cu[4] = {-1, 1, 1, -1}, cv[4] = {-1, -1, 1, 1};
		for (int i = 0; i < 4; ++i) {
			c[i][d] = s;
			c[i][u] = cu[i];
			c[i][v] = s > 0 ? cv[i] : -cv[i];
		}
		const int tri[6] = {0, 1, 2, 0, 2, 3};
		for (int i : tri)
			out.push_back({c[i][0], c[i][1], c[i][2], n[f][0], n[f][1], n[f][2]});
	}
}

void BuildEdges(std::vector<CubeVertex> &out) {
	const float p[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
	const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
	for (const auto &edge : e)
		for (int k : edge)
			out.push_back({p[k][0], p[k][1], p[k][2], 0, 1, 0});
}

} // namespace

std::unique_ptr<Renderer> Renderer::Create(sdl3::GpuDevice &device) {
	std::unique_ptr<Renderer> r(new Renderer());
	if (!r->Init(device))
		return nullptr;
	return r;
}

Renderer::~Renderer() = default;

bool Renderer::Init(sdl3::GpuDevice &device) {
	m_device = device.Get();
	m_dev = &device;
	m_depthFormat = device.TextureSupportsFormat(SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
						? SDL_GPU_TEXTUREFORMAT_D32_FLOAT
						: SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	using namespace spirv;
	auto voxelVs = MakeShader(m_device, k_voxel_vert, sizeof k_voxel_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
	auto voxelFs = MakeShader(m_device, k_voxel_frag, sizeof k_voxel_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 2);
	auto detailVs = MakeShader(m_device, k_detail_vert, sizeof k_detail_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
	auto boxVs = MakeShader(m_device, k_box_vert, sizeof k_box_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	auto boxFs = MakeShader(m_device, k_box_frag, sizeof k_box_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 1);
	auto modelVs = MakeShader(m_device, k_model_vert, sizeof k_model_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 2);
	auto modelFs = MakeShader(m_device, k_model_frag, sizeof k_model_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
	auto postVs = MakeShader(m_device, k_post_vert, sizeof k_post_vert, SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);
	auto postFs = MakeShader(m_device, k_post_frag, sizeof k_post_frag, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);
	if (!voxelVs || !voxelFs || !detailVs || !boxVs || !boxFs || !modelVs || !modelFs || !postVs || !postFs) {
		core::log::Error("shaders SPIR-V : {}", SDL_GetError());
		return false;
	}

	SDL_GPUColorTargetDescription color{};
	color.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	SDL_GPUColorTargetDescription blended = color;
	blended.blend_state.enable_blend = true;
	blended.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	blended.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blended.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	blended.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blended.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blended.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

	// ── Pipelines voxel ─────────────────────────────────────────────────
	SDL_GPUVertexBufferDescription vb{0, sizeof(ChunkVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
	SDL_GPUVertexAttribute attrs[3] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, 0},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, 4},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 8},
	};
	SDL_GPUGraphicsPipelineCreateInfo pi{};
	pi.vertex_shader = voxelVs.Get();
	pi.fragment_shader = voxelFs.Get();
	pi.vertex_input_state.vertex_buffer_descriptions = &vb;
	pi.vertex_input_state.num_vertex_buffers = 1;
	pi.vertex_input_state.vertex_attributes = attrs;
	pi.vertex_input_state.num_vertex_attributes = 3;
	pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
	pi.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	pi.depth_stencil_state.enable_depth_test = true;
	pi.depth_stencil_state.enable_depth_write = true;
	pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	pi.target_info.color_target_descriptions = &color;
	pi.target_info.num_color_targets = 1;
	pi.target_info.has_depth_stencil_target = true;
	pi.target_info.depth_stencil_format = m_depthFormat;
	auto opaque = device.CreateGraphicsPipeline(pi);
	pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pi.depth_stencil_state.enable_depth_write = false;
	pi.target_info.color_target_descriptions = &blended;
	auto translucent = device.CreateGraphicsPipeline(pi);
	// Formes détaillées : sommets flottants, faces visibles des deux côtés (maillages .obj
	// d'orientation quelconque), découpe alpha comme les cubes.
	SDL_GPUVertexBufferDescription dvb{0, sizeof(DetailVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
	SDL_GPUVertexAttribute dattrs[3] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 12},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_USHORT2, 20},
	};
	SDL_GPUGraphicsPipelineCreateInfo di = pi;
	di.vertex_shader = detailVs.Get();
	di.vertex_input_state.vertex_buffer_descriptions = &dvb;
	di.vertex_input_state.vertex_attributes = dattrs;
	di.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	di.depth_stencil_state.enable_depth_write = true;
	di.target_info.color_target_descriptions = &color;
	auto detail = device.CreateGraphicsPipeline(di);

	// ── Pipelines boîtes (triangles instanciés) et contour (lignes) ───────
	SDL_GPUVertexBufferDescription bvb[2] = {{0, sizeof(CubeVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
											 {1, sizeof(BoxInstance), SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0}};
	SDL_GPUVertexAttribute battrs[5] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
		{2, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
		{3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
		{4, 1, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 24},
	};
	SDL_GPUGraphicsPipelineCreateInfo bi = pi;
	bi.vertex_shader = boxVs.Get();
	bi.fragment_shader = boxFs.Get();
	bi.vertex_input_state.vertex_buffer_descriptions = bvb;
	bi.vertex_input_state.num_vertex_buffers = 2;
	bi.vertex_input_state.vertex_attributes = battrs;
	bi.vertex_input_state.num_vertex_attributes = 5;
	bi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
	bi.depth_stencil_state.enable_depth_write = true;
	bi.target_info.color_target_descriptions = &blended;
	auto boxes = device.CreateGraphicsPipeline(bi);
	bi.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
	bi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	bi.depth_stencil_state.enable_depth_write = false;
	auto lines = device.CreateGraphicsPipeline(bi);
	if (!opaque || !translucent || !boxes || !lines || !detail) {
		core::log::Error("pipelines GPU : {}", SDL_GetError());
		return false;
	}
	m_opaque = std::move(opaque.Value());
	m_translucent = std::move(translucent.Value());
	m_boxes = std::move(boxes.Value());
	m_lines = std::move(lines.Value());
	m_detail = std::move(detail.Value());

	// ── Tampons statiques : index de quads, cube, arêtes ─────────────────
	std::vector<uint32_t> indices(size_t(kMaxQuads) * 6);
	for (uint32_t q = 0; q < kMaxQuads; ++q) {
		const uint32_t b = q * 4;
		const uint32_t tri[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
		std::memcpy(&indices[size_t(q) * 6], tri, sizeof tri);
	}
	std::vector<CubeVertex> cube, edges;
	BuildCube(cube);
	BuildEdges(edges);
	auto upload = [&](sdl3::GpuBuffer &dst, SDL_GPUBufferUsageFlags usage, const void *data, uint32_t size) {
		auto buf = device.CreateBuffer(usage, size);
		auto tb = device.CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size);
		if (!buf || !tb)
			return false;
		{
			auto map = tb.Value().Map();
			std::memcpy(map.GetData(), data, size);
		}
		auto cmd = device.AcquireCommandBuffer();
		{
			auto pass = cmd.BeginCopyPass();
			pass.UploadToBuffer({tb.Value().Get(), 0}, {buf.Value().Get(), 0, size});
		}
		const bool ok = cmd.Submit();
		dst = std::move(buf.Value());
		return ok;
	};
	if (!upload(m_quadIndices, SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), uint32_t(indices.size() * 4)) ||
		!upload(m_cubeVertices, SDL_GPU_BUFFERUSAGE_VERTEX, cube.data(), uint32_t(cube.size() * sizeof(CubeVertex))) ||
		!upload(m_lineVertices, SDL_GPU_BUFFERUSAGE_VERTEX, edges.data(), uint32_t(edges.size() * sizeof(CubeVertex)))) {
		core::log::Error("tampons statiques : {}", SDL_GetError());
		return false;
	}

	// ── Pipeline du modèle texturé (skin du joueur) ─────────────────────
	{
		SDL_GPUVertexBufferDescription mvb{0, sizeof(ModelVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
		SDL_GPUVertexAttribute mattrs[3] = {
			{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
			{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 12},
			{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 24},
		};
		SDL_GPUGraphicsPipelineCreateInfo mi = pi;
		mi.vertex_shader = modelVs.Get();
		mi.fragment_shader = modelFs.Get();
		mi.vertex_input_state.vertex_buffer_descriptions = &mvb;
		mi.vertex_input_state.num_vertex_buffers = 1;
		mi.vertex_input_state.vertex_attributes = mattrs;
		mi.vertex_input_state.num_vertex_attributes = 3;
		mi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		mi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
		mi.depth_stencil_state.enable_depth_write = true;
		mi.target_info.color_target_descriptions = &blended;
		auto model = device.CreateGraphicsPipeline(mi);
		if (!model) {
			core::log::Error("pipeline modèle : {}", SDL_GetError());
			return false;
		}
		m_model = std::move(model.Value());
	}

	// ── Pipeline de post-traitement (triangle plein écran) ───────────────
	{
		SDL_GPUGraphicsPipelineCreateInfo ppi = pi;
		ppi.vertex_shader = postVs.Get();
		ppi.fragment_shader = postFs.Get();
		ppi.vertex_input_state.num_vertex_buffers = 0;
		ppi.vertex_input_state.num_vertex_attributes = 0;
		ppi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		ppi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		ppi.depth_stencil_state.enable_depth_test = false;
		ppi.depth_stencil_state.enable_depth_write = false;
		ppi.target_info.color_target_descriptions = &color;
		auto post = device.CreateGraphicsPipeline(ppi);
		if (!post) {
			core::log::Error("pipeline post-traitement : {}", SDL_GetError());
			return false;
		}
		m_post = std::move(post.Value());
	}

	SDL_GPUSamplerCreateInfo si{};
	si.min_filter = SDL_GPU_FILTER_NEAREST;
	si.mag_filter = SDL_GPU_FILTER_NEAREST;
	si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	si.max_lod = float(kTextureMipLevels);
	auto sampler = device.CreateSampler(si);
	if (!sampler)
		return false;
	m_sampler = std::move(sampler.Value());
	// Échantillonneur du skin (le plus proche, bordure resserrée).
	si.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	si.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	si.max_lod = float(kTextureMipLevels);
	auto skinSampler = device.CreateSampler(si);
	if (!skinSampler)
		return false;
	m_skinSampler = std::move(skinSampler.Value());
	// Échantillonneur du volume d'ombres (3D, au plus proche, bordure resserrée).
	si.min_filter = SDL_GPU_FILTER_NEAREST;
	si.mag_filter = SDL_GPU_FILTER_NEAREST;
	si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	si.max_lod = 0.f;
	auto shadowSampler = device.CreateSampler(si);
	if (!shadowSampler)
		return false;
	m_shadowSampler = std::move(shadowSampler.Value());
	// Volume d'ombres par défaut (1×1×1 vide) : aucune occlusion tant qu'une grille
	// n'est pas fournie par le jeu.
	{
		SDL_GPUTextureCreateInfo sci{};
		sci.type = SDL_GPU_TEXTURETYPE_3D;
		sci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
		sci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		sci.width = sci.height = sci.layer_count_or_depth = 1;
		sci.num_levels = 1;
		auto stex = device.CreateTexture(sci);
		if (!stex)
			return false;
		m_shadowVolume = std::move(stex.Value());
	}
	// Texture par défaut (1 couche blanche) en attendant celles des mods.
	std::vector<uint8_t> white(size_t(kTextureSize) * kTextureSize * 4, 255);
	return SetBlockTextures(white, 1);
}

bool Renderer::SetBlockTextures(std::span<const uint8_t> rgba, uint32_t layers) {
	constexpr uint32_t kSize = kTextureSize, kLevels = kTextureMipLevels;
	if (layers == 0 || rgba.size() < size_t(layers) * kSize * kSize * 4)
		return false;
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D_ARRAY;
	ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; // COLOR_TARGET : mipmaps
	ci.width = kSize;
	ci.height = kSize;
	ci.layer_count_or_depth = layers;
	ci.num_levels = kLevels;
	auto tex = m_dev->CreateTexture(ci);
	auto tb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(rgba.size()));
	if (!tex || !tb)
		return false;
	{
		auto map = tb.Value().Map();
		std::memcpy(map.GetData(), rgba.data(), rgba.size());
	}
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
	for (uint32_t l = 0; l < layers; ++l) {
		SDL_GPUTextureTransferInfo src{tb.Value().Get(), l * kSize * kSize * 4, kSize, kSize};
		SDL_GPUTextureRegion dst{};
		dst.texture = tex.Value().Get();
		dst.layer = l;
		dst.w = kSize;
		dst.h = kSize;
		dst.d = 1;
		SDL_UploadToGPUTexture(pass, &src, &dst, false);
	}
	SDL_EndGPUCopyPass(pass);
	SDL_GenerateMipmapsForGPUTexture(cmd, tex.Value().Get());
	const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
	m_blockTextures = std::move(tex.Value());
	return ok;
}

void Renderer::QueueChunkMesh(uint64_t key, const math::IVec3 &origin, std::vector<ChunkVertex> opaque,
								   std::vector<ChunkVertex> translucent, std::vector<DetailVertex> detail) {
	// Remplace un téléversement en attente pour la même clé.
	for (PendingUpload &p : m_pending)
		if (p.key == key) {
			p.origin = origin;
			p.opaque = std::move(opaque);
			p.translucent = std::move(translucent);
			p.detail = std::move(detail);
			return;
		}
	m_pending.push_back({key, origin, std::move(opaque), std::move(translucent), std::move(detail)});
}

void Renderer::RemoveChunk(uint64_t key) {
	std::erase_if(m_pending, [&](const PendingUpload &p) { return p.key == key; });
	m_chunks.Erase(key); // les tampons GPU sont libérés en fin d'utilisation par SDL
}

void Renderer::Clear() {
	m_pending.clear();
	m_chunks.Clear();
}

void Renderer::FlushUploads(sdl3::GpuCommandBuffer &cmd) {
	DIAG_ZONE("Renderer::FlushUploads");
	m_stats.uploadedBytes = 0;
	if (m_pending.empty())
		return;
	// Sélection dans la limite du budget (au moins un maillage par image).
	size_t total = 0, count = 0;
	for (; count < m_pending.size(); ++count) {
		const size_t sz = (m_pending[count].opaque.size() + m_pending[count].translucent.size()) * sizeof(ChunkVertex) +
						  m_pending[count].detail.size() * sizeof(DetailVertex);
		if (count > 0 && total + sz > m_uploadBudget)
			break;
		total += sz;
	}
	sdl3::GpuDevice &dev = *m_dev;
	std::optional<sdl3::GpuTransferBuffer> staging;
	uint8_t *mapped = nullptr;
	if (total > 0) {
		auto tb = dev.CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(total));
		if (!tb)
			return;
		staging.emplace(std::move(tb.Value()));
		mapped = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(m_device, staging->Get(), false));
	}
	struct Copy {
		SDL_GPUBuffer *dst;
		uint32_t offset, size;
	};
	std::vector<Copy> copies;
	uint32_t offset = 0;
	auto makeBuffer = [&]<typename V>(std::vector<V> &verts, sdl3::GpuBuffer &out, uint32_t &quads) {
		quads = std::min<uint32_t>(uint32_t(verts.size() / 4), kMaxQuads);
		out = {};
		if (quads == 0)
			return;
		const uint32_t size = quads * 4 * uint32_t(sizeof(V));
		auto buf = dev.CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, size);
		if (!buf) {
			quads = 0;
			return;
		}
		out = std::move(buf.Value());
		std::memcpy(mapped + offset, verts.data(), size);
		copies.push_back({out.Get(), offset, size});
		offset += size;
	};
	for (size_t i = 0; i < count; ++i) {
		PendingUpload &p = m_pending[i];
		ChunkGpu &gpu = m_chunks[p.key];
		gpu.origin = p.origin;
		makeBuffer(p.opaque, gpu.opaque, gpu.opaqueQuads);
		makeBuffer(p.translucent, gpu.translucent, gpu.translucentQuads);
		makeBuffer(p.detail, gpu.detail, gpu.detailQuads);
		if (gpu.opaqueQuads == 0 && gpu.translucentQuads == 0 && gpu.detailQuads == 0)
			m_chunks.Erase(p.key); // chunk vide : rien à dessiner
	}
	if (mapped)
		SDL_UnmapGPUTransferBuffer(m_device, staging->Get());
	if (!copies.empty()) {
		auto pass = cmd.BeginCopyPass();
		for (const Copy &c : copies)
			pass.UploadToBuffer({staging->Get(), c.offset}, {c.dst, 0, c.size});
	}
	m_pending.erase(m_pending.begin(), m_pending.begin() + std::ptrdiff_t(count));
	m_stats.uploadedBytes = total;
}

void Renderer::Render(sdl3::GpuCommandBuffer &cmd, sdl3::GpuTexture &color, sdl3::GpuTexture &depth, float aspect,
					   const Camera &camera, const SceneParams &params, std::span<const BoxInstance> boxes,
					   const BoxInstance *outline) {
	DIAG_ZONE("Renderer::Render");
	FlushUploads(cmd);
	RenderPass(cmd, color.Get(), depth.Get(), aspect, camera, params, boxes, outline);
}

void Renderer::RenderPass(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *color, SDL_GPUTexture *depth, float aspect,
							   const Camera &camera, const SceneParams &params, std::span<const BoxInstance> boxes,
							   const BoxInstance *outline) {
	DIAG_ZONE("Renderer::RenderPass");

	// Tête dans un bloc opaque : plein écran noir (aucun rendu du monde).
	if (params.blackout) {
		SDL_GPUColorTargetInfo ct{};
		ct.texture = color;
		ct.clear_color = {0.f, 0.f, 0.f, 1.f};
		ct.load_op = SDL_GPU_LOADOP_CLEAR;
		ct.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPUDepthStencilTargetInfo dt{};
		dt.texture = depth;
		dt.clear_depth = 1.f;
		dt.load_op = SDL_GPU_LOADOP_CLEAR;
		dt.store_op = SDL_GPU_STOREOP_DONT_CARE;
		dt.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
		dt.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
		SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd.Get(), &ct, 1, &dt);
		SDL_EndGPURenderPass(pass);
		return;
	}

	// Instances de boîtes (entités) : un tampon redimensionné au besoin.
	const size_t boxCount = boxes.size() + (outline ? 1 : 0);
	std::optional<sdl3::GpuTransferBuffer> boxStaging;
	if (boxCount > 0) {
		if (boxCount > m_instanceCapacity) {
			m_instanceCapacity = std::max<uint32_t>(uint32_t(boxCount), m_instanceCapacity * 2 + 256);
			auto buf = m_dev->CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, m_instanceCapacity * uint32_t(sizeof(BoxInstance)));
			if (buf)
				m_instances = std::move(buf.Value());
		}
		const uint32_t bytes = uint32_t(boxCount * sizeof(BoxInstance));
		auto tb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes);
		if (tb && m_instances) {
			auto *p = static_cast<BoxInstance *>(SDL_MapGPUTransferBuffer(m_device, tb.Value().Get(), false));
			std::copy(boxes.begin(), boxes.end(), p);
			if (outline)
				p[boxes.size()] = *outline;
			SDL_UnmapGPUTransferBuffer(m_device, tb.Value().Get());
			auto pass = cmd.BeginCopyPass();
			pass.UploadToBuffer({tb.Value().Get(), 0}, {m_instances.Get(), 0, bytes});
			boxStaging.emplace(std::move(tb.Value()));
		}
	}

	const math::FMatrix4 vp = camera.ViewProjection(aspect);
	const math::FFrustum frustum = math::FFrustum::FromMatrix(vp);
	FrameUniform frame{vp, {camera.position, 1.f}, {params.fogStart, params.fogEnd, 0, 0}};
	FragUniform frag{params.skyColor, {0.5f, params.daylight, 0.f, std::max(params.lodInterval, 1.f)}, {camera.position, 0.f},
					 {std::max(params.reflectionBounces, 0.f), 0.f, 0.f, 0.f}};

	// Chunks visibles, triés par distance.
	struct Visible {
		const ChunkGpu *chunk;
		float dist2;
	};
	std::vector<Visible> visible;
	visible.reserve(m_chunks.Size());
	for (const auto &e : m_chunks) {
		const ChunkGpu &c = e.value;
		const math::FVector3 o = c.origin.ToFloat();
		const math::FAABB box{o, o + math::FVector3(32.f)};
		if (!frustum.Intersects(box))
			continue;
		visible.push_back({&c, (box.Center() - camera.position).LengthSq()});
	}
	std::ranges::sort(visible, {}, &Visible::dist2);

	SDL_GPUColorTargetInfo colorTarget{};
	colorTarget.texture = color;
	colorTarget.clear_color = {params.skyColor.x, params.skyColor.y, params.skyColor.z, 1.f};
	colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
	colorTarget.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPUDepthStencilTargetInfo depthTarget{};
	depthTarget.texture = depth;
	depthTarget.clear_depth = 1.f;
	depthTarget.load_op = SDL_GPU_LOADOP_CLEAR;
	depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
	depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd.Get(), &colorTarget, 1, &depthTarget);
	cmd.PushVertexUniformData(0, frame);
	cmd.PushFragmentUniformData(0, frag);
	cmd.PushFragmentUniformData(1, MakeLights(params, m_shadowCenter, m_shadowSize));
	SDL_GPUTextureSamplerBinding texBinding{m_blockTextures.Get(), m_sampler.Get()};
	SDL_GPUTextureSamplerBinding shadowBinding{m_shadowVolume.Get(), m_shadowSampler.Get()};
	SDL_GPUBufferBinding indexBinding{m_quadIndices.Get(), 0};

	m_stats = {m_chunks.Size(), 0, 0, 0, 0, m_stats.uploadedBytes};
	auto drawChunks = [&](bool translucentPass) {
		SDL_BindGPUGraphicsPipeline(pass, translucentPass ? m_translucent.Get() : m_opaque.Get());
		SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
		SDL_BindGPUFragmentSamplers(pass, 1, &shadowBinding, 1);
		SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
		FragUniform f = frag;
		f.params.x = translucentPass ? 0.02f : 0.5f;
		cmd.PushFragmentUniformData(0, f);
		auto drawOne = [&](const ChunkGpu &c) {
			const sdl3::GpuBuffer &buf = translucentPass ? c.translucent : c.opaque;
			const uint32_t quads = translucentPass ? c.translucentQuads : c.opaqueQuads;
			if (!quads)
				return;
			const math::FVector4 origin(c.origin.ToFloat(), 0.f);
			cmd.PushVertexUniformData(1, origin);
			SDL_GPUBufferBinding vb{buf.Get(), 0};
			SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
			SDL_DrawGPUIndexedPrimitives(pass, quads * 6, 1, 0, 0, 0);
			++m_stats.drawCalls;
			m_stats.verticesDrawn += quads * 4;
		};
		if (translucentPass)
			for (auto it = visible.rbegin(); it != visible.rend(); ++it)
				drawOne(*it->chunk);
		else
			for (const Visible &v : visible) {
				drawOne(*v.chunk);
				++m_stats.chunksDrawn;
			}
	};
	drawChunks(false);
	// Formes détaillées (nodebox, maillages) : même texture-tableau, découpe alpha.
	{
		SDL_BindGPUGraphicsPipeline(pass, m_detail.Get());
		SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
		SDL_BindGPUFragmentSamplers(pass, 1, &shadowBinding, 1);
		SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
		FragUniform f = frag;
		f.params.x = 0.5f;
		cmd.PushFragmentUniformData(0, f);
		for (const Visible &v : visible) {
			const ChunkGpu &c = *v.chunk;
			if (!c.detailQuads)
				continue;
			const math::FVector4 origin(c.origin.ToFloat(), 0.f);
			cmd.PushVertexUniformData(1, origin);
			SDL_GPUBufferBinding vb{c.detail.Get(), 0};
			SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
			SDL_DrawGPUIndexedPrimitives(pass, c.detailQuads * 6, 1, 0, 0, 0);
			++m_stats.drawCalls;
			m_stats.verticesDrawn += c.detailQuads * 4;
		}
	}
	if (!boxes.empty() && m_instances) {
		SDL_BindGPUGraphicsPipeline(pass, m_boxes.Get());
		SDL_GPUBufferBinding vbs[2] = {{m_cubeVertices.Get(), 0}, {m_instances.Get(), 0}};
		SDL_BindGPUVertexBuffers(pass, 0, vbs, 2);
		SDL_DrawGPUPrimitives(pass, 36, uint32_t(boxes.size()), 0, 0);
		++m_stats.drawCalls;
	}
	drawChunks(true);
	if (outline && m_instances) {
		SDL_BindGPUGraphicsPipeline(pass, m_lines.Get());
		SDL_GPUBufferBinding vbs[2] = {{m_lineVertices.Get(), 0}, {m_instances.Get(), 0}};
		SDL_BindGPUVertexBuffers(pass, 0, vbs, 2);
		SDL_DrawGPUPrimitives(pass, 24, 1, 0, uint32_t(boxes.size()));
		++m_stats.drawCalls;
	}
	SDL_EndGPURenderPass(pass);
	for (const auto &e : m_chunks)
		m_stats.gpuBytes += (e.value.opaqueQuads + e.value.translucentQuads) * 4 * sizeof(ChunkVertex) + e.value.detailQuads * 4 * sizeof(DetailVertex);
}

bool Renderer::SetSkinTexture(const uint8_t *rgba, int w, int h) {
	if (!rgba || w <= 0 || h <= 0)
		return false;
	int levels = 1;
	while ((1 << levels) <= std::max(w, h))
		++levels; // mipmaps : pleine résolution → 1×1
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; // COLOR_TARGET : mipmaps
	ci.width = static_cast<uint32_t>(w);
	ci.height = static_cast<uint32_t>(h);
	ci.layer_count_or_depth = 1;
	ci.num_levels = static_cast<uint32_t>(levels);
	auto tex = m_dev->CreateTexture(ci);
	auto tb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(w * h * 4));
	if (!tex || !tb)
		return false;
	{
		auto map = tb.Value().Map();
		std::memcpy(map.GetData(), rgba, size_t(w * h * 4));
	}
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureTransferInfo src{tb.Value().Get(), 0, uint32_t(w), uint32_t(h)};
	SDL_GPUTextureRegion dst{};
	dst.texture = tex.Value().Get();
	dst.w = uint32_t(w);
	dst.h = uint32_t(h);
	dst.d = 1;
	SDL_UploadToGPUTexture(pass, &src, &dst, false);
	SDL_EndGPUCopyPass(pass);
	SDL_GenerateMipmapsForGPUTexture(cmd, tex.Value().Get());
	const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
	m_skinTexture = std::move(tex.Value());
	return ok;
}

bool Renderer::SetShadowVolume(std::span<const uint8_t> opacity, uint32_t size, const math::FVector3 &center) {
	if (size == 0 || opacity.size() < size_t(size) * size * size)
		return false;
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_3D;
	ci.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
	ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ci.width = size;
	ci.height = size;
	ci.layer_count_or_depth = size;
	ci.num_levels = 1;
	auto tex = m_dev->CreateTexture(ci);
	auto tb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(size) * size * size);
	if (!tex || !tb)
		return false;
	{
		auto map = tb.Value().Map();
		std::memcpy(map.GetData(), opacity.data(), size_t(size) * size * size);
	}
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
	SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureTransferInfo src{tb.Value().Get(), 0, size, size};
	SDL_GPUTextureRegion dst{};
	dst.texture = tex.Value().Get();
	dst.w = size;
	dst.h = size;
	dst.d = size;
	SDL_UploadToGPUTexture(pass, &src, &dst, false);
	SDL_EndGPUCopyPass(pass);
	const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
	m_shadowVolume = std::move(tex.Value());
	m_shadowSize = size;
	m_shadowCenter = center;
	return ok;
}

bool Renderer::SetModelMesh(std::span<const ModelVertex> vertices) {
	if (vertices.empty())
		return false;
	const uint32_t size = uint32_t(vertices.size() * sizeof(ModelVertex));
	auto buf = m_dev->CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, size);
	auto tb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, size);
	if (!buf || !tb)
		return false;
	{
		auto map = tb.Value().Map();
		std::memcpy(map.GetData(), vertices.data(), size);
	}
	auto cmd = m_dev->AcquireCommandBuffer();
	{
		auto pass = cmd.BeginCopyPass();
		pass.UploadToBuffer({tb.Value().Get(), 0}, {buf.Value().Get(), 0, size});
	}
	const bool ok = cmd.Submit();
	m_modelVertices = std::move(buf.Value());
	m_modelVertexCount = uint32_t(vertices.size());
	return ok;
}

void Renderer::RenderModel(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *color, SDL_GPUTexture *depth, float aspect,
								const Camera &camera, const SceneParams &params, const math::FMatrix4 &model) {
	if (!m_model || !m_modelVertices || !m_skinTexture || m_modelVertexCount == 0)
		return;
	const math::FMatrix4 vp = camera.ViewProjection(aspect);
	FrameUniform frame{vp, {camera.position, 1.f}, {params.fogStart, params.fogEnd, 0, 0}};
	FragUniform frag{params.skyColor, {0.5f, params.daylight, 0.f, std::max(params.lodInterval, 1.f)}, {camera.position, 0.f},
					 {std::max(params.reflectionBounces, 0.f), 0.f, 0.f, 0.f}};

	SDL_GPUColorTargetInfo colorTarget{};
	colorTarget.texture = color;
	colorTarget.clear_color = {0, 0, 0, 0};
	colorTarget.load_op = SDL_GPU_LOADOP_LOAD; // conserve la scène déjà rendue
	colorTarget.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPUDepthStencilTargetInfo depthTarget{};
	depthTarget.texture = depth;
	depthTarget.clear_depth = 1.f;
	depthTarget.load_op = SDL_GPU_LOADOP_LOAD; // occlusion correcte par les chunks
	depthTarget.store_op = SDL_GPU_STOREOP_STORE;
	depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd.Get(), &colorTarget, 1, &depthTarget);
	cmd.PushVertexUniformData(0, frame);
	cmd.PushVertexUniformData(1, model);
	cmd.PushFragmentUniformData(0, frag);
	SDL_BindGPUGraphicsPipeline(pass, m_model.Get());
	SDL_GPUTextureSamplerBinding texBinding{m_skinTexture.Get(), m_skinSampler.Get()};
	SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
	SDL_GPUBufferBinding vb{m_modelVertices.Get(), 0};
	SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
	SDL_DrawGPUPrimitives(pass, m_modelVertexCount, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
}

uint64_t Renderer::UploadModel(std::string_view key, std::span<const uint8_t> rgba, int w, int h) {
	if (rgba.empty() || w <= 0 || h <= 0)
		return 0;
	const std::string skey(key);
	if (const uint64_t *handle = m_modelKeys.Find(skey))
		return *handle; // texture déjà téléversée

	int levels = 1;
	while ((1 << levels) <= std::max(w, h))
		++levels;
	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ci.width = static_cast<uint32_t>(w);
	ci.height = static_cast<uint32_t>(h);
	ci.layer_count_or_depth = 1;
	ci.num_levels = static_cast<uint32_t>(levels);
	auto tex = m_dev->CreateTexture(ci);
	auto ttb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, uint32_t(w * h * 4));
	if (!tex || !ttb)
		return 0;
	{
		auto map = ttb.Value().Map();
		std::memcpy(map.GetData(), rgba.data(), size_t(w * h * 4));
	}
	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(m_device);
	if (!cmd)
		return 0;
	{
		SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureTransferInfo tsrc{ttb.Value().Get(), 0, uint32_t(w), uint32_t(h)};
		SDL_GPUTextureRegion tdst{};
		tdst.texture = tex.Value().Get();
		tdst.w = uint32_t(w);
		tdst.h = uint32_t(h);
		tdst.d = 1;
		SDL_UploadToGPUTexture(pass, &tsrc, &tdst, false);
		SDL_EndGPUCopyPass(pass);
		SDL_GenerateMipmapsForGPUTexture(cmd, tex.Value().Get());
	}
	SDL_SubmitGPUCommandBuffer(cmd);

	ModelGpu g;
	g.texture = std::move(tex.Value());
	const uint64_t handle = m_nextModel++;
	m_modelKeys.InsertOrAssign(skey, handle);
	m_models.InsertOrAssign(handle, std::move(g));
	return handle;
}

void Renderer::ReleaseModel(uint64_t handle) {
	m_models.Erase(handle);
	std::vector<std::string> stale;
	for (const auto &e : m_modelKeys)
		if (e.value == handle)
			stale.push_back(e.key);
	for (const std::string &k : stale)
		m_modelKeys.Erase(k);
}

bool Renderer::UpdateModelMesh(uint64_t handle, std::span<const ModelVertex> vertices) {
	ModelGpu *g = m_models.Find(handle);
	if (!g || vertices.empty())
		return false;
	const uint32_t vsize = uint32_t(vertices.size() * sizeof(ModelVertex));
	auto buf = m_dev->CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, vsize);
	auto tb = m_dev->CreateTransferBuffer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, vsize);
	if (!buf || !tb)
		return false;
	{
		auto map = tb.Value().Map();
		std::memcpy(map.GetData(), vertices.data(), vsize);
	}
	auto cmd = m_dev->AcquireCommandBuffer();
	{
		auto pass = cmd.BeginCopyPass();
		pass.UploadToBuffer({tb.Value().Get(), 0}, {buf.Value().Get(), 0, vsize});
	}
	const bool ok = cmd.Submit();
	g->vertices = std::move(buf.Value());
	g->vertexCount = uint32_t(vertices.size());
	return ok;
}

void Renderer::RenderModels(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *color, SDL_GPUTexture *depth, float aspect,
								 const Camera &camera, const SceneParams &params, std::span<const ModelDraw> draws) {
	if (draws.empty() || !m_model)
		return;
	const math::FMatrix4 vp = camera.ViewProjection(aspect);
	FrameUniform frame{vp, {camera.position, 1.f}, {params.fogStart, params.fogEnd, 0, 0}};
	FragUniform frag{params.skyColor, {0.5f, params.daylight, 0.f, std::max(params.lodInterval, 1.f)}, {camera.position, 0.f},
					 {std::max(params.reflectionBounces, 0.f), 0.f, 0.f, 0.f}};

	SDL_GPUColorTargetInfo colorTarget{};
	colorTarget.texture = color;
	colorTarget.clear_color = {0, 0, 0, 0};
	colorTarget.load_op = SDL_GPU_LOADOP_LOAD;
	colorTarget.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPUDepthStencilTargetInfo depthTarget{};
	depthTarget.texture = depth;
	depthTarget.clear_depth = 1.f;
	depthTarget.load_op = SDL_GPU_LOADOP_LOAD;
	depthTarget.store_op = SDL_GPU_STOREOP_STORE;
	depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd.Get(), &colorTarget, 1, &depthTarget);
	cmd.PushVertexUniformData(0, frame);
	cmd.PushFragmentUniformData(0, frag);
	SDL_BindGPUGraphicsPipeline(pass, m_model.Get());
	for (const ModelDraw &draw : draws) {
		const ModelGpu *g = m_models.Find(draw.model);
		if (!g || g->vertexCount == 0 || !g->vertices || !g->texture)
			continue;
		cmd.PushVertexUniformData(1, draw.transform);
		SDL_GPUTextureSamplerBinding texBinding{g->texture.Get(), m_skinSampler.Get()};
		SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
		SDL_GPUBufferBinding vb{g->vertices.Get(), 0};
		SDL_BindGPUVertexBuffers(pass, 0, &vb, 1);
		SDL_DrawGPUPrimitives(pass, g->vertexCount, 1, 0, 0);
		++m_stats.drawCalls;
	}
	SDL_EndGPURenderPass(pass);
}

void Renderer::PostProcess(sdl3::GpuCommandBuffer &cmd, SDL_GPUTexture *scene, SDL_GPUTexture *out, const PostParams &params) {
	if (!m_post)
		return;
	SDL_GPUColorTargetInfo colorTarget{};
	colorTarget.texture = out;
	colorTarget.clear_color = {0, 0, 0, 0};
	colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
	colorTarget.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd.Get(), &colorTarget, 1, nullptr);
	cmd.PushFragmentUniformData(0, params);
	SDL_BindGPUGraphicsPipeline(pass, m_post.Get());
	SDL_GPUTextureSamplerBinding texBinding{scene, m_skinSampler.Get()};
	SDL_BindGPUFragmentSamplers(pass, 0, &texBinding, 1);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
}

} // namespace voxel
