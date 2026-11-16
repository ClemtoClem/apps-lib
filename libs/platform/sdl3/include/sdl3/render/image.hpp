#pragma once
#include <SDL3_image/SDL_image.h>

#include <core/core.hpp>
#include <cstddef>
#include <span>
#include "sdl3/io/iostream.hpp"
#include "sdl3/render/render.hpp"
#include "sdl3/render/gpu.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// SDL3_image 3.x does not require initialization — just call the load functions directly.

// ============================================================================
// Version
// ============================================================================

/// SDL3_image version number compiled into the library (IMG_Version).
[[nodiscard]] inline int ImgVersion() { return IMG_Version(); }

// ============================================================================
// Loading functions
// ============================================================================

/// Load an image file as a Surface (any format SDL3_image supports).
[[nodiscard]] Result<Surface, StringView> ImgLoad(const String &path);

/// Load an image directly as a Texture in the given Renderer.
[[nodiscard]] Result<Texture, StringView> ImgLoadTexture(Renderer &ren, const String &path);

/// Load from an open IO stream. `closeio = true` transfers stream ownership
/// to SDL (which then closes it); default keeps it owned by the caller.
[[nodiscard]] Result<Surface, StringView> ImgLoadIO(IOStream &src, bool closeio = false);

/// Load from an IO stream, forcing a specific decoder (`type` = "PNG", "JPG"…).
[[nodiscard]] Result<Surface, StringView> ImgLoadTypedIO(IOStream &src, bool closeio, StringView type);

/// Load a Texture from an IO stream.
[[nodiscard]] Result<Texture, StringView> ImgLoadTextureIO(Renderer &ren, IOStream &src, bool closeio = false);

/// Load a Texture from an IO stream, forcing a specific decoder.
[[nodiscard]] Result<Texture, StringView> ImgLoadTextureTypedIO(Renderer &ren, IOStream &src, bool closeio,
															   StringView type);

/// Load an image from a memory buffer as a Surface.
[[nodiscard]] Result<Surface, StringView> ImgLoadMemory(const void *data, size_t size);

/// Load an image from a memory buffer as a Surface (standard-library view).
[[nodiscard]] Result<Surface, StringView> ImgLoadMemory(std::span<const std::byte> data);

/// Load a texture from a memory buffer.
[[nodiscard]] Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, const void *data, size_t size);

/// Load a texture from a memory buffer (standard-library view).
[[nodiscard]] Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, std::span<const std::byte> data);

/// Rasterise an SVG at a fixed pixel size from an IO stream.
[[nodiscard]] Result<Surface, StringView> ImgLoadSizedSvgIO(IOStream &src, int width, int height);

/// Rasterise an SVG at a fixed pixel size from a memory buffer.
[[nodiscard]] Result<Surface, StringView> ImgLoadSizedSvgMemory(const void *data, size_t size, int width, int height);

/// Rasterise an SVG at a fixed pixel size from a memory buffer (std view).
[[nodiscard]] Result<Surface, StringView> ImgLoadSizedSvgMemory(std::span<const std::byte> data, int width, int height);

/// Build a Surface from a NULL-terminated XPM array (indexed colours).
[[nodiscard]] Result<Surface, StringView> ImgReadXPMFromArray(char **xpm);

/// Build a Surface from a NULL-terminated XPM array, expanded to RGB888.
[[nodiscard]] Result<Surface, StringView> ImgReadXPMFromArrayToRgb888(char **xpm);

// ============================================================================
// Saving functions
// ============================================================================

// `Surface` est prise par référence const (IMG_Save* ne modifie pas la surface)
// et le SDL_Surface* brut ne sort jamais de l'API — il n'est déréférencé qu'ici,
// en interne (cf. memory/feedback_wrap_c_pointers.md).

/// Save to a file, inferring the format from the extension (IMG_Save).
[[nodiscard]] Result<bool, StringView> ImgSave(const Surface &surface, const String &path);

/// Écrit `surface` en PNG. Err porte le message d'erreur SDL d'origine.
[[nodiscard]] Result<bool, StringView> ImgSavePng(const Surface &surface, const String &path);

