#pragma once
/**
 * @file gl.hpp
 * @brief OpenGL : contexte attaché à une fenêtre et fonctions d'aide (SDL_video.h / SDL_opengl.h).
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// GlContext — RAII SDL_GLContext (contexte OpenGL lié à une fenêtre)
// ============================================================================

/// Contexte OpenGL attaché à une fenêtre (RAII sur SDL_GLContext).
class GlContext {
	SDL_GLContext m_handle = nullptr;

public:
	constexpr GlContext() noexcept = default;
	/// Enveloppe un contexte OpenGL existant (adopte la propriété).
	explicit GlContext(SDL_GLContext h) noexcept : m_handle(h) {}
	/// Détruit le contexte OpenGL.
	~GlContext();

	GlContext(const GlContext &) = delete;
	GlContext &operator=(const GlContext &) = delete;
	GlContext(GlContext &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
	GlContext &operator=(GlContext &&o) noexcept {
		if (this != &o) {
			if (m_handle)
				SDL_GL_DestroyContext(m_handle);
			m_handle = o.m_handle;
			o.m_handle = nullptr;
		}
		return *this;
	}

	/// Poignée SDL brute du contexte.
	[[nodiscard]] SDL_GLContext Get() const noexcept { return m_handle; }
	/// Vrai si le contexte est valide.
	[[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

	// ── Factory ──────────────────────────────────────────────────────────────

	/// Crée un contexte OpenGL pour la fenêtre `win`.
	[[nodiscard]] static Result<GlContext, Error> Create(SDL_Window *win);

	// ── Context management ───────────────────────────────────────────────────

	/// Rend ce contexte courant pour la fenêtre `win`.
	bool MakeCurrent(SDL_Window *win) noexcept { return m_handle && SDL_GL_MakeCurrent(win, m_handle); }

	/// Fenêtre OpenGL courante (non possédée), ou nullptr.
	[[nodiscard]] static SDL_Window *CurrentWindow() noexcept { return SDL_GL_GetCurrentWindow(); }
	/// Contexte OpenGL courant (non possédé), ou nullptr.
	[[nodiscard]] static SDL_GLContext CurrentContext() noexcept { return SDL_GL_GetCurrentContext(); }
};

// ============================================================================
// Fonctions libres OpenGL — wrappers des aides SDL_GL_* et SDL_EGL_*
// ============================================================================

namespace gl {

// ── Library loading ───────────────────────────────────────────────────────────

/// Charge la bibliothèque OpenGL (nullptr = celle du système).
inline bool LoadLibrary(const char *path = nullptr) noexcept { return SDL_GL_LoadLibrary(path); }
/// Décharge la bibliothèque OpenGL.
inline void UnloadLibrary() noexcept { SDL_GL_UnloadLibrary(); }

// ── Proc address ──────────────────────────────────────────────────────────────

/// Adresse d'une fonction OpenGL.
[[nodiscard]] inline SDL_FunctionPointer ProcAddress(const char *proc) noexcept { return SDL_GL_GetProcAddress(proc); }
/// Adresse d'une fonction EGL.
[[nodiscard]] SDL_FunctionPointer EglProcAddress(const char *proc) noexcept;

// ── Extension query ───────────────────────────────────────────────────────────

/// Vrai si l'extension OpenGL `ext` est supportée.
[[nodiscard]] inline bool ExtensionSupported(const char *ext) noexcept { return SDL_GL_ExtensionSupported(ext); }

// ── Attributes ───────────────────────────────────────────────────────────────

/// Fixe un attribut du futur contexte OpenGL.
inline bool SetAttribute(SDL_GLAttr attr, int value) noexcept { return SDL_GL_SetAttribute(attr, value); }
/// Lit un attribut OpenGL ; NONE s'il n'est pas défini.
Option<int> GetAttribute(SDL_GLAttr attr) noexcept;
/// Réinitialise les attributs OpenGL.
inline void ResetAttributes() noexcept { SDL_GL_ResetAttributes(); }

// ── Swap interval ─────────────────────────────────────────────────────────────

/// Fixe l'intervalle d'échange (vsync).
inline bool SetSwapInterval(int interval) noexcept { return SDL_GL_SetSwapInterval(interval); }
/// Lit l'intervalle d'échange ; NONE s'il ne peut pas être lu.
Option<int> GetSwapInterval() noexcept;

// ── Swap ──────────────────────────────────────────────────────────────────────

/// Échange les tampons de la fenêtre OpenGL.
inline bool SwapWindow(SDL_Window *win) noexcept { return SDL_GL_SwapWindow(win); }

// ── EGL helpers ───────────────────────────────────────────────────────────────

/// Display EGL courant.
[[nodiscard]] inline SDL_EGLDisplay EglDisplay() noexcept { return SDL_EGL_GetCurrentDisplay(); }
/// Config EGL courante.
[[nodiscard]] inline SDL_EGLConfig EglConfig() noexcept { return SDL_EGL_GetCurrentConfig(); }
/// Surface EGL d'une fenêtre.
[[nodiscard]] inline SDL_EGLSurface EglWindowSurface(SDL_Window *win) noexcept { return SDL_EGL_GetWindowSurface(win); }

} // namespace gl
} // namespace sdl3
