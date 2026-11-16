#pragma once
/**
 * @file error.hpp
 * @brief Gestion des erreurs SDL (SDL_error.h) : pose, lecture, effacement.
 */
#include <core/core.hpp>
#include <cstdarg>
#include <SDL3/SDL.h>

namespace sdl3 {

/// Pose un message d'erreur (chaîne simple).
inline bool SetError(StringView msg) { return SDL_SetError("%.*s", int(msg.GetSize()), msg.GetData()) == 0; }

/// Pose un message d'erreur au format printf (le format n'est pas littéral ici).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
template <class... Args> inline bool SetError(StringView fmt, Args &&...args) {
	return SDL_SetError(fmt.GetData(), std::forward<Args>(args)...) == 0;
}
#pragma GCC diagnostic pop

/// Lit le dernier message d'erreur SDL.
inline StringView GetError() { return StringView(SDL_GetError()); }

/// Efface le dernier message d'erreur SDL.
inline bool ClearError() { return SDL_ClearError(); }

/// Vrai si la dernière erreur est « mémoire insuffisante ».
inline bool ErrorIsOutOfMemory() { return SDL_OutOfMemory(); }

/// Signale un paramètre invalide (équivaut à SDL_SetError("Parameter '%s' is invalid", param)).
inline bool InvalidParamError(const char *param) { return SDL_InvalidParamError(param); }

/// Marque une fonctionnalité comme non supportée par la plateforme.
inline bool Unsupported() { return SDL_Unsupported(); }

/// Variante va_list de SetError (pour les wrappers à arguments variadiques).
inline bool SetErrorV(const char *fmt, va_list ap) { return SDL_SetErrorV(fmt, ap); }

/// Convertit un booléen de succès en `Result<bool, Error>`.
Result<bool, Error> CheckError(bool result);

/// Convertit un résultat (pointeur/handle) en `Result<T, Error>` : Ok si non-nul.
template <class T> inline Result<T, Error> CheckError(T result) {
	if (result) return Ok(result);
	return Err(GetError());
}

/// Convertit un résultat en `Result<T, Error>` : Ok si `result != invalidValue`.
template <class T> inline Result<T, Error> CheckError(T result, T invalidValue) {
	if (result == invalidValue) return Ok(result);
	return Err(GetError());
}

/// Convertit un résultat en `Result<T, Error>` : Ok si `result != validValue`.
template <class T> inline Result<T, Error> CheckErrorIfNot(T result, T validValue) {
	if (result != validValue) return Ok(result);
	return Err(GetError());
}

} /* namespace sdl3 */
