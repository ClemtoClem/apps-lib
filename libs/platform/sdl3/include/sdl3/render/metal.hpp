#pragma once
/**
 * @file metal.hpp
 * @brief Vue CAMetalLayer attachée à une fenêtre (SDL_metal.h, plateformes Apple).
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// Metal — vue CAMetalLayer sur une fenêtre (SDL_metal.h, plateformes Apple)
// ============================================================================

/// Vue CAMetalLayer (NSView/UIView) attachée à une fenêtre SDL (RAII).
class MetalView {
	SDL_MetalView m_view = nullptr;

public:
	MetalView() noexcept = default;

	/// Crée une vue Metal et l'attache à la fenêtre `window`.
	explicit MetalView(SDL_Window *window) : m_view(SDL_Metal_CreateView(window)) {}

	/// Détruit la vue Metal sous-jacente.
	~MetalView() { Reset(); }

	MetalView(const MetalView &) = delete;
	MetalView &operator=(const MetalView &) = delete;

	/// Transfert de propriété.
	MetalView(MetalView &&o) noexcept : m_view(o.m_view) { o.m_view = nullptr; }
	MetalView &operator=(MetalView &&o) noexcept {
		if (this != &o) {
			Reset();
			m_view = o.m_view;
			o.m_view = nullptr;
		}
		return *this;
	}

	/// Détruit la vue (libère le CAMetalLayer).
	void Reset() noexcept {
		if (m_view)
			SDL_Metal_DestroyView(m_view);
		m_view = nullptr;
	}

	/// Pointeur vers le CAMetalLayer sous-jacent (à caster en `id<CAMetalLayer>`).
	[[nodiscard]] void *GetLayer() const noexcept { return SDL_Metal_GetLayer(m_view); }

	/// Poignée SDL brute de la vue.
	[[nodiscard]] SDL_MetalView Get() const noexcept { return m_view; }
	/// Vrai si la vue est valide.
	[[nodiscard]] explicit operator bool() const noexcept { return m_view != nullptr; }
};

} // namespace sdl3
