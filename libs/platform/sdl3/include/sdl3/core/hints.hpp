#pragma once
/**
 * @file hints.hpp
 * @brief Hints de configuration (SDL_hints.h) : lecture/écriture et observation.
 */
#include <SDL3/SDL.h>
#include <functional>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// HintPriority
// ============================================================================

/// Priorité d'un hint (ordre de résolution des valeurs).
enum class HintPriority {
	DEFAULT  = SDL_HINT_DEFAULT,  ///< priorité par défaut
	NORMAL   = SDL_HINT_NORMAL,	  ///< normale
	OVERRIDE = SDL_HINT_OVERRIDE, ///< prioritaire sur les autres
};

// ============================================================================
// Fonctions libres (SDL_hints.h)
// ============================================================================

namespace hints {

/// Fixe un hint avec la priorité normale.
inline bool Set(const char *name, const char *value) { return SDL_SetHint(name, value); }
inline bool Set(StringView name, StringView value) { return SDL_SetHint(name.GetData(), value.GetData()); }

/// Fixe un hint avec une priorité explicite.
inline bool SetWithPriority(const char *name, const char *value, HintPriority priority) {
	return SDL_SetHintWithPriority(name, value, static_cast<SDL_HintPriority>(priority));
}

/// Réinitialise un hint à sa valeur par défaut (ou NULL).
inline bool Reset(const char *name) { return SDL_ResetHint(name); }

/// Réinitialise tous les hints.
inline void ResetAll() { SDL_ResetHints(); }

/// Lit la valeur d'un hint, ou NULL s'il n'est pas défini.
[[nodiscard]] inline const char *Get(const char *name) { return SDL_GetHint(name); }

/// Lit la valeur booléenne d'un hint.
[[nodiscard]] inline bool GetBool(const char *name, bool defaultValue = false) {
	return SDL_GetHintBoolean(name, defaultValue);
}

} // namespace hints

// ============================================================================
// HintCallback — RAII SDL_AddHintCallback / SDL_RemoveHintCallback
// Le callback est invoqué une première fois lors de l'enregistrement, avec la
// valeur courante du hint dans `oldValue` et `newValue`, puis à chaque
// changement de valeur.
// ============================================================================

class HintCallback {
public:
	/// Callback invoqué à chaque changement de valeur d'un hint.
	using Fn = std::function<void(StringView name, StringView oldValue, StringView newValue)>;

private:
	struct Ctx {
		Fn fn;
		const char *name = nullptr;
	};

	static void SDLCALL Trampoline(void *ud, const char *name, const char *oldValue, const char *newValue) noexcept;

	Ctx *m_ctx = nullptr;

public:
	HintCallback() = default;

	/// Observe le hint `name` ; `fn` reçoit (nom, ancienne valeur, nouvelle valeur).
	HintCallback(const char *name, Fn fn) : m_ctx(new Ctx{std::move(fn), name}) {
		SDL_AddHintCallback(name, Trampoline, m_ctx);
	}

	/// Retire l'observation.
	~HintCallback();

	HintCallback(const HintCallback &) = delete;
	HintCallback &operator=(const HintCallback &) = delete;

	/// Transfert de propriété.
	HintCallback(HintCallback &&o) noexcept : m_ctx(o.m_ctx) { o.m_ctx = nullptr; }
	HintCallback &operator=(HintCallback &&o) noexcept {
		if (this != &o) {
			if (m_ctx) {
				SDL_RemoveHintCallback(m_ctx->name, Trampoline, m_ctx);
				delete m_ctx;
			}
			m_ctx = o.m_ctx;
			o.m_ctx = nullptr;
		}
		return *this;
	}

	/// Vrai si un callback est installé.
	[[nodiscard]] explicit operator bool() const noexcept { return m_ctx != nullptr; }
};

} // namespace sdl3
