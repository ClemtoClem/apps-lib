#pragma once
#include <memory>
#include <cstdint>
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
class Cp15 {
  private:
    Core *m_core;
    uint32_t m_ctrlReg         = 0x00000078;
    uint32_t m_dtcmReg         = 0x00000000;
    uint32_t m_itcmReg         = 0x00000000;
    uint32_t m_exceptionAddr   = 0;
    bool     m_dtcmReadEnabled = false, m_dtcmWriteEnabled = false;
    bool     m_itcmReadEnabled = false, m_itcmWriteEnabled = false;
    uint32_t m_dtcmAddr = 0, m_dtcmSize = 0;
    uint32_t m_itcmSize = 0;

  public:
    Cp15(Core *core_) : m_core(core_) {}

    uint32_t Read(int cn, int cm, int cp);
    void     Write(int cn, int cm, int cp, uint32_t value);

    void IoState(StateArchive &archive) {
        archive.Io(m_ctrlReg);
        archive.Io(m_dtcmReg);
        archive.Io(m_itcmReg);
        archive.Io(m_exceptionAddr);
        archive.Io(m_dtcmReadEnabled);
        archive.Io(m_dtcmWriteEnabled);
        archive.Io(m_itcmReadEnabled);
        archive.Io(m_itcmWriteEnabled);
        archive.Io(m_dtcmAddr);
        archive.Io(m_dtcmSize);
        archive.Io(m_itcmSize);
    }

    uint32_t GetExceptionAddr() {
        return m_exceptionAddr;
    }

    bool GetDtcmReadEnabled() {
        return m_dtcmReadEnabled;
    }

    bool GetDtcmWriteEnabled() {
        return m_dtcmWriteEnabled;
    }

    bool GetItcmReadEnabled() {
        return m_itcmReadEnabled;
    }

    bool GetItcmWriteEnabled() {
        return m_itcmWriteEnabled;
    }

    uint32_t GetDtcmAddr() {
        return m_dtcmAddr;
    }

    uint32_t GetDtcmSize() {
        return m_dtcmSize;
    }

    uint32_t GetItcmSize() {
        return m_itcmSize;
    }
};

} // namespace consoles::arm

