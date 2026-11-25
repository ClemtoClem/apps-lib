
#pragma once
#include <cstdint>
#include <memory>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
class Bios {
  public:
    Bios(Core *core_, int (Bios::**swiTable_)(bool, uint32_t **)) : core(core_), m_swiTable(swiTable_) {
    }
    int  Execute(uint8_t vector, bool cpu, uint32_t **registers);
    void CheckWaitFlags(bool cpu);
    bool ShouldCheck() {
        return m_waitFlags;
    }
    int SwiWaitByLoop(bool cpu, uint32_t **registers);
    int SwiInterruptWait(bool cpu, uint32_t **registers);
    int SwiVBlankIntrWait(bool cpu, uint32_t **registers);
    int SwiHalt(bool cpu, uint32_t **registers);
    int SwiDivide(bool cpu, uint32_t **registers);
    int SwiCpuSet(bool cpu, uint32_t **registers);
    int SwiCpuFastSet(bool cpu, uint32_t **registers);
    int SwiSquareRoot(bool cpu, uint32_t **registers);
    int SwiGetCrc16(bool cpu, uint32_t **registers);
    int SwiIsDebugger(bool cpu, uint32_t **registers);
    int SwiBitUnpack(bool cpu, uint32_t **registers);
    int SwiLz77Uncomp(bool cpu, uint32_t **registers);
    int SwiRunlenUncomp(bool cpu, uint32_t **registers);
    int SwiUnknown(bool cpu, uint32_t **registers);

    /**
     * @brief Sérialise/désérialise l'état du BIOS (masque d'attente des SWI) pour les save states.
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        archive.Io(m_waitFlags);
    }

  protected:
    Core *core;

  private:
    int (Bios::**m_swiTable)(bool, uint32_t **);
    uint32_t m_waitFlags = 0;
};



class NdsBios9 : public Bios {
  public:
    NdsBios9(Core *core_) : Bios(core_, (int(Bios::**)(bool, uint32_t **))m_swiTable9) {
    }

  private:
    static int (NdsBios9::*m_swiTable9[0x21])(bool, uint32_t **);
    int SwiDiffUnfilt8(bool cpu, uint32_t **registers);
    int SwiDiffUnfilt16(bool cpu, uint32_t **registers);
};



class NdsBios7 : public Bios {
  public:
    NdsBios7(Core *core_) : Bios(core_, (int(Bios::**)(bool, uint32_t **))m_swiTable7) {
    }

  private:
    static int (NdsBios7::*m_swiTable7[0x21])(bool, uint32_t **);
    int SwiSleep(bool cpu, uint32_t **registers);
    int SwiSoundBias(bool cpu, uint32_t **registers);
    int SwiGetSineTable(bool cpu, uint32_t **registers);
    int SwiGetPitchTable(bool cpu, uint32_t **registers);
    int SwiGetVolumeTable(bool cpu, uint32_t **registers);
};

} // namespace consoles::arm

