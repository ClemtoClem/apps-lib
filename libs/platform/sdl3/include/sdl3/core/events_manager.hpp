#pragma once
/**
 * @file events_manager.hpp
 * @brief Centralise l'état de tous les évènements d'une image.
 *
 * `EventsManager::Poll()` draine UNE fois la file SDL et fige, en mémoire,
 * l'état de chaque évènement par rapport à l'image précédente : l'interface
 * lit ensuite ces états sans réitérer la boucle `SDL_PollEvent`.
 *
 * Chaque état est un `TriggerState` :
 *  - `Deactivated`  = RELEASED / UNPLUGGED  (0)
 *  - `JustActivated` = JUST_PRESSED / JUST_PLUGGED (1)
 *  - `Activated`     = PRESSED / PLUGGED     (2)
 *  - `JustDeactivated` = JUST_RELEASED / JUST_UNPLUGGED (3)
 *
 * Lecture : `ev[MouseButton::LEFT].IsJustPressed()`, `ev[Mouse].IsJustPlugged()`.
 * Liaisons (type JS) : `Bind` / `Unbind` — cf. exemples plus bas.
 *
 * Au-delà des touches/boutons/périphériques/fenêtres, `EventsManager` fige
 * aussi l'état des **écrans** (branchement, déplacement, orientation, échelle),
 * du **pavé tactile des manettes** (`TouchpadFingers()`) et des **doigts d'un
 * écran tactile** (`TouchFingers()`), coordonnées normalisées 0..1.
 *
 * @code
 * sdl3::EventsManager ev;
 * while (running) {
 *     ev.Poll();                                   // une seule itération
 *     if (ev[SDL_SCANCODE_S].IsJustPressed()) …
 *     if (ev[sdl3::MouseButton::LEFT].IsJustPressed()) …
 *     if (ev[sdl3::DeviceType::Mouse].IsJustPlugged()) …
 *     for (const auto &f : ev.TouchFingers()) …
 * }
 * @endcode
 */
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <core/core.hpp>

#include "sdl3/core/events.hpp"
#include "sdl3/input/input.hpp"

namespace sdl3 {

// ============================================================================
// TriggerState / Trigger
// ============================================================================

enum class TriggerState : uint8_t {
	Deactivated = 0,     ///< RELEASED / UNPLUGGED
	JustActivated = 1,   ///< JUST_PRESSED / JUST_PLUGGED
	Activated = 2,       ///< PRESSED / PLUGGED
	JustDeactivated = 3, ///< JUST_RELEASED / JUST_UNPLUGGED
};

/// Valeur d'état d'un évènement, avec les prédicats de lecture usuels.
struct Trigger {
	TriggerState state = TriggerState::Deactivated;

	constexpr Trigger() = default;
	constexpr Trigger(TriggerState s) noexcept : state(s) {}
	constexpr operator TriggerState() const noexcept { return state; }

	[[nodiscard]] constexpr bool IsDeactivated() const noexcept { return state == TriggerState::Deactivated; }
	[[nodiscard]] constexpr bool IsJustActivated() const noexcept { return state == TriggerState::JustActivated; }
	[[nodiscard]] constexpr bool IsActivated() const noexcept { return state == TriggerState::Activated; }
	[[nodiscard]] constexpr bool IsJustDeactivated() const noexcept { return state == TriggerState::JustDeactivated; }

	// ── Alias (touches / boutons) ─────────────────────────────────────────────
	[[nodiscard]] constexpr bool IsReleased() const noexcept { return IsDeactivated(); }
	[[nodiscard]] constexpr bool IsJustPressed() const noexcept { return IsJustActivated(); }
	[[nodiscard]] constexpr bool IsPressed() const noexcept {
		return state == TriggerState::Activated || state == TriggerState::JustActivated;
	}
	[[nodiscard]] constexpr bool IsJustReleased() const noexcept { return IsJustDeactivated(); }
	[[nodiscard]] constexpr bool IsDown() const noexcept { return IsPressed(); }

