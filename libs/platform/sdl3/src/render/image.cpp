// Définitions de sdl3/image.hpp
#include "sdl3/render/image.hpp"

#include <cstddef>

namespace sdl3 {

namespace {

template <typename Fn>
Result<Surface, StringView> LoadSurface(Fn &&fn) {
	SDL_Surface *surface = fn();
	if (!surface)
		return Err(GetError());
	return Ok(Surface(surface));
}

template <typename Fn>
Result<Texture, StringView> LoadTexture(Fn &&fn) {
	SDL_Texture *texture = fn();
	if (!texture)
		return Err(GetError());
	return Ok(Texture(texture));
}

template <typename Fn>
Result<bool, StringView> SaveSurface(const Surface &surface, Fn &&fn) {
	if (!surface.Get())
		return Err(StringView("save: surface nulle"));
	if (!fn(surface.Get()))
		return Err(GetError());
	return Ok(true);
}

} // namespace

// ============================================================================
// Loading
// ============================================================================

Result<Surface, StringView> ImgLoad(const String &path) {
	return LoadSurface([&] { return IMG_Load(path.c_str()); });
}

Result<Texture, StringView> ImgLoadTexture(Renderer &ren, const String &path) {
	return LoadTexture([&] { return IMG_LoadTexture(ren.Get(), path.c_str()); });
}

Result<Surface, StringView> ImgLoadIO(IOStream &src, bool closeio) {
	if (!src)
		return Err(StringView("ImgLoadIO: flux nul"));
	return LoadSurface([&] { return IMG_Load_IO(src.Get(), closeio); });
}

Result<Surface, StringView> ImgLoadTypedIO(IOStream &src, bool closeio, StringView type) {
	if (!src)
		return Err(StringView("ImgLoadTypedIO: flux nul"));
	return LoadSurface([&] { return IMG_LoadTyped_IO(src.Get(), closeio, type.CStr()); });
}

Result<Texture, StringView> ImgLoadTextureIO(Renderer &ren, IOStream &src, bool closeio) {
	if (!src)
		return Err(StringView("ImgLoadTextureIO: flux nul"));
	return LoadTexture([&] { return IMG_LoadTexture_IO(ren.Get(), src.Get(), closeio); });
}

Result<Texture, StringView> ImgLoadTextureTypedIO(Renderer &ren, IOStream &src, bool closeio, StringView type) {
	if (!src)
		return Err(StringView("ImgLoadTextureTypedIO: flux nul"));
	return LoadTexture([&] { return IMG_LoadTextureTyped_IO(ren.Get(), src.Get(), closeio, type.CStr()); });
}

Result<Surface, StringView> ImgLoadMemory(const void *data, size_t size) {
	auto *io = SDL_IOFromConstMem(data, size);
	if (!io)
		return Err(GetError());
	auto *s = IMG_Load_IO(io, true);
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

Result<Surface, StringView> ImgLoadMemory(std::span<const std::byte> data) {
	return ImgLoadMemory(data.data(), data.size());
}

Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, const void *data, size_t size) {
	auto *io = SDL_IOFromConstMem(data, size);
	if (!io)
		return Err(GetError());
	auto *t = IMG_LoadTexture_IO(ren.Get(), io, true);
	if (!t)
		return Err(GetError());
	return Ok(Texture(t));
}

Result<Texture, StringView> ImgLoadTextureMemory(Renderer &ren, std::span<const std::byte> data) {
	return ImgLoadTextureMemory(ren, data.data(), data.size());
}

Result<Surface, StringView> ImgLoadSizedSvgIO(IOStream &src, int width, int height) {
	if (!src)
		return Err(StringView("ImgLoadSizedSvgIO: flux nul"));
	return LoadSurface([&] { return IMG_LoadSizedSVG_IO(src.Get(), width, height); });
}

Result<Surface, StringView> ImgLoadSizedSvgMemory(const void *data, size_t size, int width, int height) {
	auto *io = SDL_IOFromConstMem(data, size);
	if (!io)
		return Err(GetError());
	auto *s = IMG_LoadSizedSVG_IO(io, width, height);
	if (!s) {
		SDL_CloseIO(io);
		return Err(GetError());
	}
	SDL_CloseIO(io);
	return Ok(Surface(s));
}

Result<Surface, StringView> ImgLoadSizedSvgMemory(std::span<const std::byte> data, int width, int height) {
	return ImgLoadSizedSvgMemory(data.data(), data.size(), width, height);
}

Result<Surface, StringView> ImgReadXPMFromArray(char **xpm) {
	return LoadSurface([&] { return IMG_ReadXPMFromArray(xpm); });
}

Result<Surface, StringView> ImgReadXPMFromArrayToRgb888(char **xpm) {
	return LoadSurface([&] { return IMG_ReadXPMFromArrayToRGB888(xpm); });
}

// ============================================================================
// Saving
// ============================================================================

Result<bool, StringView> ImgSave(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_Save(s, path.c_str()); });
}

