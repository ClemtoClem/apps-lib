#pragma once
#include <cstdint>

#include "consoles/arm/file.hpp"

namespace consoles::arm {

class Core;

/// Sentinel "return addresses" patched into a ROM's DLDI driver stub in
/// place of real startup/isInserted/readSectors/writeSectors/clearStatus/
/// shutdown function pointers — CPU::HandleReserved() recognizes a branch to
/// one of these (the reserved 0xF condition code makes the address read back
/// as its own value on open bus, so PC == opcode here) and HLEs it instead of
/// executing real code.
enum DldiFunc {
    DLDI_START = 0xF0000000,
    DLDI_INSERT,
    DLDI_READ,
    DLDI_WRITE,
    DLDI_CLEAR,
    DLDI_STOP
};

/**
 * @brief HLE DLDI driver: patches a homebrew ROM's DLDI reserved area (if
 * present) to redirect its SD-card I/O calls here, backed by a flat image
 * file (core->settings.sdImagePath). No-op for ROMs without a DLDI header
 * (PatchRom() just doesn't find the magic signature and leaves the ROM as-is).
 */
class Dldi {
  public:
    Dldi(Core *core_) : m_core(core_) {
    }

    void PatchRom(uint8_t *rom, uint32_t offset, uint32_t size);
    bool IsPatched() {
        return m_patched;
    }

    int Startup();
    int IsInserted();
    int ReadSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf);
    int WriteSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf);
    int ClearStatus();
    int Shutdown();

  private:
    Core *m_core;
    bool  m_patched = false;
    File m_sdImage;
};

} // namespace consoles::arm

