#include "consoles/arm/hle/hle_arm7.hpp"

#include "consoles/arm/core.hpp"
#include "consoles/arm/defines.hpp"

namespace consoles::arm {

void HleArm7::Init() {
    // Permanently halt the ARM7 and set initial IPC state
    m_core->interpreter[1].Halt(2);
    m_core->ipc.WriteIpcSync(1, 0xFFFF, 0x700);
    m_core->ipc.WriteIpcFifoCnt(1, 0xFFFF, 0x8000);
}

void HleArm7::IpcSync(uint8_t value) {
    // Catch unhandled HLE IPC sync requests
    if (m_inited) {
        LOG("Unhandled HLE IPC sync sent after initialization\n");
        return;
    }

    // During init, decrement the sync value and send it back
    if (value > 0) {
        m_core->ipc.WriteIpcSync(1, 0xFFFF, uint16_t((value - 1) << 8));
        return;
    }

    // Set subsystem init flags and finish the init process
    m_core->memory.Write<uint32_t>(1, 0x2FFFF8C, 0x3FFF0);
    m_inited = true;
}

void HleArm7::IpcFifo(uint32_t value) {
    // Handle FIFO commands based on the subsystem tag
    if (!m_inited)
        return;
    switch (value & 0x1F) { // Subsystem
        case 0x6: // Touch screen
            // Poll touch input manually or enable auto-polling
            if ((value & 0xC0000000) == 0xC0000000) {
                PollTouch(value | BIT(21));
            } else if ((value & 0xC0000000) == 0x40000000) {
                m_autoTouch = (value & BIT(22)) != 0;
                PollTouch(0xC0204006);
            }
            return;

        default:
            // Stub unknown FIFO commands by replying with the same value
            LOG_VERBOSE("Unknown HLE IPC FIFO command: 0x%X\n", value);
            m_core->ipc.WriteIpcFifoSend(1, 0xFFFFFFFF, value);
            return;
    }
}

void HleArm7::RunFrame() {
    // Automatically poll extra keys and touch if enabled
    if (!m_inited)
        return;
    m_core->memory.Write<uint16_t>(1, 0x2FFFFA8, uint16_t((m_core->input.ReadExtKeyIn() & 0xB) << 10));
    if (m_autoTouch)
        PollTouch(0xC0240006);
}

void HleArm7::PollTouch(uint32_t value) {
    // Update touch values in shared memory and send a FIFO reply
    if (m_core->input.ReadExtKeyIn() & BIT(6)) { // Released
        m_core->memory.Write<uint16_t>(1, 0x2FFFFAA, 0x000);
        m_core->memory.Write<uint16_t>(1, 0x2FFFFAC, 0x600);
    } else { // Pressed
        m_core->memory.Write<uint16_t>(1, 0x2FFFFAA, m_core->spi.ReadTouchX());
        m_core->memory.Write<uint16_t>(1, 0x2FFFFAC, uint16_t((m_core->spi.ReadTouchY() >> 4) | 0x100));
    }
    m_core->ipc.WriteIpcFifoSend(1, 0xFFFFFFFF, value);
}

} // namespace consoles::arm
