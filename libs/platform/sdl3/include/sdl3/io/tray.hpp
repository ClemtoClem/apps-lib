#pragma once
/**
 * @file tray.hpp
 * @brief Icône de notification système et arbre de menus (SDL_tray.h).
 */
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include <core/core.hpp>
#include "sdl3/render/render.hpp" // Surface

namespace sdl3 {

// ============================================================================
// TrayEntry flags
// ============================================================================

/// Drapeaux décrivant une entrée de menu du tray (type requis + options).
enum class TrayEntryFlags: Uint32 {
	BUTTON 		= SDL_TRAYENTRY_BUTTON,		///< bouton (requis)
	CHECKBOX 	= SDL_TRAYENTRY_CHECKBOX,	///< case à cocher (requis)
	SUBMENU 	= SDL_TRAYENTRY_SUBMENU,	///< sous-menu (requis)
	DISABLED 	= SDL_TRAYENTRY_DISABLED,	///< désactivé (optionnel)
	CHECKED 	= SDL_TRAYENTRY_CHECKED		///< coché (optionnel, checkbox)
};

/// `TrayEntryFlags` est un jeu de DRAPEAUX (un type d'entrée requis, plus des
/// options facultatives) : sans ces opérateurs, une `enum class` ne se combine
/// pas, et l'appelant devait retomber sur le type C brut `SDL_TrayEntryFlags`
/// — exactement ce que ce wrapper s'interdit (cf.
/// memory/feedback_wrap_c_pointers.md). Manque relevé parce que
/// tests/sdl3_smoke_test_8.cpp ne compilait plus depuis le passage de ces
/// constantes à une `enum class`.
[[nodiscard]] constexpr TrayEntryFlags operator|(TrayEntryFlags a, TrayEntryFlags b) noexcept {
	return static_cast<TrayEntryFlags>(static_cast<Uint32>(a) | static_cast<Uint32>(b));
}
[[nodiscard]] constexpr TrayEntryFlags operator&(TrayEntryFlags a, TrayEntryFlags b) noexcept {
	return static_cast<TrayEntryFlags>(static_cast<Uint32>(a) & static_cast<Uint32>(b));
}
constexpr TrayEntryFlags &operator|=(TrayEntryFlags &a, TrayEntryFlags b) noexcept { return a = a | b; }
/// Vrai si `flags` contient AU MOINS un des bits de `wanted`.
[[nodiscard]] constexpr bool HasFlag(TrayEntryFlags flags, TrayEntryFlags wanted) noexcept {
	return static_cast<Uint32>(flags & wanted) != 0u;
}

namespace detail {
	constexpr SDL_TrayEntryFlags ToSDL(TrayEntryFlags e) {
		return static_cast<SDL_TrayEntryFlags>(e);
	}
}

class TrayMenu;

// ============================================================================
// TrayEntry — vue non-possédante sur une entrée de menu (le Tray possède
// l'arbre entier ; les entrées/menus sont détruits avec lui).
// ============================================================================

class TrayEntry : public Borrowed<SDL_TrayEntry> {
public:
	/// Callback invoqué quand l'entrée est activée.
	using Callback = std::function<void(TrayEntry)>;

	using Borrowed::Borrowed;

	/// Change le libellé de l'entrée.
	void SetLabel(const String &label) noexcept;
	/// Libellé courant de l'entrée.
	[[nodiscard]] const char *Label() const noexcept { return mHandle ? SDL_GetTrayEntryLabel(mHandle) : ""; }

	/// Coche / décoche l'entrée (checkbox).
	void SetChecked(bool checked) noexcept;
	/// État coché courant.
	[[nodiscard]] bool Checked() const noexcept { return mHandle && SDL_GetTrayEntryChecked(mHandle); }

	/// Active / désactive l'entrée.
	void SetEnabled(bool enabled) noexcept;
	/// État activé courant.
	[[nodiscard]] bool Enabled() const noexcept { return mHandle && SDL_GetTrayEntryEnabled(mHandle); }

	/// Simule un clic sur l'entrée.
	void Click() noexcept;

	/// Retire l'entrée du menu (la vue devient invalide après l'appel).
	void Remove() noexcept;

	/// Crée un sous-menu. Nécessite que l'entrée ait été créée avec le flag `SUBMENU`.
	[[nodiscard]] TrayMenu CreateSubmenu();
	/// Sous-menu de l'entrée (NONE si ce n'est pas un sous-menu).
	[[nodiscard]] Option<TrayMenu> Submenu() const;
	/// Menu parent de l'entrée (NONE pour une entrée de la racine).
	[[nodiscard]] Option<TrayMenu> Parent() const;

	/// Installe le callback d'activation.
	/// Le contexte du callback est heap-alloué et vit aussi longtemps que
	/// l'entrée (détruite avec le Tray) ; un nouvel appel remplace l'ancien
	/// sans le libérer (limitation SDL — pas de callback de nettoyage fourni).
	void SetCallback(Callback cb);
};

// ============================================================================
// TrayMenu — vue non-possédante sur un menu (racine du Tray, ou sous-menu
// d'une TrayEntry)
// ============================================================================

class TrayMenu : public Borrowed<SDL_TrayMenu> {
public:
	using Borrowed::Borrowed;

	/// Entrées du menu.
	[[nodiscard]] std::vector<TrayEntry> Entries() const;

	/// Insère une entrée à la position `pos`. `pos` négatif insère en fin de menu.
	[[nodiscard]] TrayEntry InsertAt(int pos, const String &label, TrayEntryFlags flags);
	/// Ajoute une entrée en fin de menu.
	[[nodiscard]] TrayEntry Append(const String &label, TrayEntryFlags flags) { return InsertAt(-1, label, flags); }

	/// Entrée parente (NONE pour le menu racine).
	[[nodiscard]] Option<TrayEntry> ParentEntry() const;
};

inline TrayMenu TrayEntry::CreateSubmenu() { return TrayMenu(mHandle ? SDL_CreateTraySubmenu(mHandle) : nullptr); }



// ============================================================================
// Tray — RAII SDL_Tray (icône système + arbre de menus)
// ============================================================================

/// Icône de notification système (RAII sur SDL_Tray).
class Tray : public Wrapper<SDL_Tray, SDL_DestroyTray> {
public:
	using Wrapper::Wrapper;

	/// Crée une icône avec une surface d'icône et une infobulle.
	[[nodiscard]] static Result<Tray, Error> Create(const Surface &icon, const String &tooltip);

	/// Crée une icône sans surface (certaines plateformes l'acceptent).
	[[nodiscard]] static Result<Tray, StringView> CreateWithoutIcon(const String &tooltip);

	/// Remplace l'icône.
	void SetIcon(const Surface &icon) noexcept;
	/// Remplace l'infobulle.
	void SetTooltip(const String &tooltip) noexcept;

	/// Crée le menu racine.
	[[nodiscard]] TrayMenu CreateMenu() { return TrayMenu(mHandle ? SDL_CreateTrayMenu(mHandle) : nullptr); }

	/// Menu racine (NONE s'il n'a pas été créé).
	[[nodiscard]] Option<TrayMenu> Menu() const;
};

namespace tray {
/// Met à jour les trays. À appeler périodiquement sur les plateformes qui
/// n'intègrent pas le tray dans la boucle d'événements SDL.
inline void Update() noexcept { SDL_UpdateTrays(); }
} // namespace tray

} // namespace sdl3
