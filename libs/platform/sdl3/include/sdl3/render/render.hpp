#pragma once
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include <core/core.hpp>
#include "core/wrapper.hpp"
#include "sdl3/core/error.hpp"
#include "sdl3/core/stdinc.hpp"
#include "sdl3/core/structs.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

class Surface;
class Window;
class Renderer;
// Déclaration avancée seulement : Renderer::GetGPUDevice() n'a besoin que
// d'un type de retour dans sa DÉCLARATION ici — le corps (qui a besoin du
// type complet) vit dans gpu.hpp, qui inclut déjà render.hpp (l'inverse
// créerait un cycle).
class GpuDevice;

/// Type SDL brut d'une surface.
using SurfaceRaw = SDL_Surface;
/// Type SDL brut d'une fenêtre.
using WindowRaw = SDL_Window;
/// Type SDL brut d'un renderer.
using RendererRaw = SDL_Renderer;
/// Référence non-possédante sur une Surface.
using SurfaceRef = Ref<Surface>;
/// Référence non-possédante sur une Window.
using WindowRef = Ref<Window>;
/// Référence non-possédante sur un Renderer.
using RendererRef = Ref<Renderer>;

/// Identifiant d'un écran.
using DisplayID   = SDL_DisplayID;

// ============================================================================
// Surface
// ============================================================================

enum class PixelFormat {
	UNKNOWN = SDL_PIXELFORMAT_UNKNOWN,
	INDEX1_LSB = SDL_PIXELFORMAT_INDEX1LSB,
	INDEX1_MSB = SDL_PIXELFORMAT_INDEX1MSB,
	INDEX2_LSB = SDL_PIXELFORMAT_INDEX2LSB,
	INDEX2_MSB = SDL_PIXELFORMAT_INDEX2MSB,
	INDEX4_LSB = SDL_PIXELFORMAT_INDEX4LSB,
	INDEX4_MSB = SDL_PIXELFORMAT_INDEX4MSB,
	INDEX8 = SDL_PIXELFORMAT_INDEX8,
	RGB332 = SDL_PIXELFORMAT_RGB332,
	XRGB4444 = SDL_PIXELFORMAT_XRGB4444,
	XBGR4444 = SDL_PIXELFORMAT_XBGR4444,
	XRGB1555 = SDL_PIXELFORMAT_XRGB1555,
	XBGR1555 = SDL_PIXELFORMAT_XBGR1555,
	ARGB4444 = SDL_PIXELFORMAT_ARGB4444,
	RGBA4444 = SDL_PIXELFORMAT_RGBA4444,
	ABGR4444 = SDL_PIXELFORMAT_ABGR4444,
	BGRA4444 = SDL_PIXELFORMAT_BGRA4444,
	ARGB1555 = SDL_PIXELFORMAT_ARGB1555,
	RGBA5551 = SDL_PIXELFORMAT_RGBA5551,
	ABGR1555 = SDL_PIXELFORMAT_ABGR1555,
	BGRA5551 = SDL_PIXELFORMAT_BGRA5551,
	RGB565 = SDL_PIXELFORMAT_RGB565,
	BGR565 = SDL_PIXELFORMAT_BGR565,
	RGB24 = SDL_PIXELFORMAT_RGB24,
	BGR24 = SDL_PIXELFORMAT_BGR24,
	XRGB8888 = SDL_PIXELFORMAT_XRGB8888,
	RGBX8888 = SDL_PIXELFORMAT_RGBX8888,
	XBGR8888 = SDL_PIXELFORMAT_XBGR8888,
	BGRX8888 = SDL_PIXELFORMAT_BGRX8888,
	ARGB8888 = SDL_PIXELFORMAT_ARGB8888,
	RGBA8888 = SDL_PIXELFORMAT_RGBA8888,
	ABGR8888 = SDL_PIXELFORMAT_ABGR8888,
	BGRA8888 = SDL_PIXELFORMAT_BGRA8888,
	XRGB2101010 = SDL_PIXELFORMAT_XRGB2101010,
	XBGR2101010 = SDL_PIXELFORMAT_XBGR2101010,
	ARGB2101010 = SDL_PIXELFORMAT_ARGB2101010,
	ABGR2101010 = SDL_PIXELFORMAT_ABGR2101010,
	RGB48 = SDL_PIXELFORMAT_RGB48,
	BGR48 = SDL_PIXELFORMAT_BGR48,
	RGBA64 = SDL_PIXELFORMAT_RGBA64,
	ARGB64 = SDL_PIXELFORMAT_ARGB64,
	BGRA64 = SDL_PIXELFORMAT_BGRA64,
	ABGR64 = SDL_PIXELFORMAT_ABGR64,
	RGB48_FLOAT = SDL_PIXELFORMAT_RGB48_FLOAT,
	BGR48_FLOAT = SDL_PIXELFORMAT_BGR48_FLOAT,
	RGBA64_FLOAT = SDL_PIXELFORMAT_RGBA64_FLOAT,
	ARGB64_FLOAT = SDL_PIXELFORMAT_ARGB64_FLOAT,
	BGRA64_FLOAT = SDL_PIXELFORMAT_BGRA64_FLOAT,
	ABGR64_FLOAT = SDL_PIXELFORMAT_ABGR64_FLOAT,
	RGB96_FLOAT = SDL_PIXELFORMAT_RGB96_FLOAT,
	BGR96_FLOAT = SDL_PIXELFORMAT_BGR96_FLOAT,
	RGBA128_FLOAT = SDL_PIXELFORMAT_RGBA128_FLOAT,
	ARGB128_FLOAT = SDL_PIXELFORMAT_ARGB128_FLOAT,
	BGRA128_FLOAT = SDL_PIXELFORMAT_BGRA128_FLOAT,
	ABGR128_FLOAT = SDL_PIXELFORMAT_ABGR128_FLOAT,
	YV12 = SDL_PIXELFORMAT_YV12,
	IYUV = SDL_PIXELFORMAT_IYUV,
	YUY2 = SDL_PIXELFORMAT_YUY2,
	UYVY = SDL_PIXELFORMAT_UYVY,
	YVYU = SDL_PIXELFORMAT_YVYU,
	NV12 = SDL_PIXELFORMAT_NV12,
	NV21 = SDL_PIXELFORMAT_NV21,
	P010 = SDL_PIXELFORMAT_P010,
	EXTERNAL_OES = SDL_PIXELFORMAT_EXTERNAL_OES,
	MJPG = SDL_PIXELFORMAT_MJPG,
	RGBA32 = SDL_PIXELFORMAT_RGBA32,
	ARGB32 = SDL_PIXELFORMAT_ARGB32,
	BGRA32 = SDL_PIXELFORMAT_BGRA32,
	ABGR32 = SDL_PIXELFORMAT_ABGR32,
	RGBX32 = SDL_PIXELFORMAT_RGBX32,
	XRGB32 = SDL_PIXELFORMAT_XRGB32,
	BGRX32 = SDL_PIXELFORMAT_BGRX32,
	XBGR32 = SDL_PIXELFORMAT_XBGR32,
};

