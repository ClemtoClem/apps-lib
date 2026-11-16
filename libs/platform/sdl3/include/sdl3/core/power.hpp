#pragma once
/**
 * @file power.hpp
 * @brief État de l'alimentation : secteur / batterie, secondes et pourcentage restants (SDL_power.h).
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>
#include "sdl3/input/joystick.hpp" // PowerState
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// PowerStatus — wrap du triplet (SDL_PowerState, secondes restantes, %
// restant) retourné par SDL_GetPowerInfo. Réutilise `PowerState` (déjà défini
// dans joystick.hpp pour SDL_GetJoystickPowerInfo — même énum SDL).
// ============================================================================

/// État d'alimentation d'un appareil : source, secondes restantes, pourcentage restant.
struct PowerStatus {
	PowerState state = PowerState::UNKNOWN; ///< source d'alimentation courante
	Option<int> secondsLeft = NONE;			///< secondes restantes — NONE si inconnu (ex: secteur sans batterie)
	Option<int> percent = NONE;				///< pourcentage restant — NONE si inconnu
};

namespace power {

/// Interroge l'état d'alimentation courant.
///
/// Ne jamais prendre ces valeurs pour une vérité absolue : ce sont des
/// estimations remontées par le matériel (cf. doc SDL_GetPowerInfo).
[[nodiscard]] PowerStatus Info() noexcept;

} // namespace power

} // namespace sdl3
