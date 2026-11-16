// Définitions de sdl3/joystick.hpp
// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.
#include <sdl3/sdl3.hpp>
#include "sdl3/input/joystick.hpp"

namespace sdl3 {

namespace joystick {

std::vector<JoystickID> Enumerated() {
    int count = 0;
    JoystickID *ids = SDL_GetJoysticks(&count);
    if (!ids)
        return {};
    std::vector<JoystickID> v(ids, ids + count);
    SDL_free(ids);
    return v;
}

} // namespace joystick

// ── Joystick ─────────────────────────────────────────────────────────────────

Result<Joystick, StringView> Joystick::Open(JoystickID id) {
    auto *j = SDL_OpenJoystick(id);
    if (!j)
        return Err(GetError());
    return Ok(Joystick(j));
}

SDL_PropertiesID Joystick::Properties() const noexcept {
    return mHandle ? SDL_GetJoystickProperties(mHandle) : 0;
}

uint16_t Joystick::GetProductVersion() const noexcept {
    return mHandle ? SDL_GetJoystickProductVersion(mHandle) : 0;
}

uint16_t Joystick::FirmwareVersion() const noexcept {
    return mHandle ? SDL_GetJoystickFirmwareVersion(mHandle) : 0;
}

JoystickType Joystick::GetType() const noexcept {
    return mHandle ? JoystickType(SDL_GetJoystickType(mHandle)) : JoystickType::UNKNOWN;
}

JoystickConnection Joystick::ConnectionState() const noexcept {
    return mHandle ? JoystickConnection(SDL_GetJoystickConnectionState(mHandle)) : JoystickConnection::INVALID;
}

Option<Sint16> Joystick::AxisInitialState(int index) const {
    Sint16 state = 0;
    if (!mHandle || !SDL_GetJoystickAxisInitialState(mHandle, index, &state))
        return NONE;
    return Some(state);
}

Option<BallDelta> Joystick::Ball(int index) const {
    BallDelta d;
    if (!mHandle || !SDL_GetJoystickBall(mHandle, index, &d.dx, &d.dy))
        return NONE;
    return Some(d);
}

bool Joystick::Rumble(uint16_t lowHz, uint16_t highHz, uint32_t durationMs) noexcept {
    return mHandle && SDL_RumbleJoystick(mHandle, lowHz, highHz, durationMs);
}

bool Joystick::RumbleTriggers(uint16_t left, uint16_t right, uint32_t durationMs) noexcept {
    return mHandle && SDL_RumbleJoystickTriggers(mHandle, left, right, durationMs);
}

bool Joystick::SendEffect(const void *data, int size) noexcept {
    return mHandle && SDL_SendJoystickEffect(mHandle, data, size);
}

sdl3::PowerInfo Joystick::PowerInfo() const noexcept {
    int p = -1;
    PowerState st = mHandle ? PowerState(SDL_GetJoystickPowerInfo(mHandle, &p)) : PowerState::UNKNOWN;
    return {st, p >= 0 ? Some(p) : Option<int>(NONE)};
}

namespace joystick {

Result<JoystickID, StringView> AttachVirtual(const SDL_VirtualJoystickDesc &desc) {
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0)
        return Err(GetError());
    return Ok(id);
}

} // namespace joystick

} // namespace sdl3
