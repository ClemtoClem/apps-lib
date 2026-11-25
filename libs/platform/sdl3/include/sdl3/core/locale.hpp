#pragma once
/**
 * @file locale.hpp
 * @brief Localisation utilisateur : langue et pays préférés du système (SDL_locale.h).
 */
#include <SDL3/SDL.h>
#include <vector>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Locale — wrap de SDL_Locale
// ============================================================================

/// Une locale (langue + pays), miroir de `SDL_Locale`.
struct Locale {
	String language;			   ///< code de langue ISO-639 (ex: "en")
	Option<String> country = NONE; ///< code de pays ISO-3166 (ex: "US") — NONE si non précisé par l'OS

	Locale() = default;
	/// Construit depuis une `SDL_Locale` brute.
	explicit Locale(const SDL_Locale &l)
		: language(l.language ? l.language : ""), country(l.country ? Option<String>(Some(String(l.country))) : NONE) {}
};

namespace locale {

/// Liste des locales préférées de l'utilisateur, par ordre de préférence décroissante.
[[nodiscard]] std::vector<Locale> Preferred();

} // namespace locale

} // namespace sdl3