Result<bool, StringView> ImgSavePng(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SavePNG(s, path.c_str()); });
}

Result<bool, StringView> ImgSaveJpg(const Surface &surface, const String &path, int quality) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveJPG(s, path.c_str(), quality); });
}

Result<bool, StringView> ImgSaveBmp(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveBMP(s, path.c_str()); });
}

Result<bool, StringView> ImgSaveGif(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveGIF(s, path.c_str()); });
}

Result<bool, StringView> ImgSaveIco(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveICO(s, path.c_str()); });
}

Result<bool, StringView> ImgSaveCur(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveCUR(s, path.c_str()); });
}

Result<bool, StringView> ImgSaveTga(const Surface &surface, const String &path) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveTGA(s, path.c_str()); });
}

Result<bool, StringView> ImgSaveWebp(const Surface &surface, const String &path, float quality) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveWEBP(s, path.c_str(), quality); });
}

Result<bool, StringView> ImgSaveAvif(const Surface &surface, const String &path, int quality) {
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveAVIF(s, path.c_str(), quality); });
}

Result<bool, StringView> ImgSavePngIO(const Surface &surface, IOStream &dst, bool closeio) {
	if (!dst)
		return Err(StringView("ImgSavePngIO: flux nul"));
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SavePNG_IO(s, dst.Get(), closeio); });
}

Result<bool, StringView> ImgSaveJpgIO(const Surface &surface, IOStream &dst, bool closeio, int quality) {
	if (!dst)
		return Err(StringView("ImgSaveJpgIO: flux nul"));
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveJPG_IO(s, dst.Get(), closeio, quality); });
}

Result<bool, StringView> ImgSaveTypedIO(const Surface &surface, IOStream &dst, bool closeio, StringView type) {
	if (!dst)
		return Err(StringView("ImgSaveTypedIO: flux nul"));
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveTyped_IO(s, dst.Get(), closeio, type.CStr()); });
}

// ============================================================================
// Format detection
// ============================================================================