/// Écrit `surface` en JPEG. `quality` va de 0 (minimale) à 100 (maximale).
[[nodiscard]] Result<bool, StringView> ImgSaveJpg(const Surface &surface, const String &path, int quality = 90);

[[nodiscard]] Result<bool, StringView> ImgSaveBmp(const Surface &surface, const String &path);
[[nodiscard]] Result<bool, StringView> ImgSaveGif(const Surface &surface, const String &path);
[[nodiscard]] Result<bool, StringView> ImgSaveIco(const Surface &surface, const String &path);
[[nodiscard]] Result<bool, StringView> ImgSaveCur(const Surface &surface, const String &path);
[[nodiscard]] Result<bool, StringView> ImgSaveTga(const Surface &surface, const String &path);

/// `quality` va de 0.0 (minimale) à 1.0 (maximale).
[[nodiscard]] Result<bool, StringView> ImgSaveWebp(const Surface &surface, const String &path, float quality = 0.9f);

/// `quality` va de 0 (minimale) à 100 (maximale).
[[nodiscard]] Result<bool, StringView> ImgSaveAvif(const Surface &surface, const String &path, int quality = 90);

/// Save to an IO stream. `closeio = true` transfers ownership of `dst` to SDL.
[[nodiscard]] Result<bool, StringView> ImgSavePngIO(const Surface &surface, IOStream &dst, bool closeio = false);
[[nodiscard]] Result<bool, StringView> ImgSaveJpgIO(const Surface &surface, IOStream &dst, bool closeio = false,
													int quality = 90);

/// Save to an IO stream, forcing a specific encoder (`type` = "PNG", "JPG"…).
[[nodiscard]] Result<bool, StringView> ImgSaveTypedIO(const Surface &surface, IOStream &dst, bool closeio,
													  StringView type);

// ============================================================================
// Format detection — IMG_is* (the stream must be seekable)
// ============================================================================

[[nodiscard]] bool ImgIsAni(IOStream &src);
[[nodiscard]] bool ImgIsAvif(IOStream &src);
[[nodiscard]] bool ImgIsBmp(IOStream &src);
[[nodiscard]] bool ImgIsCur(IOStream &src);
[[nodiscard]] bool ImgIsGif(IOStream &src);
[[nodiscard]] bool ImgIsIco(IOStream &src);
[[nodiscard]] bool ImgIsJpg(IOStream &src);
[[nodiscard]] bool ImgIsJxl(IOStream &src);
[[nodiscard]] bool ImgIsLbm(IOStream &src);
[[nodiscard]] bool ImgIsPcx(IOStream &src);
[[nodiscard]] bool ImgIsPng(IOStream &src);
[[nodiscard]] bool ImgIsPnm(IOStream &src);
[[nodiscard]] bool ImgIsQoi(IOStream &src);
[[nodiscard]] bool ImgIsSvg(IOStream &src);
[[nodiscard]] bool ImgIsTif(IOStream &src);
[[nodiscard]] bool ImgIsWebp(IOStream &src);
[[nodiscard]] bool ImgIsXcf(IOStream &src);
[[nodiscard]] bool ImgIsXpm(IOStream &src);
[[nodiscard]] bool ImgIsXv(IOStream &src);

// ============================================================================
// Animation — RAII IMG_Animation (GIF / APNG / WEBP / ANI …)
// ============================================================================

