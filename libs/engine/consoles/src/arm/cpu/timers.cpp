#include "consoles/arm/cpu/timers.hpp"
#include "consoles/arm/core.hpp"

namespace consoles::arm {


Timers::Timers(Core *core_, bool cpu_) : m_core(core_), m_cpu(cpu_) {
    for (int i = 0; i < 4; i++)
        m_overflowTask[i] = std::bind(&Timers::Overflow, this, i);
}

void Timers::ResetCycles() {
    for (int i = 0; i < 4; i++)
        m_endCycles[i] -= m_core->GetGlobalCycles();
}

void Timers::Overflow(int timer) {
    if (!(m_tmCntH[timer] & BIT(7)) ||
        ((timer == 0 || !(m_tmCntH[timer] & BIT(2))) && m_endCycles[timer] != m_core->GetGlobalCycles()))
        return;
    m_timers[timer] = m_tmCntL[timer];
    if (timer == 0 || !(m_tmCntH[timer] & BIT(2))) {
        m_core->Schedule(Task(&m_overflowTask[timer], static_cast<uint32_t>((0x10000 - m_timers[timer]) << m_shifts[timer])));
        m_endCycles[timer] = m_core->GetGlobalCycles() + static_cast<uint32_t>(((0x10000 - m_timers[timer]) << m_shifts[timer]));
    }
    if (m_tmCntH[timer] & BIT(6))
        m_core->interpreter[m_cpu].SendInterrupt(3 + timer);
    if (m_core->IsGbaMode() && timer < 2)
        m_core->spu.GbaFifoTimer(timer);
    if (timer < 3 && (m_tmCntH[timer + 1] & BIT(2)) && ++m_timers[timer + 1] == 0)
        Overflow(timer + 1);
}

void Timers::WriteTmCntL(int timer, uint16_t mask, uint16_t value) {
    m_tmCntL[timer] = (m_tmCntL[timer] & ~mask) | (value & mask);
}

void Timers::WriteTmCntH(int timer, uint16_t mask, uint16_t value) {
    bool dirty = false;
    if ((m_tmCntH[timer] & BIT(7)) && (timer == 0 || !(value & BIT(2))))
        m_timers[timer] = static_cast<uint16_t>(0x10000 - ((m_endCycles[timer] - m_core->GetGlobalCycles()) >> m_shifts[timer]));
    if (mask & 0x00FF) {
        int shift = (((value & 0x0003) && (timer == 0 || !(value & BIT(2)))) ? (4 + (value & 0x0003) * 2) : 0);
        if (m_shifts[timer] != shift) {
            m_shifts[timer] = static_cast<uint8_t>(shift);
            dirty         = true;
        }
    }
    if (!(m_tmCntH[timer] & BIT(7)) && (value & BIT(7))) {
        m_timers[timer] = m_tmCntL[timer];
        dirty         = true;
    }
    mask &= 0x00C7;
    m_tmCntH[timer] = (m_tmCntH[timer] & ~mask) | (value & mask);
    if (dirty && (m_tmCntH[timer] & BIT(7)) && (timer == 0 || !(m_tmCntH[timer] & BIT(2)))) {
        m_core->Schedule(Task(&m_overflowTask[timer], static_cast<uint32_t>((0x10000 - m_timers[timer]) << m_shifts[timer])));
        m_endCycles[timer] = m_core->GetGlobalCycles() + static_cast<uint32_t>(((0x10000 - m_timers[timer]) << m_shifts[timer]));
    }
}

uint16_t Timers::ReadTmCntL(int timer) {
    if ((m_tmCntH[timer] & BIT(7)) && (timer == 0 || !(m_tmCntH[timer] & BIT(2))))
        m_timers[timer] = static_cast<uint16_t>(0x10000 - ((m_endCycles[timer] - m_core->GetGlobalCycles()) >> m_shifts[timer]));
    return m_timers[timer];
}

void Timers::Rearm() {
    for (int i = 0; i < 4; i++) {
        if (!(m_tmCntH[i] & BIT(7)) || (i != 0 && (m_tmCntH[i] & BIT(2))))
            continue;
        uint32_t cycles = (m_endCycles[i] > m_core->GetGlobalCycles()) ? (m_endCycles[i] - m_core->GetGlobalCycles()) : 1;
        m_core->Schedule(Task(&m_overflowTask[i], cycles));
    }
}

} // namespace consoles::arm