namespace detail {
	constexpr SDL_PixelFormat ToSDL(PixelFormat e) {
		return static_cast<SDL_PixelFormat>(e);
	}
}

// ============================================================================
// ScaleMode / DisplayMode / DisplayOrientation
// ============================================================================

enum class ScaleMode : int {
	INVALID  = SDL_SCALEMODE_INVALID,
	NEAREST  = SDL_SCALEMODE_NEAREST,
	LINEAR   = SDL_SCALEMODE_LINEAR,
	PIXELART = SDL_SCALEMODE_PIXELART,
};

enum class DisplayOrientation : int {
	UNKNOWN         = SDL_ORIENTATION_UNKNOWN,
	LANDSCAPE       = SDL_ORIENTATION_LANDSCAPE,
	LANDSCAPE_FLIPPED = SDL_ORIENTATION_LANDSCAPE_FLIPPED,
	PORTRAIT        = SDL_ORIENTATION_PORTRAIT,
	PORTRAIT_FLIPPED = SDL_ORIENTATION_PORTRAIT_FLIPPED,
};

/// Mode d'affichage (résolution, format, taux de rafraîchissement).
struct DisplayMode {
	DisplayID displayID = 0;
	PixelFormat format = PixelFormat::UNKNOWN;
	int w = 0, h = 0;
	float pixelDensity = 0.f;
	float refreshRate = 0.f;
	int refreshRateNumerator = 0;
	int refreshRateDenominator = 0;

	constexpr DisplayMode() = default;
	explicit constexpr DisplayMode(const SDL_DisplayMode &m)
		: displayID(m.displayID), format(static_cast<PixelFormat>(m.format)), w(m.w), h(m.h),
		  pixelDensity(m.pixel_density), refreshRate(m.refresh_rate),
		  refreshRateNumerator(m.refresh_rate_numerator), refreshRateDenominator(m.refresh_rate_denominator) {}
};

class Surface : public Wrapper<SDL_Surface, SDL_DestroySurface> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Surface, Error> Create(int w, int h, PixelFormat fmt = PixelFormat::RGBA8888);

	/// Surface POSSÉDANT ses pixels, remplie par copie de `pixels` (`pitch`
	/// octets par ligne, disposés selon `fmt`). Copie plutôt que
	/// SDL_CreateSurfaceFrom (qui emprunte le tampon) : la surface peut alors
	/// survivre au tampon source — typiquement un framebuffer d'émulateur ou
	/// de rendu logiciel écrit ensuite en PNG (cf. ImgSavePng). Ajouté pour
	/// emulator (captures du framebuffer émulé en mode sans
	/// écran) : le wrapper savait charger et écrire une surface, mais pas en
	/// construire une depuis des octets bruts.
	[[nodiscard]] static Result<Surface, StringView> CreateFromPixels(int w, int h, PixelFormat fmt, const void *pixels,
																	  int pitch);

	[[nodiscard]] static Result<Surface, StringView> CreateFromFile(const String &path);

	[[nodiscard]] Result<Surface, StringView> Convert(PixelFormat fmt) const;

	/// Charge un fichier BMP/PNG/JPG via les codecs intégrés de SDL (sans SDL_image).
	[[nodiscard]] static Result<Surface, StringView> LoadBmp(const String &path);
	[[nodiscard]] static Result<Surface, StringView> LoadPng(const String &path);
	[[nodiscard]] static Result<Surface, StringView> LoadJpg(const String &path);

	[[nodiscard]] int GetWidth() const { return mHandle ? mHandle->w : 0; }
	[[nodiscard]] int GetHeight() const { return mHandle ? mHandle->h : 0; }
	[[nodiscard]] Point GetSize() const { return {GetWidth(), GetHeight()}; }

	/// Octets bruts des pixels (nullptr si la surface est vide). Pour un accès
	/// RGBA8 contigu sans se soucier du format, préférer `ToRgba8()` / `FromRgba8()`.
	[[nodiscard]] uint8_t *Pixels() noexcept { return mHandle ? static_cast<uint8_t *>(mHandle->pixels) : nullptr; }
	[[nodiscard]] const uint8_t *Pixels() const noexcept { return mHandle ? static_cast<const uint8_t *>(mHandle->pixels) : nullptr; }
	/// Octets par ligne du tampon de pixels.
	[[nodiscard]] int Pitch() const noexcept { return mHandle ? mHandle->pitch : 0; }

	/// Surface RGBA8 opaque (format RGBA32, octets contigus [R,G,B,A]).
	[[nodiscard]] static Result<Surface, StringView> Solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
	/// Surface RGBA8 (`width`×`height`×4 octets, lignes contiguës).
	[[nodiscard]] static Result<Surface, StringView> FromRgba8(std::span<const uint8_t> rgba, int w, int h);
	/// Octets RGBA8 contigus (conversion en RGBA32 si nécessaire) ; vide en cas d'échec.
	[[nodiscard]] std::vector<uint8_t> ToRgba8() const;

	bool Fill(Color c);

	bool Blit(const Surface &src, Point dest);

	/// Blit mis à l'échelle (SDL_BlitSurfaceScaled), `scaleMode` = filtre.
	bool BlitScaled(const Surface &src, Option<Rect> srcRect, const Rect &dstRect, ScaleMode scaleMode = ScaleMode::NEAREST);

	/// Copie la surface (SDL_DuplicateSurface).
	[[nodiscard]] Result<Surface, StringView> Duplicate() const;

	/// Surface redimensionnée (SDL_ScaleSurface).
	[[nodiscard]] Result<Surface, StringView> Scale(int w, int h, ScaleMode scaleMode = ScaleMode::NEAREST) const;

	/// Faut-il verrouiller la surface avant un accès direct aux pixels ?
	[[nodiscard]] bool MustLock() const { return mHandle && SDL_MUSTLOCK(mHandle); }
	bool Lock() { return mHandle && SDL_LockSurface(mHandle); }
	void Unlock() { if (mHandle) SDL_UnlockSurface(mHandle); }

	/// Lit un pixel (RGBA 8 bits) ; NONE si hors bornes ou non lisible.
	[[nodiscard]] Option<Color> ReadPixel(int x, int y) const;
	[[nodiscard]] Option<FColor> ReadPixelFloat(int x, int y) const;

	/// Écrit un pixel (RGBA 8 bits).
	bool WritePixel(int x, int y, Color c);

	/// Prémultiplie l'alpha des pixels de la surface.
	bool PremultiplyAlpha(bool linear = false) { return mHandle && SDL_PremultiplySurfaceAlpha(mHandle, linear); }

	bool SetColorKey(bool enable, Color c);

	bool SetAlphaMod(uint8_t alpha) { return mHandle && SDL_SetSurfaceAlphaMod(mHandle, alpha); }
	bool SetBlendMode(BlendMode mode);

	/// Enregistre la surface en BMP/PNG via les codecs intégrés de SDL.
	bool SaveBmp(const String &path);
	bool SavePng(const String &path);
};

