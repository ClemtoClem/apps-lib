/* @file tests/test_events_manager.cpp */
// Tests du type `Trigger` / `TriggerState` (en-tête uniquement, sans SDL_Init).

#include "test.hpp"
#include <sdl3/sdl3.hpp>

TEST(trigger_state_values) {
	// DEACTIVATED = RELEASED = UNPLUGGED = 0, etc.
	CHECK(int(sdl3::TriggerState::Deactivated) == 0);
	CHECK(int(sdl3::TriggerState::JustActivated) == 1);
	CHECK(int(sdl3::TriggerState::Activated) == 2);
	CHECK(int(sdl3::TriggerState::JustDeactivated) == 3);
}

TEST(trigger_predicates) {
	const sdl3::Trigger justPressed{sdl3::TriggerState::JustActivated};
	CHECK(justPressed.IsJustPressed());
	CHECK(justPressed.IsPressed());   // JUST_PRESSED compte comme pressé
	CHECK(justPressed.IsDown());
	CHECK(justPressed.IsJustPlugged()); // alias

	const sdl3::Trigger pressed{sdl3::TriggerState::Activated};
	CHECK(pressed.IsPressed());
	CHECK(!pressed.IsJustPressed());
	CHECK(pressed.IsPlugged());

	const sdl3::Trigger justReleased{sdl3::TriggerState::JustDeactivated};
	CHECK(justReleased.IsJustReleased());
	CHECK(!justReleased.IsPressed());
	CHECK(justReleased.IsJustUnplugged());

	const sdl3::Trigger released{sdl3::TriggerState::Deactivated};
	CHECK(released.IsReleased());
	CHECK(!released.IsPressed());
	CHECK(released.IsUnplugged());
}

TEST(display_event_enum) {
	// Les évènements d'écran couvrent branchement, déplacement, orientation,
	// modes et échelle de contenu (valeurs distinctes dans un enum scoped).
	CHECK(int(sdl3::DisplayEvent::Added) != int(sdl3::DisplayEvent::Removed));
	CHECK(int(sdl3::DisplayEvent::Moved) != int(sdl3::DisplayEvent::Orientation));
	CHECK(int(sdl3::DisplayEvent::ContentScaleChanged) != int(sdl3::DisplayEvent::CurrentModeChanged));
	CHECK(int(sdl3::DisplayEvent::UsableBoundsChanged) != int(sdl3::DisplayEvent::DesktopModeChanged));
}

TEST(touch_finger_defaults) {
	// Doigts de l'écran tactile : coordonnées normalisées, aucune fenêtre.
	const sdl3::TouchFinger f;
	CHECK(f.touch == 0);
	CHECK(f.finger == 0);
	CHECK(f.x == 0.f && f.y == 0.f);
	CHECK(f.dx == 0.f && f.dy == 0.f);
	CHECK(f.pressure == 0.f);
	CHECK(f.window == 0);
}

TEST(touchpad_finger_defaults) {
	// Doigts du pavé tactile des manettes.
	const sdl3::TouchpadFinger f;
	CHECK(f.which == 0);
	CHECK(f.touchpad == 0);
	CHECK(f.finger == 0);
	CHECK(f.x == 0.f && f.y == 0.f);
	CHECK(f.pressure == 0.f);
}
