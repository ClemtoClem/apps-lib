#pragma once
/**
 * @file haptic.hpp
 * @brief Retour de force (SDL_haptic.h) : énumération, effets et rumble.
 */
#include <SDL3/SDL.h>
#include <vector>

#include <core/core.hpp>
#include "sdl3/input/joystick.hpp"

namespace sdl3 {

// ============================================================================
// Énumération des périphériques haptiques (retour de force)
// ============================================================================

namespace haptic {

/// Identifiants des périphériques haptiques connectés.
[[nodiscard]] std::vector<SDL_HapticID> Enumerated();

/// Nom d'un périphérique haptique par son identifiant.
[[nodiscard]] inline const char *NameFor(SDL_HapticID id) noexcept { return SDL_GetHapticNameForID(id); }

/// Vrai si la souris supporte le retour de force.
[[nodiscard]] inline bool MouseHaptic() noexcept { return SDL_IsMouseHaptic(); }
/// Vrai si un joystick supporte le retour de force.
[[nodiscard]] inline bool JoystickHaptic(const Joystick &j) noexcept { return SDL_IsJoystickHaptic(j.Get()); }

} // namespace haptic

// ============================================================================
// Haptic — RAII SDL_Haptic
// ============================================================================

// ── Types d'effets et de direction ───────────────────────────────────────────

/// Type d'un effet haptique.
enum class HapticEffectType : Uint16 {
	CONSTANT       = SDL_HAPTIC_CONSTANT,	 ///< force constante
	SINE           = SDL_HAPTIC_SINE,		 ///< onde sinusoïdale
	SQUARE         = SDL_HAPTIC_SQUARE,		 ///< onde carrée
	TRIANGLE       = SDL_HAPTIC_TRIANGLE,	 ///< onde triangulaire
	SAWTOOTH_UP    = SDL_HAPTIC_SAWTOOTHUP,	 ///< dent de scie montante
	SAWTOOTH_DOWN  = SDL_HAPTIC_SAWTOOTHDOWN, ///< dent de scie descendante
	RAMP           = SDL_HAPTIC_RAMP,		 ///< rampe
	SPRING         = SDL_HAPTIC_SPRING,		 ///< ressort
	DAMPER         = SDL_HAPTIC_DAMPER,		 ///< amortisseur
	INERTIA        = SDL_HAPTIC_INERTIA,		 ///< inertie
	FRICTION       = SDL_HAPTIC_FRICTION,	 ///< friction
	LEFT_RIGHT     = SDL_HAPTIC_LEFTRIGHT,	 ///< gauche/droite
	CUSTOM         = SDL_HAPTIC_CUSTOM,		 ///< personnalisé
};

/// Système de coordonnées de la direction d'un effet.
enum class HapticDirectionType : Uint8 {
	POLAR         = SDL_HAPTIC_POLAR,		 ///< polaire
	CARTESIAN     = SDL_HAPTIC_CARTESIAN,	 ///< cartésien
	SPHERICAL     = SDL_HAPTIC_SPHERICAL,	 ///< sphérique
	STEERING_AXIS = SDL_HAPTIC_STEERING_AXIS, ///< axe de braquage
};

/// Direction d'un effet (polaires, cartésiennes, sphériques ou axe de braquage).
struct HapticDirection {
	HapticDirectionType type = HapticDirectionType::POLAR; ///< système de coordonnées
	Sint32 dir[3] = {0, 0, 0};							   ///< composantes de la direction

	/// Conversion vers le type SDL brut.
	[[nodiscard]] SDL_HapticDirection ToSDL() const {
		return {static_cast<SDL_HapticDirectionType>(type), {dir[0], dir[1], dir[2]}};
	}
};

// ── Structures d'effets (miroir des SDL_Haptic* ; ToSDL() -> SDL_HapticEffect) ─

/// Effet haptique constant.
struct HapticConstant {
	HapticEffectType type = HapticEffectType::CONSTANT; ///< type d'effet
	HapticDirection direction{};						///< direction
	Uint32 length = 0;									///< durée (ms)
	Uint16 delay = 0;									///< délai avant l'effet
	Uint16 button = 0;									///< bouton déclencheur
	Uint16 interval = 0;								///< intervalle entre répétitions
	Sint16 level = 0;									///< niveau de force
	Uint16 attackLength = 0;							///< durée d'attaque
	Uint16 attackLevel = 0;								///< niveau d'attaque
	Uint16 fadeLength = 0;								///< durée de fondu
	Uint16 fadeLevel = 0;								///< niveau de fondu