	// ── Alias (périphériques) ─────────────────────────────────────────────────
	[[nodiscard]] constexpr bool IsUnplugged() const noexcept { return IsDeactivated(); }
	[[nodiscard]] constexpr bool IsJustPlugged() const noexcept { return IsJustActivated(); }
	[[nodiscard]] constexpr bool IsPlugged() const noexcept { return IsActivated(); }
	[[nodiscard]] constexpr bool IsJustUnplugged() const noexcept { return IsJustDeactivated(); }
};

// ============================================================================
// Types d'évènements de haut niveau
// ============================================================================

enum class DeviceType : uint8_t { Mouse, Keyboard, Gamepad, Joystick, Camera, Audio, Count };

/// Évènement de branchement/débranchement d'un périphérique (alias de TriggerState).
enum class DeviceEvent : uint8_t { Plug = 1, Unplug = 3 };

/// Évènement de fenêtre.
enum class WindowEvent : uint8_t {
	Resize,      ///< SDL_EVENT_WINDOW_RESIZED (largeur/hauteur logiques)
	PixelResize, ///< SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
	Move,        ///< SDL_EVENT_WINDOW_MOVED (x/y)
	FocusGained, ///< SDL_EVENT_WINDOW_FOCUS_GAINED
	FocusLost,   ///< SDL_EVENT_WINDOW_FOCUS_LOST
	Minimized,
	Maximized,
	Restored,
	Shown,
	Hidden,
	EnterFullscreen,
	LeaveFullscreen,
};

/// Évènement d'écran (SDL_EVENT_DISPLAY_*).
enum class DisplayEvent : uint8_t {
	Added,               ///< écran branché (data1/data2 = 0)
	Removed,             ///< écran débranché (data1/data2 = 0)
	Moved,               ///< position changée (data1 = x, data2 = y, bureau virtuel)
	Orientation,         ///< orientation changée (data1 = SDL_DisplayOrientation)
	DesktopModeChanged,  ///< mode de bureau changé
	CurrentModeChanged,  ///< mode courant changé
	ContentScaleChanged, ///< échelle de contenu changée
	UsableBoundsChanged, ///< zone utilisable changée (SDL 3.4+)
};

using DeviceId = Uint32; ///< identifiant de périphérique (Mouse/Keyboard/Joystick/Camera/Audio = Uint32)
using Keycode = SDL_Keycode;

/// Doigt posé sur le pavé tactile d'une manette (coordonnées normalisées 0..1).
struct TouchpadFinger {
	JoystickID which = 0; ///< identifiant de la manette
	int touchpad = 0;	  ///< index du pavé tactile
	int finger = 0;		  ///< index du doigt
	float x = 0.f, y = 0.f, pressure = 0.f;
};

/// Doigt posé sur un écran tactile (coordonnées normalisées 0..1).
struct TouchFinger {
	SDL_TouchID touch = 0;   ///< identifiant du périphérique tactile
	SDL_FingerID finger = 0; ///< identifiant du doigt
	float x = 0.f, y = 0.f;	 ///< position (0..1)
	float dx = 0.f, dy = 0.f; ///< déplacement depuis la dernière image (−1..1)
	float pressure = 0.f;
	SDL_WindowID window = 0; ///< fenêtre sous le doigt (0 si aucune)
};

/// Geste multi-doigts reconnu à chaque image à partir des doigts (`SDL_EVENT_FINGER_*`
/// et `SDL_EVENT_GAMEPAD_TOUCHPAD_*`) — aucune dépendance à l'API de gestes SDL,
/// donc tous les gestes (pincement, rotation, balayage) sont disponibles sans
/// restriction de plateforme. `pinch`/`rotate`/`dx`/`dy` sont les variations de
/// l'image (relatives à la précédente) ; `center` est le centroïde (0..1).
struct Gesture {
	bool active = false;      ///< deux doigts posés (geste en cours)
	float pinch = 1.f;        ///< facteur d'échelle (>1 : écartement / zoom avant)
	float rotate = 0.f;       ///< rotation (radians)
	float dx = 0.f, dy = 0.f; ///< déplacement du centroïde (balayage, 0..1)
	FPoint center{};          ///< centroïde actuel (0..1)
};

// ============================================================================
// EventsManager
// ============================================================================

class EventsManager {
public:
	/// Drake UNE fois la file d'évènements SDL et fige les états de l'image.
	void Poll();

