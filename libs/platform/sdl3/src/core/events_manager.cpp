// Définitions de sdl3/events_manager.hpp
#include <algorithm>
#include <iterator>

#include <sdl3/sdl3.hpp>
#include "sdl3/core/events_manager.hpp"

namespace sdl3 {

namespace {

[[nodiscard]] constexpr TriggerState ComputeState(bool wasDown, bool isDown) noexcept {
	if (isDown)
		return wasDown ? TriggerState::Activated : TriggerState::JustActivated;
	return wasDown ? TriggerState::JustDeactivated : TriggerState::Deactivated;
}

/// Extrait l'identifiant d'un périphérique selon le type d'évènement.
[[nodiscard]] DeviceId DeviceIdOf(const SDL_Event &e, DeviceType &type) noexcept {
	switch (e.type) {
	case SDL_EVENT_MOUSE_ADDED:
	case SDL_EVENT_MOUSE_REMOVED: type = DeviceType::Mouse; return e.mdevice.which;
	case SDL_EVENT_KEYBOARD_ADDED:
	case SDL_EVENT_KEYBOARD_REMOVED: type = DeviceType::Keyboard; return e.kdevice.which;
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED: type = DeviceType::Gamepad; return e.gdevice.which;
	case SDL_EVENT_JOYSTICK_ADDED:
	case SDL_EVENT_JOYSTICK_REMOVED: type = DeviceType::Joystick; return e.jdevice.which;
	case SDL_EVENT_CAMERA_DEVICE_ADDED:
	case SDL_EVENT_CAMERA_DEVICE_REMOVED: type = DeviceType::Camera; return e.cdevice.which;
	case SDL_EVENT_AUDIO_DEVICE_ADDED:
	case SDL_EVENT_AUDIO_DEVICE_REMOVED: type = DeviceType::Audio; return e.adevice.which;
	default: type = DeviceType::Mouse; return 0;
	}
}

[[nodiscard]] bool IsDeviceAdded(uint32_t type) noexcept {
	switch (type) {
	case SDL_EVENT_MOUSE_ADDED:
	case SDL_EVENT_KEYBOARD_ADDED:
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_JOYSTICK_ADDED:
	case SDL_EVENT_CAMERA_DEVICE_ADDED:
	case SDL_EVENT_AUDIO_DEVICE_ADDED: return true;
	default: return false;
	}
}

[[nodiscard]] Option<WindowEvent> WindowEventOf(uint32_t type) noexcept {
	switch (type) {
	case SDL_EVENT_WINDOW_RESIZED: return Some(WindowEvent::Resize);
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return Some(WindowEvent::PixelResize);
	case SDL_EVENT_WINDOW_MOVED: return Some(WindowEvent::Move);
	case SDL_EVENT_WINDOW_FOCUS_GAINED: return Some(WindowEvent::FocusGained);
	case SDL_EVENT_WINDOW_FOCUS_LOST: return Some(WindowEvent::FocusLost);
	case SDL_EVENT_WINDOW_MINIMIZED: return Some(WindowEvent::Minimized);
	case SDL_EVENT_WINDOW_MAXIMIZED: return Some(WindowEvent::Maximized);
	case SDL_EVENT_WINDOW_RESTORED: return Some(WindowEvent::Restored);
	case SDL_EVENT_WINDOW_SHOWN: return Some(WindowEvent::Shown);
	case SDL_EVENT_WINDOW_HIDDEN: return Some(WindowEvent::Hidden);
	case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: return Some(WindowEvent::EnterFullscreen);
	case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: return Some(WindowEvent::LeaveFullscreen);
	default: return NONE;
	}
}

[[nodiscard]] Option<DisplayEvent> DisplayEventOf(uint32_t type) noexcept {
	switch (type) {
	case SDL_EVENT_DISPLAY_ADDED: return Some(DisplayEvent::Added);
	case SDL_EVENT_DISPLAY_REMOVED: return Some(DisplayEvent::Removed);
	case SDL_EVENT_DISPLAY_MOVED: return Some(DisplayEvent::Moved);
	case SDL_EVENT_DISPLAY_ORIENTATION: return Some(DisplayEvent::Orientation);
	case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED: return Some(DisplayEvent::DesktopModeChanged);
	case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: return Some(DisplayEvent::CurrentModeChanged);
	case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: return Some(DisplayEvent::ContentScaleChanged);
	case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED: return Some(DisplayEvent::UsableBoundsChanged);
	default: return NONE;
	}
}

/// True pour les évènements qui signalent qu'un périphérique a été DÉBRANCHÉ.
[[nodiscard]] bool IsDeviceRemoved(uint32_t type) noexcept {
	switch (type) {
	case SDL_EVENT_MOUSE_REMOVED:
	case SDL_EVENT_KEYBOARD_REMOVED:
	case SDL_EVENT_GAMEPAD_REMOVED:
	case SDL_EVENT_JOYSTICK_REMOVED:
	case SDL_EVENT_CAMERA_DEVICE_REMOVED:
	case SDL_EVENT_AUDIO_DEVICE_REMOVED: return true;
	default: return false;
	}
}

/// Ajoute à `set` tous les identifiants renvoyés par `query`.
template <typename Fn> void FillIds(std::unordered_set<DeviceId> &set, Fn &&query) {
	int count = 0;
	DeviceId *ids = query(&count);
	if (!ids)
		return;
	for (int i = 0; i < count; ++i)
		set.insert(ids[i]);
	SDL_free(ids);
}

/// Première image : recense les périphériques déjà branchés.
void SeedDevices(std::unordered_set<DeviceId> (&plugged)[size_t(DeviceType::Count)]) {
	FillIds(plugged[size_t(DeviceType::Mouse)], SDL_GetMice);
	FillIds(plugged[size_t(DeviceType::Keyboard)], SDL_GetKeyboards);
	FillIds(plugged[size_t(DeviceType::Gamepad)], SDL_GetGamepads);
	FillIds(plugged[size_t(DeviceType::Joystick)], SDL_GetJoysticks);
	FillIds(plugged[size_t(DeviceType::Camera)], SDL_GetCameras);
}

} // namespace

// ============================================================================
// Poll
// ============================================================================

void EventsManager::Poll() {
	// Première image : recense les périphériques déjà branchés (états initiaux).
	if (!m_seeded) {
		SeedDevices(m_plugged);
		m_seeded = true;
	}
	if (!m_displaysSeeded) {
		for (DisplayID id : sdl3::GetDisplays())
			m_displays.insert(id);
		m_displaysSeeded = true;
	}

	// Bascule des états « enfoncés » : l'image précédente devient la référence.
	m_prevDownKeys = m_downKeys;
	m_prevDownScans = m_downScans;
	m_prevDownMouse = m_downMouse;
	m_prevDownGamepad = m_downGamepad;
	for (size_t i = 0; i < size_t(DeviceType::Count); ++i) {
		m_justPlugged[i].clear();
		m_justUnplugged[i].clear();
	}
	m_justAddedDisplays.clear();
	m_justRemovedDisplays.clear();
	m_quit = false;
	m_wheelX = m_wheelY = 0.f;
	m_mouseRelX = m_mouseRelY = 0.f;
	m_text.clear();
	m_pressedKeys.clear();
	std::string keypad; // caractères du pavé numérique attendus dans la saisie de l'image
	m_droppedFiles.clear();
	m_droppedText.clear();

	// Drainage UNIQUE de la file d'évènements.
	while (auto ev = sdl3::PollEvent()) {
		const SDL_Event &e = ev->raw;
		switch (e.type) {
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			m_quit = true;
			break;
		case SDL_EVENT_KEY_DOWN:
			if (!e.key.repeat) {
				m_downKeys.insert(e.key.key);
				m_downScans.insert(e.key.scancode);
			}
			m_mods = e.key.mod;
			m_ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
			m_shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
			m_alt = (e.key.mod & SDL_KMOD_ALT) != 0;
			m_pressedKeys.push_back(e.key.key); // répétition comprise (raccourcis)
			if (const char c = KeypadChar(e.key.key, e.key.mod); c != 0)
				keypad += c;
			break;
		case SDL_EVENT_KEY_UP:
			m_downKeys.erase(e.key.key);
			m_downScans.erase(e.key.scancode);
			m_mods = e.key.mod;
			m_ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
			m_shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
			m_alt = (e.key.mod & SDL_KMOD_ALT) != 0;
			break;
		case SDL_EVENT_MOUSE_MOTION:
			m_mouseX = e.motion.x;
			m_mouseY = e.motion.y;
			m_mouseRelX += e.motion.xrel;
			m_mouseRelY += e.motion.yrel;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			m_downMouse.insert(e.button.button);
			m_mouseX = e.button.x;
			m_mouseY = e.button.y;
			break;
		case SDL_EVENT_MOUSE_BUTTON_UP:
			m_downMouse.erase(e.button.button);
			m_mouseX = e.button.x;
			m_mouseY = e.button.y;
			break;
		case SDL_EVENT_MOUSE_WHEEL:
			m_wheelX += e.wheel.x;
			m_wheelY += e.wheel.y;
			break;
		case SDL_EVENT_TEXT_INPUT:
			m_text += e.text.text;
			break;
		case SDL_EVENT_DROP_FILE:
			if (e.drop.data)
				m_droppedFiles.emplace_back(e.drop.data);
			break;
		case SDL_EVENT_DROP_TEXT:
			if (e.drop.data)
				m_droppedText.emplace_back(e.drop.data);
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
			m_downGamepad.insert(static_cast<SDL_GamepadButton>(e.gbutton.button));
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			m_downGamepad.erase(static_cast<SDL_GamepadButton>(e.gbutton.button));
			break;
		case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
		case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: {
			// Pavé tactile d'une manette : on suit chaque doigt (which, touchpad, finger).
			const JoystickID which = e.gtouchpad.which;
			const int pad = int(e.gtouchpad.touchpad);
			const int finger = int(e.gtouchpad.finger);
			auto it = std::find_if(m_touchpadFingers.begin(), m_touchpadFingers.end(), [&](const TouchpadFinger &f) {
				return f.which == which && f.touchpad == pad && f.finger == finger;
			});
			if (e.type == SDL_EVENT_GAMEPAD_TOUCHPAD_UP) {
				if (it != m_touchpadFingers.end())
					m_touchpadFingers.erase(it);
			} else {
				if (it == m_touchpadFingers.end()) {
					TouchpadFinger f;
					f.which = which;
					f.touchpad = pad;
					f.finger = finger;
					m_touchpadFingers.push_back(f);
					it = std::prev(m_touchpadFingers.end());
				}
				it->x = e.gtouchpad.x;
				it->y = e.gtouchpad.y;
				it->pressure = e.gtouchpad.pressure;
			}
			break;
		}
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED: {
			// Écran tactile : suivi de chaque doigt (touch, finger).
			const SDL_TouchID touch = e.tfinger.touchID;
			const SDL_FingerID finger = e.tfinger.fingerID;
			auto it = std::find_if(m_touchFingers.begin(), m_touchFingers.end(), [&](const TouchFinger &f) {
				return f.touch == touch && f.finger == finger;
			});
			if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
				if (it != m_touchFingers.end())
					m_touchFingers.erase(it);
			} else {
				if (it == m_touchFingers.end()) {
					TouchFinger f;
					f.touch = touch;
					f.finger = finger;
					m_touchFingers.push_back(f);
					it = std::prev(m_touchFingers.end());
				}
				it->x = e.tfinger.x;
				it->y = e.tfinger.y;
				it->dx = e.tfinger.dx;
				it->dy = e.tfinger.dy;
				it->pressure = e.tfinger.pressure;
				it->window = e.tfinger.windowID;
			}
			break;
		}
		default: {
			// Périphériques branchés/débranchés.
			DeviceType type;
			if (IsDeviceAdded(e.type) || IsDeviceRemoved(e.type)) {
				const DeviceId id = DeviceIdOf(e, type);
				if (IsDeviceAdded(e.type)) {
					m_plugged[size_t(type)].insert(id);
					m_justPlugged[size_t(type)].push_back(id);
				} else {
					m_plugged[size_t(type)].erase(id);
					m_justUnplugged[size_t(type)].push_back(id);
				}
			}
			// Fenêtres.
			if (auto we = WindowEventOf(e.type); we.IsSome()) {
				for (auto &[id, b] : m_windowBindings)
					if (b.event == *we) {
						SDL_Window *window = SDL_GetWindowFromID(e.window.windowID);
						int v1 = 0, v2 = 0;
						if (*we == WindowEvent::Resize || *we == WindowEvent::PixelResize) {
							v1 = int(e.window.data1);
							v2 = int(e.window.data2);
						} else if (*we == WindowEvent::Move) {
							v1 = int(e.window.data1);
							v2 = int(e.window.data2);
						}
						if (b.cb)
							b.cb(window, v1, v2);
					}
			}
			// Écrans.
			if (auto de = DisplayEventOf(e.type); de.IsSome()) {
				const DisplayID display = e.display.displayID;
				if (*de == DisplayEvent::Added) {
					m_displays.insert(display);
					m_justAddedDisplays.push_back(display);
				} else if (*de == DisplayEvent::Removed) {
					m_displays.erase(display);
					m_justRemovedDisplays.push_back(display);
				}
				for (auto &[id, b] : m_displayBindings)
					if (b.event == *de && b.cb)
						b.cb(display, int(e.display.data1), int(e.display.data2));
			}
			break;
		}
		}
		// Liaisons génériques (tout type d'évènement non couvert par l'API dédiée).
		for (auto &[id, b] : m_genericBindings)
			if (b.type == static_cast<EventType>(e.type) && b.cb)
				b.cb(*ev);
	}