	[[nodiscard]] SDL_HapticEffect ToSDL() const {
		SDL_HapticEffect e{};
		e.constant.type = static_cast<SDL_HapticEffectType>(type);
		e.constant.direction = direction.ToSDL();
		e.constant.length = length;
		e.constant.delay = delay;
		e.constant.button = button;
		e.constant.interval = interval;
		e.constant.level = level;
		e.constant.attack_length = attackLength;
		e.constant.attack_level = attackLevel;
		e.constant.fade_length = fadeLength;
		e.constant.fade_level = fadeLevel;
		return e;
	}
};

/// Effet haptique périodique (sinus, carré, triangle, dent de scie).
struct HapticPeriodic {
	HapticEffectType type = HapticEffectType::SINE; ///< type d'effet
	HapticDirection direction{};					///< direction
	Uint32 length = 0;								///< durée (ms)
	Uint16 delay = 0;								///< délai avant l'effet
	Uint16 button = 0;								///< bouton déclencheur
	Uint16 interval = 0;							///< intervalle entre répétitions
	Uint16 period = 0;								///< période (ms)
	Sint16 magnitude = 0;							///< amplitude
	Sint16 offset = 0;								///< décalage
	Uint16 phase = 0;								///< phase
	Uint16 attackLength = 0;						///< durée d'attaque
	Uint16 attackLevel = 0;							///< niveau d'attaque
	Uint16 fadeLength = 0;							///< durée de fondu
	Uint16 fadeLevel = 0;							///< niveau de fondu

	[[nodiscard]] SDL_HapticEffect ToSDL() const {
		SDL_HapticEffect e{};
		e.periodic.type = static_cast<SDL_HapticEffectType>(type);
		e.periodic.direction = direction.ToSDL();
		e.periodic.length = length;
		e.periodic.delay = delay;
		e.periodic.button = button;
		e.periodic.interval = interval;
		e.periodic.period = period;
		e.periodic.magnitude = magnitude;
		e.periodic.offset = offset;
		e.periodic.phase = phase;
		e.periodic.attack_length = attackLength;
		e.periodic.attack_level = attackLevel;
		e.periodic.fade_length = fadeLength;
		e.periodic.fade_level = fadeLevel;
		return e;
	}
};

/// Effet haptique conditionnel (ressort, amortisseur, inertie, friction).
struct HapticCondition {
	HapticEffectType type = HapticEffectType::SPRING; ///< type d'effet
	HapticDirection direction{};					  ///< direction
	Uint32 length = 0;								  ///< durée (ms)
	Uint16 delay = 0;								  ///< délai avant l'effet
	Uint16 button = 0;								  ///< bouton déclencheur
	Uint16 interval = 0;							  ///< intervalle entre répétitions
	Uint16 rightSat[3] = {0, 0, 0};					  ///< saturation positive
	Uint16 leftSat[3] = {0, 0, 0};					  ///< saturation négative
	Sint16 rightCoeff[3] = {0, 0, 0};				  ///< coefficient positif
	Sint16 leftCoeff[3] = {0, 0, 0};				  ///< coefficient négatif
	Uint16 deadband[3] = {0, 0, 0};					  ///< zone morte
	Sint16 center[3] = {0, 0, 0};					  ///< position centrale