	/// Vrai si l'utilisateur a demandé la fermeture (QUIT / fermeture de fenêtre).
	[[nodiscard]] bool IsQuitRequested() const noexcept { return m_quit; }

	// ── Lecture d'état (operator[]) ──────────────────────────────────────────
	[[nodiscard]] Trigger Key(SDL_Keycode key) const noexcept;
	/// État d'une touche physique (scancode, indépendant de la disposition).
	[[nodiscard]] Trigger KeyScan(SDL_Scancode scan) const noexcept;
	[[nodiscard]] Trigger MouseButton(uint8_t button) const noexcept;
	[[nodiscard]] Trigger GamepadButton(SDL_GamepadButton button) const noexcept;
	[[nodiscard]] Trigger Device(DeviceType type) const noexcept;

	[[nodiscard]] Trigger operator[](SDL_Keycode key) const noexcept { return Key(key); }
	[[nodiscard]] Trigger operator[](SDL_Scancode scan) const noexcept { return KeyScan(scan); }
	[[nodiscard]] Trigger operator[](sdl3::MouseButton button) const noexcept { return MouseButton(uint8_t(button)); }
	[[nodiscard]] Trigger operator[](sdl3::GamepadButton button) const noexcept {
		return GamepadButton(static_cast<SDL_GamepadButton>(button));
	}
	[[nodiscard]] Trigger operator[](DeviceType type) const noexcept { return Device(type); }

	// ── Liaisons (type JS) ───────────────────────────────────────────────────
	using KeyComboCallback = std::function<void(const std::map<Keycode, TriggerState> &)>;
	using DeviceCallback = std::function<void(DeviceId)>;
	using WindowCallback = std::function<void(SDL_Window *, int, int)>;
	using DisplayCallback = std::function<void(DisplayID, int, int)>;

	/// Déclenche `cb` quand la combinaison de touches vient d'être enfoncée
	/// (toutes appuyées, la dernière venant d'être pressée). `cb` reçoit l'état
	/// de chaque touche. Ex. : `Bind({SDLK_LCTRL, SDLK_LALT, SDLK_S}, cb)`.
	[[nodiscard]] size_t Bind(std::initializer_list<Keycode> keys, KeyComboCallback cb);

	/// Déclenche `cb` au branchement/débranchement d'un périphérique.
	[[nodiscard]] size_t Bind(DeviceType type, DeviceEvent event, DeviceCallback cb);

	/// Déclenche `cb` sur un évènement de fenêtre (`window`, puis 2 entiers :
	/// largeur/hauteur pour Resize, x/y pour Move, 0/0 sinon).
	[[nodiscard]] size_t Bind(WindowEvent event, WindowCallback cb);

	/// Déclenche `cb` sur un évènement d'écran (`display`, puis 2 entiers :
	/// x/y pour Moved, orientation pour Orientation, 0/0 sinon).
	[[nodiscard]] size_t Bind(DisplayEvent event, DisplayCallback cb);

	using GenericCallback = std::function<void(const sdl3::Event &)>;
	/// Échappatoire générique : déclenche `cb` pour chaque évènement de type
	/// `type` (DROP_BEGIN, TEXT_EDITING, … — tout type sans API dédiée).
	[[nodiscard]] size_t Bind(EventType type, GenericCallback cb);

	/// Retire une liaison (identifiant retourné par Bind).
	void Unbind(size_t id);

	/// Retire toutes les liaisons.
	void UnbindAll();

	// ── Écrans (display) ─────────────────────────────────────────────────────
	/// Nombre d'écrans actuellement connectés.
	[[nodiscard]] size_t DisplayCount() const noexcept { return m_displays.size(); }
	/// Vrai si l'écran `id` est connecté.
	[[nodiscard]] bool DisplayConnected(DisplayID id) const noexcept { return m_displays.contains(id); }
	/// Écrans branchés cette image.
	[[nodiscard]] std::span<const DisplayID> JustAddedDisplays() const noexcept { return m_justAddedDisplays; }
	/// Écrans débranchés cette image.
	[[nodiscard]] std::span<const DisplayID> JustRemovedDisplays() const noexcept { return m_justRemovedDisplays; }