// ============================================================================
// Pixels — format, conversion RGB(A), alpha prémultiplié (SDL_pixels.h)
// ============================================================================

namespace pixels {

/// Détails du format (masques, octets par pixel, etc.), ou nullptr si inconnu.
[[nodiscard]] inline const SDL_PixelFormatDetails *GetPixelFormatDetails(PixelFormat fmt) {
	return SDL_GetPixelFormatDetails(detail::ToSDL(fmt));
}

/// Nom lisible du format de pixel.
[[nodiscard]] inline const char *GetPixelFormatName(PixelFormat fmt) {
	return SDL_GetPixelFormatName(detail::ToSDL(fmt));
}

/// Récupère bpp et masques de bits d'un format.
[[nodiscard]] inline bool GetMasksForPixelFormat(PixelFormat fmt, int *bpp, Uint32 *rmask, Uint32 *gmask,
												 Uint32 *bmask, Uint32 *amask) {
	return SDL_GetMasksForPixelFormat(detail::ToSDL(fmt), bpp, rmask, gmask, bmask, amask);
}

/// Encode RGB en valeur de pixel pour un format donné.
[[nodiscard]] inline Uint32 MapRGB(PixelFormat fmt, uint8_t r, uint8_t g, uint8_t b) {
	return SDL_MapRGB(GetPixelFormatDetails(fmt), nullptr, r, g, b);
}

/// Encode RGBA en valeur de pixel pour un format donné.
[[nodiscard]] inline Uint32 MapRGBA(PixelFormat fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return SDL_MapRGBA(GetPixelFormatDetails(fmt), nullptr, r, g, b, a);
}

/// Décode une valeur de pixel en RGB.
[[nodiscard]] inline Color GetRGB(PixelFormat fmt, Uint32 pixel) {
	Color c;
	SDL_GetRGB(pixel, GetPixelFormatDetails(fmt), nullptr, &c.r, &c.g, &c.b);
	return c;
}

/// Décode une valeur de pixel en RGBA.
[[nodiscard]] inline Color GetRGBA(PixelFormat fmt, Uint32 pixel) {
	Color c;
	SDL_GetRGBA(pixel, GetPixelFormatDetails(fmt), nullptr, &c.r, &c.g, &c.b, &c.a);
	return c;
}

/// Prémultiplie l'alpha d'un tampon de pixels (conversion src_fmt -> dst_fmt).
[[nodiscard]] inline bool PremultiplyAlpha(int width, int height, PixelFormat srcFmt, const void *src, int srcPitch,
										   PixelFormat dstFmt, void *dst, int dstPitch, bool linear = false) {
	return SDL_PremultiplyAlpha(width, height, detail::ToSDL(srcFmt), src, srcPitch, detail::ToSDL(dstFmt), dst, dstPitch,
								linear);
}

} // namespace pixels

// ============================================================================
// Palette — RAII SDL_Palette (table de couleurs des formats indexés)
// ============================================================================

class Palette : public Wrapper<SDL_Palette, SDL_DestroyPalette> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Palette, Error> Create(int ncolors);

	[[nodiscard]] int NumColors() const noexcept { return mHandle ? mHandle->ncolors : 0; }

	/// Remplace `count` couleurs à partir de l'index `first`.
	bool SetColors(std::span<const Color> colors, int first = 0);

	/// Récupère les couleurs de la palette.
	[[nodiscard]] std::vector<Color> GetColors() const;
};

// ============================================================================
// Window
// ============================================================================

/// Identifiant d'une fenêtre.
using WindowID    = SDL_WindowID;
/// Drapeaux de création d'une fenêtre.
using WindowFlags = SDL_WindowFlags;

namespace windowFlags {
	constexpr WindowFlags FULLSCREEN = SDL_WINDOW_FULLSCREEN;
	constexpr WindowFlags OPEN_GL = SDL_WINDOW_OPENGL;
	constexpr WindowFlags OCCULED = SDL_WINDOW_OCCLUDED;
	constexpr WindowFlags HIDDEN = SDL_WINDOW_HIDDEN;
	constexpr WindowFlags BORDERLESS = SDL_WINDOW_BORDERLESS;
	constexpr WindowFlags RESIZABLE = SDL_WINDOW_RESIZABLE;
	constexpr WindowFlags MINIMIZED = SDL_WINDOW_MINIMIZED;
	constexpr WindowFlags MAXIMIZED = SDL_WINDOW_MAXIMIZED;
	constexpr WindowFlags MOUSE_GRABBED = SDL_WINDOW_MOUSE_GRABBED;
	constexpr WindowFlags INPUT_FOCUS = SDL_WINDOW_INPUT_FOCUS;
	constexpr WindowFlags MOUSE_FOCUS = SDL_WINDOW_MOUSE_FOCUS;
	constexpr WindowFlags EXTERNAL = SDL_WINDOW_EXTERNAL;
	constexpr WindowFlags MODAL = SDL_WINDOW_MODAL;
	constexpr WindowFlags HIGH_PIXEL_DENSITY = SDL_WINDOW_HIGH_PIXEL_DENSITY;
	constexpr WindowFlags MOUSE_CAPTURE = SDL_WINDOW_MOUSE_CAPTURE;
	constexpr WindowFlags MOUSE_RELATIVE_MODE = SDL_WINDOW_MOUSE_RELATIVE_MODE;
	constexpr WindowFlags ALWAYS_ON_TOP = SDL_WINDOW_ALWAYS_ON_TOP;
	constexpr WindowFlags UTILITY = SDL_WINDOW_UTILITY;
	constexpr WindowFlags TOOLTIP = SDL_WINDOW_TOOLTIP;
	constexpr WindowFlags POPUP_MENU = SDL_WINDOW_POPUP_MENU;
	constexpr WindowFlags KEYBOARD_GRABBED = SDL_WINDOW_KEYBOARD_GRABBED;
	constexpr WindowFlags FILL_DOCUMENT = SDL_WINDOW_FILL_DOCUMENT;
	constexpr WindowFlags VULKAN = SDL_WINDOW_VULKAN;
	constexpr WindowFlags METAL = SDL_WINDOW_METAL;
	constexpr WindowFlags TRANSPARENT = SDL_WINDOW_TRANSPARENT;
	constexpr WindowFlags NOT_FOCUSABLE = SDL_WINDOW_NOT_FOCUSABLE;
} // namespace window_flags

