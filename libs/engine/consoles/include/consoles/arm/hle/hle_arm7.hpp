#pragma once
#include <cstdint>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;

/**
 * @brief High-level emulation of the ARM7 side: instead of interpreting a
 * real ARM7 BIOS/firmware image, the ARM7 CPU is permanently halted and this
 * class directly stubs out the IPC (inter-processor communication) protocol
 * games use to ask the ARM7 "firmware" for touch-screen/extra-key input —
 * letting an NDS session boot with no firmware/ARM7 BIOS dump at all
 * (core->settings.arm7Hle, opt-in). GBA sessions are unaffected.
 */
class HleArm7 {
  public:
    HleArm7(Core *core_) : m_core(core_) {
    }

    /// Halts the ARM7 permanently and sets the initial IPC handshake state —
    /// called once from Core's constructor when core->settings.arm7Hle is set.
    void Init();

    void IpcSync(uint8_t value);
    void IpcFifo(uint32_t value);

    /// Per-frame housekeeping (auto key/touch polling) — called once per NDS
    /// frame from Core::EndFrame().
    void RunFrame();

    void IoState(StateArchive &archive) {
        archive.Io(m_inited);
        archive.Io(m_autoTouch);
    }

  private:
    Core *m_core;
    bool  m_inited    = false;
    bool  m_autoTouch = false;

    void PollTouch(uint32_t value);
};

} // namespace consoles::arm