	[[nodiscard]] SDL_HapticEffect ToSDL() const {
		SDL_HapticEffect e{};
		e.condition.type = static_cast<SDL_HapticEffectType>(type);
		e.condition.direction = direction.ToSDL();
		e.condition.length = length;
		e.condition.delay = delay;
		e.condition.button = button;
		e.condition.interval = interval;
		for (int i = 0; i < 3; ++i) {
			e.condition.right_sat[i] = rightSat[i];
			e.condition.left_sat[i] = leftSat[i];
			e.condition.right_coeff[i] = rightCoeff[i];
			e.condition.left_coeff[i] = leftCoeff[i];
			e.condition.deadband[i] = deadband[i];
			e.condition.center[i] = center[i];
		}
		return e;
	}
};

/// Effet haptique en rampe.
struct HapticRamp {
	HapticEffectType type = HapticEffectType::RAMP; ///< type d'effet
	HapticDirection direction{};					///< direction
	Uint32 length = 0;								///< durée (ms)
	Uint16 delay = 0;								///< délai avant l'effet
	Uint16 button = 0;								///< bouton déclencheur
	Uint16 interval = 0;							///< intervalle entre répétitions
	Sint16 start = 0;								///< niveau de départ
	Sint16 end = 0;									///< niveau de fin
	Uint16 attackLength = 0;						///< durée d'attaque
	Uint16 attackLevel = 0;							///< niveau d'attaque
	Uint16 fadeLength = 0;							///< durée de fondu
	Uint16 fadeLevel = 0;							///< niveau de fondu

	[[nodiscard]] SDL_HapticEffect ToSDL() const {
		SDL_HapticEffect e{};
		e.ramp.type = static_cast<SDL_HapticEffectType>(type);
		e.ramp.direction = direction.ToSDL();
		e.ramp.length = length;
		e.ramp.delay = delay;
		e.ramp.button = button;
		e.ramp.interval = interval;
		e.ramp.start = start;
		e.ramp.end = end;
		e.ramp.attack_length = attackLength;
		e.ramp.attack_level = attackLevel;
		e.ramp.fade_length = fadeLength;
		e.ramp.fade_level = fadeLevel;
		return e;
	}
};

/// Effet haptique gauche/droite (deux moteurs).
struct HapticLeftRight {
	HapticEffectType type = HapticEffectType::LEFT_RIGHT; ///< type d'effet
	Uint32 length = 0;									  ///< durée (ms)
	Uint16 largeMagnitude = 0;							  ///< amplitude du gros moteur
	Uint16 smallMagnitude = 0;							  ///< amplitude du petit moteur

	[[nodiscard]] SDL_HapticEffect ToSDL() const {
		SDL_HapticEffect e{};
		e.leftright.type = static_cast<SDL_HapticEffectType>(type);
		e.leftright.length = length;
		e.leftright.large_magnitude = largeMagnitude;
		e.leftright.small_magnitude = smallMagnitude;
		return e;
	}
};

/// Effet haptique personnalisé (échantillons fournis par l'appelant).
struct HapticCustom {
	HapticEffectType type = HapticEffectType::CUSTOM; ///< type d'effet
	HapticDirection direction{};					  ///< direction
	Uint32 length = 0;								  ///< durée (ms)
	Uint16 delay = 0;								  ///< délai avant l'effet
	Uint16 button = 0;								  ///< bouton déclencheur
	Uint16 interval = 0;							  ///< intervalle entre répétitions
	Uint8 channels = 1;								  ///< nombre de canaux
	Uint16 period = 0;								  ///< période
	Uint16 samples = 0;								  ///< nombre d'échantillons
	Uint16 *data = nullptr;							  ///< échantillons
	Uint16 attackLength = 0;						  ///< durée d'attaque
	Uint16 attackLevel = 0;							  ///< niveau d'attaque
	Uint16 fadeLength = 0;							  ///< durée de fondu
	Uint16 fadeLevel = 0;							  ///< niveau de fondu

	[[nodiscard]] SDL_HapticEffect ToSDL() const {
		SDL_HapticEffect e{};
		e.custom.type = static_cast<SDL_HapticEffectType>(type);
		e.custom.direction = direction.ToSDL();
		e.custom.length = length;
		e.custom.delay = delay;
		e.custom.button = button;
		e.custom.interval = interval;
		e.custom.channels = channels;
		e.custom.period = period;
		e.custom.samples = samples;
		e.custom.data = data;
		e.custom.attack_length = attackLength;
		e.custom.attack_level = attackLevel;
		e.custom.fade_length = fadeLength;
		e.custom.fade_level = fadeLevel;
		return e;
	}
};

/// Périphérique haptique ouvert (RAII sur SDL_Haptic).
class Haptic : public Wrapper<SDL_Haptic, SDL_CloseHaptic> {
public:
    using Wrapper::Wrapper;

    /// Ouvre un périphérique haptique par identifiant.
    [[nodiscard]] static Result<Haptic, StringView> Open(SDL_HapticID id);