class Window : public Wrapper<SDL_Window, SDL_DestroyWindow> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Window, Error> Create(const String &title, int w, int h,
														   SDL_WindowFlags flags = 0);

	[[nodiscard]] static Result<Window, Error> Create(const String &title, std::span<const int> size,
														   SDL_WindowFlags flags = 0);

	[[nodiscard]] static Result<Window, Error> Create(const String &title, std::span<const int> pos,
														   std::span<const int> size, SDL_WindowFlags flags = 0);

	// ── Getters ──────────────────────────────────────────────────────────────

	[[nodiscard]] int GetWidth() const;
	[[nodiscard]] int GetHeight() const;
	[[nodiscard]] Point GetSize() const;
	[[nodiscard]] int GetX() const;
	[[nodiscard]] int GetY() const;
	[[nodiscard]] Point GetPosition() const;
	[[nodiscard]] bool IsFullscreen() const { return mHandle && SDL_GetWindowFlags(mHandle) & SDL_WINDOW_FULLSCREEN; }
	[[nodiscard]] bool IsResizable() const { return mHandle && SDL_GetWindowFlags(mHandle) & SDL_WINDOW_RESIZABLE; }
	[[nodiscard]] bool IsBordered() const { return mHandle && !(SDL_GetWindowFlags(mHandle) & SDL_WINDOW_BORDERLESS); }
	[[nodiscard]] bool IsVisible() const { return mHandle && !(SDL_GetWindowFlags(mHandle) & SDL_WINDOW_HIDDEN); }
	[[nodiscard]] bool HasInputFocus() const;
	[[nodiscard]] bool HasMouseFocus() const;
	[[nodiscard]] bool IsMinimized() const { return mHandle && SDL_GetWindowFlags(mHandle) & SDL_WINDOW_MINIMIZED; }
	[[nodiscard]] bool IsMaximized() const { return mHandle && SDL_GetWindowFlags(mHandle) & SDL_WINDOW_MAXIMIZED; }
	[[nodiscard]] WindowID GetId() const { return mHandle ? SDL_GetWindowID(mHandle) : 0; }
	/// Échelle d'affichage (SDL_GetWindowDisplayScale : densité de pixels ×
	/// échelle de contenu de l'écran qui porte la fenêtre) ; 1 si inconnue.
	[[nodiscard]] float DisplayScale() const;
	/// Écran qui porte la fenêtre (0 si inconnu).
	[[nodiscard]] DisplayID DisplayId() const { return mHandle ? SDL_GetDisplayForWindow(mHandle) : 0; }
	[[nodiscard]] String Title() const;

	// ── Setters ──────────────────────────────────────────────────────────────

	void SetTitle(const String &t);
	void SetSize(int w, int h);
	void SetPosition(int x, int y);
	void SetFullscreen(bool on);
	void SetResizable(bool on);
	void SetBordered(bool on);
	void SetOpacity(float opacity);

	/// Taille minimale / maximale imposée au gestionnaire de fenêtres.
	bool SetMinimumSize(int w, int h) { return mHandle && SDL_SetWindowMinimumSize(mHandle, w, h); }
	bool SetMaximumSize(int w, int h) { return mHandle && SDL_SetWindowMaximumSize(mHandle, w, h); }
	/// Rapport largeur/hauteur (0 = aucune contrainte sur cette borne).
	bool SetAspectRatio(float minAspect, float maxAspect) {
		return mHandle && SDL_SetWindowAspectRatio(mHandle, minAspect, maxAspect);
	}
	bool SetAlwaysOnTop(bool on) { return mHandle && SDL_SetWindowAlwaysOnTop(mHandle, on); }
	bool SetModal(bool modal) { return mHandle && SDL_SetWindowModal(mHandle, modal); }
	bool SetIcon(const Surface &icon) { return mHandle && SDL_SetWindowIcon(mHandle, icon.Get()); }

	/// Mode plein-écran exact (résolution/taux) — passer NONE pour le mode bureau.
	bool SetFullscreenMode(Option<DisplayMode> mode);

	/// Fait clignoter la fenêtre (attention de l'utilisateur).
	bool Flash(SDL_FlashOperation op = SDL_FLASH_BRIEFLY) { return mHandle && SDL_FlashWindow(mHandle, op); }

	/// Ouvre le menu système de la fenêtre à la position donnée.
	bool ShowSystemMenu(int x, int y) { return mHandle && SDL_ShowWindowSystemMenu(mHandle, x, y); }

	/// Applique immédiatement les modifications en attente (rarement nécessaire).
	bool Sync() { return mHandle && SDL_SyncWindow(mHandle); }

	// ── Getters complémentaires ───────────────────────────────────────────────

	/// Taille réelle en pixels physiques (indépendante de l'échelle DPI).
	[[nodiscard]] Point SizeInPixels() const;
	/// Densité de pixels de la fenêtre (facteur d'échelle).
	[[nodiscard]] float PixelDensity() const { return mHandle ? SDL_GetWindowPixelDensity(mHandle) : 0.f; }
	[[nodiscard]] float Opacity() const { return mHandle ? SDL_GetWindowOpacity(mHandle) : 0.f; }
	/// Zone « sûre » de la fenêtre (hors encoche / coins arrondis).
	[[nodiscard]] Option<Rect> SafeArea() const;
	[[nodiscard]] SDL_PropertiesID Properties() const { return mHandle ? SDL_GetWindowProperties(mHandle) : 0; }
	[[nodiscard]] Option<DisplayMode> FullscreenMode() const;

	/// Active la saisie de texte (événements TEXT_INPUT ; affiche le clavier
	/// virtuel sur les plateformes qui en ont un).
	void StartTextInput();
	void StopTextInput();
	[[nodiscard]] bool TextInputActive() const { return mHandle && SDL_TextInputActive(mHandle); }

	void Show();
	void Hide();
	void Raise();
	void Maximize();
	void Minimize();
	void Restore();

	[[nodiscard]] SDL_Surface *Surface() const { return mHandle ? SDL_GetWindowSurface(mHandle) : nullptr; }
	bool UpdateSurface() { return mHandle && SDL_UpdateWindowSurface(mHandle); }

	/// Installs a hit-test callback (SDL_SetWindowHitTest) so parts of a
	/// borderless window can report themselves as draggable/resizable, e.g. a
	/// hand-drawn title bar strip returning SDL_HITTEST_DRAGGABLE. Pass an
	/// empty function to clear a previously-installed callback.
	/// Souris « relative » : curseur caché et capturé, seuls les déplacements
	/// (xrel/yrel) arrivent — la vue à la première personne d'un jeu.
	bool SetRelativeMouseMode(bool enabled) { return mHandle && SDL_SetWindowRelativeMouseMode(mHandle, enabled); }
	[[nodiscard]] bool RelativeMouseMode() const { return mHandle && SDL_GetWindowRelativeMouseMode(mHandle); }
	/// Souris « confinée » : curseur visible mais retenu dans la fenêtre.
	bool SetMouseGrab(bool enabled) { return mHandle && SDL_SetWindowMouseGrab(mHandle, enabled); }
	[[nodiscard]] bool MouseGrab() const { return mHandle && SDL_GetWindowMouseGrab(mHandle); }

	bool SetHitTest(std::function<SDL_HitTestResult(const SDL_Point &)> fn);

