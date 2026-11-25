#pragma once
/**
 * @file joystick.hpp
 * @brief Joysticks bruts (SDL_joystick.h) : énumération, lecture, vibration, joysticks virtuels.
 */
#include <SDL3/SDL.h>
#include <vector>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// JoystickType / JoystickConnectionState
// ============================================================================

/// Type physique d'un joystick.
enum class JoystickType : int {
	UNKNOWN		 = SDL_JOYSTICK_TYPE_UNKNOWN,		///< inconnu
	GAMEPAD		 = SDL_JOYSTICK_TYPE_GAMEPAD,		///< manette
	WHEEL		 = SDL_JOYSTICK_TYPE_WHEEL,			///< volant
	ARCADE_STICK = SDL_JOYSTICK_TYPE_ARCADE_STICK,	///< stick d'arcade
	FLIGHT_STICK = SDL_JOYSTICK_TYPE_FLIGHT_STICK,	///< manche de vol
	DANCE_PAD	 = SDL_JOYSTICK_TYPE_DANCE_PAD,		///< tapis de danse
	GUITAR		 = SDL_JOYSTICK_TYPE_GUITAR,			///< guitare
	DRUM_KIT	 = SDL_JOYSTICK_TYPE_DRUM_KIT,		///< batterie
	ARCADE_PAD	 = SDL_JOYSTICK_TYPE_ARCADE_PAD,		///< pavé d'arcade
	THROTTLE	 = SDL_JOYSTICK_TYPE_THROTTLE,		///< manette des gaz
};

/// Mode de connexion d'un joystick.
enum class JoystickConnection : int {
	INVALID	  = SDL_JOYSTICK_CONNECTION_INVALID,	///< invalide
	UNKNOWN	  = SDL_JOYSTICK_CONNECTION_UNKNOWN,	///< inconnu
	WIRED	  = SDL_JOYSTICK_CONNECTION_WIRED,		///< filaire
	WIRELESS  = SDL_JOYSTICK_CONNECTION_WIRELESS,	///< sans fil
};

// ============================================================================
// Ball delta — wrap du couple (dx, dy) retourné par SDL_GetJoystickBall
// ============================================================================

/// Déplacement d'une boule (trackball) d'un joystick.
struct BallDelta {
	int dx = 0, dy = 0; ///< déplacement relatif
};

// ============================================================================
// PowerInfo — wrap du couple (SDL_PowerState, pourcentage) retourné par
// SDL_GetJoystickPowerInfo
// ============================================================================

/// État d'alimentation d'un joystick.
enum class PowerState : int {
	ERROR	  = SDL_POWERSTATE_ERROR,		   /**< erreur de détermination */
	UNKNOWN	  = SDL_POWERSTATE_UNKNOWN,	   /**< impossible à déterminer */
	ON_BATTERY = SDL_POWERSTATE_ON_BATTERY,	   /**< sur batterie (non branché) */
	NO_BATTERY = SDL_POWERSTATE_NO_BATTERY,	   /**< branché, sans batterie */
	CHARGING  = SDL_POWERSTATE_CHARGING,	   /**< branché, en charge */
	CHARGED	  = SDL_POWERSTATE_CHARGED		   /**< branché, chargé */
};

/// Alimentation d'un joystick : état + pourcentage restant.
struct PowerInfo {
	PowerState state = PowerState::UNKNOWN; ///< état d'alimentation
	Option<int> percent = NONE;				///< pourcentage restant (NONE si inconnu)
};

// ============================================================================
// Énumération des manettes brutes connectées
// ============================================================================

