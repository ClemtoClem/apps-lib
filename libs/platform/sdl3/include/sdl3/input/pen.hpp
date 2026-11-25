#pragma once
/**
 * @file pen.hpp
 * @brief Stylet : type de périphérique et axes (SDL_pen.h).
 *
 * La majorité des données de stylet (position, pression, inclinaison…)
 * arrive via les événements `SDL_EVENT_PEN_*` déjà exposés dans events.hpp
 * (Event::PenMotion(), Event::PenAxis(), Event::IsPenDown(), …).
 * Ce module ne couvre que la requête statique restante : le type de device.
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// PenDeviceType / PenAxis / PenInputFlags
// ============================================================================

/// Type de périphérique stylet (écran intégré, tablette, etc.).
enum class PenDeviceType {
	INVALID = SDL_PEN_DEVICE_TYPE_INVALID, ///< type inconnu / périphérique invalide
	UNKNOWN = SDL_PEN_DEVICE_TYPE_UNKNOWN, ///< type non déterminé
	DIRECT = SDL_PEN_DEVICE_TYPE_DIRECT,	 ///< écran tactile (le stylet pointe l'écran)
	INDIRECT = SDL_PEN_DEVICE_TYPE_INDIRECT, ///< tablette graphique externe
};

/// Axes mesurés par un stylet.
enum class PenAxis {
	PRESSURE = SDL_PEN_AXIS_PRESSURE,				 ///< pression (0..1)
	X_TILT = SDL_PEN_AXIS_XTILT,					 ///< inclinaison sur l'axe X
	Y_TILT = SDL_PEN_AXIS_YTILT,					 ///< inclinaison sur l'axe Y
	DISTANCE = SDL_PEN_AXIS_DISTANCE,				 ///< distance à l'écran
	ROTATION = SDL_PEN_AXIS_ROTATION,				 ///< rotation
	SLIDER = SDL_PEN_AXIS_SLIDER,					 ///< molette latérale
	TANGENTIAL_PRESSURE = SDL_PEN_AXIS_TANGENTIAL_PRESSURE, ///< pression tangentielle
};

/// Drapeaux d'état d'un stylet (`SDL_PenInputFlags`).
namespace penInput {
inline constexpr SDL_PenInputFlags DOWN = SDL_PEN_INPUT_DOWN;						///< le stylet touche la surface
inline constexpr SDL_PenInputFlags BUTTON1 = SDL_PEN_INPUT_BUTTON_1;				///< bouton 1 enfoncé
inline constexpr SDL_PenInputFlags BUTTON2 = SDL_PEN_INPUT_BUTTON_2;				///< bouton 2 enfoncé
inline constexpr SDL_PenInputFlags BUTTON3 = SDL_PEN_INPUT_BUTTON_3;				///< bouton 3 enfoncé
inline constexpr SDL_PenInputFlags BUTTON4 = SDL_PEN_INPUT_BUTTON_4;				///< bouton 4 enfoncé
inline constexpr SDL_PenInputFlags BUTTON5 = SDL_PEN_INPUT_BUTTON_5;				///< bouton 5 enfoncé
inline constexpr SDL_PenInputFlags ERASER_TIP = SDL_PEN_INPUT_ERASER_TIP;			///< pointe « gomme »
inline constexpr SDL_PenInputFlags IN_PROXIMITY = SDL_PEN_INPUT_IN_PROXIMITY;		///< le stylet est à portée
} // namespace pen_input

namespace pen {

/// Type de périphérique d'un stylet par son identifiant.
[[nodiscard]] inline PenDeviceType DeviceType(SDL_PenID id) noexcept { return PenDeviceType(SDL_GetPenDeviceType(id)); }

} // namespace pen

} // namespace sdl3