	// ── Pavé tactile des manettes ────────────────────────────────────────────
	/// Doigts actuellement posés sur les pavés tactiles (coordonnées 0..1).
	[[nodiscard]] std::span<const TouchpadFinger> TouchpadFingers() const noexcept { return m_touchpadFingers; }
	/// Doigt précis d'un pavé tactile, NONE si aucun doigt à cet index.
	[[nodiscard]] Option<TouchpadFinger> GetTouchpadFinger(JoystickID which, int touchpad, int finger) const;

	// ── Écran tactile (doigts) ───────────────────────────────────────────────
	/// Doigts actuellement posés sur l'écran (coordonnées normalisées 0..1).
	[[nodiscard]] std::span<const TouchFinger> TouchFingers() const noexcept { return m_touchFingers; }
	/// Doigt précis d'un écran tactile, NONE si ce doigt n'est pas posé.
	[[nodiscard]] Option<TouchFinger> GetTouchFinger(SDL_TouchID touch, SDL_FingerID finger) const;

	// ── Gestes multi-doigts ──────────────────────────────────────────────────
	/// Geste de l'image (pincement → zoom, rotation, balayage), calculé à partir
	/// de `SDL_EVENT_FINGER_*` et du pavé tactile des manettes.
	[[nodiscard]] const Gesture &GestureState() const noexcept { return m_gesture; }

	// ── Souris / molette ─────────────────────────────────────────────────────
	/// Position de la souris (unités de fenêtre, non mises à l'échelle).
	[[nodiscard]] float MouseX() const noexcept { return m_mouseX; }
	[[nodiscard]] float MouseY() const noexcept { return m_mouseY; }
	[[nodiscard]] FPoint MousePosition() const noexcept { return {m_mouseX, m_mouseY}; }
	/// Déplacement relatif de la souris accumulé cette image (mode relatif, molette exclue).
	[[nodiscard]] float MouseRelX() const noexcept { return m_mouseRelX; }
	[[nodiscard]] float MouseRelY() const noexcept { return m_mouseRelY; }
	/// Molette accumulée cette image (X = horizontale/pavé, Y = verticale).
	[[nodiscard]] float WheelX() const noexcept { return m_wheelX; }
	[[nodiscard]] float WheelY() const noexcept { return m_wheelY; }

	// ── Clavier / texte ──────────────────────────────────────────────────────
	/// Modificateurs (Ctrl / Maj / Alt) au dernier évènement clavier.
	[[nodiscard]] bool Ctrl() const noexcept { return m_ctrl; }
	[[nodiscard]] bool Shift() const noexcept { return m_shift; }
	[[nodiscard]] bool Alt() const noexcept { return m_alt; }
	[[nodiscard]] SDL_Keymod Mods() const noexcept { return m_mods; }
	/// Caractère d'une touche du pavé numérique (Verr. num. active, sans Ctrl ni Alt) ; 0 sinon.
	[[nodiscard]] static char KeypadChar(Keycode key, SDL_Keymod mods) noexcept;
	/// Texte saisi cette image (UTF-8, concaténé).
	[[nodiscard]] std::string_view TextInput() const noexcept { return m_text; }
	/// Touches pressées cette image (répétition comprise) — raccourcis.
	[[nodiscard]] std::span<const Keycode> PressedKeycodes() const noexcept { return m_pressedKeys; }
	/// Touches relâchées cette image.
	[[nodiscard]] std::span<const Keycode> ReleasedKeycodes() const noexcept { return m_releasedKeys; }
	/// Vrai si la touche `key` a été pressée cette image (répétition comprise).
	[[nodiscard]] bool KeycodePressed(Keycode key) const noexcept {
		return std::find(m_pressedKeys.begin(), m_pressedKeys.end(), key) != m_pressedKeys.end();
	}
	/// Vrai si la touche `key` vient d'être enfoncée cette image (premier appui,
	/// hors répétition). Contrairement à `Key(key).IsJustPressed()`, reste vrai
	/// même si la touche a été relâchée dans la même image.
	[[nodiscard]] bool KeycodeJustPressed(Keycode key) const noexcept { return m_justPressedKeys.contains(key); }