namespace joystick {

/// Vrai si au moins un joystick est connecté.
[[nodiscard]] inline bool Any() noexcept { return SDL_HasJoystick(); }

/// Identifiants des joysticks connectés.
[[nodiscard]] std::vector<JoystickID> Enumerated();

/// Nom d'un joystick par son identifiant.
[[nodiscard]] inline const char *NameFor(JoystickID id) noexcept { return SDL_GetJoystickNameForID(id); }
/// Chemin système d'un joystick.
[[nodiscard]] inline const char *PathFor(JoystickID id) noexcept { return SDL_GetJoystickPathForID(id); }
/// Index de joueur d'un joystick.
[[nodiscard]] inline int PlayerIndexFor(JoystickID id) noexcept { return SDL_GetJoystickPlayerIndexForID(id); }
/// Identifiant vendeur d'un joystick.
[[nodiscard]] inline uint16_t VendorFor(JoystickID id) noexcept { return SDL_GetJoystickVendorForID(id); }
/// Identifiant produit d'un joystick.
[[nodiscard]] inline uint16_t ProductFor(JoystickID id) noexcept { return SDL_GetJoystickProductForID(id); }
/// Type d'un joystick par son identifiant.
[[nodiscard]] inline JoystickType TypeFor(JoystickID id) noexcept { return JoystickType(SDL_GetJoystickTypeForID(id)); }

/// Active/désactive les évènements de joystick.
inline void SetEventsEnabled(bool enabled) noexcept { SDL_SetJoystickEventsEnabled(enabled); }
/// Vrai si les évènements de joystick sont activés.
[[nodiscard]] inline bool EventsEnabled() noexcept { return SDL_JoystickEventsEnabled(); }
/// Force la mise à jour des joysticks.
inline void Update() noexcept { SDL_UpdateJoysticks(); }

} // namespace joystick

// ============================================================================
// Joystick — RAII SDL_Joystick (accès bas niveau, cf. `Gamepad` pour l'API
// haut niveau normalisée)
// ============================================================================

class Joystick : public Wrapper<SDL_Joystick, SDL_CloseJoystick> {
public:
    using Wrapper::Wrapper;

    /// Ouvre un joystick par identifiant.
    [[nodiscard]] static Result<Joystick, StringView> Open(JoystickID id);

    /// Propriétés du joystick.
    [[nodiscard]] SDL_PropertiesID Properties() const noexcept;
    /// Nom du joystick.
    [[nodiscard]] const char *GetName() const noexcept { return mHandle ? SDL_GetJoystickName(mHandle) : ""; }
    /// Chemin système du joystick.
    [[nodiscard]] const char *GetPath() const noexcept { return mHandle ? SDL_GetJoystickPath(mHandle) : ""; }

    /// Index de joueur.
    [[nodiscard]] int PlayerIndex() const noexcept { return mHandle ? SDL_GetJoystickPlayerIndex(mHandle) : -1; }
    /// Change l'index de joueur.
    bool SetPlayerIndex(int index) noexcept { return mHandle && SDL_SetJoystickPlayerIndex(mHandle, index); }

    /// Identifiant vendeur.
    [[nodiscard]] uint16_t GetVendor() const noexcept { return mHandle ? SDL_GetJoystickVendor(mHandle) : 0; }
    /// Identifiant produit.
    [[nodiscard]] uint16_t GetProduct() const noexcept { return mHandle ? SDL_GetJoystickProduct(mHandle) : 0; }
    /// Version du produit.
    [[nodiscard]] uint16_t GetProductVersion() const noexcept;
    /// Version du firmware.
    [[nodiscard]] uint16_t FirmwareVersion() const noexcept;
    /// Numéro de série.
    [[nodiscard]] const char *Serial() const noexcept { return mHandle ? SDL_GetJoystickSerial(mHandle) : ""; }
    /// Type du joystick.
    [[nodiscard]] JoystickType GetType() const noexcept;

    /// Vrai si le joystick est connecté.
    [[nodiscard]] bool Connected() const noexcept { return mHandle && SDL_JoystickConnected(mHandle); }
    /// Identifiant du joystick.
    [[nodiscard]] JoystickID GetId() const noexcept { return mHandle ? SDL_GetJoystickID(mHandle) : 0; }
    /// Mode de connexion.
    [[nodiscard]] JoystickConnection ConnectionState() const noexcept;

