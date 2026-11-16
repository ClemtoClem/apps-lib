#pragma once
/**
 * @file loadso.hpp
 * @brief Bibliothèque dynamique chargée à la volée (SDL_loadso.h) : RAII sur SDL_SharedObject.
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// SharedObject — bibliothèque dynamique chargée (RAII SDL_SharedObject)
// ============================================================================

/// Bibliothèque dynamique (DLL / .so / .dylib) chargée en mémoire (RAII).
class SharedObject : public Wrapper<SDL_SharedObject, SDL_UnloadObject> {
public:
	using Wrapper::Wrapper;

	/// Charge une bibliothèque dynamique. Retourne une erreur si le chargement échoue.
	[[nodiscard]] static Result<SharedObject, Error> Load(const char *path);

	/// Cherche l'adresse d'un symbole C exporté, réinterprété en type `Fn`.
	/// Retourne NONE si le symbole n'existe pas.
	template <typename Fn> [[nodiscard]] Option<Fn> LoadFunction(const char *name) const noexcept {
		SDL_FunctionPointer fn = SDL_LoadFunction(mHandle, name);
		if (!fn)
			return NONE;
		return Some(reinterpret_cast<Fn>(fn));
	}

	/// Récupère l'adresse brute d'un symbole (nullptr si absent).
	[[nodiscard]] SDL_FunctionPointer GetFunction(const char *name) const noexcept {
		return SDL_LoadFunction(mHandle, name);
	}
};

} // namespace sdl3
