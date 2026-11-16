#pragma once
/**
 * @file shadercross.hpp
 * @brief Transpilation de shaders (SDL_shadercross.h) : SPIR-V <-> HLSL/MSL/DXBC/DXIL.
 */
#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <span>
#include <vector>

#include <core/core.hpp>
#include "sdl3/render/gpu.hpp"
#include "sdl3/core/stdinc.hpp"

namespace sdl3 {

// ============================================================================
// SDL_shadercross — transpilation SPIR-V <-> HLSL/MSL/DXBC/DXIL
// ============================================================================

/// Type d'une variable d'entrée/sortie de shader.
enum class ShaderCrossIOVarType {
	UNKNOWN  = SDL_SHADERCROSS_IOVAR_TYPE_UNKNOWN, ///< inconnu
	INT8	 = SDL_SHADERCROSS_IOVAR_TYPE_INT8,	 ///< entier signé 8 bits
	UINT8	 = SDL_SHADERCROSS_IOVAR_TYPE_UINT8,	 ///< entier non signé 8 bits
	INT16	 = SDL_SHADERCROSS_IOVAR_TYPE_INT16,	 ///< entier signé 16 bits
	UINT16	 = SDL_SHADERCROSS_IOVAR_TYPE_UINT16,	 ///< entier non signé 16 bits
	INT32	 = SDL_SHADERCROSS_IOVAR_TYPE_INT32,	 ///< entier signé 32 bits
	UINT32	 = SDL_SHADERCROSS_IOVAR_TYPE_UINT32,	 ///< entier non signé 32 bits
	INT64	 = SDL_SHADERCROSS_IOVAR_TYPE_INT64,	 ///< entier signé 64 bits
	UINT64	 = SDL_SHADERCROSS_IOVAR_TYPE_UINT64,	 ///< entier non signé 64 bits
	FLOAT16	 = SDL_SHADERCROSS_IOVAR_TYPE_FLOAT16,	 ///< flottant 16 bits
	FLOAT32	 = SDL_SHADERCROSS_IOVAR_TYPE_FLOAT32,	 ///< flottant 32 bits
	FLOAT64	 = SDL_SHADERCROSS_IOVAR_TYPE_FLOAT64,	 ///< flottant 64 bits
};

/// Étage de shader.
enum class ShaderCrossShaderStage {
	VERTEX	 = SDL_SHADERCROSS_SHADERSTAGE_VERTEX,	 ///< vertex
	FRAGMENT = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT, ///< fragment
	COMPUTE	 = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE,	 ///< compute
};

// ── Constantes de propriétés (SDL_SHADERCROSS_PROP_*) ────────────────────────
namespace shadercrossProp {
inline constexpr const char *SHADER_DEBUG_ENABLE_BOOLEAN       = SDL_SHADERCROSS_PROP_SHADER_DEBUG_ENABLE_BOOLEAN;
inline constexpr const char *SHADER_DEBUG_NAME_STRING          = SDL_SHADERCROSS_PROP_SHADER_DEBUG_NAME_STRING;
inline constexpr const char *SHADER_CULL_UNUSED_BINDINGS_BOOLEAN = SDL_SHADERCROSS_PROP_SHADER_CULL_UNUSED_BINDINGS_BOOLEAN;
inline constexpr const char *SPIRV_PSSL_COMPATIBILITY_BOOLEAN  = SDL_SHADERCROSS_PROP_SPIRV_PSSL_COMPATIBILITY_BOOLEAN;
inline constexpr const char *SPIRV_MSL_VERSION_STRING          = SDL_SHADERCROSS_PROP_SPIRV_MSL_VERSION_STRING;
inline constexpr const char *HLSL_SKIP_SPIRV_ROUNDTRIP_BOOLEAN = SDL_SHADERCROSS_PROP_HLSL_SKIP_SPIRV_ROUNDTRIP_BOOLEAN;
} // namespace shadercross_prop

// ── Structures de métadonnées (miroir des structs C) ─────────────────────────

/// Métadonnée d'une variable d'entrée/sortie de shader.
struct ShaderCrossIOVarMetadata {
	char *name = nullptr;						 ///< nom de la variable
	Uint32 location = 0;						 ///< emplacement
	ShaderCrossIOVarType vectorType = ShaderCrossIOVarType::UNKNOWN; ///< type des composantes
	Uint32 vectorSize = 0;						 ///< nombre de composantes
};

/// Ressources d'un shader graphique (nombres de bindings par type).
struct ShaderCrossGraphicsShaderResourceInfo {
	Uint32 numSamplers = 0;			///< échantillonneurs
	Uint32 numStorageTextures = 0;	///< textures en écriture
	Uint32 numStorageBuffers = 0;	///< tampons en écriture
	Uint32 numUniformBuffers = 0;	///< tampons uniformes

