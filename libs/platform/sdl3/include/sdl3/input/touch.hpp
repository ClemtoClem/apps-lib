#pragma once
/**
 * @file touch.hpp
 * @brief Écrans tactiles : type de périphérique, doigts actifs (SDL_touch.h).
 */
#include <SDL3/SDL.h>
#include <vector>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// TouchDeviceType
// ============================================================================

/// Nature d'un périphérique tactile (écran, pavé, etc.).
enum class TouchDeviceType {
	INVALID = SDL_TOUCH_DEVICE_INVALID,						///< périphérique invalide
	DIRECT = SDL_TOUCH_DEVICE_DIRECT,						///< écran tactile (le doigt pointe l'écran)
	INDIRECT_ABSOLUTE = SDL_TOUCH_DEVICE_INDIRECT_ABSOLUTE, ///< pavé tactile absolu
	INDIRECT_RELATIVE = SDL_TOUCH_DEVICE_INDIRECT_RELATIVE, ///< pavé tactile relatif
};

namespace detail {
	constexpr SDL_TouchDeviceType ToSDL(TouchDeviceType e) {
		return static_cast<SDL_TouchDeviceType>(e);
	}
}

// ============================================================================
// Finger — wrap de SDL_Finger
// ============================================================================

/// Doigt posé sur un écran tactile (coordonnées normalisées).
struct Finger {
	SDL_FingerID id = 0;	   ///< identifiant du doigt
	float x = 0.f, y = 0.f;	   ///< position, normalisée [0, 1]
	float pressure = 0.f;	   ///< pression, normalisée [0, 1]

	Finger() = default;
	/// Construit depuis un `SDL_Finger` brut.
	constexpr explicit Finger(const SDL_Finger &f) noexcept : id(f.id), x(f.x), y(f.y), pressure(f.pressure) {}
};

// ============================================================================
// touch — énumération des périphériques tactiles et de leurs doigts actifs
// ============================================================================

namespace touch {

/// Identifiants des périphériques tactiles connectés.
[[nodiscard]] std::vector<SDL_TouchID> Devices();

/// Nom lisible d'un périphérique tactile.
[[nodiscard]] inline const char *DeviceName(SDL_TouchID id) noexcept { return SDL_GetTouchDeviceName(id); }

/// Nature (direct/indirect) d'un périphérique tactile.
[[nodiscard]] TouchDeviceType DeviceType(SDL_TouchID id) noexcept;

/// Doigts actuellement posés sur le périphérique `id`.
[[nodiscard]] std::vector<Finger> Fingers(SDL_TouchID id);

} // namespace touch

} // namespace sdl3
