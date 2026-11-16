#include "consoles/arm/io/wifi.hpp"

#include "consoles/arm/core.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace consoles::arm {

#define MS_CYCLES 34418


Wifi::Wifi(Core *core_) : m_core(core_) {
    m_bbRegisters[0x00] = 0x6D;
    m_bbRegisters[0x5D] = 0x01;
    m_bbRegisters[0x64] = 0xFF;
    m_countMsTask       = std::bind(&Wifi::CountMs, this);
}

void Wifi::ScheduleInit() {
    m_core->Schedule(Task(&m_countMsTask, MS_CYCLES));
    m_scheduled = true;
}

void Wifi::AddConnection(Core *peer) {
    m_mutex.lock();
    m_connections.push_back(&peer->wifi);
    m_mutex.unlock();
    peer->wifi.m_mutex.lock();
    peer->wifi.m_connections.push_back(this);
    peer->wifi.m_mutex.unlock();
}

void Wifi::RemConnection(Core *peer) {
    m_mutex.lock();
    auto position = std::find(m_connections.begin(), m_connections.end(), &peer->wifi);
    m_connections.erase(position);
    m_mutex.unlock();
    peer->wifi.m_mutex.lock();
    position = std::find(peer->wifi.m_connections.begin(), peer->wifi.m_connections.end(), this);
    peer->wifi.m_connections.erase(position);
    peer->wifi.m_mutex.unlock();
}

void Wifi::SendInterrupt(int bit) {
    if (!(m_wIe & m_wIrf) && (m_wIe & BIT(bit)))
        m_core->interpreter[1].SendInterrupt(24);
    m_wIrf |= BIT(bit);
    if (bit == 14) {
        m_wPostBeacon = 0xFFFF;
        m_wTxreqRead &= 0xFFF2;
    }
}

void Wifi::CountMs() {
    if (!m_packets.empty())
        ProcessPackets();
    if (m_wUsCountcnt) {
        if (m_wBeaconCount == m_wPreBeacon)
            SendInterrupt(15);
        if (--m_wBeaconCount == 0) {
            if ((m_wTxbufLoc[4] & BIT(15)) && (m_wTxreqRead & BIT(4)))
                Transfer(4);
            m_wBeaconCount = m_wBeaconInt;
            if (m_wUsComparecnt)
                SendInterrupt(14);
        }
        if (m_wPostBeacon && --m_wPostBeacon == 0)
            SendInterrupt(13);
    }
    if (!m_connections.empty() || m_wUsCountcnt)
        m_core->Schedule(Task(&m_countMsTask, MS_CYCLES));
    else
        m_scheduled = false;
}

void Wifi::ProcessPackets() {
    m_mutex.lock();
    for (size_t i = 0; i < m_packets.size(); i++) {
        uint16_t size = static_cast<uint16_t>((m_packets[i][4] + 12) / 2);
        for (size_t j = 0; j < size; j++) {
            m_core->memory.Write<uint16_t>(1, 0x4804000 + m_wRxbufWrcsr, m_packets[i][j]);
            m_wRxbufWrcsr += 2;
            if ((m_wRxbufBegin & 0x1FFE) != (m_wRxbufEnd & 0x1FFE))
                m_wRxbufWrcsr = static_cast<uint16_t>((m_wRxbufBegin & 0x1FFE) +
                              (m_wRxbufWrcsr - (m_wRxbufBegin & 0x1FFE)) % ((m_wRxbufEnd & 0x1FFE) - (m_wRxbufBegin & 0x1FFE)));
            m_wRxbufWrcsr &= 0x1FFE;
        }
        SendInterrupt(0);
    }
    m_packets.clear();
    m_mutex.unlock();
}

void Wifi::Transfer(int index) {
    uint16_t address = (m_wTxbufLoc[index] & 0xFFF) << 1;
    uint16_t size    = m_core->memory.Read<uint16_t>(1, 0x4804000 + address + 0x0A) + 8;
    LOG("Sending packet on channel %d with size 0x%X\n", index, size);

    // Read the packet once; reused both for local (same-process) connections
    // and for the optional network bridge below.
    std::vector<uint16_t> packet(size / 2);
    for (size_t j = 0; j < size; j += 2)
        packet[j / 2] = m_core->memory.Read<uint16_t>(1, static_cast<uint32_t>(0x4804000 + address + j));
    packet[4] = size - 12;

    m_mutex.lock();
    for (size_t i = 0; i < m_connections.size(); i++) {
        m_connections[i]->m_mutex.lock();
        m_connections[i]->m_packets.push_back(packet);
        m_connections[i]->m_mutex.unlock();
    }
    m_mutex.unlock();

    if (onPacketSent)
        onPacketSent(reinterpret_cast<const uint8_t *>(packet.data()), size);

    if (index != 4)
        m_wTxbufLoc[index] = static_cast<uint16_t>(m_wTxbufLoc[index] & ~BIT(15));
    SendInterrupt(1);
}

