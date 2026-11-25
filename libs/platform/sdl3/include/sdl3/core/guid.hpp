#pragma once
/**
 * @file guid.hpp
 * @brief Identifiant 128 bits de périphérique (joystick, gamepad, capteur…) — wrap de SDL_GUID.
 */
#include <SDL3/SDL.h>
#include <array>
#include <cstring>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Guid — wrap de SDL_GUID (identifiant 128 bits de périphérique : joystick,
// gamepad, capteur...)
// ============================================================================

/// Identifiant 128 bits stable d'un périphérique (miroir de `SDL_GUID`).
struct Guid {
	std::array<uint8_t, 16> data{}; ///< octets de l'identifiant

	constexpr Guid() = default;
	/// Copie les 16 octets d'un `SDL_GUID`.
	explicit Guid(const SDL_GUID &g) noexcept { std::memcpy(data.data(), g.data, 16); }

	/// Conversion vers le type SDL brut.
	[[nodiscard]] operator SDL_GUID() const noexcept {
		SDL_GUID g{};
		std::memcpy(g.data, data.data(), 16);
		return g;
	}

	/// Représentation texte (format `xxxxxxxx-xxxx-...`).
	[[nodiscard]] String ToString() const;

	/// Construit depuis la représentation texte (zéro si invalide).
	[[nodiscard]] static Guid FromString(const String &s) noexcept { return Guid(SDL_StringToGUID(s.c_str())); }

	/// Égalité octet à octet.
	[[nodiscard]] bool operator==(const Guid &o) const noexcept { return data == o.data; }
};

} // namespace sdl3