	[[nodiscard]] constexpr SDL_ShaderCross_GraphicsShaderResourceInfo ToSDL() const noexcept {
		return {numSamplers, numStorageTextures, numStorageBuffers, numUniformBuffers};
	}
};

/// Métadonnées d'un pipeline de calcul (nombre de bindings + taille du groupe).
struct ShaderCrossComputePipelineMetadata {
	Uint32 numSamplers = 0;				  ///< échantillonneurs
	Uint32 numReadonlyStorageTextures = 0; ///< textures en lecture seule
	Uint32 numReadonlyStorageBuffers = 0;  ///< tampons en lecture seule
	Uint32 numReadwriteStorageTextures = 0; ///< textures en lecture/écriture
	Uint32 numReadwriteStorageBuffers = 0;  ///< tampons en lecture/écriture
	Uint32 numUniformBuffers = 0;		   ///< tampons uniformes
	Uint32 threadcountX = 0;			   ///< taille du groupe en X
	Uint32 threadcountY = 0;			   ///< taille du groupe en Y
	Uint32 threadcountZ = 0;			   ///< taille du groupe en Z
};

/// Entrée de transpilation depuis du SPIR-V.
struct ShaderCrossSPIRVInfo {
	std::span<const Uint8> bytecode;   ///< bytecode SPIR-V
	StringView entrypoint;			   ///< point d'entrée
	ShaderCrossShaderStage shaderStage = ShaderCrossShaderStage::VERTEX; ///< étage
	SDL_PropertiesID props = 0;		   ///< propriétés additionnelles

	[[nodiscard]] SDL_ShaderCross_SPIRV_Info ToSDL() const noexcept {
		return {bytecode.data(), bytecode.size(), entrypoint.GetData(),
				static_cast<SDL_ShaderCross_ShaderStage>(shaderStage), props};
	}
};

/// Définition de préprocesseur HLSL.
struct ShaderCrossHLSLDefine {
	char *name = nullptr;  ///< nom de la macro
	char *value = nullptr; ///< valeur de la macro
};

/// Entrée de transpilation depuis du HLSL.
struct ShaderCrossHLSLInfo {
	StringView source;		///< code source HLSL
	StringView entrypoint;	///< point d'entrée
	StringView includeDir;	///< répertoire d'inclusion
	std::span<const SDL_ShaderCross_HLSL_Define> defines; // terminé par un define tout-NULL
	ShaderCrossShaderStage shaderStage = ShaderCrossShaderStage::VERTEX; ///< étage
	SDL_PropertiesID props = 0; ///< propriétés additionnelles

	[[nodiscard]] SDL_ShaderCross_HLSL_Info ToSDL() const noexcept {
		return {source.GetData(), entrypoint.GetData(),
				includeDir.empty() ? nullptr : includeDir.GetData(),
				const_cast<SDL_ShaderCross_HLSL_Define *>(defines.data()),
				static_cast<SDL_ShaderCross_ShaderStage>(shaderStage), props};
	}
};

// ── Méta-données reflétées (allouées par SDL, libérées par SDL_free) ─────────

/// Métadonnées d'un shader graphique reflétées (RAII SDL_free).
using ShaderCrossGraphicsShaderMetadata = Wrapper<SDL_ShaderCross_GraphicsShaderMetadata, SDL_free>;
/// Métadonnées d'un pipeline de calcul reflétées (RAII SDL_free).
using ShaderCrossComputePipelineMetadataRef = Wrapper<SDL_ShaderCross_ComputePipelineMetadata, SDL_free>;

// ============================================================================
// ShaderCross — RAII SDL_ShaderCross_Init / SDL_ShaderCross_Quit
// ============================================================================

/// Contexte de transpilation de shaders (RAII sur SDL_ShaderCross_Init/Quit).
class ShaderCross {
	bool m_owns = false;

public:
	ShaderCross() = default;
	/// Initialise le contexte (si `init` est vrai).
	ShaderCross(bool init) noexcept : m_owns(init && SDL_ShaderCross_Init()) {}
	/// Quitte le contexte s'il est possédé.
	~ShaderCross();

