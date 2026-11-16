#pragma once
/**
 * @file vulkan.hpp
 * @brief Vulkan : chargement du loader et création de surfaces (SDL_vulkan.h).
 */
#include <SDL3/SDL_vulkan.h>
#include <span>

#include <core/core.hpp>
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// VkLibrary — RAII Vulkan loader library (SDL_Vulkan_LoadLibrary / UnloadLibrary)
// ============================================================================

/// Loader Vulkan chargé (RAII sur SDL_Vulkan_LoadLibrary / SDL_Vulkan_UnloadLibrary).
class VkLibrary {
private:
	bool m_loaded = false;

public:
	constexpr VkLibrary() noexcept = default;
	/// Construit depuis un état de chargement connu (utilisé par VkLibrary::Load).
	explicit VkLibrary(bool loaded) noexcept : m_loaded(loaded) {}
	/// Décharge le loader Vulkan.
	~VkLibrary();

	VkLibrary(const VkLibrary &) = delete;
	VkLibrary &operator=(const VkLibrary &) = delete;
	VkLibrary(VkLibrary &&o) noexcept : m_loaded(o.m_loaded) { o.m_loaded = false; }
	VkLibrary &operator=(VkLibrary &&o) noexcept {
		if (this != &o) {
			if (m_loaded)
				SDL_Vulkan_UnloadLibrary();
			m_loaded = o.m_loaded;
			o.m_loaded = false;
		}
		return *this;
	}

	/// Vrai si le loader est chargé.
	[[nodiscard]] explicit operator bool() const noexcept { return m_loaded; }

	/// Charge le loader Vulkan. Passer nullptr pour utiliser celui du système.
	[[nodiscard]] static Result<VkLibrary, StringView> Load(const char *path = nullptr);
};

// ============================================================================
// Vulkan free functions — namespace vulkan
// ============================================================================

namespace vulkan {

// ── Instance proc address ─────────────────────────────────────────────────────

/// Retourne `vkGetInstanceProcAddr` comme pointeur de fonction opaque.
[[nodiscard]] SDL_FunctionPointer GetInstanceProcAddr() noexcept;

// ── Instance extensions ───────────────────────────────────────────────────────

/// Extensions d'instance Vulkan requises par SDL sur cette plateforme.
/// Le span retourné est valide jusqu'au prochain appel à
/// SDL_Vulkan_GetInstanceExtensions ou jusqu'à l'arrêt de SDL.
[[nodiscard]] std::span<const char *const> InstanceExtensions() noexcept;

// ── Surface creation / destruction ────────────────────────────────────────────

/// Crée une surface Vulkan pour la fenêtre `win`.
/// `allocator` peut être nullptr pour utiliser l'allocateur par défaut.
[[nodiscard]] Result<VkSurfaceKHR, StringView> CreateSurface(SDL_Window *win, VkInstance instance,
															 const VkAllocationCallbacks *allocator = nullptr);

/// Détruit une surface créée avec CreateSurface().
/// Doit être appelé avant SDL_DestroyWindow pour la fenêtre associée.
void DestroySurface(VkInstance instance, VkSurfaceKHR surface,
					const VkAllocationCallbacks *allocator = nullptr) noexcept;

// ── Presentation support ──────────────────────────────────────────────────────

/// Vrai si le couple (périphérique physique, famille de file) supporte la présentation.
[[nodiscard]] bool PresentationSupported(VkInstance instance, VkPhysicalDevice physicalDevice,
										 uint32_t queueFamilyIndex) noexcept;

} // namespace vulkan
} // namespace sdl3