private:
	std::unique_ptr<std::function<SDL_HitTestResult(const SDL_Point &)>> m_hitTest;

	static SDL_HitTestResult SDLCALL HitTestTrampoline(SDL_Window *, const SDL_Point *area, void *data);
};

// ── Écrans ──────────────────────────────────────────────────────────────────

/// Écrans connectés, dans l'ordre de SDL_GetDisplays (le premier est le principal).
[[nodiscard]] std::vector<DisplayID> GetDisplays();
/// Rectangle de l'écran dans le bureau virtuel, NONE si l'écran est inconnu.
[[nodiscard]] Option<Rect> GetDisplayBounds(DisplayID display);
/// Échelle de contenu de l'écran (SDL_GetDisplayContentScale) ; 1 si inconnue.
[[nodiscard]] float GetDisplayContentScale(DisplayID display);

// ── Requêtes d'écran supplémentaires (SDL_video.h) ───────────────────────────

/// Écran principal (SDL_GetPrimaryDisplay).
[[nodiscard]] inline DisplayID GetPrimaryDisplay() noexcept { return SDL_GetPrimaryDisplay(); }

/// Nom lisible de l'écran.
[[nodiscard]] inline const char *GetDisplayName(DisplayID display) noexcept { return SDL_GetDisplayName(display); }

/// Propriétés de l'écran (résolution physique, HDR, contenu, etc.).
[[nodiscard]] inline SDL_PropertiesID GetDisplayProperties(DisplayID display) noexcept {
	return SDL_GetDisplayProperties(display);
}

/// Orientation naturelle / courante de l'écran.
[[nodiscard]] inline DisplayOrientation GetNaturalDisplayOrientation(DisplayID display) noexcept {
	return static_cast<DisplayOrientation>(SDL_GetNaturalDisplayOrientation(display));
}
[[nodiscard]] inline DisplayOrientation GetCurrentDisplayOrientation(DisplayID display) noexcept {
	return static_cast<DisplayOrientation>(SDL_GetCurrentDisplayOrientation(display));
}

/// Mode d'affichage courant / de bureau de l'écran.
[[nodiscard]] Option<DisplayMode> GetCurrentDisplayMode(DisplayID display);
[[nodiscard]] Option<DisplayMode> GetDesktopDisplayMode(DisplayID display);

/// Mode plein-écran le plus proche d'une résolution/taux demandés.
[[nodiscard]] Option<DisplayMode> GetClosestFullscreenDisplayMode(DisplayID display, int w, int h,
																  float refreshRate = 0.f,
																  bool includeHighDensity = false);

/// Tous les modes plein-écran disponibles (à libérer, cf. implémentation).
[[nodiscard]] std::vector<DisplayMode> GetFullscreenDisplayModes(DisplayID display);

/// Zone utilisable de l'écran (hors barres système / encoches).
[[nodiscard]] Option<Rect> GetDisplayUsableBounds(DisplayID display);

/// Écran contenant un point / un rectangle.
[[nodiscard]] inline DisplayID GetDisplayForPoint(Point p) noexcept {
	SDL_Point sdlP = p; return SDL_GetDisplayForPoint(&sdlP);
}
[[nodiscard]] inline DisplayID GetDisplayForRect(const Rect &r) noexcept {
	SDL_Rect sdlR = r; return SDL_GetDisplayForRect(&sdlR);
}

// ============================================================================
// Texture
// ============================================================================

enum class TextureAccess {
	STATIC    = SDL_TEXTUREACCESS_STATIC,    /**< Changes rarely, not lockable */
    STREAMING = SDL_TEXTUREACCESS_STREAMING, /**< Changes frequently, lockable */
    TARGET    = SDL_TEXTUREACCESS_TARGET     /**< Texture can be used as a render target */
};
namespace detail {
	constexpr SDL_TextureAccess ToSDL(TextureAccess e) {
		return static_cast<SDL_TextureAccess>(e);
	}
}

