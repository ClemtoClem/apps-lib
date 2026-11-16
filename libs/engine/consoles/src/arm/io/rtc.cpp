#include "consoles/arm/io/rtc.hpp"

#include "consoles/arm/core.hpp"

#include <ctime>

namespace consoles::arm {

void Rtc::UpdateRtc(bool cs, bool sck, bool sio) {
    if (cs) {
        if (!m_sckCur && sck) {
            if (m_writeCount < 8) {
                m_command |= sio << (7 - m_writeCount);
                if (m_writeCount == 7 && (m_command & 0xF0) != 0x60) {
                    uint8_t value = 0;
                    for (int i = 0; i < 8; i++)
                        value |= ((m_command >> i) & BIT(0)) << (7 - i);
                    m_command = value;
                }
            } else if (m_command & BIT(0)) {
                sio = ReadRegister((m_command >> 1) & 0x7);
            } else {
                WriteRegister((m_command >> 1) & 0x7, sio);
            }
            m_writeCount++;
        }
    } else {
        m_writeCount = 0;
        m_command    = 0;
    }
    m_csCur  = cs;
    m_sckCur = sck;
    m_sioCur = sio;
}

void Rtc::UpdateDateTime() {
    std::time_t t    = std::time(nullptr) + std::time_t(m_timeOffset);
    std::tm    *time = std::localtime(&t);
    time->tm_year %= 100;
    time->tm_mon++;
    if (!(m_control & BIT(m_core->IsGbaMode() ? 6 : 1)))
        time->tm_hour %= 12;
    m_dateTime[0] = static_cast<uint8_t>(((time->tm_year / 10) << 4) | (time->tm_year % 10));
    m_dateTime[1] = static_cast<uint8_t>(((time->tm_mon / 10) << 4) | (time->tm_mon % 10));
    m_dateTime[2] = static_cast<uint8_t>(((time->tm_mday / 10) << 4) | (time->tm_mday % 10));
    m_dateTime[3] = uint8_t(time->tm_wday);
    m_dateTime[4] = static_cast<uint8_t>(((time->tm_hour / 10) << 4) | (time->tm_hour % 10));
    m_dateTime[5] = static_cast<uint8_t>(((time->tm_min / 10) << 4) | (time->tm_min % 10));
    m_dateTime[6] = static_cast<uint8_t>(((time->tm_sec / 10) << 4) | (time->tm_sec % 10));
    if (time->tm_hour >= 12)
        m_dateTime[4] = static_cast<uint8_t>(m_dateTime[4] | BIT(6 << m_core->IsGbaMode()));
}

void Rtc::Reset() {
    UpdateRtc(0, 0, 0);
    m_control     = 0;
    m_rtc         = 0;
    m_gpDirection = 0;
    m_gpControl   = 0;
    ResetRegisters();
}

void Rtc::ResetRegisters() {
    m_status2      = 0;
    m_clockAdjust  = 0;
    m_freeRegister = 0;
    for (int i = 0; i < 3; i++)
        m_int1[i] = m_int2[i] = 0;
}

namespace {

[[nodiscard]] int FromBcd(uint8_t value) { return (value >> 4) * 10 + (value & 0xF); }

} // namespace

void Rtc::ApplyPendingDateTime(int first) {
    // Date courante (pour une écriture de l'heure seule), puis octets écrits.
    std::time_t now  = std::time(nullptr);
    std::time_t cur  = now + std::time_t(m_timeOffset);
    std::tm     tm   = *std::localtime(&cur);
    if (first == 0) {
        tm.tm_year = 100 + FromBcd(m_pending[0]);   // 2000-2099
        tm.tm_mon  = FromBcd(m_pending[1] & 0x1F) - 1;
        tm.tm_mday = FromBcd(m_pending[2] & 0x3F);
    }
    const bool gba  = m_core->IsGbaMode();
    const bool pm   = m_pending[4] & BIT(gba ? 7 : 6);
    int        hour = FromBcd(m_pending[4] & 0x3F);
    if (!(m_control & BIT(gba ? 6 : 1)) && pm && hour < 12) // mode 12 h
        hour += 12;
    tm.tm_hour  = hour;
    tm.tm_min   = FromBcd(m_pending[5] & 0x7F);
    tm.tm_sec   = FromBcd(m_pending[6] & 0x7F);
    tm.tm_isdst = -1;
    const std::time_t target = std::mktime(&tm);
    if (target != std::time_t(-1))
        m_timeOffset = int64_t(target) - int64_t(now);
}

namespace {

// dateTime = { année, mois, jour, jour de semaine, heure, minute, seconde }.
// Les 8 premiers bits écrits sont la commande : writeCount vaut donc 8..63
// pendant la lecture du registre date+heure (octets 0..6), et 8..31 pendant
// celle du registre heure seule (octets 4..6). L'index de l'heure seule
// était calculé `writeCount / 8 - 5`, soit -4..-2 : lecture HORS du tableau
// (relevée par UBSan) — les jeux qui lisent l'horloge recevaient des octets
// voisins au lieu de l'heure. Bornés dans les deux cas par sécurité.
[[nodiscard]] size_t DateTimeByteIndex(uint8_t writeCount) {
    size_t index = writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0;
    return index < 7 ? index : 6;
}

[[nodiscard]] size_t TimeByteIndex(uint8_t writeCount) {
    size_t index = (writeCount >= 8 ? size_t(writeCount / 8) - 1 : 0) + 4;
    return index < 7 ? index : 6;
}

} // namespace

bool Rtc::ReadRegister(uint8_t index) {
    if (m_core->IsGbaMode()) {
        switch (index) {
            case 0:
                Reset();
                return 0;
            case 1:
                return (m_control >> (m_writeCount & 7)) & BIT(0);
            case 2:
                if (m_writeCount == 8)
                    UpdateDateTime();
                return (m_dateTime[DateTimeByteIndex(m_writeCount)] >> (m_writeCount % 8)) & BIT(0);
            case 3:
                if (m_writeCount == 8)
                    UpdateDateTime();
                return (m_dateTime[TimeByteIndex(m_writeCount)] >> (m_writeCount % 8)) & BIT(0);
            default:
                LOG_VERBOSE("Read from unknown GBA RTC register: %d\n", index);
                return 0;
        }
    }
    const uint8_t bit  = m_writeCount & 7;
    const size_t  byte = m_writeCount >= 8 ? size_t(m_writeCount / 8) - 1 : 0;
    switch (index) {
        case 0:
            return (m_control >> (m_writeCount & 7)) & BIT(0);
        case 1:
            return (m_status2 >> bit) & BIT(0);
        case 4:
            return (m_int1[byte < 3 ? byte : 2] >> bit) & BIT(0);
        case 5:
            return (m_int2[byte < 3 ? byte : 2] >> bit) & BIT(0);
        case 6:
            return (m_clockAdjust >> bit) & BIT(0);
        case 7:
            return (m_freeRegister >> bit) & BIT(0);
        case 2:
            if (m_writeCount == 8)
                UpdateDateTime();
            return (m_dateTime[DateTimeByteIndex(m_writeCount)] >> (m_writeCount & 7)) & BIT(0);
        case 3:
            if (m_writeCount == 8)
                UpdateDateTime();
            return (m_dateTime[TimeByteIndex(m_writeCount)] >> (m_writeCount & 7)) & BIT(0);
        default:
            LOG_VERBOSE("Read from unknown RTC register: %d\n", index);
            return 0;
    }
}

void Rtc::WriteRegister(uint8_t index, bool value) {
    if (m_core->IsGbaMode()) {
        switch (index) {
            case 1:
                if (BIT(m_writeCount & 7) & 0x6A)
                    m_control = static_cast<uint8_t>((m_control & ~BIT(m_writeCount & 7)) | (value << (m_writeCount & 7)));
                return;
            case 2:
            case 3: {
                const int     first = index == 2 ? 0 : 4;
                const uint8_t bit   = m_writeCount & 7;
                if (m_writeCount == 8) {
                    UpdateDateTime();
                    for (int i = 0; i < 7; i++)
                        m_pending[i] = m_dateTime[i];
                }
                const size_t target = size_t(first) + (m_writeCount >= 8 ? size_t(m_writeCount / 8) - 1 : 0);
                if (target < 7)
                    m_pending[target] = uint8_t((m_pending[target] & ~BIT(bit)) | (value << bit));
                if (m_writeCount == (index == 2 ? 63 : 31))
                    ApplyPendingDateTime(first);
                return;
            }
            default:
                LOG_VERBOSE("Write to unknown GBA RTC register: %d\n", index);
                return;
        }
    }
    const uint8_t bit  = m_writeCount & 7;
    const size_t  byte = m_writeCount >= 8 ? size_t(m_writeCount / 8) - 1 : 0;
    auto set = [&](uint8_t &reg) { reg = uint8_t((reg & ~BIT(bit)) | (value << bit)); };
    switch (index) {
        case 0:
            if ((BIT(m_writeCount & 7) & 0x01) && value) {
                // Remise à zéro : registres et heure, sans couper le transfert.
                m_control = 0;
                ResetRegisters();
                m_timeOffset = 0;
            } else if (BIT(m_writeCount & 7) & 0x0E) {
                m_control = static_cast<uint8_t>((m_control & ~BIT(m_writeCount & 7)) | (value << (m_writeCount & 7)));
            }
            return;
        case 1:
            set(m_status2);
            return;
        case 2: // Date + heure : 7 octets
        case 3: // Heure seule : 3 octets
        {
            const int first = index == 2 ? 0 : 4;
            if (m_writeCount == 8) {
                UpdateDateTime();
                for (int i = 0; i < 7; i++)
                    m_pending[i] = m_dateTime[i];
            }
            const size_t target = size_t(first) + byte;
            if (target < 7)
                set(m_pending[target]);
            if (m_writeCount == (index == 2 ? 63 : 31)) // dernier bit
                ApplyPendingDateTime(first);
            return;
        }
        case 4:
            if (byte < 3)
                set(m_int1[byte]);
            return;
        case 5:
            if (byte < 3)
                set(m_int2[byte]);
            return;
        case 6:
            set(m_clockAdjust);
            return;
        case 7:
            set(m_freeRegister);
            return;
        default:
            return;
    }
}

void Rtc::WriteRtc(uint8_t value) {
    m_rtc      = static_cast<uint8_t>(value & ~0x07);
    bool cs  = (m_rtc & BIT(6)) ? (value & BIT(2)) : m_csCur;
    bool sck = (m_rtc & BIT(5)) ? !(value & BIT(1)) : m_sckCur;
    bool sio = (m_rtc & BIT(4)) ? (value & BIT(0)) : m_sioCur;
    UpdateRtc(cs, sck, sio);
}

void Rtc::WriteGpData(uint16_t mask, uint16_t value) {
    if (mask & 0xFF) {
        bool cs  = (m_gpDirection & BIT(2)) ? (value & BIT(2)) : m_csCur;
        bool sio = (m_gpDirection & BIT(1)) ? (value & BIT(1)) : m_sioCur;
        bool sck = (m_gpDirection & BIT(0)) ? (value & BIT(0)) : m_sckCur;
        UpdateRtc(cs, sck, sio);
    }
}

void Rtc::WriteGpDirection(uint16_t mask, uint16_t value) {
    mask &= 0x000F;
    m_gpDirection = (m_gpDirection & ~mask) | (value & mask);
}

void Rtc::WriteGpControl(uint16_t mask, uint16_t value) {
    if (!m_gpRtc)
        return;
    mask &= 0x0001;
    m_gpControl = (m_gpControl & ~mask) | (value & mask);
    m_core->memory.UpdateMap7(0x8000000, 0x8001000);
}

uint8_t Rtc::ReadRtc() {
    bool cs  = m_csCur;
    bool sck = (m_rtc & BIT(5)) ? 0 : m_sckCur;
    bool sio = (m_rtc & BIT(4)) ? 0 : m_sioCur;
    return m_rtc | (cs << 2) | (sck << 1) | (sio << 0);
}

uint16_t Rtc::ReadGpData() {
    bool cs  = (m_gpDirection & BIT(2)) ? 0 : m_csCur;
    bool sio = (m_gpDirection & BIT(1)) ? 0 : m_sioCur;
    bool sck = (m_gpDirection & BIT(0)) ? 0 : m_sckCur;
    return (cs << 2) | (sio << 1) | (sck << 0);
}

} // namespace consoles::arm