bool ImgIsAni(IOStream &src) { return src && IMG_isANI(src.Get()); }
bool ImgIsAvif(IOStream &src) { return src && IMG_isAVIF(src.Get()); }
bool ImgIsBmp(IOStream &src) { return src && IMG_isBMP(src.Get()); }
bool ImgIsCur(IOStream &src) { return src && IMG_isCUR(src.Get()); }
bool ImgIsGif(IOStream &src) { return src && IMG_isGIF(src.Get()); }
bool ImgIsIco(IOStream &src) { return src && IMG_isICO(src.Get()); }
bool ImgIsJpg(IOStream &src) { return src && IMG_isJPG(src.Get()); }
bool ImgIsJxl(IOStream &src) { return src && IMG_isJXL(src.Get()); }
bool ImgIsLbm(IOStream &src) { return src && IMG_isLBM(src.Get()); }
bool ImgIsPcx(IOStream &src) { return src && IMG_isPCX(src.Get()); }
bool ImgIsPng(IOStream &src) { return src && IMG_isPNG(src.Get()); }
bool ImgIsPnm(IOStream &src) { return src && IMG_isPNM(src.Get()); }
bool ImgIsQoi(IOStream &src) { return src && IMG_isQOI(src.Get()); }
bool ImgIsSvg(IOStream &src) { return src && IMG_isSVG(src.Get()); }
bool ImgIsTif(IOStream &src) { return src && IMG_isTIF(src.Get()); }
bool ImgIsWebp(IOStream &src) { return src && IMG_isWEBP(src.Get()); }
bool ImgIsXcf(IOStream &src) { return src && IMG_isXCF(src.Get()); }
bool ImgIsXpm(IOStream &src) { return src && IMG_isXPM(src.Get()); }
bool ImgIsXv(IOStream &src) { return src && IMG_isXV(src.Get()); }

// ============================================================================
// Animation
// ============================================================================

Result<Animation, StringView> Animation::Load(const String &path) {
	IMG_Animation *anim = IMG_LoadAnimation(path.c_str());
	if (!anim)
		return Err(GetError());
	return Ok(Animation(anim));
}

Result<Animation, StringView> Animation::LoadIO(IOStream &src, bool closeio) {
	if (!src)
		return Err(StringView("Animation::LoadIO: flux nul"));
	IMG_Animation *anim = IMG_LoadAnimation_IO(src.Get(), closeio);
	if (!anim)
		return Err(GetError());
	return Ok(Animation(anim));
}

Result<Animation, StringView> Animation::LoadTypedIO(IOStream &src, bool closeio, StringView type) {
	if (!src)
		return Err(StringView("Animation::LoadTypedIO: flux nul"));
	IMG_Animation *anim = IMG_LoadAnimationTyped_IO(src.Get(), closeio, type.CStr());
	if (!anim)
		return Err(GetError());
	return Ok(Animation(anim));
}

Result<bool, StringView> Animation::Save(const String &path) {
	if (!mHandle)
		return Err(StringView("Animation::Save: animation nulle"));
	if (!IMG_SaveAnimation(mHandle, path.c_str()))
		return Err(GetError());
	return Ok(true);
}

// ============================================================================
// AnimationDecoder (décodage en flux)
// ============================================================================

Result<AnimationDecoder, StringView> AnimationDecoder::Create(const String &path) {
	IMG_AnimationDecoder *d = IMG_CreateAnimationDecoder(path.c_str());
	if (!d)
		return Err(GetError());
	return Ok(AnimationDecoder(d));
}

Result<AnimationDecoder, StringView> AnimationDecoder::CreateIO(IOStream &src, bool closeio, StringView type) {
	if (!src)
		return Err(StringView("AnimationDecoder::CreateIO: flux nul"));
	IMG_AnimationDecoder *d = IMG_CreateAnimationDecoder_IO(src.Get(), closeio, type.CStr());
	if (!d)
		return Err(GetError());
	return Ok(AnimationDecoder(d));
}

Result<AnimationDecoder, StringView> AnimationDecoder::CreateWithProperties(SDL_PropertiesID props) {
	IMG_AnimationDecoder *d = IMG_CreateAnimationDecoderWithProperties(props);
	if (!d)
		return Err(GetError());
	return Ok(AnimationDecoder(d));
}

bool AnimationDecoder::NextFrame(Surface &outFrame, Uint64 &outDuration) {
	if (!mHandle)
		return false;
	SDL_Surface *frame = nullptr;
	Uint64 duration = 0;
	if (!IMG_GetAnimationDecoderFrame(mHandle, &frame, &duration))
		return false;
	outFrame = Surface(frame);
	outDuration = duration;
	return true;
}

