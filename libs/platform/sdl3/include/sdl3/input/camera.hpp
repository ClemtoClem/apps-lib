#pragma once
/**
 * @file camera.hpp
 * @brief Caméras : énumération des périphériques, ouverture et acquisition de frames (SDL_camera.h).
 */
#include <SDL3/SDL.h>
#include <vector>

#include <core/core.hpp>
#include "sdl3/render/render.hpp" // PixelFormat

namespace sdl3 {

// ============================================================================
// CameraSpec — wrap de SDL_CameraSpec
// ============================================================================

/// Format vidéo d'une caméra (miroir de `SDL_CameraSpec`).
struct CameraSpec {
	PixelFormat format = PixelFormat::UNKNOWN; ///< format de pixel
	SDL_Colorspace colorspace = SDL_COLORSPACE_UNKNOWN; ///< espace de couleur
	int width = 0, height = 0;				   ///< résolution
	int framerateNumerator = 0, framerateDenominator = 0; ///< cadence (numérateur / dénominateur)

	constexpr CameraSpec() = default;
	/// Construit un format complet.
	constexpr CameraSpec(PixelFormat fmt, int w, int h, int fpsNum = 30, int fpsDen = 1,
						 SDL_Colorspace cs = SDL_COLORSPACE_UNKNOWN) noexcept
		: format(fmt), colorspace(cs), width(w), height(h), framerateNumerator(fpsNum), framerateDenominator(fpsDen) {}
	/// Construit depuis un `SDL_CameraSpec` brut.
	constexpr explicit CameraSpec(const SDL_CameraSpec &s) noexcept
		: format(PixelFormat(s.format)), colorspace(s.colorspace), width(s.width), height(s.height),
		  framerateNumerator(s.framerate_numerator), framerateDenominator(s.framerate_denominator) {}

	/// Conversion vers le type SDL brut.
	[[nodiscard]] constexpr operator SDL_CameraSpec() const noexcept {
		return {detail::ToSDL(format), colorspace, width, height, framerateNumerator, framerateDenominator};
	}
};

// ============================================================================
// CameraID / enums
// ============================================================================

/// Identifiant d'une caméra.
using CameraID = SDL_CameraID;

/// Position physique d'une caméra (avant / arrière).
enum class CameraPosition {
	UNKNOWN = SDL_CAMERA_POSITION_UNKNOWN,			 ///< position inconnue
	FRONT_FACING = SDL_CAMERA_POSITION_FRONT_FACING, ///< face avant
	BACK_FACING = SDL_CAMERA_POSITION_BACK_FACING,	 ///< face arrière
};

/// État de l'autorisation d'accès à la caméra.
enum class CameraPermission {
	DENIED = SDL_CAMERA_PERMISSION_STATE_DENIED,	 ///< refusée
	PENDING = SDL_CAMERA_PERMISSION_STATE_PENDING,	 ///< en attente de décision
	APPROVED = SDL_CAMERA_PERMISSION_STATE_APPROVED, ///< accordée
};

// ============================================================================
// Camera drivers / device enumeration
// ============================================================================

namespace camera {

/// Nombre de pilotes de caméra disponibles.
[[nodiscard]] inline int NumDrivers() noexcept { return SDL_GetNumCameraDrivers(); }
/// Nom du pilote `index`.
[[nodiscard]] inline const char *Driver(int index) noexcept { return SDL_GetCameraDriver(index); }
/// Nom du pilote courant.
[[nodiscard]] inline const char *CurrentDriver() noexcept { return SDL_GetCurrentCameraDriver(); }

/// Identifiants des caméras actuellement connectées.
[[nodiscard]] std::vector<CameraID> Enumerated();

/// Nom lisible d'une caméra.
[[nodiscard]] inline const char *Name(CameraID id) noexcept { return SDL_GetCameraName(id); }
/// Position (avant/arrière) d'une caméra.
[[nodiscard]] inline CameraPosition Position(CameraID id) noexcept { return CameraPosition(SDL_GetCameraPosition(id)); }

/// Formats vidéo supportés par la caméra `id`.
[[nodiscard]] std::vector<CameraSpec> SupportedFormats(CameraID id);

} // namespace camera

// ============================================================================
// CameraFrame — vue RAII sur une frame acquise (libérée automatiquement)
// ============================================================================

/// Frame de caméra acquise (vue RAII, libérée automatiquement à la destruction).
class CameraFrame {
	SDL_Camera *m_camera = nullptr;
	SDL_Surface *m_surface = nullptr;
	uint64_t m_timestampNs = 0;

