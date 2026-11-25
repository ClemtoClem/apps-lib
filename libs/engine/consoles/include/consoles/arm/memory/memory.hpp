
#pragma once
#include <cstdint>
#include <memory>
#include "consoles/arm/defines.hpp"
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
class VramMapping {
  public:
    void                       Add(uint8_t *mapping);
    template <typename T> T    Read(uint32_t address);
    template <typename T> void Write(uint32_t address, T value);
    uint8_t                   *GetBaseMapping() {
        return m_mappings[0];
    }
    int GetCount() {
        return m_count;
    }

  private:
    uint8_t *m_mappings[7];
    int      m_count = 0;
};



class Memory {
  public:
    Memory(Core *core_) : m_core(core_){};
    bool                       LoadNdsBios9();
    bool                       LoadNdsBios7();
    bool                       LoadGbaBios();
    void                       UpdateMap9(uint32_t start, uint32_t end);
    void                       UpdateMap7(uint32_t start, uint32_t end);
    template <typename T> T    Read(bool cpu, uint32_t address);
    template <typename T> void Write(bool cpu, uint32_t address, T value);
    uint8_t                   *GetPalette() {
        return m_palette;
    }
    uint8_t *GetOam() {
        return m_oam;
    }
    uint8_t **GetEngAExtPal() {
        return m_engAExtPal;
    }
    uint8_t **GetEngBExtPal() {
        return m_engBExtPal;
    }
    uint8_t **GetTex3D() {
        return m_tex3D;
    }
    uint8_t **GetPal3D() {
        return m_pal3D;
    }

    /**
     * @brief Sérialise/désérialise la RAM, la VRAM et les registres pour les save states.
     *
     * Les BIOS (ndsBios9/ndsBios7/gbaBios) ne sont pas sérialisés : ils sont
     * rechargés depuis le disque une seule fois, avant tout chargement d'état
     * possible. Les tables `readMap9/7`/`writeMap9/7` (caches de pointeurs) ne
     * sont pas sérialisées non plus : elles doivent être reconstruites après
     * un chargement via UpdateMap9()/UpdateMap7() et RebuildVram(), ce que
     * fait Core::IoState() une fois tous les composants restaurés.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_ram);
        archive.Io(m_wram);
        archive.Io(m_instrTcm);
        archive.Io(m_dataTcm);
        archive.Io(m_wram7);
        archive.Io(m_wifiRam);
        archive.Io(m_palette);
        archive.Io(m_vramA);
        archive.Io(m_vramB);
        archive.Io(m_vramC);
        archive.Io(m_vramD);
        archive.Io(m_vramE);
        archive.Io(m_vramF);
        archive.Io(m_vramG);
        archive.Io(m_vramH);
        archive.Io(m_vramI);
        archive.Io(m_oam);
        archive.Io(m_dmaFill);
        archive.Io(m_vramCnt);
        archive.Io(m_vramStat);
        archive.Io(m_wramCnt);
        archive.Io(m_haltCnt);
    }

    /**
     * @brief Reconstruit tous les mappings VRAM (banques, palettes étendues,
     * textures/palettes 3D) à partir des registres vramCnt[] déjà restaurés.
     *
     * Appelé par Core::IoState() après le chargement des registres bruts,
     * ce qui reproduit exactement ce que produirait une réécriture normale
     * de chaque registre VRAMCNT.
     */
    void RebuildVram();

  private:
    Core *m_core;
    uint8_t *m_readMap9[0x100000]  = {};
    uint8_t *m_readMap7[0x100000]  = {};
    uint8_t *m_writeMap9[0x100000] = {};
    uint8_t *m_writeMap7[0x100000] = {};
    uint8_t  m_ndsBios9[0x8000]    = {};
    uint8_t  m_ndsBios7[0x4000]    = {};
    uint8_t  m_gbaBios[0x4000]     = {};
    uint8_t  m_ram[0x400000]       = {};
    uint8_t  m_wram[0x8000]        = {};
    uint8_t  m_instrTcm[0x8000]    = {};
    uint8_t  m_dataTcm[0x4000]     = {};
    uint8_t  m_wram7[0x10000]      = {};
    uint8_t  m_wifiRam[0x2000]     = {};
    uint8_t  m_palette[0x800]      = {};
    uint8_t  m_vramA[0x20000]      = {};
    uint8_t  m_vramB[0x20000]      = {};
    uint8_t  m_vramC[0x20000]      = {};
    uint8_t  m_vramD[0x20000]      = {};
    uint8_t  m_vramE[0x10000]      = {};
    uint8_t  m_vramF[0x4000]       = {};
    uint8_t  m_vramG[0x4000]       = {};
    uint8_t  m_vramH[0x8000]       = {};
    uint8_t  m_vramI[0x4000]       = {};
    uint8_t  m_oam[0x800]          = {};

