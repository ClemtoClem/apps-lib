#pragma once
/**
 * @file sdl_context.hpp
 * @brief Initialisation / arrêt de SDL (SDL_init.h) : garde RAII et fonctions libres.
 */
#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <span>

#include <core/core.hpp>
#include "sdl3/core/error.hpp"
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// SdlContext — RAII SDL_Init / SDL_Quit context
// ============================================================================

/// Masque de bits des sous-systèmes SDL à initialiser.
using InitFlags = SDL_InitFlags;

/// Drapeaux d'initialisation des sous-systèmes SDL.
namespace initFlags {
inline constexpr InitFlags VIDEO = SDL_INIT_VIDEO;			///< sous-système vidéo
inline constexpr InitFlags AUDIO = SDL_INIT_AUDIO;			///< sous-système audio
inline constexpr InitFlags JOYSTICK = SDL_INIT_JOYSTICK;	///< sous-système joystick
inline constexpr InitFlags GAMEPAD = SDL_INIT_GAMEPAD;		///< sous-système manette
inline constexpr InitFlags EVENTS = SDL_INIT_EVENTS;		///< sous-système évènements
inline constexpr InitFlags SENSOR = SDL_INIT_SENSOR;		///< sous-système capteurs
inline constexpr InitFlags CAMERA = SDL_INIT_CAMERA;		///< sous-système caméra
inline constexpr InitFlags HAPTIC = SDL_INIT_HAPTIC;		///< sous-système retour de force
} // namespace init_flags

/// Garde RAII d'initialisation de SDL (appelle SDL_Quit à la destruction).
class SdlContext {
	bool m_owns = false;

public:
	SdlContext() = default;
	/// Initialise SDL avec les sous-systèmes demandés.
	explicit SdlContext(InitFlags flags) noexcept : m_owns(SDL_Init(flags)) {}

	/// Quitte SDL si le contexte en est propriétaire.
	~SdlContext();

	SdlContext(const SdlContext &) = delete;
	SdlContext &operator=(const SdlContext &) = delete;

	SdlContext(SdlContext &&o) noexcept : m_owns(o.m_owns) { o.m_owns = false; }
	SdlContext &operator=(SdlContext &&o) noexcept {
		if (this != &o) {
			if (m_owns)
				SDL_Quit();
			m_owns = o.m_owns;
			o.m_owns = false;
		}
		return *this;
	}

	/// Vrai si SDL est initialisé (et possédé par ce contexte).
	[[nodiscard]] explicit operator bool() const noexcept { return m_owns; }

	/// Initialise un sous-système supplémentaire.
	bool InitSubsystem(InitFlags flags) noexcept { return SDL_InitSubSystem(flags); }
	/// Arrête un sous-système.
	void QuitSubsystem(InitFlags flags) noexcept { SDL_QuitSubSystem(flags); }

	/// Initialise SDL et retourne une erreur explicite en cas d'échec.
	[[nodiscard]] static Result<SdlContext, Error> Create(InitFlags flags);
};

// ============================================================================
// Fonctions libres (SDL_init.h)
// ============================================================================

/// Masque des sous-systèmes actuellement initialisés (SDL_WasInit).
[[nodiscard]] inline InitFlags WasInit(InitFlags flags) noexcept { return SDL_WasInit(flags); }

/// Initialise / arrête un sous-système (SDL_InitSubSystem / SDL_QuitSubSystem).
inline bool InitSubsystem(InitFlags flags) noexcept { return SDL_InitSubSystem(flags); }
inline void QuitSubsystem(InitFlags flags) noexcept { SDL_QuitSubSystem(flags); }

/// Vrai si l'appel est fait depuis le thread principal.
[[nodiscard]] inline bool IsMainThread() noexcept { return SDL_IsMainThread(); }

/// Exécute `fn` sur le thread principal (attente optionnelle de sa complétion).
inline bool RunOnMainThread(std::function<void()> fn, bool waitComplete = true) {
	auto *heap = new std::function<void()>(std::move(fn));
	auto trampoline = [](void *ud) {
		auto *f = static_cast<std::function<void()> *>(ud);
		(*f)();
		delete f;
	};
	bool ok = SDL_RunOnMainThread(trampoline, heap, waitComplete);
	if (!ok)
		delete heap;
	return ok;
}

/// Renseigne les métadonnées de l'application (nom, version, identifiant).
inline bool SetAppMetadata(const char *name, const char *version, const char *identifier) {
	return SDL_SetAppMetadata(name, version, identifier);
}

inline bool SetAppMetadataProperty(const char *name, const char *value) {
	return SDL_SetAppMetadataProperty(name, value);
}

[[nodiscard]] inline const char *GetAppMetadataProperty(const char *name) {
	return SDL_GetAppMetadataProperty(name);
}

} // namespace sdl3