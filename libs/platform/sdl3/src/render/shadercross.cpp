// Définitions de sdl3/shadercross.hpp
#include <sdl3/sdl3.hpp>
#include "sdl3/render/shadercross.hpp"

namespace sdl3 {

// ── ShaderCross (RAII Init/Quit) ─────────────────────────────────────────────

ShaderCross::~ShaderCross() {
	if (m_owns)
		SDL_ShaderCross_Quit();
}

Result<ShaderCross, Error> ShaderCross::Create() {
	if (!SDL_ShaderCross_Init())
		return Err(GetError());
	return Ok(ShaderCross(true));
}

// ── Helpers internes ─────────────────────────────────────────────────────────

namespace {

/// Recopie une chaîne malloc'd par SDL puis la libère.
[[nodiscard]] Result<String, Error> AdoptString(void *buffer) {
	if (!buffer)
		return Err(GetError());
	String result(static_cast<const char *>(buffer));
	SDL_free(buffer);
	return Ok(std::move(result));
}

/// Recopie un tampon malloc'd par SDL puis le libère.
[[nodiscard]] Result<std::vector<uint8_t>, Error> AdoptBuffer(void *buffer, size_t size) {
	if (!buffer)
		return Err(GetError());
	const auto *bytes = static_cast<const uint8_t *>(buffer);
	std::vector<uint8_t> result(bytes, bytes + size);
	SDL_free(buffer);
	return Ok(std::move(result));
}

} // namespace

// ── Fonctions de transpilation / compilation ─────────────────────────────────

namespace shadercross {

Result<String, Error> TranspileMSLFromSPIRV(const ShaderCrossSPIRVInfo &info) {
	auto sdl = info.ToSDL();
	return AdoptString(SDL_ShaderCross_TranspileMSLFromSPIRV(&sdl));
}

Result<String, Error> TranspileHLSLFromSPIRV(const ShaderCrossSPIRVInfo &info) {
	auto sdl = info.ToSDL();
	return AdoptString(SDL_ShaderCross_TranspileHLSLFromSPIRV(&sdl));
}

Result<std::vector<uint8_t>, Error> CompileDXBCFromSPIRV(const ShaderCrossSPIRVInfo &info) {
	auto sdl = info.ToSDL();
	size_t size = 0;
	void *buffer = SDL_ShaderCross_CompileDXBCFromSPIRV(&sdl, &size);
	return AdoptBuffer(buffer, size);
}

Result<std::vector<uint8_t>, Error> CompileDXILFromSPIRV(const ShaderCrossSPIRVInfo &info) {
	auto sdl = info.ToSDL();
	size_t size = 0;
	void *buffer = SDL_ShaderCross_CompileDXILFromSPIRV(&sdl, &size);
	return AdoptBuffer(buffer, size);
}

Result<std::vector<uint8_t>, Error> CompileDXBCFromHLSL(const ShaderCrossHLSLInfo &info) {
	auto sdl = info.ToSDL();
	size_t size = 0;
	void *buffer = SDL_ShaderCross_CompileDXBCFromHLSL(&sdl, &size);
	return AdoptBuffer(buffer, size);
}

Result<std::vector<uint8_t>, Error> CompileDXILFromHLSL(const ShaderCrossHLSLInfo &info) {
	auto sdl = info.ToSDL();
	size_t size = 0;
	void *buffer = SDL_ShaderCross_CompileDXILFromHLSL(&sdl, &size);
	return AdoptBuffer(buffer, size);
}

Result<std::vector<uint8_t>, Error> CompileSPIRVFromHLSL(const ShaderCrossHLSLInfo &info) {
	auto sdl = info.ToSDL();
	size_t size = 0;
	void *buffer = SDL_ShaderCross_CompileSPIRVFromHLSL(&sdl, &size);
	return AdoptBuffer(buffer, size);
}

Result<GpuShader, Error> CompileGraphicsShaderFromSPIRV(
	const GpuDevice &device, const ShaderCrossSPIRVInfo &info,
	const ShaderCrossGraphicsShaderResourceInfo &resourceInfo, SDL_PropertiesID props) {
	auto sdl = info.ToSDL();
	auto res = resourceInfo.ToSDL();
	SDL_GPUShader *shader = SDL_ShaderCross_CompileGraphicsShaderFromSPIRV(device.Get(), &sdl, &res, props);
	if (!shader)
		return Err(GetError());
	return Ok(GpuShader(device.Get(), shader));
}

Result<GpuComputePipeline, Error> CompileComputePipelineFromSPIRV(
	const GpuDevice &device, const ShaderCrossSPIRVInfo &info,
	const ShaderCrossComputePipelineMetadata &metadata, SDL_PropertiesID props) {
	auto sdl = info.ToSDL();
	SDL_ShaderCross_ComputePipelineMetadata meta{};
	meta.num_samplers = metadata.numSamplers;
	meta.num_readonly_storage_textures = metadata.numReadonlyStorageTextures;
	meta.num_readonly_storage_buffers = metadata.numReadonlyStorageBuffers;
	meta.num_readwrite_storage_textures = metadata.numReadwriteStorageTextures;
	meta.num_readwrite_storage_buffers = metadata.numReadwriteStorageBuffers;
	meta.num_uniform_buffers = metadata.numUniformBuffers;
	meta.threadcount_x = metadata.threadcountX;
	meta.threadcount_y = metadata.threadcountY;
	meta.threadcount_z = metadata.threadcountZ;

	SDL_GPUComputePipeline *pipeline = SDL_ShaderCross_CompileComputePipelineFromSPIRV(device.Get(), &sdl, &meta, props);
	if (!pipeline)
		return Err(GetError());
	return Ok(GpuComputePipeline(device.Get(), pipeline));
}

Result<ShaderCrossGraphicsShaderMetadata, Error> ReflectGraphicsSPIRV(
	std::span<const Uint8> bytecode, SDL_PropertiesID props) {
	auto *meta = SDL_ShaderCross_ReflectGraphicsSPIRV(bytecode.data(), bytecode.size(), props);
	if (!meta)
		return Err(GetError());
	return Ok(ShaderCrossGraphicsShaderMetadata(meta));
}

Result<ShaderCrossComputePipelineMetadataRef, Error> ReflectComputeSPIRV(
	std::span<const Uint8> bytecode, SDL_PropertiesID props) {
	auto *meta = SDL_ShaderCross_ReflectComputeSPIRV(bytecode.data(), bytecode.size(), props);
	if (!meta)
		return Err(GetError());
	return Ok(ShaderCrossComputePipelineMetadataRef(meta));
}

} // namespace shadercross

} // namespace sdl3