class Texture : public Wrapper<SDL_Texture, SDL_DestroyTexture> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<Texture, Error> Create(Renderer &ren, int w, int h,
															PixelFormat fmt = PixelFormat::RGBA8888);
	[[nodiscard]] static Result<Texture, StringView> CreateFromSurface(Renderer &ren, const Surface &surface);
	/// Texture du renderer qui AFFICHE une texture SDL_GPU existante (usage
	/// SAMPLER), sans copie : le renderer doit être « gpu » et partager le
	/// device de `texture` (Renderer::GetGPUDevice). `texture` n'est pas
	/// possédée et doit vivre plus longtemps que la Texture rendue.
	[[nodiscard]] static Result<Texture, StringView> FromGpuTexture(Renderer &ren, SDL_GPUTexture *texture, int w, int h,
																	 PixelFormat fmt = PixelFormat::RGBA32);
	[[nodiscard]] static Result<Texture, StringView> CreateFromFile(Renderer &ren, const String &path);

	[[nodiscard]] FPoint GetSize() const;
	[[nodiscard]] int GetWidth() const { return int(GetSize().x); }
	[[nodiscard]] int GetHeight() const { return int(GetSize().y); }

	bool SetColorMod(uint8_t r, uint8_t g, uint8_t b) { return mHandle && SDL_SetTextureColorMod(mHandle, r, g, b); }
	bool SetColorMod(Color c) { return SetColorMod(c.r, c.g, c.b); }
	bool SetAlphaMod(uint8_t a) { return mHandle && SDL_SetTextureAlphaMod(mHandle, a); }
	bool SetBlendMode(BlendMode mode);
	bool SetScaleMode(SDL_ScaleMode mode) { return mHandle && SDL_SetTextureScaleMode(mHandle, mode); }
	bool SetScaleMode(ScaleMode mode) { return SetScaleMode(static_cast<SDL_ScaleMode>(mode)); }

	/// Replaces the full texture contents (or just `rect` if given) from a raw
	/// pixel buffer laid out per the texture's own format, `pitch` bytes per row.
	bool Update(const void *pixels, int pitch, Option<Rect> rect = NONE);
};

// Corners / Sides (rayons de coins, épaisseurs de bords) vivent dans structs.hpp
// avec les autres types géométriques (Rect/FRect/Point...).

// ============================================================================
// Génération de points de contour — utilisés en interne par Renderer pour les
// formes complexes (cercle, ellipse, arc) ; exposés en namespace `shapes` au
// cas où l'appelant veut les points bruts sans passer par un Renderer.
// ============================================================================

namespace shapes {

// Cercle par points via l'algorithme de Bresenham (style SDL3pp::GenerateCirclePoints).
[[nodiscard]] std::vector<FPoint> GenerateCirclePoints(FPoint center, float radius);

// Points d'ellipse (ou de cercle si radiusX==radiusY) ordonnés angulairement —
// requis pour l'éventail de triangles utilisé par les versions "fill".
// `rotationDeg` tourne l'ellipse autour de son centre (sens antihoraire).
[[nodiscard]] std::vector<FPoint> GenerateEllipsePoints(FPoint center, float radiusX, float radiusY,
															   float rotationDeg = 0.f);

// Points d'arc ordonnés angulairement (angles en degrés, convention SDL :
// 0° = droite, sens horaire croissant) — adaptés au triangle-fan des "fill".
[[nodiscard]] std::vector<FPoint> GenerateArcPoints(FPoint center, float radius, float startAngleDeg,
														   float endAngleDeg);

} // namespace shapes

// ============================================================================
// Renderer
// ============================================================================

class Renderer : public Wrapper<SDL_Renderer, SDL_DestroyRenderer> {
public:
	using Wrapper::Wrapper;

	Renderer(WindowRef win, Option<StringView> name)
		: Renderer(SDL_CreateRenderer(win->Get(), name.IsSome() ? name->CStr() : nullptr)) {}

	[[nodiscard]] static Result<Renderer, Error> Create(Window &window, const char *driver = nullptr);
	/// Renderer « gpu » d'une AUTRE fenêtre sur le MÊME SDL_GPUDevice que
	/// `gpuRenderer` (SDL_CreateGPURenderer) : une swapchain par fenêtre, un
	/// seul device — les textures GPU (vues 3D DIRECT) s'affichent alors dans
	/// les deux fenêtres. Err si `gpuRenderer` n'utilise pas le backend "gpu".
	/// `gpuRenderer` doit survivre au renderer rendu (il possède le device).
	[[nodiscard]] static Result<Renderer, Error> CreateSharingGpu(const Renderer &gpuRenderer, Window &window);
	/// Renderer « gpu » sur un SDL_GPUDevice existant (SDL_CreateGPURenderer) ;
	/// le renderer N'EN EST PAS propriétaire : le device doit lui survivre.
	[[nodiscard]] static Result<Renderer, Error> CreateOnGpuDevice(SDL_GPUDevice *device, Window &window);

	// ── Synchronisation verticale ────────────────────────────────────────────

	/// Mode de synchronisation verticale d'un `Renderer`.
	enum class VSync : int {
		DISABLED = SDL_RENDERER_VSYNC_DISABLED, ///< présente sans attendre le balayage
		EVERY_REFRESH = 1,                      ///< une image par rafraîchissement (défaut de SDL)
		EVERY_SECOND_REFRESH = 2,               ///< une image sur deux
		ADAPTIVE = SDL_RENDERER_VSYNC_ADAPTIVE, ///< adaptatif, si le pilote le gère
	};

	/// Règle la synchronisation verticale. AUCUN moyen de le faire n'existait
	/// dans ce wrapper : `Present()` bloquait donc systématiquement jusqu'au
	/// prochain balayage, ce qui plafonne la cadence à celle de l'écran et
	/// rend impossible toute mesure de performance au-delà (constaté en
	/// profilant sandbox — la présentation pesait à elle seule
	/// plus de la moitié du temps d'image).
	///
	/// `false` si le pilote ne gère pas le mode demandé (certains n'acceptent
	/// que DISABLED et EVERY_REFRESH) — l'appelant peut alors se rabattre.
	bool SetVSync(VSync mode) { return mHandle && SDL_SetRenderVSync(mHandle, int(mode)); }

	/// Mode de synchronisation courant, `NONE` s'il n'est pas interrogeable.
	[[nodiscard]] Option<VSync> GetVSync() const;

	[[nodiscard]] static Result<Renderer, StringView> CreateSoftware(Surface &surface);

	// ── GPU Device ───────────────────────────────────────────────────────────
	/// Le SDL_GPUDevice utilisé par ce Renderer, si (et seulement si) il a été
	/// créé avec le backend "gpu" de SDL3 (`SDL_CreateGPURenderer`/
	/// `SDL_GetGPURendererDevice`) — `Err` sinon (renderer classique
	/// opengl/vulkan/metal/direct3d, cas par défaut de `Renderer::Create`
	/// dans ce dépôt). Corps défini dans gpu.hpp (a besoin du type complet de
	/// GpuDevice, cf. la déclaration avancée en tête de ce fichier).
	///
	/// @warning Le GpuDevice retourné est EMPRUNTÉ (ce Renderer reste
	/// propriétaire du device SDL_GPU sous-jacent) alors que `sdl3::GpuDevice`
	/// est normalement un wrapper POSSESSEUR (son destructeur appelle
	/// SDL_DestroyGPUDevice) — ne JAMAIS laisser la valeur retournée ici
	/// atteindre son propre destructeur pendant que ce Renderer est encore en
	/// vie (double-free sinon). `sdl3::Renderer3D` (renderer3d.hpp) emprunte le
	/// device de la même façon : l'appelant doit garantir sa durée de vie.
	[[nodiscard]] Result<GpuDevice, StringView> GetGPUDevice() const;