// ============================================================================
// AnimationEncoder (encodage)
// ============================================================================

Result<AnimationEncoder, StringView> AnimationEncoder::Create(const String &path) {
	IMG_AnimationEncoder *e = IMG_CreateAnimationEncoder(path.c_str());
	if (!e)
		return Err(GetError());
	return Ok(AnimationEncoder(e));
}

Result<AnimationEncoder, StringView> AnimationEncoder::CreateIO(IOStream &dst, bool closeio, StringView type) {
	if (!dst)
		return Err(StringView("AnimationEncoder::CreateIO: flux nul"));
	IMG_AnimationEncoder *e = IMG_CreateAnimationEncoder_IO(dst.Get(), closeio, type.CStr());
	if (!e)
		return Err(GetError());
	return Ok(AnimationEncoder(e));
}

Result<AnimationEncoder, StringView> AnimationEncoder::CreateWithProperties(SDL_PropertiesID props) {
	IMG_AnimationEncoder *e = IMG_CreateAnimationEncoderWithProperties(props);
	if (!e)
		return Err(GetError());
	return Ok(AnimationEncoder(e));
}

bool AnimationEncoder::AddFrame(const Surface &surface, Uint64 durationMs) {
	return mHandle && surface.Get() && IMG_AddAnimationEncoderFrame(mHandle, surface.Get(), durationMs);
}

// ============================================================================
// Chargement GPU + presse-papiers
// ============================================================================

Result<GpuTexture, StringView> ImgLoadGpuTexture(SDL_GPUDevice *device, SDL_GPUCopyPass *copyPass, const String &path,
												 int *outWidth, int *outHeight) {
	SDL_GPUTexture *t = IMG_LoadGPUTexture(device, copyPass, path.c_str(), outWidth, outHeight);
	if (!t)
		return Err(GetError());
	return Ok(GpuTexture(device, t));
}

Result<GpuTexture, StringView> ImgLoadGpuTextureIO(SDL_GPUDevice *device, SDL_GPUCopyPass *copyPass, IOStream &src,
												   bool closeio, int *outWidth, int *outHeight) {
	if (!src)
		return Err(StringView("ImgLoadGpuTextureIO: flux nul"));
	SDL_GPUTexture *t = IMG_LoadGPUTexture_IO(device, copyPass, src.Get(), closeio, outWidth, outHeight);
	if (!t)
		return Err(GetError());
	return Ok(GpuTexture(device, t));
}

Result<GpuTexture, StringView> ImgLoadGpuTextureTypedIO(SDL_GPUDevice *device, SDL_GPUCopyPass *copyPass, IOStream &src,
														bool closeio, StringView type, int *outWidth, int *outHeight) {
	if (!src)
		return Err(StringView("ImgLoadGpuTextureTypedIO: flux nul"));
	SDL_GPUTexture *t = IMG_LoadGPUTextureTyped_IO(device, copyPass, src.Get(), closeio, type.CStr(), outWidth, outHeight);
	if (!t)
		return Err(GetError());
	return Ok(GpuTexture(device, t));
}

Result<Surface, StringView> ImgGetClipboardImage() {
	SDL_Surface *s = IMG_GetClipboardImage();
	if (!s)
		return Err(GetError());
	return Ok(Surface(s));
}

// ============================================================================
// Chargeurs / enregistreurs par format (variantes *_IO)
// ============================================================================

