#include "consoles/arm/cpu/cp15.hpp"
#include "consoles/arm/core.hpp"

namespace consoles::arm {


uint32_t Cp15::Read(int cn, int cm, int cp) {
    switch ((cn << 16) | (cm << 8) | (cp << 0)) {
        case 0x000000:
            return 0x41059461;
        case 0x000001:
            return 0x0F0D2112;
        case 0x010000:
            return m_ctrlReg;
        case 0x090100:
            return m_dtcmReg;
        case 0x090101:
            return m_itcmReg;
        default: {
            LOG_VERBOSE("Unknown CP15 register read: C%d,C%d,%d\n", cn, cm, cp);
            return 0;
        }
    }
}

void Cp15::Write(int cn, int cm, int cp, uint32_t value) {
    switch ((cn << 16) | (cm << 8) | (cp << 0)) {
        case 0x010000: {
            m_ctrlReg          = (m_ctrlReg & ~0x000FF085u) | (value & 0x000FF085);
            m_exceptionAddr    = (m_ctrlReg & BIT(13)) ? 0xFFFF0000 : 0x00000000;
            m_dtcmReadEnabled  = (m_ctrlReg & BIT(16)) && !(m_ctrlReg & BIT(17));
            m_dtcmWriteEnabled = (m_ctrlReg & BIT(16));
            m_itcmReadEnabled  = (m_ctrlReg & BIT(18)) && !(m_ctrlReg & BIT(19));
            m_itcmWriteEnabled = (m_ctrlReg & BIT(18));
            m_core->memory.UpdateMap9(m_dtcmAddr, m_dtcmAddr + m_dtcmSize);
            m_core->memory.UpdateMap9(0x00000000, m_itcmSize);
            return;
        }
        case 0x090100: {
            uint32_t dtcmAddrOld = m_dtcmAddr;
            uint32_t dtcmSizeOld = m_dtcmSize;
            m_dtcmReg              = value;
            m_dtcmAddr             = m_dtcmReg & 0xFFFFF000;
            m_dtcmSize             = 0x200 << ((m_dtcmReg & 0x0000003E) >> 1);
            if (m_dtcmSize < 0x1000)
                m_dtcmSize = 0x1000;
            m_core->memory.UpdateMap9(dtcmAddrOld, dtcmAddrOld + dtcmSizeOld);
            m_core->memory.UpdateMap9(m_dtcmAddr, m_dtcmAddr + m_dtcmSize);
            return;
        }
        case 0x070004:
        case 0x070802: {
            m_core->interpreter[0].Halt(0);
            return;
        }
        case 0x090101: {
            uint32_t itcmSizeOld = m_itcmSize;
            m_itcmReg              = value;
            m_itcmSize             = 0x200 << ((m_itcmReg & 0x0000003E) >> 1);
            if (m_itcmSize < 0x1000)
                m_itcmSize = 0x1000;
            m_core->memory.UpdateMap9(0x00000000, std::max(itcmSizeOld, m_itcmSize));
            return;
        }
        default: {
            LOG_VERBOSE("Unknown CP15 register write: C%d,C%d,%d\n", cn, cm, cp);
            return;
        }
    }
}

} // namespace consoles::arm