/// Animation (GIF/APNG/WEBP/ANI…) chargée en mémoire (RAII).
class Animation : public Wrapper<IMG_Animation, IMG_FreeAnimation> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Animation, StringView> Load(const String &path);
	[[nodiscard]] static Result<Animation, StringView> LoadIO(IOStream &src, bool closeio = false);
	[[nodiscard]] static Result<Animation, StringView> LoadTypedIO(IOStream &src, bool closeio, StringView type);

	[[nodiscard]] int Width() const { return mHandle ? mHandle->w : 0; }
	[[nodiscard]] int Height() const { return mHandle ? mHandle->h : 0; }
	[[nodiscard]] int Count() const { return mHandle ? mHandle->count : 0; }

	/// Frame delay in milliseconds (0 if out of range or unloaded).
	[[nodiscard]] int Delay(int index) const {
		return mHandle && index >= 0 && index < mHandle->count ? mHandle->delays[index] : 0;
	}

	/// Frame `index` as a read-only borrowed surface (owned by the animation).
	/// Returns NONE when out of range.
	[[nodiscard]] Option<Ref<SDL_Surface>> Frame(int index) const {
		if (!mHandle || index < 0 || index >= mHandle->count) return NONE;
		return Some(MakeRef(*mHandle->frames[index]));
	}

	/// Frame delays (ms), in frame order.
	[[nodiscard]] std::span<const int> Delays() const {
		return mHandle ? std::span<const int>(mHandle->delays, static_cast<std::size_t>(mHandle->count))
						: std::span<const int>{};
	}

	/// Save the animation back to a file (format inferred from the extension).
	[[nodiscard]] Result<bool, StringView> Save(const String &path);

	// ── Enregistrement vers un flux, par format ───────────────────────────────

	[[nodiscard]] Result<bool, StringView> SaveTypedIO(IOStream &dst, bool closeio, StringView type);
	[[nodiscard]] Result<bool, StringView> SaveGifIO(IOStream &dst, bool closeio);
	[[nodiscard]] Result<bool, StringView> SaveApngIO(IOStream &dst, bool closeio);
	[[nodiscard]] Result<bool, StringView> SaveWebpIO(IOStream &dst, bool closeio, int quality = 90);
	[[nodiscard]] Result<bool, StringView> SaveAvifIO(IOStream &dst, bool closeio, int quality = 90);
	[[nodiscard]] Result<bool, StringView> SaveAniIO(IOStream &dst, bool closeio);
};

// ── Chargeurs d'animation par format (IMG_Load*Animation_IO) ─────────────────

[[nodiscard]] Result<Animation, StringView> ImgLoadGifAnimationIO(IOStream &src);
[[nodiscard]] Result<Animation, StringView> ImgLoadApngAnimationIO(IOStream &src);
[[nodiscard]] Result<Animation, StringView> ImgLoadWebpAnimationIO(IOStream &src);
[[nodiscard]] Result<Animation, StringView> ImgLoadAvifAnimationIO(IOStream &src);
[[nodiscard]] Result<Animation, StringView> ImgLoadAniAnimationIO(IOStream &src);

/// Crée un curseur animé SDL à partir d'une animation (le curseur doit être
/// libéré via sdl3::Cursor — voir input.hpp).
[[nodiscard]] Result<SDL_Cursor *, StringView> ImgCreateAnimatedCursor(const Animation &anim, int hotX, int hotY);

// ============================================================================
// Décodage d'image animée EN FLUX (IMG_AnimationDecoder) — décodage trame par
// trame sans charger toute l'animation en mémoire (GIF/APNG/WEBP/AVIF/ANI…)
// ============================================================================

/// État d'un décodeur d'animation en flux.
enum class ImgAnimationDecoderStatus : int {
	INVALID  = IMG_DECODER_STATUS_INVALID,
	OK       = IMG_DECODER_STATUS_OK,
	FAILED   = IMG_DECODER_STATUS_FAILED,
	COMPLETE = IMG_DECODER_STATUS_COMPLETE,
};

/// Décodeur d'animation en flux (trame par trame, RAII).
class AnimationDecoder : public Wrapper<IMG_AnimationDecoder, IMG_CloseAnimationDecoder> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<AnimationDecoder, StringView> Create(const String &path);
	[[nodiscard]] static Result<AnimationDecoder, StringView> CreateIO(IOStream &src, bool closeio, StringView type);
	[[nodiscard]] static Result<AnimationDecoder, StringView> CreateWithProperties(SDL_PropertiesID props);

	[[nodiscard]] SDL_PropertiesID Properties() const {
		return mHandle ? IMG_GetAnimationDecoderProperties(mHandle) : 0;
	}

	/// Décore la trame suivante ; `outDuration` reçoit sa durée (ms).
	[[nodiscard]] bool NextFrame(Surface &outFrame, Uint64 &outDuration);

	[[nodiscard]] ImgAnimationDecoderStatus Status() const {
		return mHandle ? static_cast<ImgAnimationDecoderStatus>(IMG_GetAnimationDecoderStatus(mHandle))
						: ImgAnimationDecoderStatus::INVALID;
	}

	bool Reset() { return mHandle && IMG_ResetAnimationDecoder(mHandle); }
};