void Wifi::InjectPacket(const uint8_t *data, size_t len) {
    std::vector<uint16_t> copy(len / 2);
    std::memcpy(copy.data(), data, len - (len % 2));
    m_mutex.lock();
    m_packets.push_back(std::move(copy));
    m_mutex.unlock();
}

void Wifi::WriteWModeWep(uint16_t mask, uint16_t value) {
    m_wModeWep = (m_wModeWep & ~mask) | (value & mask);
}

void Wifi::WriteWIrf(uint16_t mask, uint16_t value) {
    m_wIrf &= ~(value & mask);
}

void Wifi::WriteWIe(uint16_t mask, uint16_t value) {
    if (!(m_wIe & m_wIrf) && (value & mask & m_wIrf))
        m_core->interpreter[1].SendInterrupt(24);
    mask &= 0xFBFF;
    m_wIe = (m_wIe & ~mask) | (value & mask);
}

void Wifi::WriteWMacaddr(int index, uint16_t mask, uint16_t value) {
    m_wMacaddr[index] = (m_wMacaddr[index] & ~mask) | (value & mask);
}

void Wifi::WriteWBssid(int index, uint16_t mask, uint16_t value) {
    m_wBssid[index] = (m_wBssid[index] & ~mask) | (value & mask);
}

void Wifi::WriteWAidFull(uint16_t mask, uint16_t value) {
    mask &= 0x07FF;
    m_wAidFull = (m_wAidFull & ~mask) | (value & mask);
}

void Wifi::WriteWRxcnt(uint16_t mask, uint16_t value) {
    mask &= 0xFF0E;
    m_wRxcnt = (m_wRxcnt & ~mask) | (value & mask);
    if (value & BIT(0))
        m_wRxbufWrcsr = m_wRxbufWrAddr << 1;
}

void Wifi::WriteWPowerstate(uint16_t mask, uint16_t value) {
    mask &= 0x0003;
    m_wPowerstate = (m_wPowerstate & ~mask) | (value & mask);
    if (m_wPowerstate & BIT(1))
        m_wPowerstate = static_cast<uint16_t>(m_wPowerstate & ~BIT(9));
}

void Wifi::WriteWPowerforce(uint16_t mask, uint16_t value) {
    mask &= 0x8001;
    m_wPowerforce = (m_wPowerforce & ~mask) | (value & mask);
    if (m_wPowerforce & BIT(15))
        m_wPowerstate = static_cast<uint16_t>((m_wPowerstate & ~BIT(9)) | ((m_wPowerforce & BIT(0)) << 9));
}

