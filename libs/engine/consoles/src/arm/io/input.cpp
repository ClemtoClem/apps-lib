#include "consoles/arm/io/input.hpp"

#include "consoles/arm/core.hpp"

namespace consoles::arm {


void Input::PressKey(int key) {
    if (key < 10)
        m_keyInput = static_cast<uint16_t>(m_keyInput & ~BIT(key));
    else if (key < 12)
        m_extKeyIn = static_cast<uint16_t>(m_extKeyIn & ~BIT(key - 10));
}


void Input::ReleaseKey(int key) {
    if (key < 10)
        m_keyInput |= BIT(key);
    else if (key < 12)
        m_extKeyIn |= BIT(key - 10);
}


void Input::PressScreen() {
    m_extKeyIn = static_cast<uint16_t>(m_extKeyIn & ~BIT(6)); // bit tactile enfoncé
}


void Input::ReleaseScreen() {
    m_extKeyIn |= BIT(6); // relâché
}

} // namespace consoles::arm