	void Release() noexcept;

public:
	CameraFrame() = default;
	/// Enveloppe une frame acquise (adopte la propriété temporaire).
	CameraFrame(SDL_Camera *cam, SDL_Surface *surf, uint64_t ts) noexcept
		: m_camera(cam), m_surface(surf), m_timestampNs(ts) {}
	/// Relâche la frame auprès de la caméra.
	~CameraFrame() { Release(); }

	CameraFrame(const CameraFrame &) = delete;
	CameraFrame &operator=(const CameraFrame &) = delete;

	CameraFrame(CameraFrame &&o) noexcept
		: m_camera(o.m_camera), m_surface(o.m_surface), m_timestampNs(o.m_timestampNs) {
		o.m_camera = nullptr;
		o.m_surface = nullptr;
	}
	CameraFrame &operator=(CameraFrame &&o) noexcept {
		if (this != &o) {
			Release();
			m_camera = o.m_camera;
			m_surface = o.m_surface;
			m_timestampNs = o.m_timestampNs;
			o.m_camera = nullptr;
			o.m_surface = nullptr;
		}
		return *this;
	}

	/// Vrai si la frame contient des pixels.
	[[nodiscard]] explicit operator bool() const noexcept { return m_surface != nullptr; }

	/// Largeur en pixels.
	[[nodiscard]] int GetWidth() const noexcept { return m_surface ? m_surface->w : 0; }
	/// Hauteur en pixels.
	[[nodiscard]] int GetHeight() const noexcept { return m_surface ? m_surface->h : 0; }
	/// Dimensions (largeur, hauteur).
	[[nodiscard]] Point GetSize() const noexcept { return {GetWidth(), GetHeight()}; }
	/// Pas (octets par ligne) du tampon.
	[[nodiscard]] int Pitch() const noexcept { return m_surface ? m_surface->pitch : 0; }
	/// Format de pixel de la frame.
	[[nodiscard]] PixelFormat Format() const noexcept { return m_surface ? PixelFormat(m_surface->format) : PixelFormat::UNKNOWN; }
	/// Pointeur vers les pixels (read-only).
	[[nodiscard]] const void *Pixels() const noexcept { return m_surface ? m_surface->pixels : nullptr; }
	/// Horodatage de la frame (nanosecondes).
	[[nodiscard]] uint64_t TimestampNs() const noexcept { return m_timestampNs; }
};

// ============================================================================
// Camera — RAII SDL_Camera
// ============================================================================

/// Caméra ouverte (RAII sur SDL_Camera).
class Camera : public Wrapper<SDL_Camera, SDL_CloseCamera> {
public:
	using Wrapper::Wrapper;

	/// Ouvre une caméra. `spec` peut être NONE pour laisser SDL choisir un format par défaut.
	[[nodiscard]] static Result<Camera, StringView> Open(CameraID id, Option<CameraSpec> spec = NONE);

	/// État de l'autorisation d'accès.
	[[nodiscard]] CameraPermission Permission() const noexcept;

	/// Identifiant de la caméra.
	[[nodiscard]] CameraID GetId() const noexcept { return mHandle ? SDL_GetCameraID(mHandle) : 0; }

	/// Propriétés de la caméra.
	[[nodiscard]] SDL_PropertiesID Properties() const noexcept;

	/// Format négocié (valide une fois la permission accordée) ; NONE sinon.
	[[nodiscard]] Option<CameraSpec> Format() const;

	/// Récupère la dernière frame disponible (non bloquant). La frame retournée
	/// est auto-libérée (RAII) — aucun appel de « release » manuel nécessaire.
	[[nodiscard]] Option<CameraFrame> AcquireFrame() noexcept;
};

} // namespace sdl3