	// ── Glisser-déposer ──────────────────────────────────────────────────────
	/// Fichiers déposés cette image (SDL_EVENT_DROP_FILE).
	[[nodiscard]] std::span<const std::string> DroppedFiles() const noexcept { return m_droppedFiles; }
	/// Textes déposés cette image (SDL_EVENT_DROP_TEXT).
	[[nodiscard]] std::span<const std::string> DroppedText() const noexcept { return m_droppedText; }

private:
	struct KeyBinding {
		std::vector<Keycode> keys;
		KeyComboCallback cb;
	};
	struct DeviceBinding {
		DeviceType type;
		DeviceEvent event;
		DeviceCallback cb;
	};
	struct WindowBinding {
		WindowEvent event;
		WindowCallback cb;
	};
	struct DisplayBinding {
		DisplayEvent event;
		DisplayCallback cb;
	};
	struct GenericBinding {
		EventType type;
		GenericCallback cb;
	};

	void FireBindings();
	/// Recalcule le geste multi-doigts (pincement/rotation/balayage) à partir des
	/// doigts courants et de ceux de l'image précédente.
	void ComputeGesture();

	// Touches / boutons « enfoncés » (courant et image précédente).
	std::unordered_set<Keycode> m_downKeys, m_prevDownKeys;
	std::unordered_set<SDL_Scancode> m_downScans, m_prevDownScans;
	std::unordered_set<uint8_t> m_downMouse, m_prevDownMouse;
	std::unordered_set<SDL_GamepadButton> m_downGamepad, m_prevDownGamepad;

	// État d'entrée continu (souris, modificateurs) et par-image (molette, texte…).
	float m_mouseX = 0.f, m_mouseY = 0.f;
	float m_mouseRelX = 0.f, m_mouseRelY = 0.f;
	float m_wheelX = 0.f, m_wheelY = 0.f;
	std::string m_text;
	bool m_ctrl = false, m_shift = false, m_alt = false;
	SDL_Keymod m_mods = SDL_KMOD_NONE;
	std::vector<Keycode> m_pressedKeys;		///< pressées cette image (répétition comprise)
	std::vector<Keycode> m_releasedKeys;	///< relâchées cette image
	std::unordered_set<Keycode> m_justPressedKeys; ///< enfoncées cette image (premier appui, hors répétition)
	std::vector<std::string> m_droppedFiles; ///< fichiers déposés cette image
	std::vector<std::string> m_droppedText;	 ///< textes déposés cette image

	// Périphériques branchés (courant) et évènements de l'image.
	std::unordered_set<DeviceId> m_plugged[size_t(DeviceType::Count)];
	std::vector<DeviceId> m_justPlugged[size_t(DeviceType::Count)];
	std::vector<DeviceId> m_justUnplugged[size_t(DeviceType::Count)];
	bool m_seeded = false; ///< première image : recense les périphériques déjà branchés

	// Écrans connectés (courant) et évènements de l'image.
	std::unordered_set<DisplayID> m_displays;
	std::vector<DisplayID> m_justAddedDisplays, m_justRemovedDisplays;
	bool m_displaysSeeded = false;

	// Doigts (pavé tactile des manettes, écran tactile) actuellement posés.
	std::vector<TouchpadFinger> m_touchpadFingers;
	std::vector<TouchFinger> m_touchFingers;
	// Doigts de l'image précédente (calcul du geste multi-doigts).
	std::vector<TouchpadFinger> m_prevTouchpadFingers;
	std::vector<TouchFinger> m_prevTouchFingers;
	Gesture m_gesture;
	// Pincement natif (SDL_EVENT_PINCH_*, pavé tactile d'ordinateur) de l'image.
	bool m_pinchActive = false;
	float m_pinchScale = 1.f;

	bool m_quit = false;

	std::unordered_map<size_t, KeyBinding> m_keyBindings;
	std::unordered_map<size_t, DeviceBinding> m_deviceBindings;
	std::unordered_map<size_t, WindowBinding> m_windowBindings;
	std::unordered_map<size_t, DisplayBinding> m_displayBindings;
	std::unordered_map<size_t, GenericBinding> m_genericBindings;
	size_t m_nextId = 1;
};

} // namespace sdl3