#define DEFINE_IMG_LOAD_IO(Name, ImgFn)                                                              \
	Result<Surface, StringView> Name(IOStream &src) {                                                \
		if (!src)                                                                                    \
			return Err(StringView(#Name ": flux nul"));                                              \
		SDL_Surface *s = ImgFn(src.Get());                                                           \
		if (!s)                                                                                      \
			return Err(GetError());                                                                  \
		return Ok(Surface(s));                                                                       \
	}

DEFINE_IMG_LOAD_IO(ImgLoadBmpIO, IMG_LoadBMP_IO)
DEFINE_IMG_LOAD_IO(ImgLoadPngIO, IMG_LoadPNG_IO)
DEFINE_IMG_LOAD_IO(ImgLoadJpgIO, IMG_LoadJPG_IO)
DEFINE_IMG_LOAD_IO(ImgLoadTgaIO, IMG_LoadTGA_IO)
DEFINE_IMG_LOAD_IO(ImgLoadTifIO, IMG_LoadTIF_IO)
DEFINE_IMG_LOAD_IO(ImgLoadWebpIO, IMG_LoadWEBP_IO)
DEFINE_IMG_LOAD_IO(ImgLoadGifIO, IMG_LoadGIF_IO)
DEFINE_IMG_LOAD_IO(ImgLoadSvgIO, IMG_LoadSVG_IO)
DEFINE_IMG_LOAD_IO(ImgLoadIcoIO, IMG_LoadICO_IO)
DEFINE_IMG_LOAD_IO(ImgLoadCurIO, IMG_LoadCUR_IO)
DEFINE_IMG_LOAD_IO(ImgLoadXpmIO, IMG_LoadXPM_IO)
DEFINE_IMG_LOAD_IO(ImgLoadXvIO, IMG_LoadXV_IO)
DEFINE_IMG_LOAD_IO(ImgLoadPcxIO, IMG_LoadPCX_IO)
DEFINE_IMG_LOAD_IO(ImgLoadLbmIO, IMG_LoadLBM_IO)
DEFINE_IMG_LOAD_IO(ImgLoadPnmIO, IMG_LoadPNM_IO)
DEFINE_IMG_LOAD_IO(ImgLoadQoiIO, IMG_LoadQOI_IO)
DEFINE_IMG_LOAD_IO(ImgLoadJxlIO, IMG_LoadJXL_IO)
DEFINE_IMG_LOAD_IO(ImgLoadAvifIO, IMG_LoadAVIF_IO)
DEFINE_IMG_LOAD_IO(ImgLoadXcfIO, IMG_LoadXCF_IO)

#undef DEFINE_IMG_LOAD_IO

#define DEFINE_IMG_SAVE_IO(Name, ImgFn)                                                              \
	Result<bool, StringView> Name(const Surface &surface, IOStream &dst, bool closeio) {            \
		if (!dst)                                                                                    \
			return Err(StringView(#Name ": flux nul"));                                              \
		return SaveSurface(surface, [&](SDL_Surface *s) { return ImgFn(s, dst.Get(), closeio); });   \
	}

DEFINE_IMG_SAVE_IO(ImgSaveBmpIO, IMG_SaveBMP_IO)
DEFINE_IMG_SAVE_IO(ImgSaveGifIO, IMG_SaveGIF_IO)
DEFINE_IMG_SAVE_IO(ImgSaveIcoIO, IMG_SaveICO_IO)
DEFINE_IMG_SAVE_IO(ImgSaveCurIO, IMG_SaveCUR_IO)
DEFINE_IMG_SAVE_IO(ImgSaveTgaIO, IMG_SaveTGA_IO)

#undef DEFINE_IMG_SAVE_IO

Result<bool, StringView> ImgSaveWebpIO(const Surface &surface, IOStream &dst, bool closeio, float quality) {
	if (!dst)
		return Err(StringView("ImgSaveWebpIO: flux nul"));
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveWEBP_IO(s, dst.Get(), closeio, quality); });
}

Result<bool, StringView> ImgSaveAvifIO(const Surface &surface, IOStream &dst, bool closeio, int quality) {
	if (!dst)
		return Err(StringView("ImgSaveAvifIO: flux nul"));
	return SaveSurface(surface, [&](SDL_Surface *s) { return IMG_SaveAVIF_IO(s, dst.Get(), closeio, quality); });
}

// ============================================================================
// Animation : enregistrement par format + chargeurs par format + curseur animé
// ============================================================================

Result<bool, StringView> Animation::SaveTypedIO(IOStream &dst, bool closeio, StringView type) {
	if (!mHandle)
		return Err(StringView("Animation::SaveTypedIO: animation nulle"));
	if (!dst)
		return Err(StringView("Animation::SaveTypedIO: flux nul"));
	if (!IMG_SaveAnimationTyped_IO(mHandle, dst.Get(), closeio, type.CStr()))
		return Err(GetError());
	return Ok(true);
}

#define DEFINE_ANIM_SAVE_IO(Name, ImgFn)                                                             \
	Result<bool, StringView> Name(IOStream &dst, bool closeio) {                                     \
		if (!mHandle)                                                                               \
			return Err(StringView(#Name ": animation nulle"));                                       \
		if (!dst)                                                                                    \
			return Err(StringView(#Name ": flux nul"));                                              \
		if (!ImgFn(mHandle, dst.Get(), closeio))                                                    \
			return Err(GetError());                                                                  \
		return Ok(true);                                                                             \
	}

DEFINE_ANIM_SAVE_IO(Animation::SaveGifIO, IMG_SaveGIFAnimation_IO)
DEFINE_ANIM_SAVE_IO(Animation::SaveApngIO, IMG_SaveAPNGAnimation_IO)
DEFINE_ANIM_SAVE_IO(Animation::SaveAniIO, IMG_SaveANIAnimation_IO)

#undef DEFINE_ANIM_SAVE_IO

Result<bool, StringView> Animation::SaveWebpIO(IOStream &dst, bool closeio, int quality) {
	if (!mHandle || !dst)
		return Err(StringView("Animation::SaveWebpIO: animation ou flux nul"));
	if (!IMG_SaveWEBPAnimation_IO(mHandle, dst.Get(), closeio, quality))
		return Err(GetError());
	return Ok(true);
}

Result<bool, StringView> Animation::SaveAvifIO(IOStream &dst, bool closeio, int quality) {
	if (!mHandle || !dst)
		return Err(StringView("Animation::SaveAvifIO: animation ou flux nul"));
	if (!IMG_SaveAVIFAnimation_IO(mHandle, dst.Get(), closeio, quality))
		return Err(GetError());
	return Ok(true);
}

#define DEFINE_ANIM_LOAD_IO(Name, ImgFn)                                                             \
	Result<Animation, StringView> Name(IOStream &src) {                                              \
		if (!src)                                                                                    \
			return Err(StringView(#Name ": flux nul"));                                              \
		IMG_Animation *a = ImgFn(src.Get());                                                         \
		if (!a)                                                                                      \
			return Err(GetError());                                                                  \
		return Ok(Animation(a));                                                                     \
	}

DEFINE_ANIM_LOAD_IO(ImgLoadGifAnimationIO, IMG_LoadGIFAnimation_IO)
DEFINE_ANIM_LOAD_IO(ImgLoadApngAnimationIO, IMG_LoadAPNGAnimation_IO)
DEFINE_ANIM_LOAD_IO(ImgLoadWebpAnimationIO, IMG_LoadWEBPAnimation_IO)
DEFINE_ANIM_LOAD_IO(ImgLoadAvifAnimationIO, IMG_LoadAVIFAnimation_IO)
DEFINE_ANIM_LOAD_IO(ImgLoadAniAnimationIO, IMG_LoadANIAnimation_IO)

#undef DEFINE_ANIM_LOAD_IO

Result<SDL_Cursor *, StringView> ImgCreateAnimatedCursor(const Animation &anim, int hotX, int hotY) {
	SDL_Cursor *c = IMG_CreateAnimatedCursor(anim.Get(), hotX, hotY);
	if (!c)
		return Err(GetError());
	return Ok(c);
}

} // namespace sdl3