	// Pavé numérique : un chiffre sans évènement de saisie correspondant (saisie de texte non
	// démarrée, méthode de saisie qui l'ignore) est ajouté au texte de l'image, sans doublon.
	std::string received = m_text; // saisie reçue de SDL, chaque caractère apparié une seule fois
	for (const char c : keypad) {
		if (const size_t at = received.find(c); at != std::string::npos)
			received[at] = '\0';
		else
			m_text += c;
	}

	// Liaisons clavier (combinaisons).
	FireBindings();
}

char EventsManager::KeypadChar(Keycode key, SDL_Keymod mods) noexcept {
	if ((mods & SDL_KMOD_NUM) == 0 || (mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0)
		return 0; // sans Verr. num., le pavé sert au déplacement
	switch (key) {
	case SDLK_KP_0: return '0';
	case SDLK_KP_1: return '1';
	case SDLK_KP_2: return '2';
	case SDLK_KP_3: return '3';
	case SDLK_KP_4: return '4';
	case SDLK_KP_5: return '5';
	case SDLK_KP_6: return '6';
	case SDLK_KP_7: return '7';
	case SDLK_KP_8: return '8';
	case SDLK_KP_9: return '9';
	case SDLK_KP_PERIOD: return '.';
	case SDLK_KP_COMMA: return ',';
	case SDLK_KP_PLUS: return '+';
	case SDLK_KP_MINUS: return '-';
	case SDLK_KP_MULTIPLY: return '*';
	case SDLK_KP_DIVIDE: return '/';
	default: return 0;
	}
}

// ============================================================================
// Lecture d'état
// ============================================================================

Trigger EventsManager::Key(SDL_Keycode key) const noexcept {
	return ComputeState(m_prevDownKeys.contains(key), m_downKeys.contains(key));
}

Trigger EventsManager::KeyScan(SDL_Scancode scan) const noexcept {
	return ComputeState(m_prevDownScans.contains(scan), m_downScans.contains(scan));
}

Trigger EventsManager::MouseButton(uint8_t button) const noexcept {
	return ComputeState(m_prevDownMouse.contains(button), m_downMouse.contains(button));
}

Trigger EventsManager::GamepadButton(SDL_GamepadButton button) const noexcept {
	return ComputeState(m_prevDownGamepad.contains(button), m_downGamepad.contains(button));
}

Trigger EventsManager::Device(DeviceType type) const noexcept {
	const size_t i = size_t(type);
	if (!m_justPlugged[i].empty())
		return TriggerState::JustActivated;
	if (!m_justUnplugged[i].empty())
		return TriggerState::JustDeactivated;
	return m_plugged[i].empty() ? TriggerState::Deactivated : TriggerState::Activated;
}

// ============================================================================
// Liaisons
// ============================================================================

size_t EventsManager::Bind(std::initializer_list<Keycode> keys, KeyComboCallback cb) {
	const size_t id = m_nextId++;
	KeyBinding b;
	b.keys.assign(keys);
	b.cb = std::move(cb);
	m_keyBindings.emplace(id, std::move(b));
	return id;
}

size_t EventsManager::Bind(DeviceType type, DeviceEvent event, DeviceCallback cb) {
	const size_t id = m_nextId++;
	DeviceBinding b;
	b.type = type;
	b.event = event;
	b.cb = std::move(cb);
	m_deviceBindings.emplace(id, std::move(b));
	return id;
}

size_t EventsManager::Bind(WindowEvent event, WindowCallback cb) {
	const size_t id = m_nextId++;
	WindowBinding b;
	b.event = event;
	b.cb = std::move(cb);
	m_windowBindings.emplace(id, std::move(b));
	return id;
}

size_t EventsManager::Bind(DisplayEvent event, DisplayCallback cb) {
	const size_t id = m_nextId++;
	DisplayBinding b;
	b.event = event;
	b.cb = std::move(cb);
	m_displayBindings.emplace(id, std::move(b));
	return id;
}

size_t EventsManager::Bind(EventType type, GenericCallback cb) {
	const size_t id = m_nextId++;
	GenericBinding b;
	b.type = type;
	b.cb = std::move(cb);
	m_genericBindings.emplace(id, std::move(b));
	return id;
}

void EventsManager::Unbind(size_t id) {
	m_keyBindings.erase(id);
	m_deviceBindings.erase(id);
	m_windowBindings.erase(id);
	m_displayBindings.erase(id);
	m_genericBindings.erase(id);
}

void EventsManager::UnbindAll() {
	m_keyBindings.clear();
	m_deviceBindings.clear();
	m_windowBindings.clear();
	m_displayBindings.clear();
	m_genericBindings.clear();
}

Option<TouchpadFinger> EventsManager::GetTouchpadFinger(JoystickID which, int touchpad, int finger) const {
	for (const TouchpadFinger &f : m_touchpadFingers)
		if (f.which == which && f.touchpad == touchpad && f.finger == finger)
			return Some(f);
	return NONE;
}

Option<TouchFinger> EventsManager::GetTouchFinger(SDL_TouchID touch, SDL_FingerID finger) const {
	for (const TouchFinger &f : m_touchFingers)
		if (f.touch == touch && f.finger == finger)
			return Some(f);
	return NONE;
}

void EventsManager::FireBindings() {
	// Combinaisons de touches : toutes enfoncées, la dernière venant d'être pressée.
	for (auto &[id, b] : m_keyBindings) {
		if (b.keys.empty() || !b.cb)
			continue;
		bool allDown = true;
		bool justPressed = false;
		for (Keycode k : b.keys) {
			if (!m_downKeys.contains(k)) {
				allDown = false;
				break;
			}
			if (!m_prevDownKeys.contains(k))
				justPressed = true;
		}
		if (!allDown || !justPressed)
			continue;
		std::map<Keycode, TriggerState> states;
		for (Keycode k : b.keys)
			states[k] = Key(k).state;
		b.cb(states);
	}

	// Périphériques branchés/débranchés.
	for (auto &[id, b] : m_deviceBindings) {
		if (!b.cb)
			continue;
		const size_t i = size_t(b.type);
		if (b.event == DeviceEvent::Plug)
			for (DeviceId did : m_justPlugged[i])
				b.cb(did);
		else
			for (DeviceId did : m_justUnplugged[i])
				b.cb(did);
	}
}

} // namespace sdl3