void Wifi::WriteWRxbufBegin(uint16_t mask, uint16_t value) {
    m_wRxbufBegin = (m_wRxbufBegin & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufEnd(uint16_t mask, uint16_t value) {
    m_wRxbufEnd = (m_wRxbufEnd & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufWrAddr(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    m_wRxbufWrAddr = (m_wRxbufWrAddr & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufRdAddr(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    m_wRxbufRdAddr = (m_wRxbufRdAddr & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufReadcsr(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    m_wRxbufReadcsr = (m_wRxbufReadcsr & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufGap(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    m_wRxbufGap = (m_wRxbufGap & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufGapdisp(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    m_wRxbufGapdisp = (m_wRxbufGapdisp & ~mask) | (value & mask);
}

void Wifi::WriteWRxbufCount(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    m_wRxbufCount = (m_wRxbufCount & ~mask) | (value & mask);
}

void Wifi::WriteWTxbufWrAddr(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    m_wTxbufWrAddr = (m_wTxbufWrAddr & ~mask) | (value & mask);
}

void Wifi::WriteWTxbufCount(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    m_wTxbufCount = (m_wTxbufCount & ~mask) | (value & mask);
}

void Wifi::WriteWTxbufWrData(uint16_t mask, uint16_t value) {
    m_core->memory.Write<uint16_t>(1, 0x4804000 + m_wTxbufWrAddr, value & mask);
    m_wTxbufWrAddr += 2;
    if (m_wTxbufWrAddr == m_wTxbufGap)
        m_wTxbufWrAddr = static_cast<uint16_t>(m_wTxbufWrAddr + (m_wTxbufGapdisp << 1));
    m_wTxbufWrAddr &= 0x1FFF;
    if (m_wTxbufCount > 0 && --m_wTxbufCount == 0)
        SendInterrupt(8);
}

void Wifi::WriteWTxbufGap(uint16_t mask, uint16_t value) {
    mask &= 0x1FFE;
    m_wTxbufGap = (m_wTxbufGap & ~mask) | (value & mask);
}

void Wifi::WriteWTxbufGapdisp(uint16_t mask, uint16_t value) {
    mask &= 0x0FFF;
    m_wTxbufGapdisp = (m_wTxbufGapdisp & ~mask) | (value & mask);
}

void Wifi::WriteWTxbufLoc(int index, uint16_t mask, uint16_t value) {
    m_wTxbufLoc[index] = (m_wTxbufLoc[index] & ~mask) | (value & mask);
    if (index != 4 && (m_wTxbufLoc[index] & BIT(15)) && (m_wTxreqRead & BIT(index)))
        Transfer(index);
}

void Wifi::WriteWBeaconInt(uint16_t mask, uint16_t value) {
    mask &= 0x03FF;
    m_wBeaconInt   = (m_wBeaconInt & ~mask) | (value & mask);
    m_wBeaconCount = m_wBeaconInt;
}

void Wifi::WriteWTxreqReset(uint16_t mask, uint16_t value) {
    mask &= 0x000F;
    m_wTxreqRead &= ~(value & mask);
}

void Wifi::WriteWTxreqSet(uint16_t mask, uint16_t value) {
    mask &= 0x000F;
    m_wTxreqRead |= (value & mask);
    for (int i = 0; i < 4; i++) {
        if ((m_wTxbufLoc[i] & BIT(15)) && (m_wTxreqRead & BIT(i)))
            Transfer(i);
    }
}

void Wifi::WriteWUsCountcnt(uint16_t mask, uint16_t value) {
    mask &= 0x0001;
    m_wUsCountcnt = (m_wUsCountcnt & ~mask) | (value & mask);
}

void Wifi::WriteWUsComparecnt(uint16_t mask, uint16_t value) {
    mask &= 0x0001;
    m_wUsComparecnt = (m_wUsComparecnt & ~mask) | (value & mask);
    if (value & BIT(1))
        SendInterrupt(14);
}

void Wifi::WriteWPreBeacon(uint16_t mask, uint16_t value) {
    m_wPreBeacon = (m_wPreBeacon & ~mask) | (value & mask);
}

void Wifi::WriteWBeaconCount(uint16_t mask, uint16_t value) {
    m_wBeaconCount = (m_wBeaconCount & ~mask) | (value & mask);
}

void Wifi::WriteWConfig(int index, uint16_t mask, uint16_t value) {
    const uint16_t masks[] = {0x81FF, 0xFFFF, 0xFFFF, 0xFFFF, 0x0FFF, 0x8FFF, 0xFFFF, 0xFFFF,
                              0x00FF, 0x00FF, 0x00FF, 0x00FF, 0xFFFF, 0xFF3F, 0x7A7F};
    mask &= masks[index];
    m_wConfig[index] = (m_wConfig[index] & ~mask) | (value & mask);
}

void Wifi::WriteWPostBeacon(uint16_t mask, uint16_t value) {
    m_wPostBeacon = (m_wPostBeacon & ~mask) | (value & mask);
}

void Wifi::WriteWBbCnt(uint16_t /*mask*/, uint16_t value) {
    int index = value & 0x00FF;
    switch ((value & 0xF000) >> 12) {
        case 5: {
            if ((index >= 0x01 && index <= 0x0C) || (index >= 0x13 && index <= 0x15) ||
                (index >= 0x1B && index <= 0x26) || (index >= 0x28 && index <= 0x4C) ||
                (index >= 0x4E && index <= 0x5C) || (index >= 0x62 && index <= 0x63) || (index == 0x65) ||
                (index >= 0x67 && index <= 0x68))
                m_bbRegisters[index] = static_cast<uint8_t>(m_wBbWrite);
            break;
        }
        case 6: {
            m_wBbRead = m_bbRegisters[index];
            break;
        }
    }
}

void Wifi::WriteWBbWrite(uint16_t mask, uint16_t value) {
    m_wBbWrite = (m_wBbWrite & ~mask) | (value & mask);
}

void Wifi::WriteWIrfSet(uint16_t mask, uint16_t value) {
    if (!(m_wIe & m_wIrf) && (m_wIe & value & mask))
        m_core->interpreter[1].SendInterrupt(24);
    mask &= 0xFBFF;
    m_wIrf |= (value & mask);
}

uint16_t Wifi::ReadWRxbufRdData() {
    uint16_t value = m_core->memory.Read<uint16_t>(1, 0x4804000 + m_wRxbufRdAddr);
    if ((m_wRxbufRdAddr += 2) == m_wRxbufGap)
        m_wRxbufRdAddr = static_cast<uint16_t>(m_wRxbufRdAddr + (m_wRxbufGapdisp << 1));
    if ((m_wRxbufBegin & 0x1FFE) != (m_wRxbufEnd & 0x1FFE))
        m_wRxbufRdAddr = static_cast<uint16_t>((m_wRxbufBegin & 0x1FFE) +
                       (m_wRxbufRdAddr - (m_wRxbufBegin & 0x1FFE)) % ((m_wRxbufEnd & 0x1FFE) - (m_wRxbufBegin & 0x1FFE)));
    m_wRxbufRdAddr &= 0x1FFF;
    if (m_wRxbufCount > 0 && --m_wRxbufCount == 0)
        SendInterrupt(9);
    return value;
}

} // namespace consoles::arm

