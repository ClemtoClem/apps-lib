
#include <cstring>
#include "consoles/arm/core.hpp"
#include "consoles/arm/defines.hpp"
#include "consoles/arm/hle/dldi.hpp"
#include "consoles/arm/settings.hpp"

#include <vector>

namespace consoles::arm {

namespace {
/// Taille de l'en-tête DLDI lu et réécrit par PatchRom (derniers champs à +0x94).
constexpr uint32_t kDldiHeaderSize = 0x98;
/// Plafond d'un transfert demandé par le jeu (le jeu choisit `numSectors`) : 16 Mio.
constexpr uint32_t kMaxSectorsPerTransfer = 1u << 15;
} // namespace

void Dldi::PatchRom(uint8_t *rom, uint32_t offset, uint32_t size) {
    // Scan the ROM for DLDI drivers and patch them if found. La ROM est une donnée non
    // fiable : l'en-tête complet doit tenir dans l'image, sinon lecture/écriture hors tampon.
    if (!rom || size < kDldiHeaderSize)
        return;
    for (uint32_t i = 0; i <= size - kDldiHeaderSize; i += 4) {
        // Check for the DLDI magic number
        if (uint32_t(U8TO32(rom, i)) != 0xBF8DA5ED)
            continue;

        // Check for the DLDI magic string
        const char *str = " Chishm\0";
        bool        match = true;
        for (size_t j = 0; j < 8; j++) {
            if (rom[i + 4 + j] != uint8_t(str[j])) {
                match = false;
                break;
            }
        }
        if (!match)
            continue;

        // Ensure there's room to patch the DLDI driver
        if (rom[i + 0x0F] < 0x08) { // Space in ROM
            LOG("Not enough space to patch DLDI driver at ROM offset 0x%X\n", offset + i);
            break;
        }

        // Patch the DLDI driver to use the HLE functions
        rom[i + 0x0C] = 0x01;                              // DLDI driver version
        rom[i + 0x0D] = 0x08;                              // Size of driver in terms of 1 << n (256 bytes)
        rom[i + 0x0E] = 0x00;                              // Sections to adjust
        strcpy((char *)&rom[i + 0x10], "NooDS DLDI");       // Long driver name
        uint32_t address = U8TO32(rom, i + 0x40);           // Address of driver
        U32TO8(rom, i + 0x44, address + 0x98);              // End of driver code
        U32TO8(rom, i + 0x58, address + 0x98);              // Start of BSS area
        U32TO8(rom, i + 0x5C, address + 0x98);              // End of BSS area
        memcpy(&rom[i + 0x60], "NOOD", 4);                  // Short driver name
        U32TO8(rom, i + 0x64, 0x00000023);                  // Feature flags (read, write, NDS slot)
        U32TO8(rom, i + 0x68, address + 0x80);              // Address of Startup()
        U32TO8(rom, i + 0x6C, address + 0x84);              // Address of IsInserted()
        U32TO8(rom, i + 0x70, address + 0x88);              // Address of ReadSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x74, address + 0x8C);              // Address of WriteSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x78, address + 0x90);              // Address of ClearStatus()
        U32TO8(rom, i + 0x7C, address + 0x94);              // Address of Shutdown()
        U32TO8(rom, i + 0x80, uint32_t(DLDI_START));         // Startup()
        U32TO8(rom, i + 0x84, uint32_t(DLDI_INSERT));        // IsInserted()
        U32TO8(rom, i + 0x88, uint32_t(DLDI_READ));          // ReadSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x8C, uint32_t(DLDI_WRITE));         // WriteSectors(sector, numSectors, buf)
        U32TO8(rom, i + 0x90, uint32_t(DLDI_CLEAR));         // ClearStatus()
        U32TO8(rom, i + 0x94, uint32_t(DLDI_STOP));          // Shutdown()

        LOG("Patched DLDI driver at ROM offset 0x%X\n", offset + i);
        m_patched = true;
    }
}

int Dldi::Startup() {
    auto file = File::Open(m_core->settings.sdImagePath, "rb+");
    if (!file)
        return 0;
    m_sdImage = std::move(*file);
    return 1;
}

int Dldi::IsInserted() {
    return m_sdImage.IsOpen() ? 1 : 0;
}

int Dldi::ReadSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf) {
    if (!m_sdImage.IsOpen() || numSectors > kMaxSectorsPerTransfer)
        return 0;
    const uint64_t offset = uint64_t(sector) << 9;
    const uint64_t size   = uint64_t(numSectors) << 9;

    std::vector<uint8_t> data(size);
    m_sdImage.Seek(int64_t(offset));
    (void)m_sdImage.Read(data.data(), size);

    for (uint64_t i = 0; i < size; i++)
        m_core->memory.Write<uint8_t>(cpu, buf + uint32_t(i), data[i]);
    return 1;
}

int Dldi::WriteSectors(bool cpu, uint32_t sector, uint32_t numSectors, uint32_t buf) {
    if (!m_sdImage.IsOpen() || numSectors > kMaxSectorsPerTransfer)
        return 0;
    const uint64_t offset = uint64_t(sector) << 9;
    const uint64_t size   = uint64_t(numSectors) << 9;

    std::vector<uint8_t> data(size);
    for (uint64_t i = 0; i < size; i++)
        data[i] = m_core->memory.Read<uint8_t>(cpu, buf + uint32_t(i));

    m_sdImage.Seek(int64_t(offset));
    m_sdImage.Write(data.data(), size);
    return 1;
}

int Dldi::ClearStatus() {
    return m_sdImage.IsOpen() ? 1 : 0;
}

int Dldi::Shutdown() {
    if (!m_sdImage.IsOpen())
        return 0;
    m_sdImage.Reset();
    return 1;
}

} // namespace consoles::arm
