#include "consoles/arm/io/ipc.hpp"

#include "consoles/arm/core.hpp"
#include "consoles/arm/io/rtc.hpp"

namespace consoles::arm {


void Ipc::WriteIpcSync(bool cpu, uint16_t mask, uint16_t value) {
    mask &= 0x4F00;
    m_ipcSync[cpu] = (m_ipcSync[cpu] & ~mask) | (value & mask);

    // Override for HLE ARM7 (see hle_arm7.hpp) if enabled: the real ARM7 is
    // permanently halted in this mode, so it will never see the normal
    // cross-copy/interrupt below — redirect straight to the HLE handler.
    if (m_core->arm7Hle && !cpu) {
        m_core->hleArm7.IpcSync(uint8_t((m_ipcSync[cpu] >> 8) & 0xF));
        return;
    }

    m_ipcSync[!cpu] = (m_ipcSync[!cpu] & ~((mask >> 8) & 0x000F)) | (((value & mask) >> 8) & 0x000F);
    if ((value & BIT(13)) && (m_ipcSync[!cpu] & BIT(14)))
        m_core->interpreter[!cpu].SendInterrupt(16);
}

void Ipc::WriteIpcFifoCnt(bool cpu, uint16_t mask, uint16_t value) {
    if ((value & BIT(3)) && !m_fifos[cpu].empty()) {
        while (!m_fifos[cpu].empty())
            m_fifos[cpu].pop();
        m_ipcFifoRecv[!cpu] = 0;
        m_ipcFifoCnt[cpu] |= BIT(0);
        m_ipcFifoCnt[cpu] = static_cast<uint16_t>(m_ipcFifoCnt[cpu] & ~BIT(1));
        m_ipcFifoCnt[!cpu] |= BIT(8);
        m_ipcFifoCnt[!cpu] = static_cast<uint16_t>(m_ipcFifoCnt[!cpu] & ~BIT(9));
        if (m_ipcFifoCnt[cpu] & BIT(2))
            m_core->interpreter[cpu].SendInterrupt(17);
    }
    if ((m_ipcFifoCnt[cpu] & BIT(0)) && !(m_ipcFifoCnt[cpu] & BIT(2)) && (value & BIT(2)))
        m_core->interpreter[cpu].SendInterrupt(17);
    if (!(m_ipcFifoCnt[cpu] & BIT(8)) && !(m_ipcFifoCnt[cpu] & BIT(10)) && (value & BIT(10)))
        m_core->interpreter[cpu].SendInterrupt(18);
    if (value & BIT(14))
        m_ipcFifoCnt[cpu] = static_cast<uint16_t>(m_ipcFifoCnt[cpu] & ~BIT(14));
    mask &= 0x8404;
    m_ipcFifoCnt[cpu] = (m_ipcFifoCnt[cpu] & ~mask) | (value & mask);
}

void Ipc::WriteIpcFifoSend(bool cpu, uint32_t mask, uint32_t value) {
    if (m_ipcFifoCnt[cpu] & BIT(15)) {
        if (m_fifos[cpu].size() < 16) {
            // Override for HLE ARM7 if enabled — see WriteIpcSync() above.
            if (m_core->arm7Hle && !cpu) {
                m_core->hleArm7.IpcFifo(value & mask);
                return;
            }
            m_fifos[cpu].push(value & mask);
            if (m_fifos[cpu].size() == 1) {
                m_ipcFifoCnt[cpu] = static_cast<uint16_t>(m_ipcFifoCnt[cpu] & ~BIT(0));
                m_ipcFifoCnt[!cpu] = static_cast<uint16_t>(m_ipcFifoCnt[!cpu] & ~BIT(8));
                if (m_ipcFifoCnt[!cpu] & BIT(10))
                    m_core->interpreter[!cpu].SendInterrupt(18);
            } else if (m_fifos[cpu].size() == 16) {
                m_ipcFifoCnt[cpu] |= BIT(1);
                m_ipcFifoCnt[!cpu] |= BIT(9);
            }
        } else {
            m_ipcFifoCnt[cpu] |= BIT(14);
        }
    }
}

uint32_t Ipc::ReadIpcFifoRecv(bool cpu) {
    if (!m_fifos[!cpu].empty()) {
        m_ipcFifoRecv[cpu] = m_fifos[!cpu].front();
        if (m_ipcFifoCnt[cpu] & BIT(15)) {
            m_fifos[!cpu].pop();
            if (m_fifos[!cpu].empty()) {
                m_ipcFifoCnt[cpu] |= BIT(8);
                m_ipcFifoCnt[!cpu] |= BIT(0);
                if (m_ipcFifoCnt[!cpu] & BIT(2))
                    m_core->interpreter[!cpu].SendInterrupt(17);
            } else if (m_fifos[!cpu].size() == 15) {
                m_ipcFifoCnt[cpu] = static_cast<uint16_t>(m_ipcFifoCnt[cpu] & ~BIT(9));
                m_ipcFifoCnt[!cpu] = static_cast<uint16_t>(m_ipcFifoCnt[!cpu] & ~BIT(1));
            }
        }
    } else {
        m_ipcFifoCnt[cpu] |= BIT(14);
    }
    return m_ipcFifoRecv[cpu];
}

} // namespace consoles::arm

