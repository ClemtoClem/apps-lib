#include "consoles/arm/memory/memory.hpp"

#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"
#include "consoles/arm/file.hpp"

#include <algorithm>
#include <cstring>

namespace consoles::arm {

#define DEF_IO_8(addr, func)                                                                                           \
    case addr:                                                                                                         \
        base -= addr;                                                                                                  \
        size = 1;                                                                                                      \
        func;                                                                                                          \
        break;
#define DEF_IO16(addr, func)                                                                                           \
    case addr:                                                                                                         \
    case addr + 1:                                                                                                     \
        base -= addr;                                                                                                  \
        size = 2;                                                                                                      \
        func;                                                                                                          \
        break;
#define DEF_IO32(addr, func)                                                                                           \
    case addr:                                                                                                         \
    case addr + 1:                                                                                                     \
    case addr + 2:                                                                                                     \
    case addr + 3:                                                                                                     \
        base -= addr;                                                                                                  \
        size = 4;                                                                                                      \
        func;                                                                                                          \
        break;
// Valeur d'écriture d'un registre : chaque gestionnaire la reçoit dans sa
// propre largeur (8, 16 ou 32 bits) — troncature explicite.
namespace {
struct IoWriteValue {
    uint32_t bits;
    template <typename T> operator T() const noexcept { return static_cast<T>(bits); }
};
} // namespace
#define IOWR_PARAMS8 IoWriteValue{data << (base * 8)}
#define IOWR_PARAMS  IoWriteValue{mask << (base * 8)}, IoWriteValue{data << (base * 8)}


void VramMapping::Add(uint8_t *mapping) {
    m_mappings[m_count++] = mapping;
}

template <typename T> T VramMapping::Read(uint32_t address) {
    T value = 0;
    for (int m = 0; m < m_count; m++) {
        for (unsigned int i = 0; i < sizeof(T); i++)
            value |= m_mappings[m][address + i] << (i * 8);
    }
    return value;
}

template <typename T> void VramMapping::Write(uint32_t address, T value) {
    for (int m = 0; m < m_count; m++) {
        for (unsigned int i = 0; i < sizeof(T); i++)
            m_mappings[m][address + i] = static_cast<uint8_t>(value >> (i * 8));
    }
}

bool Memory::LoadNdsBios9() {
    if (auto res = File::Open(m_core->settings.ndsBios9Path, "rb"); res) {
        (void)res->Read(m_ndsBios9, 0x1000);
        return true;
    }
    m_ndsBios9[3] = 0xFF;
    m_core->interpreter[0].SetBios(&m_core->ndsBios9);
    return false;
}

bool Memory::LoadNdsBios7() {
    if (auto res = File::Open(m_core->settings.ndsBios7Path, "rb"); res) {
        (void)res->Read(m_ndsBios7, 0x4000);
        return true;
    }
    m_ndsBios7[3] = 0xFF;
    m_core->interpreter[1].SetBios(&m_core->ndsBios7);
    return false;
}

bool Memory::LoadGbaBios() {
    if (auto res = File::Open(m_core->settings.gbaBiosPath, "rb"); res) {
        (void)res->Read(m_gbaBios, 0x4000);
        return true;
    }
    return false;
}

void Memory::UpdateMap9(uint32_t start, uint32_t end) {
    for (uint64_t address = start; address < end; address += 0x1000) {
        uint8_t *data = nullptr;
        if (m_core->cp15.GetItcmReadEnabled() && address < m_core->cp15.GetItcmSize()) {
            data = &m_instrTcm[address & 0x7FFF];
        } else if (m_core->cp15.GetDtcmReadEnabled() && address >= m_core->cp15.GetDtcmAddr() &&
                   address < m_core->cp15.GetDtcmAddr() + m_core->cp15.GetDtcmSize()) {
            data = &m_dataTcm[(address - m_core->cp15.GetDtcmAddr()) & 0x3FFF];
        } else {
            switch (address & 0xFF000000) {
                case 0x02000000:
                    data = &m_ram[address & 0x3FFFFF];
                    break;
                case 0x03000000:
                    switch (m_wramCnt) {
                        case 0:
                            data = &m_wram[(address & 0x7FFF)];
                            break;
                        case 1:
                            data = &m_wram[(address & 0x3FFF) + 0x4000];
                            break;
                        case 2:
                            data = &m_wram[(address & 0x3FFF)];
                            break;
                    }
                    break;
                case 0x06000000: {
                    VramMapping *mapping;
                    switch (address & 0xFFE00000) {
                        case 0x06000000:
                            mapping = &m_engABg[(address & 0x7FFFF) >> 14];
                            break;
                        case 0x06200000:
                            mapping = &m_engBBg[(address & 0x1FFFF) >> 14];
                            break;
                        case 0x06400000:
                            mapping = &m_engAObj[(address & 0x3FFFF) >> 14];
                            break;
                        case 0x06600000:
                            mapping = &m_engBObj[(address & 0x1FFFF) >> 14];
                            break;
                        default:
                            mapping = &m_lcdc[(address & 0xFFFFF) >> 14];
                            break;
                    }
                    if (mapping->GetCount() == 1)
                        data = &mapping->GetBaseMapping()[address & 0x3FFF];
                    break;
                }
                case 0x08000000:
                case 0x09000000:
                    data = m_core->cartridgeGba.GetRom(static_cast<uint32_t>(address));
                    break;
                case 0xFF000000:
                    if ((address & 0xFFFF8000) == 0xFFFF0000)
                        data = &m_ndsBios9[address & 0xFFFF];
                    break;
            }
        }
        m_readMap9[address >> 12] = data;
    }
    for (uint64_t address = start; address < end; address += 0x1000) {
        uint8_t *data = nullptr;
        if (m_core->cp15.GetItcmWriteEnabled() && address < m_core->cp15.GetItcmSize()) {
            data = &m_instrTcm[address & 0x7FFF];
        } else if (m_core->cp15.GetDtcmWriteEnabled() && address >= m_core->cp15.GetDtcmAddr() &&
                   address < m_core->cp15.GetDtcmAddr() + m_core->cp15.GetDtcmSize()) {
            data = &m_dataTcm[(address - m_core->cp15.GetDtcmAddr()) & 0x3FFF];
        } else {
            switch (address & 0xFF000000) {
                case 0x02000000:
                    data = &m_ram[address & 0x3FFFFF];
                    break;
                case 0x03000000:
                    switch (m_wramCnt) {
                        case 0:
                            data = &m_wram[(address & 0x7FFF)];
                            break;
                        case 1:
                            data = &m_wram[(address & 0x3FFF) + 0x4000];
                            break;
                        case 2:
                            data = &m_wram[(address & 0x3FFF)];
                            break;
                    }
                    break;
                case 0x06000000:
                    VramMapping *mapping;
                    switch (address & 0xFFE00000) {
                        case 0x06000000:
                            mapping = &m_engABg[(address & 0x7FFFF) >> 14];
                            break;
                        case 0x06200000:
                            mapping = &m_engBBg[(address & 0x1FFFF) >> 14];
                            break;
                        case 0x06400000:
                            mapping = &m_engAObj[(address & 0x3FFFF) >> 14];
                            break;
                        case 0x06600000:
                            mapping = &m_engBObj[(address & 0x1FFFF) >> 14];
                            break;
                        default:
                            mapping = &m_lcdc[(address & 0xFFFFF) >> 14];
                            break;
                    }
                    if (mapping->GetCount() == 1)
                        data = &mapping->GetBaseMapping()[address & 0x3FFF];
                    break;
            }
        }
        m_writeMap9[address >> 12] = data;
    }
}

void Memory::UpdateMap7(uint32_t start, uint32_t end) {
    for (uint64_t address = start; address < end; address += 0x1000) {
        uint8_t *data = nullptr;
        if (m_core->IsGbaMode()) {
            switch (address & 0xFF000000) {
                case 0x02000000:
                    data = &m_ram[address & 0x3FFFF];
                    break;
                case 0x03000000:
                    data = &m_wram[address & 0x7FFF];
                    break;
                case 0x06000000:
                    data = &m_vramC[address & ((address & 0x10000) ? 0x17FFF : 0xFFFF)];
                    break;
                case 0x08000000:
                case 0x09000000:
                case 0x0A000000:
                case 0x0B000000:
                case 0x0C000000:
                    if (address > 0x8000000 || !m_core->rtc.ReadGpControl())
                        data = m_core->cartridgeGba.GetRom(static_cast<uint32_t>(address));
                    break;
            }
        } else {
            switch (address & 0xFF000000) {
                case 0x00000000:
                    if (address < 0x4000)
                        data = &m_ndsBios7[address];
                    break;
                case 0x02000000:
                    data = &m_ram[address & 0x3FFFFF];
                    break;
                case 0x03000000:
                    if (!(address & 0x00800000)) {
                        switch (m_wramCnt) {
                            case 1:
                                data = &m_wram[(address & 0x3FFF)];
                                break;
                            case 2:
                                data = &m_wram[(address & 0x3FFF) + 0x4000];
                                break;
                            case 3:
                                data = &m_wram[(address & 0x7FFF)];
                                break;
                        }
                    }
                    if (!data)
                        data = &m_wram7[address & 0xFFFF];
                    break;
                case 0x04000000:
                    if (address & 0x00800000) {
                        uint32_t addr = static_cast<uint32_t>(address & ~0x00008000u);
                        if (addr >= 0x04804000 && addr < 0x04806000)
                            data = &m_wifiRam[addr & 0x1FFF];
                    }
                    break;
                case 0x06000000: {
                    VramMapping *mapping = &m_vram7[(address & 0x3FFFF) >> 17];
                    if (mapping->GetCount() == 1)
                        data = &mapping->GetBaseMapping()[address & 0x1FFFF];
                    break;
                }
                case 0x08000000:
                case 0x09000000:
                    data = m_core->cartridgeGba.GetRom(static_cast<uint32_t>(address));
                    break;
            }
        }
        m_readMap7[address >> 12] = data;
    }
    for (uint64_t address = start; address < end; address += 0x1000) {
        uint8_t *data = nullptr;
        if (m_core->IsGbaMode()) {
            switch (address & 0xFF000000) {
                case 0x02000000:
                    data = &m_ram[address & 0x3FFFF];
                    break;
                case 0x03000000:
                    data = &m_wram[address & 0x7FFF];
                    break;
                case 0x06000000:
                    data = &m_vramC[address & ((address & 0x10000) ? 0x17FFF : 0xFFFF)];
                    break;
                case 0x07000000:
                    data = &m_oam[address & 0x3FF];
                    break;
            }
        } else {
            switch (address & 0xFF000000) {
                case 0x02000000:
                    data = &m_ram[address & 0x3FFFFF];
                    break;
                case 0x03000000:
                    if (!(address & 0x00800000)) {
                        switch (m_wramCnt) {
                            case 1:
                                data = &m_wram[(address & 0x3FFF)];
                                break;
                            case 2:
                                data = &m_wram[(address & 0x3FFF) + 0x4000];
                                break;
                            case 3:
                                data = &m_wram[(address & 0x7FFF)];
                                break;
                        }
                    }
                    if (!data)
                        data = &m_wram7[address & 0xFFFF];
                    break;
                case 0x04000000:
                    if (address & 0x00800000) {
                        uint32_t addr = static_cast<uint32_t>(address & ~0x00008000u);
                        if (addr >= 0x04804000 && addr < 0x04806000)
                            data = &m_wifiRam[addr & 0x1FFF];
                    }
                    break;
                case 0x06000000: {
                    VramMapping *mapping = &m_vram7[(address & 0x3FFFF) >> 17];
                    if (mapping->GetCount() == 1)
                        data = &mapping->GetBaseMapping()[address & 0x1FFFF];
                    break;
                }
            }
        }
        m_writeMap7[address >> 12] = data;
    }
}

template <typename T> T Memory::ReadFallback(bool cpu, uint32_t address) {
    uint8_t *data = nullptr;
    if (cpu == 0) {
        switch (address & 0xFF000000) {
            case 0x04000000:
                return IoRead9<T>(address);
            case 0x05000000:
                data = &m_palette[address & 0x7FF];
                break;
            case 0x06000000: {
                VramMapping *mapping;
                switch (address & 0xFFE00000) {
                    case 0x06000000:
                        mapping = &m_engABg[(address & 0x7FFFF) >> 14];
                        break;
                    case 0x06200000:
                        mapping = &m_engBBg[(address & 0x1FFFF) >> 14];
                        break;
                    case 0x06400000:
                        mapping = &m_engAObj[(address & 0x3FFFF) >> 14];
                        break;
                    case 0x06600000:
                        mapping = &m_engBObj[(address & 0x1FFFF) >> 14];
                        break;
                    default:
                        mapping = &m_lcdc[(address & 0xFFFFF) >> 14];
                        break;
                }
                if (mapping->GetCount() == 0)
                    break;
                return mapping->Read<T>(address & 0x3FFF);
            }
            case 0x07000000:
                data = &m_oam[address & 0x7FF];
                break;
            case 0x08000000:
            case 0x09000000:
                return (T)0xFFFFFFFF;
            case 0x0A000000:
                return m_core->cartridgeGba.SramRead(address + 0x4000000);
        }
    } else if (m_core->IsGbaMode()) {
        switch (address & 0xFF000000) {
            case 0x00000000:
                if (address < 0x4000)
                    data = (m_core->interpreter[1].GetPC() < 0x4000) ? (m_lastGbaBios = &m_gbaBios[address]) : m_lastGbaBios;
                break;
            case 0x04000000:
                return IoReadGba<T>(address);
            case 0x05000000:
                data = &m_palette[address & 0x3FF];
                break;
            case 0x07000000:
                data = &m_oam[address & 0x3FF];
                break;
            case 0x0D000000:
                if (m_core->cartridgeGba.IsEeprom(address))
                    return m_core->cartridgeGba.EepromRead();
                [[fallthrough]]; // pas d'EEPROM ici : zone ROM ordinaire
            case 0x08000000:
            case 0x09000000:
            case 0x0A000000:
            case 0x0B000000:
            case 0x0C000000:
                if (address >= 0x80000C4 && address < 0x80000CA)
                    return IoReadGba<T>(address);
                if ((data = m_core->cartridgeGba.GetRom(address)))
                    break;
                return (T)0xFFFFFFFF;
            case 0x0E000000:
                return m_core->cartridgeGba.SramRead(address);
        }
    } else {
        switch (address & 0xFF000000) {
            case 0x04000000:
                return IoRead7<T>(address);
            case 0x06000000: {
                VramMapping *mapping = &m_vram7[(address & 0x3FFFF) >> 17];
                if (mapping->GetCount() == 0)
                    break;
                return mapping->Read<T>(address & 0x1FFFF);
            }
            case 0x08000000:
            case 0x09000000:
                return (T)0xFFFFFFFF;
            case 0x0A000000:
                return m_core->cartridgeGba.SramRead(address + 0x4000000);
        }
    }
    if (data) {
        T value = 0;
        for (size_t i = 0; i < sizeof(T); i++)
            value |= data[i] << (i * 8);
        return value;
    }
    if (!m_core->IsGbaMode()) {
        LOG_VERBOSE("Unmapped ARM%d memory read: 0x%X\n", ((cpu == 0) ? 9 : 7), address);
        return 0;
    } else {
        LOG_VERBOSE("Unmapped GBA memory read: 0x%X\n", address);
        // Bus ouvert : relit l'instruction préchargée. Si le PC lui-même est
        // hors mémoire (saut dans une zone vide), cette relecture repassait
        // ici indéfiniment — une récursion que -O2 transforme en boucle
        // infinie. On rend 0 à la place.
        uint32_t pc = m_core->interpreter[1].GetPC();
        if (address == pc)
            return 0;
        return Read<T>(cpu, pc);
    }
}

template <typename T> void Memory::WriteFallback(bool cpu, uint32_t address, T value) {
    uint8_t *data = nullptr;
    if (cpu == 0) {
        switch (address & 0xFF000000) {
            case 0x04000000:
                IoWrite9<T>(address, value);
                return;
            case 0x05000000:
                data = &m_palette[address & 0x7FF];
                break;
            case 0x06000000: {
                VramMapping *mapping;
                switch (address & 0xFFE00000) {
                    case 0x06000000:
                        mapping = &m_engABg[(address & 0x7FFFF) >> 14];
                        break;
                    case 0x06200000:
                        mapping = &m_engBBg[(address & 0x1FFFF) >> 14];
                        break;
                    case 0x06400000:
                        mapping = &m_engAObj[(address & 0x3FFFF) >> 14];
                        break;
                    case 0x06600000:
                        mapping = &m_engBObj[(address & 0x1FFFF) >> 14];
                        break;
                    default:
                        mapping = &m_lcdc[(address & 0xFFFFF) >> 14];
                        break;
                }
                if (mapping->GetCount() == 0)
                    break;
                mapping->Write<T>(address & 0x3FFF, value);
                return;
            }
            case 0x07000000:
                data = &m_oam[address & 0x7FF];
                break;
            case 0x0A000000:
                m_core->cartridgeGba.SramWrite(address + 0x4000000, static_cast<uint8_t>(value));
                return;
        }
    } else if (m_core->IsGbaMode()) {
        switch (address & 0xFF000000) {
            case 0x04000000:
                IoWriteGba<T>(address, value);
                return;
            case 0x05000000:
                data = &m_palette[address & 0x3FF];
                break;
            case 0x07000000:
                data = &m_oam[address & 0x3FF];
                break;
            case 0x08000000:
                if (address >= 0x80000C4 && address < 0x80000CA)
                    return IoWriteGba<T>(address, value);
                break;
            case 0x0D000000:
                if (m_core->cartridgeGba.IsEeprom(address))
                    return m_core->cartridgeGba.EepromWrite(static_cast<uint8_t>(value));
                break;
            case 0x0E000000:
                m_core->cartridgeGba.SramWrite(address, static_cast<uint8_t>(value));
                return;
        }
    } else {
        switch (address & 0xFF000000) {
            case 0x04000000:
                IoWrite7<T>(address, value);
                return;
            case 0x06000000: {
                VramMapping *mapping = &m_vram7[(address & 0x3FFFF) >> 17];
                if (mapping->GetCount() == 0)
                    break;
                mapping->Write<T>(address & 0x1FFFF, value);
                return;
            }
            case 0x0A000000:
                m_core->cartridgeGba.SramWrite(address + 0x4000000, static_cast<uint8_t>(value));
                return;
        }
    }
    if (data) {
        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = static_cast<uint8_t>(value >> (i * 8));
        return;
    }
    if (!m_core->IsGbaMode())
        LOG_VERBOSE("Unmapped ARM%d memory write: 0x%X\n", ((cpu == 0) ? 9 : 7), address);
    else
        LOG_VERBOSE("Unmapped GBA memory write: 0x%X\n", address);
}

template <typename T> T Memory::IoRead9(uint32_t address) {
    T      value = 0;
    size_t i     = 0;
    while (i < sizeof(T)) {
        uint32_t base = static_cast<uint32_t>(address + i);
        size_t   size;
        uint32_t data;
        switch (base) {
            DEF_IO32(0x4000000, data = m_core->gpu2D[0].ReadDispCnt())
            DEF_IO16(0x4000004, data = m_core->gpu.ReadDispStat(0))
            DEF_IO16(0x4000006, data = m_core->gpu.ReadVCount())
            DEF_IO16(0x4000008, data = m_core->gpu2D[0].ReadBgCnt(0))
            DEF_IO16(0x400000A, data = m_core->gpu2D[0].ReadBgCnt(1))
            DEF_IO16(0x400000C, data = m_core->gpu2D[0].ReadBgCnt(2))
            DEF_IO16(0x400000E, data = m_core->gpu2D[0].ReadBgCnt(3))
            DEF_IO16(0x4000048, data = m_core->gpu2D[0].ReadWinIn())
            DEF_IO16(0x400004A, data = m_core->gpu2D[0].ReadWinOut())
            DEF_IO16(0x4000050, data = m_core->gpu2D[0].ReadBldCnt())
            DEF_IO16(0x4000052, data = m_core->gpu2D[0].ReadBldAlpha())
            DEF_IO16(0x4000060, data = m_core->gpu3DRenderer.ReadDisp3DCnt())
            DEF_IO32(0x4000064, data = m_core->gpu.ReadDispCapCnt())
            DEF_IO16(0x400006C, data = m_core->gpu2D[0].ReadMasterBright())
            DEF_IO32(0x40000B0, data = m_core->dma[0].ReadDmaSad(0))
            DEF_IO32(0x40000B4, data = m_core->dma[0].ReadDmaDad(0))
            DEF_IO32(0x40000B8, data = m_core->dma[0].ReadDmaCnt(0))
            DEF_IO32(0x40000BC, data = m_core->dma[0].ReadDmaSad(1))
            DEF_IO32(0x40000C0, data = m_core->dma[0].ReadDmaDad(1))
            DEF_IO32(0x40000C4, data = m_core->dma[0].ReadDmaCnt(1))
            DEF_IO32(0x40000C8, data = m_core->dma[0].ReadDmaSad(2))
            DEF_IO32(0x40000CC, data = m_core->dma[0].ReadDmaDad(2))
            DEF_IO32(0x40000D0, data = m_core->dma[0].ReadDmaCnt(2))
            DEF_IO32(0x40000D4, data = m_core->dma[0].ReadDmaSad(3))
            DEF_IO32(0x40000D8, data = m_core->dma[0].ReadDmaDad(3))
            DEF_IO32(0x40000DC, data = m_core->dma[0].ReadDmaCnt(3))
            DEF_IO32(0x40000E0, data = ReadDmaFill(0))
            DEF_IO32(0x40000E4, data = ReadDmaFill(1))
            DEF_IO32(0x40000E8, data = ReadDmaFill(2))
            DEF_IO32(0x40000EC, data = ReadDmaFill(3))
            DEF_IO16(0x4000100, data = m_core->timers[0].ReadTmCntL(0))
            DEF_IO16(0x4000102, data = m_core->timers[0].ReadTmCntH(0))
            DEF_IO16(0x4000104, data = m_core->timers[0].ReadTmCntL(1))
            DEF_IO16(0x4000106, data = m_core->timers[0].ReadTmCntH(1))
            DEF_IO16(0x4000108, data = m_core->timers[0].ReadTmCntL(2))
            DEF_IO16(0x400010A, data = m_core->timers[0].ReadTmCntH(2))
            DEF_IO16(0x400010C, data = m_core->timers[0].ReadTmCntL(3))
            DEF_IO16(0x400010E, data = m_core->timers[0].ReadTmCntH(3))
            DEF_IO16(0x4000130, data = m_core->input.ReadKeyInput())
            DEF_IO16(0x4000180, data = m_core->ipc.ReadIpcSync(0))
            DEF_IO16(0x4000184, data = m_core->ipc.ReadIpcFifoCnt(0))
            DEF_IO16(0x40001A0, data = m_core->cartridgeNds.ReadAuxSpiCnt(0))
            DEF_IO_8(0x40001A2, data = m_core->cartridgeNds.ReadAuxSpiData(0))
            DEF_IO32(0x40001A4, data = m_core->cartridgeNds.ReadRomCtrl(0))
            DEF_IO_8(0x4000208, data = m_core->interpreter[0].ReadIme())
            DEF_IO32(0x4000210, data = m_core->interpreter[0].ReadIe())
            DEF_IO32(0x4000214, data = m_core->interpreter[0].ReadIrf())
            DEF_IO_8(0x4000240, data = ReadVramCnt(0))
            DEF_IO_8(0x4000241, data = ReadVramCnt(1))
            DEF_IO_8(0x4000242, data = ReadVramCnt(2))
            DEF_IO_8(0x4000243, data = ReadVramCnt(3))
            DEF_IO_8(0x4000244, data = ReadVramCnt(4))
            DEF_IO_8(0x4000245, data = ReadVramCnt(5))
            DEF_IO_8(0x4000246, data = ReadVramCnt(6))
            DEF_IO_8(0x4000247, data = ReadWramCnt())
            DEF_IO_8(0x4000248, data = ReadVramCnt(7))
            DEF_IO_8(0x4000249, data = ReadVramCnt(8))
            DEF_IO16(0x4000280, data = m_core->divSqrt.ReadDivCnt())
            DEF_IO32(0x4000290, data = m_core->divSqrt.ReadDivNumerL())
            DEF_IO32(0x4000294, data = m_core->divSqrt.ReadDivNumerH())
            DEF_IO32(0x4000298, data = m_core->divSqrt.ReadDivDenomL())
            DEF_IO32(0x400029C, data = m_core->divSqrt.ReadDivDenomH())
            DEF_IO32(0x40002A0, data = m_core->divSqrt.ReadDivResultL())
            DEF_IO32(0x40002A4, data = m_core->divSqrt.ReadDivResultH())
            DEF_IO32(0x40002A8, data = m_core->divSqrt.ReadDivRemResultL())
            DEF_IO32(0x40002AC, data = m_core->divSqrt.ReadDivRemResultH())
            DEF_IO16(0x40002B0, data = m_core->divSqrt.ReadSqrtCnt())
            DEF_IO32(0x40002B4, data = m_core->divSqrt.ReadSqrtResult())
            DEF_IO32(0x40002B8, data = m_core->divSqrt.ReadSqrtParamL())
            DEF_IO32(0x40002BC, data = m_core->divSqrt.ReadSqrtParamH())
            DEF_IO_8(0x4000300, data = m_core->interpreter[0].ReadPostFlg())
            DEF_IO16(0x4000304, data = m_core->gpu.ReadPowCnt1())
            DEF_IO32(0x4000600, data = m_core->gpu3D.ReadGxStat())
            DEF_IO32(0x4000604, data = m_core->gpu3D.ReadRamCount())
            DEF_IO32(0x4000620, data = m_core->gpu3D.ReadPosResult(0))
            DEF_IO32(0x4000624, data = m_core->gpu3D.ReadPosResult(1))
            DEF_IO32(0x4000628, data = m_core->gpu3D.ReadPosResult(2))
            DEF_IO32(0x400062C, data = m_core->gpu3D.ReadPosResult(3))
            DEF_IO16(0x4000630, data = m_core->gpu3D.ReadVecResult(0))
            DEF_IO16(0x4000632, data = m_core->gpu3D.ReadVecResult(1))
            DEF_IO16(0x4000634, data = m_core->gpu3D.ReadVecResult(2))
            DEF_IO32(0x4000640, data = m_core->gpu3D.ReadClipMtxResult(0))
            DEF_IO32(0x4000644, data = m_core->gpu3D.ReadClipMtxResult(1))
            DEF_IO32(0x4000648, data = m_core->gpu3D.ReadClipMtxResult(2))
            DEF_IO32(0x400064C, data = m_core->gpu3D.ReadClipMtxResult(3))
            DEF_IO32(0x4000650, data = m_core->gpu3D.ReadClipMtxResult(4))
            DEF_IO32(0x4000654, data = m_core->gpu3D.ReadClipMtxResult(5))
            DEF_IO32(0x4000658, data = m_core->gpu3D.ReadClipMtxResult(6))
            DEF_IO32(0x400065C, data = m_core->gpu3D.ReadClipMtxResult(7))
            DEF_IO32(0x4000660, data = m_core->gpu3D.ReadClipMtxResult(8))
            DEF_IO32(0x4000664, data = m_core->gpu3D.ReadClipMtxResult(9))
            DEF_IO32(0x4000668, data = m_core->gpu3D.ReadClipMtxResult(10))
            DEF_IO32(0x400066C, data = m_core->gpu3D.ReadClipMtxResult(11))
            DEF_IO32(0x4000670, data = m_core->gpu3D.ReadClipMtxResult(12))
            DEF_IO32(0x4000674, data = m_core->gpu3D.ReadClipMtxResult(13))
            DEF_IO32(0x4000678, data = m_core->gpu3D.ReadClipMtxResult(14))
            DEF_IO32(0x400067C, data = m_core->gpu3D.ReadClipMtxResult(15))
            DEF_IO32(0x4000680, data = m_core->gpu3D.ReadVecMtxResult(0))
            DEF_IO32(0x4000684, data = m_core->gpu3D.ReadVecMtxResult(1))
            DEF_IO32(0x4000688, data = m_core->gpu3D.ReadVecMtxResult(2))
            DEF_IO32(0x400068C, data = m_core->gpu3D.ReadVecMtxResult(3))
            DEF_IO32(0x4000690, data = m_core->gpu3D.ReadVecMtxResult(4))
            DEF_IO32(0x4000694, data = m_core->gpu3D.ReadVecMtxResult(5))
            DEF_IO32(0x4000698, data = m_core->gpu3D.ReadVecMtxResult(6))
            DEF_IO32(0x400069C, data = m_core->gpu3D.ReadVecMtxResult(7))
            DEF_IO32(0x40006A0, data = m_core->gpu3D.ReadVecMtxResult(8))
            DEF_IO32(0x4001000, data = m_core->gpu2D[1].ReadDispCnt())
            DEF_IO16(0x4001008, data = m_core->gpu2D[1].ReadBgCnt(0))
            DEF_IO16(0x400100A, data = m_core->gpu2D[1].ReadBgCnt(1))
            DEF_IO16(0x400100C, data = m_core->gpu2D[1].ReadBgCnt(2))
            DEF_IO16(0x400100E, data = m_core->gpu2D[1].ReadBgCnt(3))
            DEF_IO16(0x4001048, data = m_core->gpu2D[1].ReadWinIn())
            DEF_IO16(0x400104A, data = m_core->gpu2D[1].ReadWinOut())
            DEF_IO16(0x4001050, data = m_core->gpu2D[1].ReadBldCnt())
            DEF_IO16(0x4001052, data = m_core->gpu2D[1].ReadBldAlpha())
            DEF_IO16(0x400106C, data = m_core->gpu2D[1].ReadMasterBright())
            DEF_IO32(0x4100000, data = m_core->ipc.ReadIpcFifoRecv(0))
            DEF_IO32(0x4100010, data = m_core->cartridgeNds.ReadRomDataIn(0))
            default:
                if (i == 0) {
                    LOG_VERBOSE("Unknown ARM9 I/O register read: 0x%X\n", address);
                    return 0;
                }
                i++;
                continue;
        }
        value = static_cast<T>(value | ((data >> (base * 8)) << (i * 8)));
        i += size - base;
    }
    return value;
}

template <typename T> T Memory::IoRead7(uint32_t address) {
    if (address >= 0x04808000 && address < 0x04810000)
        address &= ~0x00008000u;
    T      value = 0;
    size_t i     = 0;
    while (i < sizeof(T)) {
        uint32_t base = static_cast<uint32_t>(address + i);
        size_t   size;
        uint32_t data;
        switch (base) {
            DEF_IO16(0x4000004, data = m_core->gpu.ReadDispStat(1))
            DEF_IO16(0x4000006, data = m_core->gpu.ReadVCount())
            DEF_IO32(0x40000B0, data = m_core->dma[1].ReadDmaSad(0))
            DEF_IO32(0x40000B4, data = m_core->dma[1].ReadDmaDad(0))
            DEF_IO32(0x40000B8, data = m_core->dma[1].ReadDmaCnt(0))
            DEF_IO32(0x40000BC, data = m_core->dma[1].ReadDmaSad(1))
            DEF_IO32(0x40000C0, data = m_core->dma[1].ReadDmaDad(1))
            DEF_IO32(0x40000C4, data = m_core->dma[1].ReadDmaCnt(1))
            DEF_IO32(0x40000C8, data = m_core->dma[1].ReadDmaSad(2))
            DEF_IO32(0x40000CC, data = m_core->dma[1].ReadDmaDad(2))
            DEF_IO32(0x40000D0, data = m_core->dma[1].ReadDmaCnt(2))
            DEF_IO32(0x40000D4, data = m_core->dma[1].ReadDmaSad(3))
            DEF_IO32(0x40000D8, data = m_core->dma[1].ReadDmaDad(3))
            DEF_IO32(0x40000DC, data = m_core->dma[1].ReadDmaCnt(3))
            DEF_IO16(0x4000100, data = m_core->timers[1].ReadTmCntL(0))
            DEF_IO16(0x4000102, data = m_core->timers[1].ReadTmCntH(0))
            DEF_IO16(0x4000104, data = m_core->timers[1].ReadTmCntL(1))
            DEF_IO16(0x4000106, data = m_core->timers[1].ReadTmCntH(1))
            DEF_IO16(0x4000108, data = m_core->timers[1].ReadTmCntL(2))
            DEF_IO16(0x400010A, data = m_core->timers[1].ReadTmCntH(2))
            DEF_IO16(0x400010C, data = m_core->timers[1].ReadTmCntL(3))
            DEF_IO16(0x400010E, data = m_core->timers[1].ReadTmCntH(3))
            DEF_IO16(0x4000130, data = m_core->input.ReadKeyInput())
            DEF_IO16(0x4000136, data = m_core->input.ReadExtKeyIn())
            DEF_IO_8(0x4000138, data = m_core->rtc.ReadRtc())
            DEF_IO16(0x4000180, data = m_core->ipc.ReadIpcSync(1))
            DEF_IO16(0x4000184, data = m_core->ipc.ReadIpcFifoCnt(1))
            DEF_IO16(0x40001A0, data = m_core->cartridgeNds.ReadAuxSpiCnt(1))
            DEF_IO_8(0x40001A2, data = m_core->cartridgeNds.ReadAuxSpiData(1))
            DEF_IO32(0x40001A4, data = m_core->cartridgeNds.ReadRomCtrl(1))
            DEF_IO16(0x40001C0, data = m_core->spi.ReadSpiCnt())
            DEF_IO_8(0x40001C2, data = m_core->spi.ReadSpiData())
            DEF_IO_8(0x4000208, data = m_core->interpreter[1].ReadIme())
            DEF_IO32(0x4000210, data = m_core->interpreter[1].ReadIe())
            DEF_IO32(0x4000214, data = m_core->interpreter[1].ReadIrf())
            DEF_IO_8(0x4000240, data = ReadVramStat())
            DEF_IO_8(0x4000241, data = ReadWramCnt())
            DEF_IO_8(0x4000300, data = m_core->interpreter[1].ReadPostFlg())
            DEF_IO_8(0x4000304, data = 0) // Sound/Wifi power control bit[0] = Sound on/off, bit[1] = Wifi on/off <----------------------------------------------- A FAIRE
            DEF_IO_8(0x4000301, data = ReadHaltCnt())
            DEF_IO32(0x4000400, data = m_core->spu.ReadSoundCnt(0))
            DEF_IO32(0x4000410, data = m_core->spu.ReadSoundCnt(1))
            DEF_IO32(0x4000420, data = m_core->spu.ReadSoundCnt(2))
            DEF_IO32(0x4000430, data = m_core->spu.ReadSoundCnt(3))
            DEF_IO32(0x4000440, data = m_core->spu.ReadSoundCnt(4))
            DEF_IO32(0x4000450, data = m_core->spu.ReadSoundCnt(5))
            DEF_IO32(0x4000460, data = m_core->spu.ReadSoundCnt(6))
            DEF_IO32(0x4000470, data = m_core->spu.ReadSoundCnt(7))
            DEF_IO32(0x4000480, data = m_core->spu.ReadSoundCnt(8))
            DEF_IO32(0x4000490, data = m_core->spu.ReadSoundCnt(9))
            DEF_IO32(0x40004A0, data = m_core->spu.ReadSoundCnt(10))
            DEF_IO32(0x40004B0, data = m_core->spu.ReadSoundCnt(11))
            DEF_IO32(0x40004C0, data = m_core->spu.ReadSoundCnt(12))
            DEF_IO32(0x40004D0, data = m_core->spu.ReadSoundCnt(13))
            DEF_IO32(0x40004E0, data = m_core->spu.ReadSoundCnt(14))
            DEF_IO32(0x40004F0, data = m_core->spu.ReadSoundCnt(15))
            DEF_IO16(0x4000500, data = m_core->spu.ReadMainSoundCnt())
            DEF_IO16(0x4000504, data = m_core->spu.ReadSoundBias())
            DEF_IO_8(0x4000508, data = m_core->spu.ReadSndCapCnt(0))
            DEF_IO_8(0x4000509, data = m_core->spu.ReadSndCapCnt(1))
            DEF_IO32(0x4000510, data = m_core->spu.ReadSndCapDad(0))
            DEF_IO32(0x4000518, data = m_core->spu.ReadSndCapDad(1))
            DEF_IO32(0x4100000, data = m_core->ipc.ReadIpcFifoRecv(1))
            DEF_IO32(0x4100010, data = m_core->cartridgeNds.ReadRomDataIn(1))
            DEF_IO16(0x4800006, data = m_core->wifi.ReadWModeWep())
            DEF_IO16(0x4800010, data = m_core->wifi.ReadWIrf())
            DEF_IO16(0x4800012, data = m_core->wifi.ReadWIe())
            DEF_IO16(0x4800018, data = m_core->wifi.ReadWMacaddr(0))
            DEF_IO16(0x480001A, data = m_core->wifi.ReadWMacaddr(1))
            DEF_IO16(0x480001C, data = m_core->wifi.ReadWMacaddr(2))
            DEF_IO16(0x4800020, data = m_core->wifi.ReadWBssid(0))
            DEF_IO16(0x4800022, data = m_core->wifi.ReadWBssid(1))
            DEF_IO16(0x4800024, data = m_core->wifi.ReadWBssid(2))
            DEF_IO16(0x480002A, data = m_core->wifi.ReadWAidFull())
            DEF_IO16(0x4800030, data = m_core->wifi.ReadWRxcnt())
            DEF_IO16(0x480003C, data = m_core->wifi.ReadWPowerstate())
            DEF_IO16(0x4800040, data = m_core->wifi.ReadWPowerforce())
            DEF_IO16(0x4800050, data = m_core->wifi.ReadWRxbufBegin())
            DEF_IO16(0x4800052, data = m_core->wifi.ReadWRxbufEnd())
            DEF_IO16(0x4800054, data = m_core->wifi.ReadWRxbufWrcsr())
            DEF_IO16(0x4800056, data = m_core->wifi.ReadWRxbufWrAddr())
            DEF_IO16(0x4800058, data = m_core->wifi.ReadWRxbufRdAddr())
            DEF_IO16(0x480005A, data = m_core->wifi.ReadWRxbufReadcsr())
            DEF_IO16(0x480005C, data = m_core->wifi.ReadWRxbufCount())
            DEF_IO16(0x4800060, data = m_core->wifi.ReadWRxbufRdData())
            DEF_IO16(0x4800062, data = m_core->wifi.ReadWRxbufGap())
            DEF_IO16(0x4800064, data = m_core->wifi.ReadWRxbufGapdisp())
            DEF_IO16(0x4800068, data = m_core->wifi.ReadWTxbufWrAddr())
            DEF_IO16(0x480006C, data = m_core->wifi.ReadWTxbufCount())
            DEF_IO16(0x4800074, data = m_core->wifi.ReadWTxbufGap())
            DEF_IO16(0x4800076, data = m_core->wifi.ReadWTxbufGapdisp())
            DEF_IO16(0x4800080, data = m_core->wifi.ReadWTxbufLoc(4))
            DEF_IO16(0x480008C, data = m_core->wifi.ReadWBeaconInt())
            DEF_IO16(0x4800090, data = m_core->wifi.ReadWTxbufLoc(1))
            DEF_IO16(0x48000A0, data = m_core->wifi.ReadWTxbufLoc(0))
            DEF_IO16(0x48000A4, data = m_core->wifi.ReadWTxbufLoc(2))
            DEF_IO16(0x48000A8, data = m_core->wifi.ReadWTxbufLoc(3))
            DEF_IO16(0x48000B0, data = m_core->wifi.ReadWTxreqRead())
            DEF_IO16(0x48000E8, data = m_core->wifi.ReadWUsCountcnt())
            DEF_IO16(0x48000EA, data = m_core->wifi.ReadWUsComparecnt())
            DEF_IO16(0x4800110, data = m_core->wifi.ReadWPreBeacon())
            DEF_IO16(0x480011C, data = m_core->wifi.ReadWBeaconCount())
            DEF_IO16(0x4800120, data = m_core->wifi.ReadWConfig(0))
            DEF_IO16(0x4800122, data = m_core->wifi.ReadWConfig(1))
            DEF_IO16(0x4800124, data = m_core->wifi.ReadWConfig(2))
            DEF_IO16(0x4800128, data = m_core->wifi.ReadWConfig(3))
            DEF_IO16(0x4800130, data = m_core->wifi.ReadWConfig(4))
            DEF_IO16(0x4800132, data = m_core->wifi.ReadWConfig(5))
            DEF_IO16(0x4800134, data = m_core->wifi.ReadWPostBeacon())
            DEF_IO16(0x4800140, data = m_core->wifi.ReadWConfig(6))
            DEF_IO16(0x4800142, data = m_core->wifi.ReadWConfig(7))
            DEF_IO16(0x4800144, data = m_core->wifi.ReadWConfig(8))
            DEF_IO16(0x4800146, data = m_core->wifi.ReadWConfig(9))
            DEF_IO16(0x4800148, data = m_core->wifi.ReadWConfig(10))
            DEF_IO16(0x480014A, data = m_core->wifi.ReadWConfig(11))
            DEF_IO16(0x480014C, data = m_core->wifi.ReadWConfig(12))
            DEF_IO16(0x4800150, data = m_core->wifi.ReadWConfig(13))
            DEF_IO16(0x4800154, data = m_core->wifi.ReadWConfig(14))
            DEF_IO16(0x480015C, data = m_core->wifi.ReadWBbRead())
            default:
                if (i == 0) {
                    LOG_VERBOSE("Unknown ARM7 I/O register read: 0x%X\n", address);
                    return 0;
                }
                i++;
                continue;
        }
        value = static_cast<T>(value | ((data >> (base * 8)) << (i * 8)));
        i += size - base;
    }
    return value;
}

template <typename T> T Memory::IoReadGba(uint32_t address) {
    T      value = 0;
    size_t i     = 0;
    while (i < sizeof(T)) {
        uint32_t base = static_cast<uint32_t>(address + i);
        size_t   size;
        uint32_t data;
        switch (base) {
            DEF_IO16(0x4000000, data = m_core->gpu2D[0].ReadDispCnt())
            DEF_IO16(0x4000004, data = m_core->gpu.ReadDispStat(1))
            DEF_IO16(0x4000006, data = m_core->gpu.ReadVCount())
            DEF_IO16(0x4000008, data = m_core->gpu2D[0].ReadBgCnt(0))
            DEF_IO16(0x400000A, data = m_core->gpu2D[0].ReadBgCnt(1))
            DEF_IO16(0x400000C, data = m_core->gpu2D[0].ReadBgCnt(2))
            DEF_IO16(0x400000E, data = m_core->gpu2D[0].ReadBgCnt(3))
            DEF_IO16(0x4000048, data = m_core->gpu2D[0].ReadWinIn())
            DEF_IO16(0x400004A, data = m_core->gpu2D[0].ReadWinOut())
            DEF_IO16(0x4000050, data = m_core->gpu2D[0].ReadBldCnt())
            DEF_IO16(0x4000052, data = m_core->gpu2D[0].ReadBldAlpha())
            DEF_IO16(0x4000060, data = m_core->spu.ReadGbaSoundCntL(0))
            DEF_IO16(0x4000062, data = m_core->spu.ReadGbaSoundCntH(0))
            DEF_IO16(0x4000064, data = m_core->spu.ReadGbaSoundCntX(0))
            DEF_IO16(0x4000068, data = m_core->spu.ReadGbaSoundCntH(1))
            DEF_IO16(0x400006C, data = m_core->spu.ReadGbaSoundCntX(1))
            DEF_IO16(0x4000070, data = m_core->spu.ReadGbaSoundCntL(2))
            DEF_IO16(0x4000072, data = m_core->spu.ReadGbaSoundCntH(2))
            DEF_IO16(0x4000074, data = m_core->spu.ReadGbaSoundCntX(2))
            DEF_IO16(0x4000078, data = m_core->spu.ReadGbaSoundCntH(3))
            DEF_IO16(0x400007C, data = m_core->spu.ReadGbaSoundCntX(3))
            DEF_IO16(0x4000080, data = m_core->spu.ReadGbaMainSoundCntL())
            DEF_IO16(0x4000082, data = m_core->spu.ReadGbaMainSoundCntH())
            DEF_IO16(0x4000084, data = m_core->spu.ReadGbaMainSoundCntX())
            DEF_IO16(0x4000088, data = m_core->spu.ReadGbaSoundBias())
            DEF_IO_8(0x4000090, data = m_core->spu.ReadGbaWaveRam(0))
            DEF_IO_8(0x4000091, data = m_core->spu.ReadGbaWaveRam(1))
            DEF_IO_8(0x4000092, data = m_core->spu.ReadGbaWaveRam(2))
            DEF_IO_8(0x4000093, data = m_core->spu.ReadGbaWaveRam(3))
            DEF_IO_8(0x4000094, data = m_core->spu.ReadGbaWaveRam(4))
            DEF_IO_8(0x4000095, data = m_core->spu.ReadGbaWaveRam(5))
            DEF_IO_8(0x4000096, data = m_core->spu.ReadGbaWaveRam(6))
            DEF_IO_8(0x4000097, data = m_core->spu.ReadGbaWaveRam(7))
            DEF_IO_8(0x4000098, data = m_core->spu.ReadGbaWaveRam(8))
            DEF_IO_8(0x4000099, data = m_core->spu.ReadGbaWaveRam(9))
            DEF_IO_8(0x400009A, data = m_core->spu.ReadGbaWaveRam(10))
            DEF_IO_8(0x400009B, data = m_core->spu.ReadGbaWaveRam(11))
            DEF_IO_8(0x400009C, data = m_core->spu.ReadGbaWaveRam(12))
            DEF_IO_8(0x400009D, data = m_core->spu.ReadGbaWaveRam(13))
            DEF_IO_8(0x400009E, data = m_core->spu.ReadGbaWaveRam(14))
            DEF_IO_8(0x400009F, data = m_core->spu.ReadGbaWaveRam(15))
            DEF_IO32(0x40000B8, data = m_core->dma[1].ReadDmaCnt(0))
            DEF_IO32(0x40000C4, data = m_core->dma[1].ReadDmaCnt(1))
            DEF_IO32(0x40000D0, data = m_core->dma[1].ReadDmaCnt(2))
            DEF_IO32(0x40000DC, data = m_core->dma[1].ReadDmaCnt(3))
            DEF_IO16(0x4000100, data = m_core->timers[1].ReadTmCntL(0))
            DEF_IO16(0x4000102, data = m_core->timers[1].ReadTmCntH(0))
            DEF_IO16(0x4000104, data = m_core->timers[1].ReadTmCntL(1))
            DEF_IO16(0x4000106, data = m_core->timers[1].ReadTmCntH(1))
            DEF_IO16(0x4000108, data = m_core->timers[1].ReadTmCntL(2))
            DEF_IO16(0x400010A, data = m_core->timers[1].ReadTmCntH(2))
            DEF_IO16(0x400010C, data = m_core->timers[1].ReadTmCntL(3))
            DEF_IO16(0x400010E, data = m_core->timers[1].ReadTmCntH(3))
            DEF_IO16(0x4000130, data = m_core->input.ReadKeyInput())
            DEF_IO16(0x4000200, data = m_core->interpreter[1].ReadIe())
            DEF_IO16(0x4000202, data = m_core->interpreter[1].ReadIrf())
            DEF_IO_8(0x4000208, data = m_core->interpreter[1].ReadIme())
            DEF_IO_8(0x4000300, data = m_core->interpreter[1].ReadPostFlg())
            DEF_IO16(0x80000C4, data = m_core->rtc.ReadGpData())
            DEF_IO16(0x80000C6, data = m_core->rtc.ReadGpDirection())
            DEF_IO16(0x80000C8, data = m_core->rtc.ReadGpControl())
            default:
                if (i == 0) {
                    LOG_VERBOSE("Unknown GBA I/O register read: 0x%X\n", address);
                    return 0;
                }
                i++;
                continue;
        }
        value = static_cast<T>(value | ((data >> (base * 8)) << (i * 8)));
        i += size - base;
    }
    return value;
}

template <typename T> void Memory::IoWrite9(uint32_t address, T value) {
    size_t i = 0;
    while (i < sizeof(T)) {
        uint32_t base = static_cast<uint32_t>(address + i);
        size_t   size;
        uint32_t mask = static_cast<uint32_t>(((uint64_t)1 << ((sizeof(T) - i) * 8)) - 1);
        uint32_t data = value >> (i * 8);
        switch (base) {
            DEF_IO32(0x4000000, m_core->gpu2D[0].WriteDispCnt(IOWR_PARAMS))
            DEF_IO16(0x4000004, m_core->gpu.WriteDispStat(0, IOWR_PARAMS))
            DEF_IO16(0x4000008, m_core->gpu2D[0].WriteBgCnt(0, IOWR_PARAMS))
            DEF_IO16(0x400000A, m_core->gpu2D[0].WriteBgCnt(1, IOWR_PARAMS))
            DEF_IO16(0x400000C, m_core->gpu2D[0].WriteBgCnt(2, IOWR_PARAMS))
            DEF_IO16(0x400000E, m_core->gpu2D[0].WriteBgCnt(3, IOWR_PARAMS))
            DEF_IO16(0x4000010, m_core->gpu2D[0].WriteBgHOfs(0, IOWR_PARAMS))
            DEF_IO16(0x4000012, m_core->gpu2D[0].WriteBgVOfs(0, IOWR_PARAMS))
            DEF_IO16(0x4000014, m_core->gpu2D[0].WriteBgHOfs(1, IOWR_PARAMS))
            DEF_IO16(0x4000016, m_core->gpu2D[0].WriteBgVOfs(1, IOWR_PARAMS))
            DEF_IO16(0x4000018, m_core->gpu2D[0].WriteBgHOfs(2, IOWR_PARAMS))
            DEF_IO16(0x400001A, m_core->gpu2D[0].WriteBgVOfs(2, IOWR_PARAMS))
            DEF_IO16(0x400001C, m_core->gpu2D[0].WriteBgHOfs(3, IOWR_PARAMS))
            DEF_IO16(0x400001E, m_core->gpu2D[0].WriteBgVOfs(3, IOWR_PARAMS))
            DEF_IO16(0x4000020, m_core->gpu2D[0].WriteBgPA(2, IOWR_PARAMS))
            DEF_IO16(0x4000022, m_core->gpu2D[0].WriteBgPB(2, IOWR_PARAMS))
            DEF_IO16(0x4000024, m_core->gpu2D[0].WriteBgPC(2, IOWR_PARAMS))
            DEF_IO16(0x4000026, m_core->gpu2D[0].WriteBgPD(2, IOWR_PARAMS))
            DEF_IO32(0x4000028, m_core->gpu2D[0].WriteBgX(2, IOWR_PARAMS))
            DEF_IO32(0x400002C, m_core->gpu2D[0].WriteBgY(2, IOWR_PARAMS))
            DEF_IO16(0x4000030, m_core->gpu2D[0].WriteBgPA(3, IOWR_PARAMS))
            DEF_IO16(0x4000032, m_core->gpu2D[0].WriteBgPB(3, IOWR_PARAMS))
            DEF_IO16(0x4000034, m_core->gpu2D[0].WriteBgPC(3, IOWR_PARAMS))
            DEF_IO16(0x4000036, m_core->gpu2D[0].WriteBgPD(3, IOWR_PARAMS))
            DEF_IO32(0x4000038, m_core->gpu2D[0].WriteBgX(3, IOWR_PARAMS))
            DEF_IO32(0x400003C, m_core->gpu2D[0].WriteBgY(3, IOWR_PARAMS))
            DEF_IO16(0x4000040, m_core->gpu2D[0].WriteWinH(0, IOWR_PARAMS))
            DEF_IO16(0x4000042, m_core->gpu2D[0].WriteWinH(1, IOWR_PARAMS))
            DEF_IO16(0x4000044, m_core->gpu2D[0].WriteWinV(0, IOWR_PARAMS))
            DEF_IO16(0x4000046, m_core->gpu2D[0].WriteWinV(1, IOWR_PARAMS))
            DEF_IO16(0x4000048, m_core->gpu2D[0].WriteWinIn(IOWR_PARAMS))
            DEF_IO16(0x400004A, m_core->gpu2D[0].WriteWinOut(IOWR_PARAMS))
            DEF_IO16(0x400004C, m_core->gpu2D[0].WriteMosaic(IOWR_PARAMS))
            DEF_IO16(0x4000050, m_core->gpu2D[0].WriteBldCnt(IOWR_PARAMS))
            DEF_IO16(0x4000052, m_core->gpu2D[0].WriteBldAlpha(IOWR_PARAMS))
            DEF_IO_8(0x4000054, m_core->gpu2D[0].WriteBldY(IOWR_PARAMS8))
            DEF_IO16(0x4000060, m_core->gpu3DRenderer.WriteDisp3DCnt(IOWR_PARAMS))
            DEF_IO32(0x4000064, m_core->gpu.WriteDispCapCnt(IOWR_PARAMS))
            DEF_IO16(0x400006C, m_core->gpu2D[0].WriteMasterBright(IOWR_PARAMS))
            DEF_IO32(0x40000B0, m_core->dma[0].WriteDmaSad(0, IOWR_PARAMS))
            DEF_IO32(0x40000B4, m_core->dma[0].WriteDmaDad(0, IOWR_PARAMS))
            DEF_IO32(0x40000B8, m_core->dma[0].WriteDmaCnt(0, IOWR_PARAMS))
            DEF_IO32(0x40000BC, m_core->dma[0].WriteDmaSad(1, IOWR_PARAMS))
            DEF_IO32(0x40000C0, m_core->dma[0].WriteDmaDad(1, IOWR_PARAMS))
            DEF_IO32(0x40000C4, m_core->dma[0].WriteDmaCnt(1, IOWR_PARAMS))
            DEF_IO32(0x40000C8, m_core->dma[0].WriteDmaSad(2, IOWR_PARAMS))
            DEF_IO32(0x40000CC, m_core->dma[0].WriteDmaDad(2, IOWR_PARAMS))
            DEF_IO32(0x40000D0, m_core->dma[0].WriteDmaCnt(2, IOWR_PARAMS))
            DEF_IO32(0x40000D4, m_core->dma[0].WriteDmaSad(3, IOWR_PARAMS))
            DEF_IO32(0x40000D8, m_core->dma[0].WriteDmaDad(3, IOWR_PARAMS))
            DEF_IO32(0x40000DC, m_core->dma[0].WriteDmaCnt(3, IOWR_PARAMS))
            DEF_IO32(0x40000E0, WriteDmaFill(0, IOWR_PARAMS))
            DEF_IO32(0x40000E4, WriteDmaFill(1, IOWR_PARAMS))
            DEF_IO32(0x40000E8, WriteDmaFill(2, IOWR_PARAMS))
            DEF_IO32(0x40000EC, WriteDmaFill(3, IOWR_PARAMS))
            DEF_IO16(0x4000100, m_core->timers[0].WriteTmCntL(0, IOWR_PARAMS))
            DEF_IO16(0x4000102, m_core->timers[0].WriteTmCntH(0, IOWR_PARAMS))
            DEF_IO16(0x4000104, m_core->timers[0].WriteTmCntL(1, IOWR_PARAMS))
            DEF_IO16(0x4000106, m_core->timers[0].WriteTmCntH(1, IOWR_PARAMS))
            DEF_IO16(0x4000108, m_core->timers[0].WriteTmCntL(2, IOWR_PARAMS))
            DEF_IO16(0x400010A, m_core->timers[0].WriteTmCntH(2, IOWR_PARAMS))
            DEF_IO16(0x400010C, m_core->timers[0].WriteTmCntL(3, IOWR_PARAMS))
            DEF_IO16(0x400010E, m_core->timers[0].WriteTmCntH(3, IOWR_PARAMS))
            DEF_IO16(0x4000180, m_core->ipc.WriteIpcSync(0, IOWR_PARAMS))
            DEF_IO16(0x4000184, m_core->ipc.WriteIpcFifoCnt(0, IOWR_PARAMS))
            DEF_IO32(0x4000188, m_core->ipc.WriteIpcFifoSend(0, IOWR_PARAMS))
            DEF_IO16(0x40001A0, m_core->cartridgeNds.WriteAuxSpiCnt(0, IOWR_PARAMS))
            DEF_IO_8(0x40001A2, m_core->cartridgeNds.WriteAuxSpiData(0, IOWR_PARAMS8))
            DEF_IO32(0x40001A4, m_core->cartridgeNds.WriteRomCtrl(0, IOWR_PARAMS))
            DEF_IO32(0x40001A8, m_core->cartridgeNds.WriteRomCmdOutL(0, IOWR_PARAMS))
            DEF_IO32(0x40001AC, m_core->cartridgeNds.WriteRomCmdOutH(0, IOWR_PARAMS))
            DEF_IO_8(0x4000208, m_core->interpreter[0].WriteIme(IOWR_PARAMS8))
            DEF_IO32(0x4000210, m_core->interpreter[0].WriteIe(IOWR_PARAMS))
            DEF_IO32(0x4000214, m_core->interpreter[0].WriteIrf(IOWR_PARAMS))
            DEF_IO_8(0x4000240, WriteVramCnt(0, IOWR_PARAMS8))
            DEF_IO_8(0x4000241, WriteVramCnt(1, IOWR_PARAMS8))
            DEF_IO_8(0x4000242, WriteVramCnt(2, IOWR_PARAMS8))
            DEF_IO_8(0x4000243, WriteVramCnt(3, IOWR_PARAMS8))
            DEF_IO_8(0x4000244, WriteVramCnt(4, IOWR_PARAMS8))
            DEF_IO_8(0x4000245, WriteVramCnt(5, IOWR_PARAMS8))
            DEF_IO_8(0x4000246, WriteVramCnt(6, IOWR_PARAMS8))
            DEF_IO_8(0x4000247, WriteWramCnt(IOWR_PARAMS8))
            DEF_IO_8(0x4000248, WriteVramCnt(7, IOWR_PARAMS8))
            DEF_IO_8(0x4000249, WriteVramCnt(8, IOWR_PARAMS8))
            DEF_IO16(0x4000280, m_core->divSqrt.WriteDivCnt(IOWR_PARAMS))
            DEF_IO32(0x4000290, m_core->divSqrt.WriteDivNumerL(IOWR_PARAMS))
            DEF_IO32(0x4000294, m_core->divSqrt.WriteDivNumerH(IOWR_PARAMS))
            DEF_IO32(0x4000298, m_core->divSqrt.WriteDivDenomL(IOWR_PARAMS))
            DEF_IO32(0x400029C, m_core->divSqrt.WriteDivDenomH(IOWR_PARAMS))
            DEF_IO16(0x40002B0, m_core->divSqrt.WriteSqrtCnt(IOWR_PARAMS))
            DEF_IO32(0x40002B8, m_core->divSqrt.WriteSqrtParamL(IOWR_PARAMS))
            DEF_IO32(0x40002BC, m_core->divSqrt.WriteSqrtParamH(IOWR_PARAMS))
            DEF_IO_8(0x4000300, m_core->interpreter[0].WritePostFlg(IOWR_PARAMS8))
            DEF_IO16(0x4000304, m_core->gpu.WritePowCnt1(IOWR_PARAMS))
            DEF_IO16(0x4000330, m_core->gpu3DRenderer.WriteEdgeColor(0, IOWR_PARAMS))
            DEF_IO16(0x4000332, m_core->gpu3DRenderer.WriteEdgeColor(1, IOWR_PARAMS))
            DEF_IO16(0x4000334, m_core->gpu3DRenderer.WriteEdgeColor(2, IOWR_PARAMS))
            DEF_IO16(0x4000336, m_core->gpu3DRenderer.WriteEdgeColor(3, IOWR_PARAMS))
            DEF_IO16(0x4000338, m_core->gpu3DRenderer.WriteEdgeColor(4, IOWR_PARAMS))
            DEF_IO16(0x400033A, m_core->gpu3DRenderer.WriteEdgeColor(5, IOWR_PARAMS))
            DEF_IO16(0x400033C, m_core->gpu3DRenderer.WriteEdgeColor(6, IOWR_PARAMS))
            DEF_IO16(0x400033E, m_core->gpu3DRenderer.WriteEdgeColor(7, IOWR_PARAMS))
            DEF_IO32(0x4000350, m_core->gpu3DRenderer.WriteClearColor(IOWR_PARAMS))
            DEF_IO16(0x4000354, m_core->gpu3DRenderer.WriteClearDepth(IOWR_PARAMS))
            DEF_IO32(0x4000358, m_core->gpu3DRenderer.WriteFogColor(IOWR_PARAMS))
            DEF_IO16(0x400035C, m_core->gpu3DRenderer.WriteFogOffset(IOWR_PARAMS))
            DEF_IO_8(0x4000360, m_core->gpu3DRenderer.WriteFogTable(0, IOWR_PARAMS8))
            DEF_IO_8(0x4000361, m_core->gpu3DRenderer.WriteFogTable(1, IOWR_PARAMS8))
            DEF_IO_8(0x4000362, m_core->gpu3DRenderer.WriteFogTable(2, IOWR_PARAMS8))
            DEF_IO_8(0x4000363, m_core->gpu3DRenderer.WriteFogTable(3, IOWR_PARAMS8))
            DEF_IO_8(0x4000364, m_core->gpu3DRenderer.WriteFogTable(4, IOWR_PARAMS8))
            DEF_IO_8(0x4000365, m_core->gpu3DRenderer.WriteFogTable(5, IOWR_PARAMS8))
            DEF_IO_8(0x4000366, m_core->gpu3DRenderer.WriteFogTable(6, IOWR_PARAMS8))
            DEF_IO_8(0x4000367, m_core->gpu3DRenderer.WriteFogTable(7, IOWR_PARAMS8))
            DEF_IO_8(0x4000368, m_core->gpu3DRenderer.WriteFogTable(8, IOWR_PARAMS8))
            DEF_IO_8(0x4000369, m_core->gpu3DRenderer.WriteFogTable(9, IOWR_PARAMS8))
            DEF_IO_8(0x400036A, m_core->gpu3DRenderer.WriteFogTable(10, IOWR_PARAMS8))
            DEF_IO_8(0x400036B, m_core->gpu3DRenderer.WriteFogTable(11, IOWR_PARAMS8))
            DEF_IO_8(0x400036C, m_core->gpu3DRenderer.WriteFogTable(12, IOWR_PARAMS8))
            DEF_IO_8(0x400036D, m_core->gpu3DRenderer.WriteFogTable(13, IOWR_PARAMS8))
            DEF_IO_8(0x400036E, m_core->gpu3DRenderer.WriteFogTable(14, IOWR_PARAMS8))
            DEF_IO_8(0x400036F, m_core->gpu3DRenderer.WriteFogTable(15, IOWR_PARAMS8))
            DEF_IO_8(0x4000370, m_core->gpu3DRenderer.WriteFogTable(16, IOWR_PARAMS8))
            DEF_IO_8(0x4000371, m_core->gpu3DRenderer.WriteFogTable(17, IOWR_PARAMS8))
            DEF_IO_8(0x4000372, m_core->gpu3DRenderer.WriteFogTable(18, IOWR_PARAMS8))
            DEF_IO_8(0x4000373, m_core->gpu3DRenderer.WriteFogTable(19, IOWR_PARAMS8))
            DEF_IO_8(0x4000374, m_core->gpu3DRenderer.WriteFogTable(20, IOWR_PARAMS8))
            DEF_IO_8(0x4000375, m_core->gpu3DRenderer.WriteFogTable(21, IOWR_PARAMS8))
            DEF_IO_8(0x4000376, m_core->gpu3DRenderer.WriteFogTable(22, IOWR_PARAMS8))
            DEF_IO_8(0x4000377, m_core->gpu3DRenderer.WriteFogTable(23, IOWR_PARAMS8))
            DEF_IO_8(0x4000378, m_core->gpu3DRenderer.WriteFogTable(24, IOWR_PARAMS8))
            DEF_IO_8(0x4000379, m_core->gpu3DRenderer.WriteFogTable(25, IOWR_PARAMS8))
            DEF_IO_8(0x400037A, m_core->gpu3DRenderer.WriteFogTable(26, IOWR_PARAMS8))
            DEF_IO_8(0x400037B, m_core->gpu3DRenderer.WriteFogTable(27, IOWR_PARAMS8))
            DEF_IO_8(0x400037C, m_core->gpu3DRenderer.WriteFogTable(28, IOWR_PARAMS8))
            DEF_IO_8(0x400037D, m_core->gpu3DRenderer.WriteFogTable(29, IOWR_PARAMS8))
            DEF_IO_8(0x400037E, m_core->gpu3DRenderer.WriteFogTable(30, IOWR_PARAMS8))
            DEF_IO_8(0x400037F, m_core->gpu3DRenderer.WriteFogTable(31, IOWR_PARAMS8))
            DEF_IO16(0x4000380, m_core->gpu3DRenderer.WriteToonTable(0, IOWR_PARAMS))
            DEF_IO16(0x4000382, m_core->gpu3DRenderer.WriteToonTable(1, IOWR_PARAMS))
            DEF_IO16(0x4000384, m_core->gpu3DRenderer.WriteToonTable(2, IOWR_PARAMS))
            DEF_IO16(0x4000386, m_core->gpu3DRenderer.WriteToonTable(3, IOWR_PARAMS))
            DEF_IO16(0x4000388, m_core->gpu3DRenderer.WriteToonTable(4, IOWR_PARAMS))
            DEF_IO16(0x400038A, m_core->gpu3DRenderer.WriteToonTable(5, IOWR_PARAMS))
            DEF_IO16(0x400038C, m_core->gpu3DRenderer.WriteToonTable(6, IOWR_PARAMS))
            DEF_IO16(0x400038E, m_core->gpu3DRenderer.WriteToonTable(7, IOWR_PARAMS))
            DEF_IO16(0x4000390, m_core->gpu3DRenderer.WriteToonTable(8, IOWR_PARAMS))
            DEF_IO16(0x4000392, m_core->gpu3DRenderer.WriteToonTable(9, IOWR_PARAMS))
            DEF_IO16(0x4000394, m_core->gpu3DRenderer.WriteToonTable(10, IOWR_PARAMS))
            DEF_IO16(0x4000396, m_core->gpu3DRenderer.WriteToonTable(11, IOWR_PARAMS))
            DEF_IO16(0x4000398, m_core->gpu3DRenderer.WriteToonTable(12, IOWR_PARAMS))
            DEF_IO16(0x400039A, m_core->gpu3DRenderer.WriteToonTable(13, IOWR_PARAMS))
            DEF_IO16(0x400039C, m_core->gpu3DRenderer.WriteToonTable(14, IOWR_PARAMS))
            DEF_IO16(0x400039E, m_core->gpu3DRenderer.WriteToonTable(15, IOWR_PARAMS))
            DEF_IO16(0x40003A0, m_core->gpu3DRenderer.WriteToonTable(16, IOWR_PARAMS))
            DEF_IO16(0x40003A2, m_core->gpu3DRenderer.WriteToonTable(17, IOWR_PARAMS))
            DEF_IO16(0x40003A4, m_core->gpu3DRenderer.WriteToonTable(18, IOWR_PARAMS))
            DEF_IO16(0x40003A6, m_core->gpu3DRenderer.WriteToonTable(19, IOWR_PARAMS))
            DEF_IO16(0x40003A8, m_core->gpu3DRenderer.WriteToonTable(20, IOWR_PARAMS))
            DEF_IO16(0x40003AA, m_core->gpu3DRenderer.WriteToonTable(21, IOWR_PARAMS))
            DEF_IO16(0x40003AC, m_core->gpu3DRenderer.WriteToonTable(22, IOWR_PARAMS))
            DEF_IO16(0x40003AE, m_core->gpu3DRenderer.WriteToonTable(23, IOWR_PARAMS))
            DEF_IO16(0x40003B0, m_core->gpu3DRenderer.WriteToonTable(24, IOWR_PARAMS))
            DEF_IO16(0x40003B2, m_core->gpu3DRenderer.WriteToonTable(25, IOWR_PARAMS))
            DEF_IO16(0x40003B4, m_core->gpu3DRenderer.WriteToonTable(26, IOWR_PARAMS))
            DEF_IO16(0x40003B6, m_core->gpu3DRenderer.WriteToonTable(27, IOWR_PARAMS))
            DEF_IO16(0x40003B8, m_core->gpu3DRenderer.WriteToonTable(28, IOWR_PARAMS))
            DEF_IO16(0x40003BA, m_core->gpu3DRenderer.WriteToonTable(29, IOWR_PARAMS))
            DEF_IO16(0x40003BC, m_core->gpu3DRenderer.WriteToonTable(30, IOWR_PARAMS))
            DEF_IO16(0x40003BE, m_core->gpu3DRenderer.WriteToonTable(31, IOWR_PARAMS))
            DEF_IO32(0x4000400, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000404, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000408, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x400040C, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000410, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000414, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000418, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x400041C, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000420, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000424, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000428, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x400042C, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000430, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000434, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000438, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x400043C, m_core->gpu3D.WriteGxFifo(IOWR_PARAMS))
            DEF_IO32(0x4000440, m_core->gpu3D.WriteMtxMode(IOWR_PARAMS))
            DEF_IO32(0x4000444, m_core->gpu3D.WriteMtxPush(IOWR_PARAMS))
            DEF_IO32(0x4000448, m_core->gpu3D.WriteMtxPop(IOWR_PARAMS))
            DEF_IO32(0x400044C, m_core->gpu3D.WriteMtxStore(IOWR_PARAMS))
            DEF_IO32(0x4000450, m_core->gpu3D.WriteMtxRestore(IOWR_PARAMS))
            DEF_IO32(0x4000454, m_core->gpu3D.WriteMtxIdentity(IOWR_PARAMS))
            DEF_IO32(0x4000458, m_core->gpu3D.WriteMtxLoad44(IOWR_PARAMS))
            DEF_IO32(0x400045C, m_core->gpu3D.WriteMtxLoad43(IOWR_PARAMS))
            DEF_IO32(0x4000460, m_core->gpu3D.WriteMtxMult44(IOWR_PARAMS))
            DEF_IO32(0x4000464, m_core->gpu3D.WriteMtxMult43(IOWR_PARAMS))
            DEF_IO32(0x4000468, m_core->gpu3D.WriteMtxMult33(IOWR_PARAMS))
            DEF_IO32(0x400046C, m_core->gpu3D.WriteMtxScale(IOWR_PARAMS))
            DEF_IO32(0x4000470, m_core->gpu3D.WriteMtxTrans(IOWR_PARAMS))
            DEF_IO32(0x4000480, m_core->gpu3D.WriteColor(IOWR_PARAMS))
            DEF_IO32(0x4000484, m_core->gpu3D.WriteNormal(IOWR_PARAMS))
            DEF_IO32(0x4000488, m_core->gpu3D.WriteTexCoord(IOWR_PARAMS))
            DEF_IO32(0x400048C, m_core->gpu3D.WriteVtx16(IOWR_PARAMS))
            DEF_IO32(0x4000490, m_core->gpu3D.WriteVtx10(IOWR_PARAMS))
            DEF_IO32(0x4000494, m_core->gpu3D.WriteVtxXY(IOWR_PARAMS))
            DEF_IO32(0x4000498, m_core->gpu3D.WriteVtxXZ(IOWR_PARAMS))
            DEF_IO32(0x400049C, m_core->gpu3D.WriteVtxYZ(IOWR_PARAMS))
            DEF_IO32(0x40004A0, m_core->gpu3D.WriteVtxDiff(IOWR_PARAMS))
            DEF_IO32(0x40004A4, m_core->gpu3D.WritePolygonAttr(IOWR_PARAMS))
            DEF_IO32(0x40004A8, m_core->gpu3D.WriteTexImageParam(IOWR_PARAMS))
            DEF_IO32(0x40004AC, m_core->gpu3D.WritePlttBase(IOWR_PARAMS))
            DEF_IO32(0x40004C0, m_core->gpu3D.WriteDifAmb(IOWR_PARAMS))
            DEF_IO32(0x40004C4, m_core->gpu3D.WriteSpeEmi(IOWR_PARAMS))
            DEF_IO32(0x40004C8, m_core->gpu3D.WriteLightVector(IOWR_PARAMS))
            DEF_IO32(0x40004CC, m_core->gpu3D.WriteLightColor(IOWR_PARAMS))
            DEF_IO32(0x40004D0, m_core->gpu3D.WriteShininess(IOWR_PARAMS))
            DEF_IO32(0x4000500, m_core->gpu3D.WriteBeginVtxs(IOWR_PARAMS))
            DEF_IO32(0x4000504, m_core->gpu3D.WriteEndVtxs(IOWR_PARAMS))
            DEF_IO32(0x4000540, m_core->gpu3D.WriteSwapBuffers(IOWR_PARAMS))
            DEF_IO32(0x4000580, m_core->gpu3D.WriteViewport(IOWR_PARAMS))
            DEF_IO32(0x40005C0, m_core->gpu3D.WriteBoxTest(IOWR_PARAMS))
            DEF_IO32(0x40005C4, m_core->gpu3D.WritePosTest(IOWR_PARAMS))
            DEF_IO32(0x40005C8, m_core->gpu3D.WriteVecTest(IOWR_PARAMS))
            DEF_IO32(0x4000600, m_core->gpu3D.WriteGxStat(IOWR_PARAMS))
            DEF_IO32(0x4001000, m_core->gpu2D[1].WriteDispCnt(IOWR_PARAMS))
            DEF_IO16(0x4001008, m_core->gpu2D[1].WriteBgCnt(0, IOWR_PARAMS))
            DEF_IO16(0x400100A, m_core->gpu2D[1].WriteBgCnt(1, IOWR_PARAMS))
            DEF_IO16(0x400100C, m_core->gpu2D[1].WriteBgCnt(2, IOWR_PARAMS))
            DEF_IO16(0x400100E, m_core->gpu2D[1].WriteBgCnt(3, IOWR_PARAMS))
            DEF_IO16(0x4001010, m_core->gpu2D[1].WriteBgHOfs(0, IOWR_PARAMS))
            DEF_IO16(0x4001012, m_core->gpu2D[1].WriteBgVOfs(0, IOWR_PARAMS))
            DEF_IO16(0x4001014, m_core->gpu2D[1].WriteBgHOfs(1, IOWR_PARAMS))
            DEF_IO16(0x4001016, m_core->gpu2D[1].WriteBgVOfs(1, IOWR_PARAMS))
            DEF_IO16(0x4001018, m_core->gpu2D[1].WriteBgHOfs(2, IOWR_PARAMS))
            DEF_IO16(0x400101A, m_core->gpu2D[1].WriteBgVOfs(2, IOWR_PARAMS))
            DEF_IO16(0x400101C, m_core->gpu2D[1].WriteBgHOfs(3, IOWR_PARAMS))
            DEF_IO16(0x400101E, m_core->gpu2D[1].WriteBgVOfs(3, IOWR_PARAMS))
            DEF_IO16(0x4001020, m_core->gpu2D[1].WriteBgPA(2, IOWR_PARAMS))
            DEF_IO16(0x4001022, m_core->gpu2D[1].WriteBgPB(2, IOWR_PARAMS))
            DEF_IO16(0x4001024, m_core->gpu2D[1].WriteBgPC(2, IOWR_PARAMS))
            DEF_IO16(0x4001026, m_core->gpu2D[1].WriteBgPD(2, IOWR_PARAMS))
            DEF_IO32(0x4001028, m_core->gpu2D[1].WriteBgX(2, IOWR_PARAMS))
            DEF_IO32(0x400102C, m_core->gpu2D[1].WriteBgY(2, IOWR_PARAMS))
            DEF_IO16(0x4001030, m_core->gpu2D[1].WriteBgPA(3, IOWR_PARAMS))
            DEF_IO16(0x4001032, m_core->gpu2D[1].WriteBgPB(3, IOWR_PARAMS))
            DEF_IO16(0x4001034, m_core->gpu2D[1].WriteBgPC(3, IOWR_PARAMS))
            DEF_IO16(0x4001036, m_core->gpu2D[1].WriteBgPD(3, IOWR_PARAMS))
            DEF_IO32(0x4001038, m_core->gpu2D[1].WriteBgX(3, IOWR_PARAMS))
            DEF_IO32(0x400103C, m_core->gpu2D[1].WriteBgY(3, IOWR_PARAMS))
            DEF_IO16(0x4001040, m_core->gpu2D[1].WriteWinH(0, IOWR_PARAMS))
            DEF_IO16(0x4001042, m_core->gpu2D[1].WriteWinH(1, IOWR_PARAMS))
            DEF_IO16(0x4001044, m_core->gpu2D[1].WriteWinV(0, IOWR_PARAMS))
            DEF_IO16(0x4001046, m_core->gpu2D[1].WriteWinV(1, IOWR_PARAMS))
            DEF_IO16(0x4001048, m_core->gpu2D[1].WriteWinIn(IOWR_PARAMS))
            DEF_IO16(0x400104A, m_core->gpu2D[1].WriteWinOut(IOWR_PARAMS))
            DEF_IO16(0x400104C, m_core->gpu2D[1].WriteMosaic(IOWR_PARAMS))
            DEF_IO16(0x4001050, m_core->gpu2D[1].WriteBldCnt(IOWR_PARAMS))
            DEF_IO16(0x4001052, m_core->gpu2D[1].WriteBldAlpha(IOWR_PARAMS))
            DEF_IO_8(0x4001054, m_core->gpu2D[1].WriteBldY(IOWR_PARAMS8))
            DEF_IO16(0x400106C, m_core->gpu2D[1].WriteMasterBright(IOWR_PARAMS))
            default:
                if (i == 0) {
                    LOG_VERBOSE("Unknown ARM9 I/O register write: 0x%X\n", address);
                    return;
                }
                i++;
                continue;
        }
        i += size - base;
    }
}

template <typename T> void Memory::IoWrite7(uint32_t address, T value) {
    if (address >= 0x04808000 && address < 0x04810000)
        address &= ~0x00008000u;
    size_t i = 0;
    while (i < sizeof(T)) {
        uint32_t base = static_cast<uint32_t>(address + i);
        size_t   size;
        uint32_t mask = static_cast<uint32_t>(((uint64_t)1 << ((sizeof(T) - i) * 8)) - 1);
        uint32_t data = value >> (i * 8);
        switch (base) {
            DEF_IO16(0x4000004, m_core->gpu.WriteDispStat(1, IOWR_PARAMS))
            DEF_IO32(0x40000B0, m_core->dma[1].WriteDmaSad(0, IOWR_PARAMS))
            DEF_IO32(0x40000B4, m_core->dma[1].WriteDmaDad(0, IOWR_PARAMS))
            DEF_IO32(0x40000B8, m_core->dma[1].WriteDmaCnt(0, IOWR_PARAMS))
            DEF_IO32(0x40000BC, m_core->dma[1].WriteDmaSad(1, IOWR_PARAMS))
            DEF_IO32(0x40000C0, m_core->dma[1].WriteDmaDad(1, IOWR_PARAMS))
            DEF_IO32(0x40000C4, m_core->dma[1].WriteDmaCnt(1, IOWR_PARAMS))
            DEF_IO32(0x40000C8, m_core->dma[1].WriteDmaSad(2, IOWR_PARAMS))
            DEF_IO32(0x40000CC, m_core->dma[1].WriteDmaDad(2, IOWR_PARAMS))
            DEF_IO32(0x40000D0, m_core->dma[1].WriteDmaCnt(2, IOWR_PARAMS))
            DEF_IO32(0x40000D4, m_core->dma[1].WriteDmaSad(3, IOWR_PARAMS))
            DEF_IO32(0x40000D8, m_core->dma[1].WriteDmaDad(3, IOWR_PARAMS))
            DEF_IO32(0x40000DC, m_core->dma[1].WriteDmaCnt(3, IOWR_PARAMS))
            DEF_IO16(0x4000100, m_core->timers[1].WriteTmCntL(0, IOWR_PARAMS))
            DEF_IO16(0x4000102, m_core->timers[1].WriteTmCntH(0, IOWR_PARAMS))
            DEF_IO16(0x4000104, m_core->timers[1].WriteTmCntL(1, IOWR_PARAMS))
            DEF_IO16(0x4000106, m_core->timers[1].WriteTmCntH(1, IOWR_PARAMS))
            DEF_IO16(0x4000108, m_core->timers[1].WriteTmCntL(2, IOWR_PARAMS))
            DEF_IO16(0x400010A, m_core->timers[1].WriteTmCntH(2, IOWR_PARAMS))
            DEF_IO16(0x400010C, m_core->timers[1].WriteTmCntL(3, IOWR_PARAMS))
            DEF_IO16(0x400010E, m_core->timers[1].WriteTmCntH(3, IOWR_PARAMS))
            DEF_IO_8(0x4000138, m_core->rtc.WriteRtc(IOWR_PARAMS8))
            DEF_IO16(0x4000180, m_core->ipc.WriteIpcSync(1, IOWR_PARAMS))
            DEF_IO16(0x4000184, m_core->ipc.WriteIpcFifoCnt(1, IOWR_PARAMS))
            DEF_IO32(0x4000188, m_core->ipc.WriteIpcFifoSend(1, IOWR_PARAMS))
            DEF_IO16(0x40001A0, m_core->cartridgeNds.WriteAuxSpiCnt(1, IOWR_PARAMS))
            DEF_IO_8(0x40001A2, m_core->cartridgeNds.WriteAuxSpiData(1, IOWR_PARAMS8))
            DEF_IO32(0x40001A4, m_core->cartridgeNds.WriteRomCtrl(1, IOWR_PARAMS))
            DEF_IO32(0x40001A8, m_core->cartridgeNds.WriteRomCmdOutL(1, IOWR_PARAMS))
            DEF_IO32(0x40001AC, m_core->cartridgeNds.WriteRomCmdOutH(1, IOWR_PARAMS))
            DEF_IO16(0x40001C0, m_core->spi.WriteSpiCnt(IOWR_PARAMS))
            DEF_IO_8(0x40001C2, m_core->spi.WriteSpiData(IOWR_PARAMS8))
            DEF_IO_8(0x4000208, m_core->interpreter[1].WriteIme(IOWR_PARAMS8))
            DEF_IO32(0x4000210, m_core->interpreter[1].WriteIe(IOWR_PARAMS))
            DEF_IO32(0x4000214, m_core->interpreter[1].WriteIrf(IOWR_PARAMS))
            DEF_IO_8(0x4000300, m_core->interpreter[1].WritePostFlg(IOWR_PARAMS8))
            DEF_IO_8(0x4000301, WriteHaltCnt(IOWR_PARAMS8))
            DEF_IO32(0x4000400, m_core->spu.WriteSoundCnt(0, IOWR_PARAMS))
            DEF_IO32(0x4000404, m_core->spu.WriteSoundSad(0, IOWR_PARAMS))
            DEF_IO16(0x4000408, m_core->spu.WriteSoundTmr(0, IOWR_PARAMS))
            DEF_IO16(0x400040A, m_core->spu.WriteSoundPnt(0, IOWR_PARAMS))
            DEF_IO32(0x400040C, m_core->spu.WriteSoundLen(0, IOWR_PARAMS))
            DEF_IO32(0x4000410, m_core->spu.WriteSoundCnt(1, IOWR_PARAMS))
            DEF_IO32(0x4000414, m_core->spu.WriteSoundSad(1, IOWR_PARAMS))
            DEF_IO16(0x4000418, m_core->spu.WriteSoundTmr(1, IOWR_PARAMS))
            DEF_IO16(0x400041A, m_core->spu.WriteSoundPnt(1, IOWR_PARAMS))
            DEF_IO32(0x400041C, m_core->spu.WriteSoundLen(1, IOWR_PARAMS))
            DEF_IO32(0x4000420, m_core->spu.WriteSoundCnt(2, IOWR_PARAMS))
            DEF_IO32(0x4000424, m_core->spu.WriteSoundSad(2, IOWR_PARAMS))
            DEF_IO16(0x4000428, m_core->spu.WriteSoundTmr(2, IOWR_PARAMS))
            DEF_IO16(0x400042A, m_core->spu.WriteSoundPnt(2, IOWR_PARAMS))
            DEF_IO32(0x400042C, m_core->spu.WriteSoundLen(2, IOWR_PARAMS))
            DEF_IO32(0x4000430, m_core->spu.WriteSoundCnt(3, IOWR_PARAMS))
            DEF_IO32(0x4000434, m_core->spu.WriteSoundSad(3, IOWR_PARAMS))
            DEF_IO16(0x4000438, m_core->spu.WriteSoundTmr(3, IOWR_PARAMS))
            DEF_IO16(0x400043A, m_core->spu.WriteSoundPnt(3, IOWR_PARAMS))
            DEF_IO32(0x400043C, m_core->spu.WriteSoundLen(3, IOWR_PARAMS))
            DEF_IO32(0x4000440, m_core->spu.WriteSoundCnt(4, IOWR_PARAMS))
            DEF_IO32(0x4000444, m_core->spu.WriteSoundSad(4, IOWR_PARAMS))
            DEF_IO16(0x4000448, m_core->spu.WriteSoundTmr(4, IOWR_PARAMS))
            DEF_IO16(0x400044A, m_core->spu.WriteSoundPnt(4, IOWR_PARAMS))
            DEF_IO32(0x400044C, m_core->spu.WriteSoundLen(4, IOWR_PARAMS))
            DEF_IO32(0x4000450, m_core->spu.WriteSoundCnt(5, IOWR_PARAMS))
            DEF_IO32(0x4000454, m_core->spu.WriteSoundSad(5, IOWR_PARAMS))
            DEF_IO16(0x4000458, m_core->spu.WriteSoundTmr(5, IOWR_PARAMS))
            DEF_IO16(0x400045A, m_core->spu.WriteSoundPnt(5, IOWR_PARAMS))
            DEF_IO32(0x400045C, m_core->spu.WriteSoundLen(5, IOWR_PARAMS))
            DEF_IO32(0x4000460, m_core->spu.WriteSoundCnt(6, IOWR_PARAMS))
            DEF_IO32(0x4000464, m_core->spu.WriteSoundSad(6, IOWR_PARAMS))
            DEF_IO16(0x4000468, m_core->spu.WriteSoundTmr(6, IOWR_PARAMS))
            DEF_IO16(0x400046A, m_core->spu.WriteSoundPnt(6, IOWR_PARAMS))
            DEF_IO32(0x400046C, m_core->spu.WriteSoundLen(6, IOWR_PARAMS))
            DEF_IO32(0x4000470, m_core->spu.WriteSoundCnt(7, IOWR_PARAMS))
            DEF_IO32(0x4000474, m_core->spu.WriteSoundSad(7, IOWR_PARAMS))
            DEF_IO16(0x4000478, m_core->spu.WriteSoundTmr(7, IOWR_PARAMS))
            DEF_IO16(0x400047A, m_core->spu.WriteSoundPnt(7, IOWR_PARAMS))
            DEF_IO32(0x400047C, m_core->spu.WriteSoundLen(7, IOWR_PARAMS))
            DEF_IO32(0x4000480, m_core->spu.WriteSoundCnt(8, IOWR_PARAMS))
            DEF_IO32(0x4000484, m_core->spu.WriteSoundSad(8, IOWR_PARAMS))
            DEF_IO16(0x4000488, m_core->spu.WriteSoundTmr(8, IOWR_PARAMS))
            DEF_IO16(0x400048A, m_core->spu.WriteSoundPnt(8, IOWR_PARAMS))
            DEF_IO32(0x400048C, m_core->spu.WriteSoundLen(8, IOWR_PARAMS))
            DEF_IO32(0x4000490, m_core->spu.WriteSoundCnt(9, IOWR_PARAMS))
            DEF_IO32(0x4000494, m_core->spu.WriteSoundSad(9, IOWR_PARAMS))
            DEF_IO16(0x4000498, m_core->spu.WriteSoundTmr(9, IOWR_PARAMS))
            DEF_IO16(0x400049A, m_core->spu.WriteSoundPnt(9, IOWR_PARAMS))
            DEF_IO32(0x400049C, m_core->spu.WriteSoundLen(9, IOWR_PARAMS))
            DEF_IO32(0x40004A0, m_core->spu.WriteSoundCnt(10, IOWR_PARAMS))
            DEF_IO32(0x40004A4, m_core->spu.WriteSoundSad(10, IOWR_PARAMS))
            DEF_IO16(0x40004A8, m_core->spu.WriteSoundTmr(10, IOWR_PARAMS))
            DEF_IO16(0x40004AA, m_core->spu.WriteSoundPnt(10, IOWR_PARAMS))
            DEF_IO32(0x40004AC, m_core->spu.WriteSoundLen(10, IOWR_PARAMS))
            DEF_IO32(0x40004B0, m_core->spu.WriteSoundCnt(11, IOWR_PARAMS))
            DEF_IO32(0x40004B4, m_core->spu.WriteSoundSad(11, IOWR_PARAMS))
            DEF_IO16(0x40004B8, m_core->spu.WriteSoundTmr(11, IOWR_PARAMS))
            DEF_IO16(0x40004BA, m_core->spu.WriteSoundPnt(11, IOWR_PARAMS))
            DEF_IO32(0x40004BC, m_core->spu.WriteSoundLen(11, IOWR_PARAMS))
            DEF_IO32(0x40004C0, m_core->spu.WriteSoundCnt(12, IOWR_PARAMS))
            DEF_IO32(0x40004C4, m_core->spu.WriteSoundSad(12, IOWR_PARAMS))
            DEF_IO16(0x40004C8, m_core->spu.WriteSoundTmr(12, IOWR_PARAMS))
            DEF_IO16(0x40004CA, m_core->spu.WriteSoundPnt(12, IOWR_PARAMS))
            DEF_IO32(0x40004CC, m_core->spu.WriteSoundLen(12, IOWR_PARAMS))
            DEF_IO32(0x40004D0, m_core->spu.WriteSoundCnt(13, IOWR_PARAMS))
            DEF_IO32(0x40004D4, m_core->spu.WriteSoundSad(13, IOWR_PARAMS))
            DEF_IO16(0x40004D8, m_core->spu.WriteSoundTmr(13, IOWR_PARAMS))
            DEF_IO16(0x40004DA, m_core->spu.WriteSoundPnt(13, IOWR_PARAMS))
            DEF_IO32(0x40004DC, m_core->spu.WriteSoundLen(13, IOWR_PARAMS))
            DEF_IO32(0x40004E0, m_core->spu.WriteSoundCnt(14, IOWR_PARAMS))
            DEF_IO32(0x40004E4, m_core->spu.WriteSoundSad(14, IOWR_PARAMS))
            DEF_IO16(0x40004E8, m_core->spu.WriteSoundTmr(14, IOWR_PARAMS))
            DEF_IO16(0x40004EA, m_core->spu.WriteSoundPnt(14, IOWR_PARAMS))
            DEF_IO32(0x40004EC, m_core->spu.WriteSoundLen(14, IOWR_PARAMS))
            DEF_IO32(0x40004F0, m_core->spu.WriteSoundCnt(15, IOWR_PARAMS))
            DEF_IO32(0x40004F4, m_core->spu.WriteSoundSad(15, IOWR_PARAMS))
            DEF_IO16(0x40004F8, m_core->spu.WriteSoundTmr(15, IOWR_PARAMS))
            DEF_IO16(0x40004FA, m_core->spu.WriteSoundPnt(15, IOWR_PARAMS))
            DEF_IO32(0x40004FC, m_core->spu.WriteSoundLen(15, IOWR_PARAMS))
            DEF_IO16(0x4000500, m_core->spu.WriteMainSoundCnt(IOWR_PARAMS))
            DEF_IO16(0x4000504, m_core->spu.WriteSoundBias(IOWR_PARAMS))
            DEF_IO_8(0x4000508, m_core->spu.WriteSndCapCnt(0, IOWR_PARAMS8))
            DEF_IO_8(0x4000509, m_core->spu.WriteSndCapCnt(1, IOWR_PARAMS8))
            DEF_IO32(0x4000510, m_core->spu.WriteSndCapDad(0, IOWR_PARAMS))
            DEF_IO16(0x4000514, m_core->spu.WriteSndCapLen(0, IOWR_PARAMS))
            DEF_IO32(0x4000518, m_core->spu.WriteSndCapDad(1, IOWR_PARAMS))
            DEF_IO16(0x400051C, m_core->spu.WriteSndCapLen(1, IOWR_PARAMS))
            DEF_IO16(0x4800006, m_core->wifi.WriteWModeWep(IOWR_PARAMS))
            DEF_IO16(0x4800010, m_core->wifi.WriteWIrf(IOWR_PARAMS))
            DEF_IO16(0x4800012, m_core->wifi.WriteWIe(IOWR_PARAMS))
            DEF_IO16(0x4800018, m_core->wifi.WriteWMacaddr(0, IOWR_PARAMS))
            DEF_IO16(0x480001A, m_core->wifi.WriteWMacaddr(1, IOWR_PARAMS))
            DEF_IO16(0x480001C, m_core->wifi.WriteWMacaddr(2, IOWR_PARAMS))
            DEF_IO16(0x4800020, m_core->wifi.WriteWBssid(0, IOWR_PARAMS))
            DEF_IO16(0x4800022, m_core->wifi.WriteWBssid(1, IOWR_PARAMS))
            DEF_IO16(0x4800024, m_core->wifi.WriteWBssid(2, IOWR_PARAMS))
            DEF_IO16(0x480002A, m_core->wifi.WriteWAidFull(IOWR_PARAMS))
            DEF_IO16(0x4800030, m_core->wifi.WriteWRxcnt(IOWR_PARAMS))
            DEF_IO16(0x480003C, m_core->wifi.WriteWPowerstate(IOWR_PARAMS))
            DEF_IO16(0x4800040, m_core->wifi.WriteWPowerforce(IOWR_PARAMS))
            DEF_IO16(0x4800050, m_core->wifi.WriteWRxbufBegin(IOWR_PARAMS))
            DEF_IO16(0x4800052, m_core->wifi.WriteWRxbufEnd(IOWR_PARAMS))
            DEF_IO16(0x4800056, m_core->wifi.WriteWRxbufWrAddr(IOWR_PARAMS))
            DEF_IO16(0x4800058, m_core->wifi.WriteWRxbufRdAddr(IOWR_PARAMS))
            DEF_IO16(0x480005A, m_core->wifi.WriteWRxbufReadcsr(IOWR_PARAMS))
            DEF_IO16(0x480005C, m_core->wifi.WriteWRxbufCount(IOWR_PARAMS))
            DEF_IO16(0x4800062, m_core->wifi.WriteWRxbufGap(IOWR_PARAMS))
            DEF_IO16(0x4800064, m_core->wifi.WriteWRxbufGapdisp(IOWR_PARAMS))
            DEF_IO16(0x4800068, m_core->wifi.WriteWTxbufWrAddr(IOWR_PARAMS))
            DEF_IO16(0x480006C, m_core->wifi.WriteWTxbufCount(IOWR_PARAMS))
            DEF_IO16(0x4800070, m_core->wifi.WriteWTxbufWrData(IOWR_PARAMS))
            DEF_IO16(0x4800074, m_core->wifi.WriteWTxbufGap(IOWR_PARAMS))
            DEF_IO16(0x4800076, m_core->wifi.WriteWTxbufGapdisp(IOWR_PARAMS))
            DEF_IO16(0x4800080, m_core->wifi.WriteWTxbufLoc(4, IOWR_PARAMS))
            DEF_IO16(0x480008C, m_core->wifi.WriteWBeaconInt(IOWR_PARAMS))
            DEF_IO16(0x4800090, m_core->wifi.WriteWTxbufLoc(1, IOWR_PARAMS))
            DEF_IO16(0x48000A0, m_core->wifi.WriteWTxbufLoc(0, IOWR_PARAMS))
            DEF_IO16(0x48000A4, m_core->wifi.WriteWTxbufLoc(2, IOWR_PARAMS))
            DEF_IO16(0x48000A8, m_core->wifi.WriteWTxbufLoc(3, IOWR_PARAMS))
            DEF_IO16(0x48000AC, m_core->wifi.WriteWTxreqReset(IOWR_PARAMS))
            DEF_IO16(0x48000AE, m_core->wifi.WriteWTxreqSet(IOWR_PARAMS))
            DEF_IO16(0x48000E8, m_core->wifi.WriteWUsCountcnt(IOWR_PARAMS))
            DEF_IO16(0x48000EA, m_core->wifi.WriteWUsComparecnt(IOWR_PARAMS))
            DEF_IO16(0x4800110, m_core->wifi.WriteWPreBeacon(IOWR_PARAMS))
            DEF_IO16(0x480011C, m_core->wifi.WriteWBeaconCount(IOWR_PARAMS))
            DEF_IO16(0x4800120, m_core->wifi.WriteWConfig(0, IOWR_PARAMS))
            DEF_IO16(0x4800122, m_core->wifi.WriteWConfig(1, IOWR_PARAMS))
            DEF_IO16(0x4800124, m_core->wifi.WriteWConfig(2, IOWR_PARAMS))
            DEF_IO16(0x4800128, m_core->wifi.WriteWConfig(3, IOWR_PARAMS))
            DEF_IO16(0x4800130, m_core->wifi.WriteWConfig(4, IOWR_PARAMS))
            DEF_IO16(0x4800132, m_core->wifi.WriteWConfig(5, IOWR_PARAMS))
            DEF_IO16(0x4800134, m_core->wifi.WriteWPostBeacon(IOWR_PARAMS))
            DEF_IO16(0x4800140, m_core->wifi.WriteWConfig(6, IOWR_PARAMS))
            DEF_IO16(0x4800142, m_core->wifi.WriteWConfig(7, IOWR_PARAMS))
            DEF_IO16(0x4800144, m_core->wifi.WriteWConfig(8, IOWR_PARAMS))
            DEF_IO16(0x4800146, m_core->wifi.WriteWConfig(9, IOWR_PARAMS))
            DEF_IO16(0x4800148, m_core->wifi.WriteWConfig(10, IOWR_PARAMS))
            DEF_IO16(0x480014A, m_core->wifi.WriteWConfig(11, IOWR_PARAMS))
            DEF_IO16(0x480014C, m_core->wifi.WriteWConfig(12, IOWR_PARAMS))
            DEF_IO16(0x4800150, m_core->wifi.WriteWConfig(13, IOWR_PARAMS))
            DEF_IO16(0x4800154, m_core->wifi.WriteWConfig(14, IOWR_PARAMS))
            DEF_IO16(0x4800158, m_core->wifi.WriteWBbCnt(IOWR_PARAMS))
            DEF_IO16(0x480015A, m_core->wifi.WriteWBbWrite(IOWR_PARAMS))
            DEF_IO16(0x480021C, m_core->wifi.WriteWIrfSet(IOWR_PARAMS))
            default:
                if (i == 0) {
                    LOG_VERBOSE("Unknown ARM7 I/O register write: 0x%X\n", address);
                    return;
                }
                i++;
                continue;
        }
        i += size - base;
    }
}

template <typename T> void Memory::IoWriteGba(uint32_t address, T value) {
    size_t i = 0;
    while (i < sizeof(T)) {
        uint32_t base = static_cast<uint32_t>(address + i);
        size_t   size;
        uint32_t mask = static_cast<uint32_t>(((uint64_t)1 << ((sizeof(T) - i) * 8)) - 1);
        uint32_t data = value >> (i * 8);
        switch (base) {
            DEF_IO16(0x4000000, m_core->gpu2D[0].WriteDispCnt(IOWR_PARAMS))
            DEF_IO16(0x4000004, m_core->gpu.WriteDispStat(1, IOWR_PARAMS))
            DEF_IO16(0x4000008, m_core->gpu2D[0].WriteBgCnt(0, IOWR_PARAMS))
            DEF_IO16(0x400000A, m_core->gpu2D[0].WriteBgCnt(1, IOWR_PARAMS))
            DEF_IO16(0x400000C, m_core->gpu2D[0].WriteBgCnt(2, IOWR_PARAMS))
            DEF_IO16(0x400000E, m_core->gpu2D[0].WriteBgCnt(3, IOWR_PARAMS))
            DEF_IO16(0x4000010, m_core->gpu2D[0].WriteBgHOfs(0, IOWR_PARAMS))
            DEF_IO16(0x4000012, m_core->gpu2D[0].WriteBgVOfs(0, IOWR_PARAMS))
            DEF_IO16(0x4000014, m_core->gpu2D[0].WriteBgHOfs(1, IOWR_PARAMS))
            DEF_IO16(0x4000016, m_core->gpu2D[0].WriteBgVOfs(1, IOWR_PARAMS))
            DEF_IO16(0x4000018, m_core->gpu2D[0].WriteBgHOfs(2, IOWR_PARAMS))
            DEF_IO16(0x400001A, m_core->gpu2D[0].WriteBgVOfs(2, IOWR_PARAMS))
            DEF_IO16(0x400001C, m_core->gpu2D[0].WriteBgHOfs(3, IOWR_PARAMS))
            DEF_IO16(0x400001E, m_core->gpu2D[0].WriteBgVOfs(3, IOWR_PARAMS))
            DEF_IO16(0x4000020, m_core->gpu2D[0].WriteBgPA(2, IOWR_PARAMS))
            DEF_IO16(0x4000022, m_core->gpu2D[0].WriteBgPB(2, IOWR_PARAMS))
            DEF_IO16(0x4000024, m_core->gpu2D[0].WriteBgPC(2, IOWR_PARAMS))
            DEF_IO16(0x4000026, m_core->gpu2D[0].WriteBgPD(2, IOWR_PARAMS))
            DEF_IO32(0x4000028, m_core->gpu2D[0].WriteBgX(2, IOWR_PARAMS))
            DEF_IO32(0x400002C, m_core->gpu2D[0].WriteBgY(2, IOWR_PARAMS))
            DEF_IO16(0x4000030, m_core->gpu2D[0].WriteBgPA(3, IOWR_PARAMS))
            DEF_IO16(0x4000032, m_core->gpu2D[0].WriteBgPB(3, IOWR_PARAMS))
            DEF_IO16(0x4000034, m_core->gpu2D[0].WriteBgPC(3, IOWR_PARAMS))
            DEF_IO16(0x4000036, m_core->gpu2D[0].WriteBgPD(3, IOWR_PARAMS))
            DEF_IO32(0x4000038, m_core->gpu2D[0].WriteBgX(3, IOWR_PARAMS))
            DEF_IO32(0x400003C, m_core->gpu2D[0].WriteBgY(3, IOWR_PARAMS))
            DEF_IO16(0x4000040, m_core->gpu2D[0].WriteWinH(0, IOWR_PARAMS))
            DEF_IO16(0x4000042, m_core->gpu2D[0].WriteWinH(1, IOWR_PARAMS))
            DEF_IO16(0x4000044, m_core->gpu2D[0].WriteWinV(0, IOWR_PARAMS))
            DEF_IO16(0x4000046, m_core->gpu2D[0].WriteWinV(1, IOWR_PARAMS))
            DEF_IO16(0x4000048, m_core->gpu2D[0].WriteWinIn(IOWR_PARAMS))
            DEF_IO16(0x400004A, m_core->gpu2D[0].WriteWinOut(IOWR_PARAMS))
            DEF_IO16(0x400004C, m_core->gpu2D[0].WriteMosaic(IOWR_PARAMS))
            DEF_IO16(0x4000050, m_core->gpu2D[0].WriteBldCnt(IOWR_PARAMS))
            DEF_IO16(0x4000052, m_core->gpu2D[0].WriteBldAlpha(IOWR_PARAMS))
            DEF_IO_8(0x4000054, m_core->gpu2D[0].WriteBldY(IOWR_PARAMS8))
            DEF_IO16(0x4000060, m_core->spu.WriteGbaSoundCntL(0, IOWR_PARAMS8))
            DEF_IO16(0x4000062, m_core->spu.WriteGbaSoundCntH(0, IOWR_PARAMS))
            DEF_IO16(0x4000064, m_core->spu.WriteGbaSoundCntX(0, IOWR_PARAMS))
            DEF_IO16(0x4000068, m_core->spu.WriteGbaSoundCntH(1, IOWR_PARAMS))
            DEF_IO16(0x400006C, m_core->spu.WriteGbaSoundCntX(1, IOWR_PARAMS))
            DEF_IO16(0x4000070, m_core->spu.WriteGbaSoundCntL(2, IOWR_PARAMS8))
            DEF_IO16(0x4000072, m_core->spu.WriteGbaSoundCntH(2, IOWR_PARAMS))
            DEF_IO16(0x4000074, m_core->spu.WriteGbaSoundCntX(2, IOWR_PARAMS))
            DEF_IO16(0x4000078, m_core->spu.WriteGbaSoundCntH(3, IOWR_PARAMS))
            DEF_IO16(0x400007C, m_core->spu.WriteGbaSoundCntX(3, IOWR_PARAMS))
            DEF_IO16(0x4000080, m_core->spu.WriteGbaMainSoundCntL(IOWR_PARAMS))
            DEF_IO16(0x4000082, m_core->spu.WriteGbaMainSoundCntH(IOWR_PARAMS))
            DEF_IO16(0x4000084, m_core->spu.WriteGbaMainSoundCntX(IOWR_PARAMS8))
            DEF_IO16(0x4000088, m_core->spu.WriteGbaSoundBias(IOWR_PARAMS))
            DEF_IO_8(0x4000090, m_core->spu.WriteGbaWaveRam(0, IOWR_PARAMS8))
            DEF_IO_8(0x4000091, m_core->spu.WriteGbaWaveRam(1, IOWR_PARAMS8))
            DEF_IO_8(0x4000092, m_core->spu.WriteGbaWaveRam(2, IOWR_PARAMS8))
            DEF_IO_8(0x4000093, m_core->spu.WriteGbaWaveRam(3, IOWR_PARAMS8))
            DEF_IO_8(0x4000094, m_core->spu.WriteGbaWaveRam(4, IOWR_PARAMS8))
            DEF_IO_8(0x4000095, m_core->spu.WriteGbaWaveRam(5, IOWR_PARAMS8))
            DEF_IO_8(0x4000096, m_core->spu.WriteGbaWaveRam(6, IOWR_PARAMS8))
            DEF_IO_8(0x4000097, m_core->spu.WriteGbaWaveRam(7, IOWR_PARAMS8))
            DEF_IO_8(0x4000098, m_core->spu.WriteGbaWaveRam(8, IOWR_PARAMS8))
            DEF_IO_8(0x4000099, m_core->spu.WriteGbaWaveRam(9, IOWR_PARAMS8))
            DEF_IO_8(0x400009A, m_core->spu.WriteGbaWaveRam(10, IOWR_PARAMS8))
            DEF_IO_8(0x400009B, m_core->spu.WriteGbaWaveRam(11, IOWR_PARAMS8))
            DEF_IO_8(0x400009C, m_core->spu.WriteGbaWaveRam(12, IOWR_PARAMS8))
            DEF_IO_8(0x400009D, m_core->spu.WriteGbaWaveRam(13, IOWR_PARAMS8))
            DEF_IO_8(0x400009E, m_core->spu.WriteGbaWaveRam(14, IOWR_PARAMS8))
            DEF_IO_8(0x400009F, m_core->spu.WriteGbaWaveRam(15, IOWR_PARAMS8))
            DEF_IO32(0x40000A0, m_core->spu.WriteGbaFifoA(IOWR_PARAMS))
            DEF_IO32(0x40000A4, m_core->spu.WriteGbaFifoB(IOWR_PARAMS))
            DEF_IO32(0x40000B0, m_core->dma[1].WriteDmaSad(0, IOWR_PARAMS))
            DEF_IO32(0x40000B4, m_core->dma[1].WriteDmaDad(0, IOWR_PARAMS))
            DEF_IO32(0x40000B8, m_core->dma[1].WriteDmaCnt(0, IOWR_PARAMS))
            DEF_IO32(0x40000BC, m_core->dma[1].WriteDmaSad(1, IOWR_PARAMS))
            DEF_IO32(0x40000C0, m_core->dma[1].WriteDmaDad(1, IOWR_PARAMS))
            DEF_IO32(0x40000C4, m_core->dma[1].WriteDmaCnt(1, IOWR_PARAMS))
            DEF_IO32(0x40000C8, m_core->dma[1].WriteDmaSad(2, IOWR_PARAMS))
            DEF_IO32(0x40000CC, m_core->dma[1].WriteDmaDad(2, IOWR_PARAMS))
            DEF_IO32(0x40000D0, m_core->dma[1].WriteDmaCnt(2, IOWR_PARAMS))
            DEF_IO32(0x40000D4, m_core->dma[1].WriteDmaSad(3, IOWR_PARAMS))
            DEF_IO32(0x40000D8, m_core->dma[1].WriteDmaDad(3, IOWR_PARAMS))
            DEF_IO32(0x40000DC, m_core->dma[1].WriteDmaCnt(3, IOWR_PARAMS))
            DEF_IO16(0x4000100, m_core->timers[1].WriteTmCntL(0, IOWR_PARAMS))
            DEF_IO16(0x4000102, m_core->timers[1].WriteTmCntH(0, IOWR_PARAMS))
            DEF_IO16(0x4000104, m_core->timers[1].WriteTmCntL(1, IOWR_PARAMS))
            DEF_IO16(0x4000106, m_core->timers[1].WriteTmCntH(1, IOWR_PARAMS))
            DEF_IO16(0x4000108, m_core->timers[1].WriteTmCntL(2, IOWR_PARAMS))
            DEF_IO16(0x400010A, m_core->timers[1].WriteTmCntH(2, IOWR_PARAMS))
            DEF_IO16(0x400010C, m_core->timers[1].WriteTmCntL(3, IOWR_PARAMS))
            DEF_IO16(0x400010E, m_core->timers[1].WriteTmCntH(3, IOWR_PARAMS))
            DEF_IO16(0x4000200, m_core->interpreter[1].WriteIe(IOWR_PARAMS))
            DEF_IO16(0x4000202, m_core->interpreter[1].WriteIrf(IOWR_PARAMS))
            DEF_IO_8(0x4000208, m_core->interpreter[1].WriteIme(IOWR_PARAMS8))
            DEF_IO_8(0x4000300, m_core->interpreter[1].WritePostFlg(IOWR_PARAMS8))
            DEF_IO_8(0x4000301, WriteGbaHaltCnt(IOWR_PARAMS8))
            DEF_IO16(0x80000C4, m_core->rtc.WriteGpData(IOWR_PARAMS))
            DEF_IO16(0x80000C6, m_core->rtc.WriteGpDirection(IOWR_PARAMS))
            DEF_IO16(0x80000C8, m_core->rtc.WriteGpControl(IOWR_PARAMS))
            default:
                if (i == 0) {
                    LOG_VERBOSE("Unknown GBA I/O register write: 0x%X\n", address);
                    return;
                }
                i++;
                continue;
        }
        i += size - base;
    }
}

void Memory::WriteDmaFill(int channel, uint32_t mask, uint32_t value) {
    m_dmaFill[channel] = (m_dmaFill[channel] & ~mask) | (value & mask);
}

void Memory::WriteVramCnt(int index, uint8_t value) {
    const uint8_t masks[] = {0x9B, 0x9B, 0x9F, 0x9F, 0x87, 0x9F, 0x9F, 0x83, 0x83};
    if ((value & masks[index]) == (m_vramCnt[index] & masks[index]))
        return;
    m_vramCnt[index] = value & masks[index];
    RebuildVram();
}

void Memory::RebuildVram() {
    std::fill(std::begin(m_engABg), std::end(m_engABg), VramMapping{});
    std::fill(std::begin(m_engBBg), std::end(m_engBBg), VramMapping{});
    std::fill(std::begin(m_engAObj), std::end(m_engAObj), VramMapping{});
    std::fill(std::begin(m_engBObj), std::end(m_engBObj), VramMapping{});
    std::fill(std::begin(m_lcdc), std::end(m_lcdc), VramMapping{});
    std::fill(std::begin(m_vram7), std::end(m_vram7), VramMapping{});
    memset(m_engAExtPal, 0, sizeof(m_engAExtPal));
    memset(m_engBExtPal, 0, sizeof(m_engBExtPal));
    memset(m_tex3D, 0, sizeof(m_tex3D));
    memset(m_pal3D, 0, sizeof(m_pal3D));
    m_vramStat = 0;
    if (m_vramCnt[0] & BIT(7)) {
        uint8_t ofs = (m_vramCnt[0] & 0x18) >> 3;
        switch (m_vramCnt[0] & 0x07) {
            case 0:
                for (int i = 0; i < 8; i++)
                    m_lcdc[i].Add(&m_vramA[i << 14]);
                break;
            case 1:
                for (int i = 0; i < 8; i++)
                    m_engABg[(ofs << 3) + i].Add(&m_vramA[i << 14]);
                break;
            case 2:
                for (int i = 0; i < 8; i++)
                    m_engAObj[(ofs << 3) + i].Add(&m_vramA[i << 14]);
                break;
            case 3:
                m_tex3D[ofs] = &m_vramA[0];
                break;
        }
    }
    if (m_vramCnt[1] & BIT(7)) {
        uint8_t ofs = (m_vramCnt[1] & 0x18) >> 3;
        switch (m_vramCnt[1] & 0x07) {
            case 0:
                for (int i = 0; i < 8; i++)
                    m_lcdc[8 + i].Add(&m_vramB[i << 14]);
                break;
            case 1:
                for (int i = 0; i < 8; i++)
                    m_engABg[(ofs << 3) + i].Add(&m_vramB[i << 14]);
                break;
            case 2:
                for (int i = 0; i < 8; i++)
                    m_engAObj[(ofs << 3) + i].Add(&m_vramB[i << 14]);
                break;
            case 3:
                m_tex3D[ofs] = &m_vramB[0];
                break;
        }
    }
    if (m_vramCnt[2] & BIT(7)) {
        uint8_t ofs = (m_vramCnt[2] & 0x18) >> 3;
        switch (m_vramCnt[2] & 0x07) {
            case 0:
                for (int i = 0; i < 8; i++)
                    m_lcdc[16 + i].Add(&m_vramC[i << 14]);
                break;
            case 1:
                for (int i = 0; i < 8; i++)
                    m_engABg[(ofs << 3) + i].Add(&m_vramC[i << 14]);
                break;
            case 2:
                m_vram7[ofs & BIT(0)].Add(&m_vramC[0]);
                m_vramStat |= BIT(0);
                break;
            case 3:
                m_tex3D[ofs] = &m_vramC[0];
                break;
            case 4:
                for (int i = 0; i < 8; i++)
                    m_engBBg[i].Add(&m_vramC[i << 14]);
                break;
        }
    }
    if (m_vramCnt[3] & BIT(7)) {
        uint8_t ofs = (m_vramCnt[3] & 0x18) >> 3;
        switch (m_vramCnt[3] & 0x07) {
            case 0:
                for (int i = 0; i < 8; i++)
                    m_lcdc[24 + i].Add(&m_vramD[i << 14]);
                break;
            case 1:
                for (int i = 0; i < 8; i++)
                    m_engABg[(ofs << 3) + i].Add(&m_vramD[i << 14]);
                break;
            case 2:
                m_vram7[ofs & BIT(0)].Add(&m_vramD[0]);
                m_vramStat |= BIT(1);
                break;
            case 3:
                m_tex3D[ofs] = &m_vramD[0];
                break;
            case 4:
                for (int i = 0; i < 8; i++)
                    m_engBObj[i].Add(&m_vramD[i << 14]);
                break;
        }
    }
    if (m_vramCnt[4] & BIT(7)) {
        switch (m_vramCnt[4] & 0x07) {
            case 0:
                for (int i = 0; i < 4; i++)
                    m_lcdc[32 + i].Add(&m_vramE[i << 14]);
                break;
            case 1:
                for (int i = 0; i < 4; i++)
                    m_engABg[i].Add(&m_vramE[i << 14]);
                break;
            case 2:
                for (int i = 0; i < 4; i++)
                    m_engAObj[i].Add(&m_vramE[i << 14]);
                break;
            case 3:
                for (int i = 0; i < 4; i++)
                    m_pal3D[i] = &m_vramE[i << 14];
                break;
            case 4:
                for (int i = 0; i < 4; i++)
                    m_engAExtPal[i] = &m_vramE[i << 13];
                break;
        }
    }
    if (m_vramCnt[5] & BIT(7)) {
        uint8_t ofs = (m_vramCnt[5] & 0x18) >> 3;
        switch (m_vramCnt[5] & 0x07) {
            case 0:
                m_lcdc[36].Add(&m_vramF[0]);
                break;
            case 1:
                for (int i = 0; i < 2; i++)
                    m_engABg[((ofs & 2) << 1) + (ofs & 1) + (i << 1)].Add(&m_vramF[0]);
                break;
            case 2:
                for (int i = 0; i < 2; i++)
                    m_engAObj[((ofs & 2) << 1) + (ofs & 1) + (i << 1)].Add(&m_vramF[0]);
                break;
            case 3:
                m_pal3D[((ofs & 2) << 1) + (ofs & 1)] = &m_vramF[0];
                break;
            case 4:
                for (int i = 0; i < 2; i++)
                    m_engAExtPal[((ofs & 1) << 1) + i] = &m_vramF[i << 13];
                break;
            case 5:
                m_engAExtPal[4] = &m_vramF[0];
                break;
        }
    }
    if (m_vramCnt[6] & BIT(7)) {
        uint8_t ofs = (m_vramCnt[6] & 0x18) >> 3;
        switch (m_vramCnt[6] & 0x07) {
            case 0:
                m_lcdc[37].Add(&m_vramG[0]);
                break;
            case 1:
                for (int i = 0; i < 2; i++)
                    m_engABg[((ofs & 2) << 1) + (ofs & 1) + (i << 1)].Add(&m_vramG[0]);
                break;
            case 2:
                for (int i = 0; i < 2; i++)
                    m_engAObj[((ofs & 2) << 1) + (ofs & 1) + (i << 1)].Add(&m_vramG[0]);
                break;
            case 3:
                m_pal3D[((ofs & 2) << 1) + (ofs & 1)] = &m_vramG[0];
                break;
            case 4:
                for (int i = 0; i < 2; i++)
                    m_engAExtPal[((ofs & 1) << 1) + i] = &m_vramG[i << 13];
                break;
            case 5:
                m_engAExtPal[4] = &m_vramG[0];
                break;
        }
    }
    if (m_vramCnt[7] & BIT(7)) {
        switch (m_vramCnt[7] & 0x07) {
            case 0:
                for (int i = 0; i < 2; i++)
                    m_lcdc[38 + i].Add(&m_vramH[i << 14]);
                break;
            case 1:
                for (int i = 0; i < 2; i++) {
                    m_engBBg[0 + i].Add(&m_vramH[i << 14]);
                    m_engBBg[4 + i].Add(&m_vramH[i << 14]);
                }
                break;
            case 2:
                for (int i = 0; i < 4; i++)
                    m_engBExtPal[i] = &m_vramH[i << 13];
                break;
        }
    }
    if (m_vramCnt[8] & BIT(7)) {
        switch (m_vramCnt[8] & 0x07) {
            case 0:
                m_lcdc[40].Add(&m_vramI[0]);
                break;
            case 1:
                for (int i = 0; i < 2; i++) {
                    m_engBBg[2 + i].Add(&m_vramI[0]);
                    m_engBBg[6 + i].Add(&m_vramI[0]);
                }
                break;
            case 2:
                for (int i = 0; i < 8; i++)
                    m_engBObj[i].Add(&m_vramI[0]);
                break;
            case 3:
                m_engBExtPal[4] = &m_vramI[0];
                break;
        }
    }
    UpdateMap9(0x06000000, 0x07000000);
    UpdateMap7(0x06000000, 0x07000000);
    m_core->gpu.Invalidate3D();
}

void Memory::WriteWramCnt(uint8_t value) {
    m_wramCnt = value & 0x03;
    UpdateMap9(0x03000000, 0x04000000);
    UpdateMap7(0x03000000, 0x04000000);
}

void Memory::WriteHaltCnt(uint8_t value) {
    m_haltCnt = value & 0xC0;
    switch (m_haltCnt >> 6) {
        case 1:
            m_core->EnterGbaMode();
            break;
        case 2:
            m_core->interpreter[1].Halt(0);
            break;
        case 3:
            LOG("Unhandled request for sleep mode\n");
            break;
    }
}

void Memory::WriteGbaHaltCnt(uint8_t value) {
    m_core->interpreter[1].Halt(0);
    if (value & BIT(7))
        LOG("Unhandled request for stop mode\n");
}

// Instanciations explicites : les chemins lents sont définis ici mais appelés
// aussi depuis d'autres unités (memory.hpp, codes Action Replay). En -O3, ce
// fichier peut intégrer tous ses propres appels et n'émettre aucune copie
// utilisable par l'éditeur de liens — d'où des références indéfinies en
// release seulement.
template uint8_t Memory::ReadFallback<uint8_t>(bool, uint32_t);
template uint16_t Memory::ReadFallback<uint16_t>(bool, uint32_t);
template uint32_t Memory::ReadFallback<uint32_t>(bool, uint32_t);
template void Memory::WriteFallback<uint8_t>(bool, uint32_t, uint8_t);
template void Memory::WriteFallback<uint16_t>(bool, uint32_t, uint16_t);
template void Memory::WriteFallback<uint32_t>(bool, uint32_t, uint32_t);

} // namespace consoles::arm