    VramMapping m_engABg[32];
    VramMapping m_engBBg[8];
    VramMapping m_engAObj[16];
    VramMapping m_engBObj[8];
    VramMapping m_lcdc[64];
    VramMapping m_vram7[2];

    uint8_t *m_engAExtPal[5] = {};
    uint8_t *m_engBExtPal[5] = {};
    uint8_t *m_tex3D[4]      = {};
    uint8_t *m_pal3D[6]      = {};
    uint8_t *m_lastGbaBios   = nullptr;
    uint32_t m_dmaFill[4]    = {};
    uint8_t  m_vramCnt[9]    = {};
    uint8_t  m_vramStat      = 0;
    uint8_t  m_wramCnt       = 0;
    uint8_t  m_haltCnt       = 0;

    template <typename T> T    ReadFallback(bool cpu, uint32_t address);
    template <typename T> void WriteFallback(bool cpu, uint32_t address, T value);
    template <typename T> T    IoRead9(uint32_t address);
    template <typename T> T    IoRead7(uint32_t address);
    template <typename T> T    IoReadGba(uint32_t address);
    template <typename T> void IoWrite9(uint32_t address, T value);
    template <typename T> void IoWrite7(uint32_t address, T value);
    template <typename T> void IoWriteGba(uint32_t address, T value);

    uint32_t ReadDmaFill(int channel) {
        return m_dmaFill[channel];
    }
    uint8_t ReadVramCnt(int block) {
        return m_vramCnt[block];
    }
    uint8_t ReadVramStat() {
        return m_vramStat;
    }
    uint8_t ReadWramCnt() {
        return m_wramCnt;
    }
    uint8_t ReadHaltCnt() {
        return m_haltCnt;
    }

    void WriteDmaFill(int channel, uint32_t mask, uint32_t value);
    void WriteVramCnt(int index, uint8_t value);
    void WriteWramCnt(uint8_t value);
    void WriteHaltCnt(uint8_t value);
    void WriteGbaHaltCnt(uint8_t value);
};


template uint8_t                     Memory::Read(bool cpu, uint32_t address);
template uint16_t                    Memory::Read(bool cpu, uint32_t address);
template uint32_t                    Memory::Read(bool cpu, uint32_t address);
template <typename T> FORCE_INLINE T Memory::Read(bool cpu, uint32_t address) {
    address = static_cast<uint32_t>(address & (~(sizeof(T) - 1)));
    uint8_t **readMap = (cpu == 0) ? m_readMap9 : m_readMap7;
    if (readMap[address >> 12]) {
        uint8_t *data  = &readMap[address >> 12][address & 0xFFF];
        T        value = 0;
        for (size_t i = 0; i < sizeof(T); i++)
            value |= data[i] << (i * 8);
        return value;
    }
    return ReadFallback<T>(cpu, address);
}


template void Memory::Write(bool cpu, uint32_t address, uint8_t value);
template void Memory::Write(bool cpu, uint32_t address, uint16_t value);
template void Memory::Write(bool cpu, uint32_t address, uint32_t value);
template <typename T> FORCE_INLINE void Memory::Write(bool cpu, uint32_t address, T value) {
    address = static_cast<uint32_t>(address & (~(sizeof(T) - 1)));
    uint8_t **writeMap = (cpu == 0) ? m_writeMap9 : m_writeMap7;
    if (writeMap[address >> 12]) {
        uint8_t *data = &writeMap[address >> 12][address & 0xFFF];
        for (size_t i = 0; i < sizeof(T); i++)
            data[i] = static_cast<uint8_t>(value >> (i * 8));
        return;
    }
    return WriteFallback<T>(cpu, address, value);
}

} // namespace consoles::arm