    /// Ouvre le périphérique haptique de la souris.
    [[nodiscard]] static Result<Haptic, StringView> OpenFromMouse();

    /// Ouvre le périphérique haptique d'un joystick.
    [[nodiscard]] static Result<Haptic, StringView> OpenFromJoystick(Joystick &j);

    /// Identifiant du périphérique.
    [[nodiscard]] SDL_HapticID GetId() const noexcept { return mHandle ? SDL_GetHapticID(mHandle) : 0; }
    /// Nom du périphérique.
    [[nodiscard]] const char *Name() const noexcept { return mHandle ? SDL_GetHapticName(mHandle) : ""; }

    /// Nombre maximal d'effets stockables.
    [[nodiscard]] int MaxEffects() const noexcept { return mHandle ? SDL_GetMaxHapticEffects(mHandle) : 0; }
    /// Nombre maximal d'effets jouables simultanément.
    [[nodiscard]] int MaxEffectsPlaying() const noexcept;
    /// Fonctionnalités supportées (bitmap SDL_HAPTIC_*).
    [[nodiscard]] uint32_t Features() const noexcept { return mHandle ? SDL_GetHapticFeatures(mHandle) : 0; }
    /// Nombre d'axes.
    [[nodiscard]] int NumAxes() const noexcept { return mHandle ? SDL_GetNumHapticAxes(mHandle) : 0; }

    /// Fixe le gain global.
    bool SetGain(int gain) noexcept { return mHandle && SDL_SetHapticGain(mHandle, gain); }
    /// Fixe l'auto-centrage.
    bool SetAutocenter(int a) noexcept { return mHandle && SDL_SetHapticAutocenter(mHandle, a); }
    /// Met en pause les effets.
    bool Pause() noexcept { return mHandle && SDL_PauseHaptic(mHandle); }
    /// Reprend les effets.
    bool Resume() noexcept { return mHandle && SDL_ResumeHaptic(mHandle); }
    /// Arrête tous les effets.
    bool StopAllEffects() noexcept { return mHandle && SDL_StopHapticEffects(mHandle); }

    // ── API simple : rumble générique (pas besoin de décrire un effet) ───────

    /// Vrai si le rumble est supporté.
    [[nodiscard]] bool RumbleSupported() const noexcept { return mHandle && SDL_HapticRumbleSupported(mHandle); }
    /// Initialise le rumble.
    bool InitRumble() noexcept { return mHandle && SDL_InitHapticRumble(mHandle); }
    /// Joue un rumble (force 0..1, durée en ms).
    bool PlayRumble(float strength, uint32_t lengthMs) noexcept;
    /// Arrête le rumble.
    bool StopRumble() noexcept { return mHandle && SDL_StopHapticRumble(mHandle); }

    // ── API avancée : effets décrits par SDL_HapticEffect (union native SDL) ─

    /// Vrai si l'effet est supporté.
    [[nodiscard]] bool EffectSupported(const SDL_HapticEffect &effect) const noexcept;
    /// Crée un effet (retourne son identifiant).
    [[nodiscard]] Result<SDL_HapticEffectID, StringView> CreateEffect(const SDL_HapticEffect &effect);

    /// Surcharge acceptant n'importe quelle structure d'effet wrappée (ToSDL()).
    template <typename Effect>
    [[nodiscard]] Result<SDL_HapticEffectID, StringView> CreateEffect(const Effect &effect) {
        SDL_HapticEffect e = effect.ToSDL();
        return CreateEffect(e);
    }

    /// Met à jour un effet existant.
    bool UpdateEffect(SDL_HapticEffectID id, const SDL_HapticEffect &effect) noexcept;
    /// Joue un effet (itérations : 1 = une fois, infini = SDL_HAPTIC_INFINITY).
    bool RunEffect(SDL_HapticEffectID id, uint32_t iterations = 1) noexcept;
    /// Arrête un effet.
    bool StopEffect(SDL_HapticEffectID id) noexcept { return mHandle && SDL_StopHapticEffect(mHandle, id); }
    /// Détruit un effet.
    void DestroyEffect(SDL_HapticEffectID id) noexcept;
    /// Vrai si l'effet est en cours de lecture.
    [[nodiscard]] bool EffectRunning(SDL_HapticEffectID id) const noexcept;
};

} // namespace sdl3