	// ── Getters ──────────────────────────────────────────────────────────────

	Point GetOutputSize() const;

	int GetOutputWidth() const;

	int GetOutputHeight() const;

	// ── Draw state ───────────────────────────────────────────────────────────
	bool SetDrawColor(Color c) { return (!mHandle) ? false : SDL_SetRenderDrawColor(mHandle, c.r, c.g, c.b, c.a); }
	bool SetDrawColor(FColor c);

	[[nodiscard]] Color GetDrawColor() const;
	[[nodiscard]] FColor GetDrawColorFloat() const;

	bool SetBlendMode(BlendMode mode);
	bool SetClipRect(const Rect &r);
	bool SetClipRect(const FRect &r);
	bool ClearClipRect() { return mHandle && SDL_SetRenderClipRect(mHandle, nullptr); }
	bool SetScale(float sx, float sy) { return mHandle && SDL_SetRenderScale(mHandle, sx, sy); }
	bool SetViewport(const Rect &r);
	bool SetViewport(const FRect &r);
	bool ClearViewport() { return mHandle && SDL_SetRenderViewport(mHandle, nullptr); }

	// ── Clear & present ──────────────────────────────────────────────────────

	bool Clear() { return (!mHandle) ? false : SDL_RenderClear(mHandle); }
	bool Present() { return (!mHandle) ? false : SDL_RenderPresent(mHandle); }

	// ── Primitives ───────────────────────────────────────────────────────────

	bool DrawPoint(float x, float y) { return (!mHandle) ? false : SDL_RenderPoint(mHandle, x, y); }
	bool DrawPoint(FPoint p) { return DrawPoint(p.x, p.y); }

	bool DrawPoints(const FPoint *pts, int count) { return mHandle && SDL_RenderPoints(mHandle, (const SDL_FPoint*)pts, count); }
	bool DrawPoints(std::span<const FPoint> pts);

	bool DrawLine(float x1, float y1, float x2, float y2);
	bool DrawLine(FPoint a, FPoint b) { return DrawLine(a.x, a.y, b.x, b.y); }
	/// Texte de débogage SDL3 (police bitmap 8×8 intégrée, couleur de dessin
	/// courante) : chiffres et libellés courts sans police chargée.
	bool DrawDebugText(float x, float y, const char *text) { return mHandle && SDL_RenderDebugText(mHandle, x, y, text); }

	bool DrawLines(const FPoint *pts, int count) { return mHandle && SDL_RenderLines(mHandle, (const SDL_FPoint*)pts, count); }
	bool DrawLines(std::span<const FPoint> pts);

	bool DrawRect(const FRect &r);
	bool FillRect(const FRect &r);

	bool DrawRects(const FRect *rects, int count) { return mHandle && SDL_RenderRects(mHandle, (const SDL_FRect*)rects, count); }
	bool DrawRects(std::span<const FRect> rects);
	bool FillRects(const FRect *rects, int count);
	bool FillRects(std::span<const FRect> rects);

	// ── Texture rendering ────────────────────────────────────────────────────

	bool Render(const Texture &tex, float x, float y);
	bool Render(const Texture &tex, const FRect &dst);
	bool Render(const Texture &tex, const FRect &src, const FRect &dst);
	bool RenderRotated(const Texture &tex, const FRect &dst, double angleDeg, FPoint center = {0.f, 0.f},
					   FlipMode flip = FlipMode::NONE);

	// ── Geometry (triangles) ─────────────────────────────────────────────────

	bool RenderGeometry(std::span<const Vertex> vertices, std::span<const int> indices = {});
	bool RenderGeometry(const Texture &tex, std::span<const Vertex> vertices, std::span<const int> indices = {});

	// Remplit un polygone (convexe, ou concave "raisonnable") défini par des
	// points de contour ordonnés angulairement, via un éventail de triangles
	// centré sur le centroïde — teinté avec la couleur de dessin courante.
	bool RenderGeometryFromPoints(std::span<const FPoint> points);

	// ── Formes complexes ─────────────────────────────────────────────────────

	bool DrawCircle(FPoint center, float radius) { return DrawPoints(shapes::GenerateCirclePoints(center, radius)); }
	bool FillCircle(FPoint center, float radius);

