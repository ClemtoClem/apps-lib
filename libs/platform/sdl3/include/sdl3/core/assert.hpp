#pragma once
/**
 * @file assert.hpp
 * @brief Assertions (SDL_assert.h) : point d'arrêt, rapport, handler personnalisé.
 */
#include <SDL3/SDL.h>
#include <functional>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// Assertions (SDL_assert.h)
// ============================================================================

/// Conduite à tenir quand une assertion échoue.
enum class AssertState {
	RETRY		  = SDL_ASSERTION_RETRY,		 ///< réessayer (débogueur)
	BREAK		  = SDL_ASSERTION_BREAK,		 ///< s'arrêter sur un point d'arrêt
	ABORT		  = SDL_ASSERTION_ABORT,		 ///< abandonner le programme
	IGNORE		  = SDL_ASSERTION_IGNORE,		 ///< ignorer une fois
	ALWAYS_IGNORE = SDL_ASSERTION_ALWAYS_IGNORE, ///< ignorer toujours
};

namespace assert {

/// Déclenche un point d'arrêt dans le débogueur attaché.
inline void TriggerBreakpoint() { SDL_TriggerBreakpoint(); }

/// Liste chaînée des assertions ayant échoué (mémoire gérée par SDL).
[[nodiscard]] inline const SDL_AssertData *GetReport() { return SDL_GetAssertionReport(); }

/// Efface l'historique des échecs d'assertion.
inline void ResetReport() { SDL_ResetAssertionReport(); }

/// Handler d'assertion par défaut (interne à SDL).
[[nodiscard]] inline SDL_AssertionHandler GetDefaultHandler() { return SDL_GetDefaultAssertionHandler(); }

} // namespace assert

// ============================================================================
// AssertionHandler — RAII SDL_SetAssertionHandler
// Le callback est invoqué (sous mutex, un thread à la fois) quand une
// assertion échoue ; sa valeur de retour (AssertState) indique la conduite
// à tenir.
// ============================================================================

/// Handler d'assertion personnalisé (RAII sur SDL_SetAssertionHandler).
class AssertionHandler {
public:
	/// Callback invoqué sur chaque échec d'assertion.
	using Fn = std::function<AssertState(const SDL_AssertData &)>;

private:
	struct Ctx {
		Fn fn;
	};

	static SDL_AssertState SDLCALL Trampoline(const SDL_AssertData *data, void *ud) noexcept;

	Ctx *m_ctx = nullptr;

public:
	AssertionHandler() = default;

	/// Installe le handler `fn`.
	explicit AssertionHandler(Fn fn) : m_ctx(new Ctx{std::move(fn)}) {
		SDL_SetAssertionHandler(Trampoline, m_ctx);
	}

	/// Désinstalle le handler.
	~AssertionHandler();

	AssertionHandler(const AssertionHandler &) = delete;
	AssertionHandler &operator=(const AssertionHandler &) = delete;

	/// Transfert de propriété.
	AssertionHandler(AssertionHandler &&o) noexcept : m_ctx(o.m_ctx) { o.m_ctx = nullptr; }
	AssertionHandler &operator=(AssertionHandler &&o) noexcept {
		if (this != &o) {
			if (m_ctx) {
				SDL_SetAssertionHandler(nullptr, nullptr);
				delete m_ctx;
			}
			m_ctx = o.m_ctx;
			o.m_ctx = nullptr;
		}
		return *this;
	}

	/// Vrai si un handler est installé.
	[[nodiscard]] explicit operator bool() const noexcept { return m_ctx != nullptr; }
};

} // namespace sdl3