	ShaderCross(const ShaderCross &) = delete;
	ShaderCross &operator=(const ShaderCross &) = delete;
	ShaderCross(ShaderCross &&o) noexcept : m_owns(o.m_owns) { o.m_owns = false; }
	ShaderCross &operator=(ShaderCross &&o) noexcept {
		if (this != &o) {
			if (m_owns)
				SDL_ShaderCross_Quit();
			m_owns = o.m_owns;
			o.m_owns = false;
		}
		return *this;
	}

	/// Vrai si le contexte est initialisé.
	[[nodiscard]] explicit operator bool() const noexcept { return m_owns; }
	/// Initialise le contexte (avec message d'erreur en cas d'échec).
	[[nodiscard]] static Result<ShaderCross, Error> Create();
};

// ============================================================================
// Fonctions libres (namespace shadercross)
// ============================================================================

namespace shadercross {

/// Formats GPU émis par la transpilation SPIR-V.
[[nodiscard]] inline GpuShaderFormat GetSPIRVShaderFormats() noexcept {
	return SDL_ShaderCross_GetSPIRVShaderFormats();
}

/// Formats GPU émis par la transpilation HLSL.
[[nodiscard]] inline GpuShaderFormat GetHLSLShaderFormats() noexcept {
	return SDL_ShaderCross_GetHLSLShaderFormats();
}

/// Transpile du SPIR-V vers du code MSL (chaîne allouée par SDL, recopiée).
[[nodiscard]] Result<String, Error> TranspileMSLFromSPIRV(const ShaderCrossSPIRVInfo &info);

/// Transpile du SPIR-V vers du code HLSL.
[[nodiscard]] Result<String, Error> TranspileHLSLFromSPIRV(const ShaderCrossSPIRVInfo &info);

/// Compile du SPIR-V vers du bytecode DXBC.
[[nodiscard]] Result<std::vector<uint8_t>, Error> CompileDXBCFromSPIRV(const ShaderCrossSPIRVInfo &info);

/// Compile du SPIR-V vers du bytecode DXIL.
[[nodiscard]] Result<std::vector<uint8_t>, Error> CompileDXILFromSPIRV(const ShaderCrossSPIRVInfo &info);

/// Compile du HLSL vers du bytecode DXBC (round-trip SPIRV-Cross).
[[nodiscard]] Result<std::vector<uint8_t>, Error> CompileDXBCFromHLSL(const ShaderCrossHLSLInfo &info);

/// Compile du HLSL vers du bytecode DXIL (round-trip SPIRV-Cross).
[[nodiscard]] Result<std::vector<uint8_t>, Error> CompileDXILFromHLSL(const ShaderCrossHLSLInfo &info);

/// Compile du HLSL vers du bytecode SPIR-V.
[[nodiscard]] Result<std::vector<uint8_t>, Error> CompileSPIRVFromHLSL(const ShaderCrossHLSLInfo &info);

/// Compile un shader graphique SDL_GPU depuis du SPIR-V.
[[nodiscard]] Result<GpuShader, Error> CompileGraphicsShaderFromSPIRV(
	const GpuDevice &device, const ShaderCrossSPIRVInfo &info,
	const ShaderCrossGraphicsShaderResourceInfo &resourceInfo, SDL_PropertiesID props);

/// Compile un pipeline de calcul SDL_GPU depuis du SPIR-V.
[[nodiscard]] Result<GpuComputePipeline, Error> CompileComputePipelineFromSPIRV(
	const GpuDevice &device, const ShaderCrossSPIRVInfo &info,
	const ShaderCrossComputePipelineMetadata &metadata, SDL_PropertiesID props);

/// Reflète les métadonnées d'un shader graphique SPIR-V (à libérer via SDL_free).
[[nodiscard]] Result<ShaderCrossGraphicsShaderMetadata, Error> ReflectGraphicsSPIRV(
	std::span<const Uint8> bytecode, SDL_PropertiesID props);

/// Reflète les métadonnées d'un pipeline de calcul SPIR-V.
[[nodiscard]] Result<ShaderCrossComputePipelineMetadataRef, Error> ReflectComputeSPIRV(
	std::span<const Uint8> bytecode, SDL_PropertiesID props);

} // namespace shadercross

} // namespace sdl3