	bool DrawEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg = 0.f);
	bool FillEllipse(FPoint center, float radiusX, float radiusY, float rotationDeg = 0.f);

	bool DrawArc(FPoint center, float radius, float startAngleDeg, float endAngleDeg);

	// Contour d'une part de camembert : arc + deux rayons vers le centre.
	bool DrawPie(FPoint center, float radius, float startAngleDeg, float endAngleDeg);

	// Contour d'un rectangle à coins arrondis (rayons indépendants par coin).
	/// Rayons bornés à la moitié du plus petit côté : au-delà, les quarts de
	/// disque d'angle se chevauchent et la bande centrale prend une largeur
	/// négative — un carré de 8 px au rayon 8 se dessinait en « × ».
	[[nodiscard]] static Corners ClampCorners(const FRect &rect, const Corners &c) noexcept;

	/// Comme SDL_RenderRect, le trait reste À L'INTÉRIEUR du rectangle : les
	/// bords droit/bas passent par les pixels x+w-1 et y+h-1 (et non x+w/y+h,
	/// hors de la boîte — donc rognés dès qu'un clip la borne).
	bool DrawRoundedRect(const FRect &outer, const Corners &corners);

	// Rectangle à coins arrondis rempli (4 secteurs de coin + 3 bandes rectangulaires).
	bool FillRoundedRect(const FRect &rect, const Corners &corners);

	// Bordure (anneau) d'un rectangle à coins arrondis, remplie entre le
	// contour `rect` (intérieur) et `rect` étendu de `border` (extérieur).
	bool DrawRoundedBorderedRect(const FRect &rect, const Sides &border, const Corners &c);

	// Contour d'un polygone fermé (renvoie au premier point).
	bool DrawPolygon(std::span<const FPoint> points);

	// Polygone rempli (convexe, ou concave "raisonnable") via éventail de triangles.
	bool FillPolygon(std::span<const FPoint> points);

	// Courbe de Bézier (degré arbitraire, via de Casteljau) approximée par des
	// segments — `step` dans (0, 1], plus petit = plus de segments = plus lisse.
	bool DrawBezier(std::span<const FPoint> controlPoints, float step = 0.05f);

	// ── Texture creation ─────────────────────────────────────────────────────

	[[nodiscard]] Result<Texture, StringView> CreateTexture(PixelFormat fmt, TextureAccess access, int w, int h);

	[[nodiscard]] Result<Texture, StringView> CreateTextureFromSurface(const Surface &surf);

	// ── Render target ────────────────────────────────────────────────────────

	bool SetTarget(Texture &tex) { return mHandle && SDL_SetRenderTarget(mHandle, tex.Get()); }
	bool ResetTarget() { return mHandle && SDL_SetRenderTarget(mHandle, nullptr); }
	/// Shader de fragments personnalisé des dessins suivants (renderer « gpu » seulement,
	/// voir `GpuRenderState`) ; nullptr rétablit les shaders du renderer.
	bool SetGpuRenderState(SDL_GPURenderState *state) { return mHandle && SDL_SetGPURenderState(mHandle, state); }

	// ── Pixel readback ───────────────────────────────────────────────────────

	/// Lit les pixels de la cible de rendu COURANTE (texture posée via
	/// SetTarget, ou le backbuffer sinon) dans une nouvelle Surface CPU —
	/// SDL_RenderReadPixels. `rect` NONE = tout le viewport courant (attention
	/// si un viewport décalé/agrandi est actif via SetViewport : la lecture
	/// suit CE viewport, pas forcément [0,largeur)x[0,hauteur) de la cible —
	/// ClearViewport() avant l'appel si un rectangle pleine-cible exact est
	/// voulu, cf. usage dans ui/shader_effect.hpp).
	///
	/// Format des pixels retournés NON garanti par SDL (dépend du backend/de
	/// la cible) : appeler Surface::Convert(PixelFormat::RGBA32) — PAS
	/// RGBA8888 — avant tout accès aux octets si un ordre d'octets précis
	/// [R,G,B,A] tightly-packed est nécessaire.
	///
	/// @warning Piège réel rencontré (cf. rapport de tâche M23) :
	/// `PixelFormat::RGBA8888` (SDL_PIXELFORMAT_RGBA8888) est un format
	/// PACKED — son ordre en MÉMOIRE sur little-endian est l'INVERSE de son
	/// nom ([A,B,G,R], pas [R,G,B,A]), un pixel converti puis lu octet-par-
	/// octet "à la main" en RGBA8888 apparaît donc CHANNEL-SWAPPED. SEUL
	/// `PixelFormat::RGBA32` garantit l'ordre littéral [R,G,B,A] en mémoire
	/// quelle que soit l'endianness (cf. doc SDL_pixels.h : RGBA32 est un
	/// alias PLATEFORME-DÉPENDANT — ABGR8888 sur little-endian, RGBA8888 sur
	/// big-endian — précisément pour donner cette garantie). Confirmé
	/// empiriquement round-trip complet (upload littéral -> sdl3::Texture ->
	/// composite -> ReadPixels -> Convert) : RGBA8888 aux DEUX bouts corrompt
	/// silencieusement les couleurs (une texture taguée RGBA8888 puis nourrie
	/// d'octets déjà en ordre littéral [R,G,B,A] s'affiche comme totalement
	/// transparente, le premier octet — R — étant relu comme alpha) ; RGBA32
	/// aux deux bouts round-trippe exactement. Même discipline que le reste
	/// du round-trip GPU<->CPU de ce dépôt (offscreen.hpp) : ne jamais deviner
	/// un layout, toujours le vérifier par pixel-readback réel — cf. ui/
	/// shader_effect.hpp::ShaderEffectSystem pour le consommateur qui a
	/// débusqué ce piège. Primitive ReadPixels elle-même AJOUTÉE pour M23
	/// (ui::ShaderEffectSystem) : aucun wrapper de SDL_RenderReadPixels
	/// n'existait auparavant dans ce dépôt.
	///
	/// @warning Lente (lecture GPU->CPU synchrone, avertissement officiel
	/// SDL) — pas destinée à un usage par frame sur une grande cible en
	/// dehors des tests/outils et des effets shader de widget (déjà coûteux
	/// par nature, cf. shader_effect.hpp).
	[[nodiscard]] Result<Surface, StringView> ReadPixels(Option<Rect> rect = NONE) const;

	// ── Output info ──────────────────────────────────────────────────────────

	[[nodiscard]] Point OutputSize() const;
};

// Texture factory methods that need complete Renderer type

// ============================================================================
// GpuRenderState — état de rendu GPU personnalisé pour le renderer 2D
// (SDL_GPURenderState, SDL 3.4+). Permet d'utiliser des shaders SPIR-V/HLSL/
// MSL custom avec le backend "gpu" du renderer.
// ============================================================================

/// Paramètres de création (miroir de SDL_GPURenderStateCreateInfo).
struct GpuRenderStateCreateInfo {
	SDL_GPUShader *fragmentShader = nullptr;
	Sint32 numSamplerBindings = 0;
	const SDL_GPUTextureSamplerBinding *samplerBindings = nullptr;
	Sint32 numStorageTextures = 0;
	SDL_GPUTexture *const *storageTextures = nullptr;
	Sint32 numStorageBuffers = 0;
	SDL_GPUBuffer *const *storageBuffers = nullptr;
	SDL_PropertiesID props = 0;

	[[nodiscard]] SDL_GPURenderStateCreateInfo ToSDL() const {
		return {fragmentShader, numSamplerBindings, samplerBindings, numStorageTextures, storageTextures,
				numStorageBuffers, storageBuffers, props};
	}
};

class GpuRenderState : public Wrapper<SDL_GPURenderState, SDL_DestroyGPURenderState> {
public:
	using Wrapper::Wrapper;

	[[nodiscard]] static Result<GpuRenderState, Error> Create(SDL_Renderer *renderer, const GpuRenderStateCreateInfo &info);

	bool SetSamplerBindings(std::span<const SDL_GPUTextureSamplerBinding> bindings);
	bool SetStorageTextures(std::span<SDL_GPUTexture *const> textures);
	bool SetStorageBuffers(std::span<SDL_GPUBuffer *const> buffers);
	bool SetFragmentUniforms(Uint32 slot, const void *data, Uint32 length);
};

} // namespace sdl3
