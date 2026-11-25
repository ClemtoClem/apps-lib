#include "consoles/arm/memory/dma.hpp"

#include "consoles/arm/core.hpp"

namespace consoles::arm {


Dma::Dma(Core *core_, bool cpu_) : m_core(core_), m_cpu(cpu_) {
    for (int i = 0; i < 4; i++)
        m_transferTask[i] = std::bind(&Dma::Transfer, this, i);
}

void Dma::Transfer(int channel) {
    int dstAddrCnt  = (m_dmaCnt[channel] & 0x00600000) >> 21;
    int srcAddrCnt  = (m_dmaCnt[channel] & 0x01800000) >> 23;
    int mode        = (m_dmaCnt[channel] & 0x38000000) >> 27;
    int gxFifoCount = 0;
    if (m_core->IsGbaMode() && mode == 6 && (channel == 1 || channel == 2)) {
        for (unsigned int i = 0; i < 4; i++) {
            m_core->memory.Write<uint32_t>(m_cpu, m_dstAddrs[channel], m_core->memory.Read<uint32_t>(m_cpu, m_srcAddrs[channel]));
            if (srcAddrCnt == 0)
                m_srcAddrs[channel] += 4;
            else if (srcAddrCnt == 1)
                m_srcAddrs[channel] -= 4;
        }
    } else if (m_dmaCnt[channel] & BIT(26)) {
        for (unsigned int i = 0; i < m_wordCounts[channel]; i++) {
            m_core->memory.Write<uint32_t>(m_cpu, m_dstAddrs[channel], m_core->memory.Read<uint32_t>(m_cpu, m_srcAddrs[channel]));
            if (srcAddrCnt == 0)
                m_srcAddrs[channel] += 4;
            else if (srcAddrCnt == 1)
                m_srcAddrs[channel] -= 4;
            if (dstAddrCnt == 0 || dstAddrCnt == 3)
                m_dstAddrs[channel] += 4;
            else if (dstAddrCnt == 1)
                m_dstAddrs[channel] -= 4;
            if (mode == 7 && ++gxFifoCount == 112)
                break;
        }
    } else {
        for (unsigned int i = 0; i < m_wordCounts[channel]; i++) {
            m_core->memory.Write<uint16_t>(m_cpu, m_dstAddrs[channel], m_core->memory.Read<uint16_t>(m_cpu, m_srcAddrs[channel]));
            if (srcAddrCnt == 0)
                m_srcAddrs[channel] += 2;
            else if (srcAddrCnt == 1)
                m_srcAddrs[channel] -= 2;
            if (dstAddrCnt == 0 || dstAddrCnt == 3)
                m_dstAddrs[channel] += 2;
            else if (dstAddrCnt == 1)
                m_dstAddrs[channel] -= 2;
            if (mode == 7 && ++gxFifoCount == 112)
                break;
        }
    }
    if (mode == 7) {
        m_wordCounts[channel] -= static_cast<uint32_t>(gxFifoCount);
        if (m_wordCounts[channel] > 0) {
            if (m_core->gpu3D.ReadGxStat() & BIT(25))
                m_core->Schedule(Task(&m_transferTask[channel], 1));
            return;
        }
    }
    if ((m_dmaCnt[channel] & BIT(25)) && mode != 0) {
        m_wordCounts[channel] = m_dmaCnt[channel] & 0x001FFFFF;
        if (dstAddrCnt == 3)
            m_dstAddrs[channel] = m_dmaDad[channel];
        if (mode == 7 && m_core->gpu3D.ReadGxStat() & BIT(25))
            m_core->Schedule(Task(&m_transferTask[channel], 1));
    } else {
        m_dmaCnt[channel] &= ~BIT(31);
    }
    if (m_dmaCnt[channel] & BIT(30))
        m_core->interpreter[m_cpu].SendInterrupt(8 + channel);
}

void Dma::Trigger(int mode, uint8_t channels) {
    if (m_cpu == 1)
        mode <<= 1;
    for (int i = 0; i < 4; i++) {
        if ((channels & BIT(i)) && (m_dmaCnt[i] & BIT(31)) && ((m_dmaCnt[i] & 0x38000000) >> 27) == uint32_t(mode))
            m_core->Schedule(Task(&m_transferTask[i], 1));
    }
}

void Dma::WriteDmaSad(int channel, uint32_t mask, uint32_t value) {
    mask &= ((m_cpu == 0 || channel != 0) ? 0x0FFFFFFF : 0x07FFFFFF);
    m_dmaSad[channel] = (m_dmaSad[channel] & ~mask) | (value & mask);
}

void Dma::WriteDmaDad(int channel, uint32_t mask, uint32_t value) {
    mask &= ((m_cpu == 0 || channel == 3) ? 0x0FFFFFFF : 0x07FFFFFF);
    m_dmaDad[channel] = (m_dmaDad[channel] & ~mask) | (value & mask);
}

void Dma::WriteDmaCnt(int channel, uint32_t mask, uint32_t value) {
    uint32_t old = m_dmaCnt[channel];
    mask &= ((m_cpu == 0) ? 0xFFFFFFFF : (channel == 3 ? 0xF7E0FFFF : 0xF7E03FFF));
    m_dmaCnt[channel] = (m_dmaCnt[channel] & ~mask) | (value & mask);
    if ((m_dmaCnt[channel] & BIT(31)) && ((m_dmaCnt[channel] & 0x38000000) >> 27) == 7 &&
        (m_core->gpu3D.ReadGxStat() & BIT(25)))
        m_core->Schedule(Task(&m_transferTask[channel], 1));
    if ((old & BIT(31)) || !(m_dmaCnt[channel] & BIT(31)))
        return;
    m_dstAddrs[channel]   = m_dmaDad[channel];
    m_srcAddrs[channel]   = m_dmaSad[channel];
    m_wordCounts[channel] = m_dmaCnt[channel] & 0x001FFFFF;
    if (((m_dmaCnt[channel] & 0x38000000) >> 27) == 0)
        m_core->Schedule(Task(&m_transferTask[channel], 1));
}

uint32_t Dma::ReadDmaCnt(int channel) {
    return m_dmaCnt[channel] & ~(m_core->IsGbaMode() ? 0x0000FFFFu : 0x00000000u);
}

void Dma::Rearm() {
    for (int i = 0; i < 4; i++) {
        if (!(m_dmaCnt[i] & BIT(31)))
            continue;
        int mode = (m_dmaCnt[i] & 0x38000000) >> 27;
        if (mode == 7 && m_wordCounts[i] > 0 && (m_core->gpu3D.ReadGxStat() & BIT(25)))
            m_core->Schedule(Task(&m_transferTask[i], 1));
    }
}

} // namespace consoles::arm

