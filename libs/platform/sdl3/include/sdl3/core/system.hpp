#pragma once
/**
 * @file system.hpp
 * @brief Intégration plateforme (SDL_system.h) : facteur de forme, sandbox, spécificités OS.
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// Intégration plateforme (SDL_system.h)
// ============================================================================

/// Facteur de forme de l'appareil courant (téléphone, tablette, console…).
enum class FormFactor {
	UNKNOWN  = SDL_FORMFACTOR_UNKNOWN,	///< inconnu
	DESKTOP  = SDL_FORMFACTOR_DESKTOP,	///< ordinateur de bureau
	LAPTOP   = SDL_FORMFACTOR_LAPTOP,	///< portable
	PHONE    = SDL_FORMFACTOR_PHONE,	///< téléphone
	TABLET   = SDL_FORMFACTOR_TABLET,	///< tablette
	CONSOLE  = SDL_FORMFACTOR_CONSOLE,	///< console
	HANDHELD = SDL_FORMFACTOR_HANDHELD, ///< console portable
	WATCH    = SDL_FORMFACTOR_WATCH,	///< montre
	TV       = SDL_FORMFACTOR_TV,		///< téléviseur
	HEADSET  = SDL_FORMFACTOR_HEADSET,	///< casque
	CAR      = SDL_FORMFACTOR_CAR,		///< véhicule
};

/// Environnement sandbox dans lequel tourne le processus.
enum class Sandbox {
	NONE			  = SDL_SANDBOX_NONE,				///< aucun
	UNKNOWN_CONTAINER = SDL_SANDBOX_UNKNOWN_CONTAINER,	///< conteneur inconnu
	FLATPAK			  = SDL_SANDBOX_FLATPAK,			///< Flatpak
	SNAP			  = SDL_SANDBOX_SNAP,				///< Snap
	MACOS			  = SDL_SANDBOX_MACOS,				///< sandbox macOS
	LOMIRI			  = SDL_SANDBOX_LOMIRI,				///< Lomiri
};

namespace system {

// ── Facteur de forme de l'appareil courant ───────────────────────────────────

/// Facteur de forme de l'appareil courant.
[[nodiscard]] inline FormFactor GetDeviceFormFactor() noexcept {
	return static_cast<FormFactor>(SDL_GetDeviceFormFactor());
}

/// Nom lisible d'un facteur de forme.
[[nodiscard]] inline const char *GetDeviceFormFactorName(FormFactor formFactor) noexcept {
	return SDL_GetDeviceFormFactorName(static_cast<SDL_FormFactor>(formFactor));
}

/// Vrai si l'appareil est un téléphone.
[[nodiscard]] inline bool IsPhone() noexcept { return SDL_IsPhone(); }
/// Vrai si l'appareil est une tablette.
[[nodiscard]] inline bool IsTablet() noexcept { return SDL_IsTablet(); }
/// Vrai si l'appareil est un téléviseur.
[[nodiscard]] inline bool IsTV() noexcept { return SDL_IsTV(); }

// ── Environnement sandbox ────────────────────────────────────────────────────

/// Environnement sandbox courant.
[[nodiscard]] inline Sandbox GetSandbox() noexcept {
	return static_cast<Sandbox>(SDL_GetSandbox());
}

// ── Détections matérielles / d'exécution ─────────────────────────────────────

/// Vrai sur Ubuntu Touch.
[[nodiscard]] inline bool IsUbuntuTouch() noexcept { return SDL_IsUbuntuTouch(); }

// ── Cycle de vie applicatif (délégués iOS/Android) ───────────────────────────

/// Signale que l'application va être terminée.
inline void OnApplicationWillTerminate() { SDL_OnApplicationWillTerminate(); }
/// Signale une alerte mémoire système.
inline void OnApplicationDidReceiveMemoryWarning() { SDL_OnApplicationDidReceiveMemoryWarning(); }
/// Signale le passage en arrière-plan.
inline void OnApplicationWillEnterBackground() { SDL_OnApplicationWillEnterBackground(); }
/// Signale l'entrée en arrière-plan.
inline void OnApplicationDidEnterBackground() { SDL_OnApplicationDidEnterBackground(); }
/// Signale le retour imminent au premier plan.
inline void OnApplicationWillEnterForeground() { SDL_OnApplicationWillEnterForeground(); }
/// Signale le retour au premier plan.
inline void OnApplicationDidEnterForeground() { SDL_OnApplicationDidEnterForeground(); }

} // namespace system

// ============================================================================
// Fonctions spécifiques à Linux (priorité de thread)
// ============================================================================

#ifdef SDL_PLATFORM_LINUX
namespace system {
/// Fixe la priorité (nice) d'un thread Linux.
[[nodiscard]] inline bool SetLinuxThreadPriority(Sint64 threadID, int priority) {
	return SDL_SetLinuxThreadPriority(threadID, priority);
}
/// Fixe la priorité et la politique d'ordonnancement d'un thread Linux.
[[nodiscard]] inline bool SetLinuxThreadPriorityAndPolicy(Sint64 threadID, int sdlPriority, int schedPolicy) {
	return SDL_SetLinuxThreadPriorityAndPolicy(threadID, sdlPriority, schedPolicy);
}
} // namespace system
#endif // SDL_PLATFORM_LINUX

// ============================================================================
// Fonctions spécifiques à Android
// ============================================================================

#ifdef SDL_PLATFORM_ANDROID
namespace system {

/// Version du SDK Android.
[[nodiscard]] inline int GetAndroidSDKVersion() noexcept { return SDL_GetAndroidSDKVersion(); }
/// Vrai sur un Chromebook.
[[nodiscard]] inline bool IsChromebook() noexcept { return SDL_IsChromebook(); }
/// Vrai en mode DeX (Samsung).
[[nodiscard]] inline bool IsDeXMode() noexcept { return SDL_IsDeXMode(); }
/// Environnement JNI.
[[nodiscard]] inline void *GetAndroidJNIEnv() noexcept { return SDL_GetAndroidJNIEnv(); }
/// Activité Android courante.
[[nodiscard]] inline void *GetAndroidActivity() noexcept { return SDL_GetAndroidActivity(); }
/// Chemin de stockage interne.
[[nodiscard]] inline const char *GetAndroidInternalStoragePath() noexcept { return SDL_GetAndroidInternalStoragePath(); }
/// État du stockage externe.
[[nodiscard]] inline Uint32 GetAndroidExternalStorageState() noexcept { return SDL_GetAndroidExternalStorageState(); }
/// Chemin de stockage externe.
[[nodiscard]] inline const char *GetAndroidExternalStoragePath() noexcept { return SDL_GetAndroidExternalStoragePath(); }
/// Chemin du cache.
[[nodiscard]] inline const char *GetAndroidCachePath() noexcept { return SDL_GetAndroidCachePath(); }

/// Simule l'appui sur le bouton retour Android.
inline void SendAndroidBackButton() { SDL_SendAndroidBackButton(); }

/// Demande une autorisation Android (résultat via callback).
[[nodiscard]] inline bool RequestAndroidPermission(
	const char *permission, SDL_RequestAndroidPermissionCallback cb, void *userdata) {
	return SDL_RequestAndroidPermission(permission, cb, userdata);
}

/// Affiche un « toast » Android.
[[nodiscard]] inline bool ShowAndroidToast(const char *message, int duration, int gravity, int xoffset, int yoffset) {
	return SDL_ShowAndroidToast(message, duration, gravity, xoffset, yoffset);
}

/// Envoie un message au thread principal Android.
[[nodiscard]] inline bool SendAndroidMessage(Uint32 command, int param) {
	return SDL_SendAndroidMessage(command, param);
}

} // namespace system
#endif // SDL_PLATFORM_ANDROID

// ============================================================================
// Fonctions spécifiques à Windows
// ============================================================================

#ifdef SDL_PLATFORM_WINDOWS
namespace system {
/// Installe un hook de messages Windows.
inline void SetWindowsMessageHook(SDL_WindowsMessageHook callback, void *userdata) {
	SDL_SetWindowsMessageHook(callback, userdata);
}
/// Index de l'adaptateur Direct3D 9 d'un écran.
[[nodiscard]] inline int GetDirect3D9AdapterIndex(SDL_DisplayID displayID) {
	return SDL_GetDirect3D9AdapterIndex(displayID);
}
/// Informations de sortie DXGI d'un écran.
[[nodiscard]] inline bool GetDXGIOutputInfo(SDL_DisplayID displayID, int *adapterIndex, int *outputIndex) {
	return SDL_GetDXGIOutputInfo(displayID, adapterIndex, outputIndex);
}
} // namespace system
#endif // SDL_PLATFORM_WINDOWS

// ============================================================================
// Fonctions spécifiques à X11
// ============================================================================

#ifdef SDL_PLATFORM_LINUX
namespace system {
/// Installe un hook d'évènements X11.
inline void SetX11EventHook(SDL_X11EventHook callback, void *userdata) {
	SDL_SetX11EventHook(callback, userdata);
}
} // namespace system
#endif // SDL_PLATFORM_LINUX

// ============================================================================
// Fonctions spécifiques à iOS
// ============================================================================

#ifdef SDL_PLATFORM_IOS
namespace system {
/// Installe le callback d'animation iOS.
[[nodiscard]] inline bool SetiOSAnimationCallback(
	SDL_Window *window, int interval, SDL_iOSAnimationCallback callback, void *callbackParam) {
	return SDL_SetiOSAnimationCallback(window, interval, callback, callbackParam);
}
/// Active/désactive la pompe d'évènements iOS.
inline void SetiOSEventPump(bool enabled) { SDL_SetiOSEventPump(enabled); }
/// Signale un changement d'orientation de la barre d'état.
inline void OnApplicationDidChangeStatusBarOrientation() { SDL_OnApplicationDidChangeStatusBarOrientation(); }
} // namespace system
#endif // SDL_PLATFORM_IOS

} // namespace sdl3