// ============================================================================
// Encodage d'image animée (IMG_AnimationEncoder)
// ============================================================================

/// Encodeur d'animation (RAII).
class AnimationEncoder : public Wrapper<IMG_AnimationEncoder, IMG_CloseAnimationEncoder> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<AnimationEncoder, StringView> Create(const String &path);
	[[nodiscard]] static Result<AnimationEncoder, StringView> CreateIO(IOStream &dst, bool closeio, StringView type);
	[[nodiscard]] static Result<AnimationEncoder, StringView> CreateWithProperties(SDL_PropertiesID props);

	bool AddFrame(const Surface &surface, Uint64 durationMs);
};

// ============================================================================
// Chargement direct en texture GPU (IMG_LoadGPUTexture*) et presse-papiers
// ============================================================================

[[nodiscard]] Result<GpuTexture, StringView> ImgLoadGpuTexture(SDL_GPUDevice *device, SDL_GPUCopyPass *copyPass,
															   const String &path, int *outWidth, int *outHeight);
[[nodiscard]] Result<GpuTexture, StringView> ImgLoadGpuTextureIO(SDL_GPUDevice *device, SDL_GPUCopyPass *copyPass,
																 IOStream &src, bool closeio, int *outWidth, int *outHeight);
[[nodiscard]] Result<GpuTexture, StringView> ImgLoadGpuTextureTypedIO(SDL_GPUDevice *device, SDL_GPUCopyPass *copyPass,
																	  IOStream &src, bool closeio, StringView type,
																	  int *outWidth, int *outHeight);

/// Récupère l'image présente dans le presse-papiers (si c'en est une).
[[nodiscard]] Result<Surface, StringView> ImgGetClipboardImage();

// ============================================================================
// Chargeurs / enregistreurs par format (variantes *_IO) — IMG_Load*_IO /
// IMG_Save*_IO, pour forcer un codec précis ou traiter un flux non seekable.
// ============================================================================

[[nodiscard]] Result<Surface, StringView> ImgLoadBmpIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadPngIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadJpgIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadTgaIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadTifIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadWebpIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadGifIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadSvgIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadIcoIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadCurIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadXpmIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadXvIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadPcxIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadLbmIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadPnmIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadQoiIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadJxlIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadAvifIO(IOStream &src);
[[nodiscard]] Result<Surface, StringView> ImgLoadXcfIO(IOStream &src);

[[nodiscard]] Result<bool, StringView> ImgSaveBmpIO(const Surface &surface, IOStream &dst, bool closeio = false);
[[nodiscard]] Result<bool, StringView> ImgSaveGifIO(const Surface &surface, IOStream &dst, bool closeio = false);
[[nodiscard]] Result<bool, StringView> ImgSaveIcoIO(const Surface &surface, IOStream &dst, bool closeio = false);
[[nodiscard]] Result<bool, StringView> ImgSaveCurIO(const Surface &surface, IOStream &dst, bool closeio = false);
[[nodiscard]] Result<bool, StringView> ImgSaveTgaIO(const Surface &surface, IOStream &dst, bool closeio = false);
[[nodiscard]] Result<bool, StringView> ImgSaveWebpIO(const Surface &surface, IOStream &dst, bool closeio = false,
													 float quality = 0.9f);
[[nodiscard]] Result<bool, StringView> ImgSaveAvifIO(const Surface &surface, IOStream &dst, bool closeio = false,
													 int quality = 90);

} // namespace sdl3