    /// Nombre d'axes.
    [[nodiscard]] int GetNumAxes() const noexcept { return mHandle ? SDL_GetNumJoystickAxes(mHandle) : 0; }
    /// Nombre de boules.
    [[nodiscard]] int GetNumBalls() const noexcept { return mHandle ? SDL_GetNumJoystickBalls(mHandle) : 0; }
    /// Nombre de chapeaux.
    [[nodiscard]] int GetNumHats() const noexcept { return mHandle ? SDL_GetNumJoystickHats(mHandle) : 0; }
    /// Nombre de boutons.
    [[nodiscard]] int GetNumButtons() const noexcept { return mHandle ? SDL_GetNumJoystickButtons(mHandle) : 0; }

    /// Valeur d'un axe (−32768..32767).
    [[nodiscard]] Sint16 GetAxis(int index) const noexcept { return mHandle ? SDL_GetJoystickAxis(mHandle, index) : 0; }

    /// État initial d'un axe (avant tout mouvement).
    [[nodiscard]] Option<Sint16> AxisInitialState(int index) const;

    /// Déplacement d'une boule depuis la dernière lecture.
    [[nodiscard]] Option<BallDelta> Ball(int index) const;

    /// Position d'un chapeau (bitmap SDL_HAT_*).
    [[nodiscard]] uint8_t Hat(int index) const noexcept { return mHandle ? SDL_GetJoystickHat(mHandle, index) : 0; }
    /// État d'un bouton.
    [[nodiscard]] bool Button(int index) const noexcept { return mHandle && SDL_GetJoystickButton(mHandle, index); }

    /// Vibration (basse/haute fréquence).
    bool Rumble(uint16_t lowHz, uint16_t highHz, uint32_t durationMs) noexcept;

    /// Vibration des gâchettes (moteurs séparés).
    bool RumbleTriggers(uint16_t left, uint16_t right, uint32_t durationMs) noexcept;

    /// Envoie des données d'effet spécifiques au pilote (usage avancé).
    bool SendEffect(const void *data, int size) noexcept;

    /// Alimentation du joystick. `percent` (charge restante, [0,100]) est NONE si inconnu.
    [[nodiscard]] sdl3::PowerInfo PowerInfo() const noexcept;
};

// ============================================================================
// Joysticks virtuels (SDL_joystick.h)
// ============================================================================

namespace joystick {

/// Retrouve la vue d'un joystick déjà ouvert ailleurs, à partir de son ID.
[[nodiscard]] inline SDL_Joystick *FromId(JoystickID id) noexcept { return SDL_GetJoystickFromID(id); }

/// Vrai si ce joystick est virtuel (logiciel).
[[nodiscard]] inline bool IsVirtual(JoystickID id) noexcept { return SDL_IsJoystickVirtual(id); }

/// Attache un joystick virtuel ; retourne son nouvel ID ou une erreur.
[[nodiscard]] Result<JoystickID, StringView> AttachVirtual(const SDL_VirtualJoystickDesc &desc);

/// Détache un joystick virtuel.
inline bool DetachVirtual(JoystickID id) noexcept { return SDL_DetachVirtualJoystick(id); }

/// Injecte un état (axe / boule / bouton / chapeau / pavé tactile) dans un joystick virtuel.
inline bool SetVirtualAxis(JoystickID id, int axis, Sint16 value) noexcept {
	return SDL_SetJoystickVirtualAxis(SDL_GetJoystickFromID(id), axis, value);
}
inline bool SetVirtualBall(JoystickID id, int ball, Sint16 dx, Sint16 dy) noexcept {
	return SDL_SetJoystickVirtualBall(SDL_GetJoystickFromID(id), ball, dx, dy);
}
inline bool SetVirtualButton(JoystickID id, int button, bool down) noexcept {
	return SDL_SetJoystickVirtualButton(SDL_GetJoystickFromID(id), button, down);
}
inline bool SetVirtualHat(JoystickID id, int hat, uint8_t value) noexcept {
	return SDL_SetJoystickVirtualHat(SDL_GetJoystickFromID(id), hat, value);
}
inline bool SetVirtualTouchpad(JoystickID id, int pad, int finger, bool down, float x, float y, float pressure) noexcept {
	return SDL_SetJoystickVirtualTouchpad(SDL_GetJoystickFromID(id), pad, finger, down, x, y, pressure);
}

} // namespace joystick

} // namespace sdl3
