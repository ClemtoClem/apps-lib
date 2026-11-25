#include "consoles/arm/io/spi.hpp"

#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"
#include "consoles/arm/file.hpp"

#include <cstring>

namespace consoles::arm {

Spi::~Spi() = default;

uint16_t Spi::Crc16(uint32_t value, uint8_t *data, size_t size) {
    static const uint16_t table[] = {0xC0C1, 0xC181, 0xC301, 0xC601, 0xCC01, 0xD801, 0xF001, 0xA001};
    for (size_t i = 0; i < size; i++) {
        value ^= data[i];
        for (size_t j = 0; j < 8; j++)
            value = (value >> 1) ^ ((value & 1) ? (table[j] << (7 - j)) : 0);
    }
    return static_cast<uint16_t>(value);
}

bool Spi::LoadFirmware() {
    if (auto file = File::Open(m_core->settings.firmwarePath, "rb")) {
        m_firmwareData = file->ReadAll();
        m_firmSize = m_firmwareData.size();
        m_firmware = m_firmwareData.data();
        if (m_core->GetId() > 0) {
            m_firmware[0x36] = static_cast<uint8_t>(m_firmware[0x36] + m_core->GetId());
            uint16_t crc   = Crc16(0, &m_firmware[0x2C], 0x138);
            m_firmware[0x2A] = static_cast<uint8_t>(crc >> 0);
            m_firmware[0x2B] = static_cast<uint8_t>(crc >> 8);
        }
        RepairTouchCalibration();
        return (m_firmSize > 0x20000);
    }
    m_firmSize = 0x20000;
    m_firmwareData.assign(m_firmSize, 0);
    m_firmware = m_firmwareData.data();
    m_firmware[0x20] = 0xC0;
    m_firmware[0x21] = 0x3F;
    m_firmware[0x2C] = 0x38;
    m_firmware[0x2D] = 0x01;
    m_firmware[0x36] = static_cast<uint8_t>(m_core->GetId());
    m_firmware[0x37] = 0x09;
    m_firmware[0x38] = 0xBF;
    m_firmware[0x39] = 0x12;
    m_firmware[0x3A] = 0x34;
    m_firmware[0x3B] = 0x56;
    m_firmware[0x3C] = 0xFE;
    m_firmware[0x3D] = 0x3F;
    uint16_t crc   = Crc16(0, &m_firmware[0x2C], 0x138);
    m_firmware[0x2A] = static_cast<uint8_t>(crc >> 0);
    m_firmware[0x2B] = static_cast<uint8_t>(crc >> 8);
    for (uint32_t addr = 0x1FA00; addr <= 0x1FC00; addr += 0x100) {
        m_firmware[addr + 0xE7] = 0xFF;
        m_firmware[addr + 0xF5] = 0x28;
        crc                   = Crc16(0, &m_firmware[addr], 0xFE);
        m_firmware[addr + 0xFE] = static_cast<uint8_t>(crc >> 0);
        m_firmware[addr + 0xFF] = static_cast<uint8_t>(crc >> 8);
    }
    for (uint32_t addr = 0x1FE00; addr <= 0x1FF00; addr += 0x100) {
        m_firmware[addr + 0x00] = 5;
        m_firmware[addr + 0x02] = 2;
        m_firmware[addr + 0x03] = 5;
        m_firmware[addr + 0x04] = 25;
        m_firmware[addr + 0x06] = 'N';
        m_firmware[addr + 0x08] = 'o';
        m_firmware[addr + 0x0A] = 'o';
        m_firmware[addr + 0x0C] = 'D';
        m_firmware[addr + 0x0E] = 'S';
        m_firmware[addr + 0x1A] = 5;
        m_firmware[addr + 0x5E] = 0xF0;
        m_firmware[addr + 0x5F] = 0x0F;
        m_firmware[addr + 0x60] = 0xF0;
        m_firmware[addr + 0x61] = 0x0B;
        m_firmware[addr + 0x62] = 0xFF;
        m_firmware[addr + 0x63] = 0xBF;
        crc                   = Crc16(0xFFFF, &m_firmware[addr], 0x70);
        m_firmware[addr + 0x72] = static_cast<uint8_t>(crc >> 0);
        m_firmware[addr + 0x73] = static_cast<uint8_t>(crc >> 8);
    }
    return false;
}

// Étalonnage de l'écran tactile des réglages utilisateur (0x58-0x63) : un
// firmware dont les points sont confondus (dump vierge, tout à zéro) rend le
// tactile inutilisable — setTouch ne convertit plus rien et le jeu, qui lit
// les mêmes octets, divise par zéro. On y met alors l'étalonnage du firmware
// généré (CAN 0..0xFF0 ↔ 0..255 px), CRC de chaque copie recalculé.
bool Spi::RepairTouchCalibration() {
    if (!m_firmware || m_firmSize < 0x200)
        return false;
    bool repaired = false;
    for (size_t base = m_firmSize - 0x200; base < m_firmSize; base += 0x100) {
        uint8_t *settings = &m_firmware[base];
        const uint16_t adcX1 = U8TO16(settings, 0x58), adcY1 = U8TO16(settings, 0x5A);
        const uint16_t adcX2 = U8TO16(settings, 0x5E), adcY2 = U8TO16(settings, 0x60);
        if (adcX1 != adcX2 && adcY1 != adcY2 && settings[0x5C] != settings[0x62] && settings[0x5D] != settings[0x63])
            continue;
        static constexpr uint8_t defaultCalibration[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                            0xF0, 0x0F, 0xF0, 0x0B, 0xFF, 0xBF};
        memcpy(&settings[0x58], defaultCalibration, sizeof defaultCalibration);
        const uint16_t crc = Crc16(0xFFFF, settings, 0x70);
        settings[0x72]     = static_cast<uint8_t>(crc >> 0);
        settings[0x73]     = static_cast<uint8_t>(crc >> 8);
        repaired           = true;
    }
    if (repaired)
        LOG("Firmware : étalonnage tactile absent, étalonnage par défaut appliqué\n");
    return repaired;
}

void Spi::DirectBoot() {
    for (uint32_t i = 0; i < 0x70; i++)
        m_core->memory.Write<uint8_t>(0, 0x27FFC80 + i, m_firmware[m_firmSize - 0x100 + i]);
}

void Spi::SetTouch(int x, int y) {
    if (!m_firmware)
        return;
    uint16_t adcX1 = U8TO16(m_firmware, m_firmSize - 0xA8);
    uint16_t adcY1 = U8TO16(m_firmware, m_firmSize - 0xA6);
    uint8_t  scrX1 = m_firmware[m_firmSize - 0xA4];
    uint8_t  scrY1 = m_firmware[m_firmSize - 0xA3];
    uint16_t adcX2 = U8TO16(m_firmware, m_firmSize - 0xA2);
    uint16_t adcY2 = U8TO16(m_firmware, m_firmSize - 0xA0);
    uint8_t  scrX2 = m_firmware[m_firmSize - 0x9E];
    uint8_t  scrY2 = m_firmware[m_firmSize - 0x9D];
    if (x < 1)
        x = 1;
    else if (x > 254)
        x = 254;
    if (y < 1)
        y = 1;
    else if (y > 190)
        y = 190;
    if (scrX2 - scrX1 != 0)
        m_touchX = static_cast<uint16_t>((x - (scrX1 - 1)) * (adcX2 - adcX1) / (scrX2 - scrX1) + adcX1);
    if (scrY2 - scrY1 != 0)
        m_touchY = static_cast<uint16_t>((y - (scrY1 - 1)) * (adcY2 - adcY1) / (scrY2 - scrY1) + adcY1);
}

void Spi::ClearTouch() {
    m_touchX = 0x000;
    m_touchY = 0xFFF;
}

void Spi::SendMicData(const int16_t *samples, size_t count, size_t rate) {
    const std::scoped_lock lock(m_mutex);
    m_micData.assign(samples, samples + count);
    m_micBuffer = m_micData.data();
    m_micBufSize = count;
    m_micCycles  = m_core->GetGlobalCycles();
    m_micStep    = static_cast<uint32_t>((60 * 263 * 355 * 6) / rate);

}

void Spi::WriteSpiCnt(uint16_t mask, uint16_t value) {
    mask &= 0xCF03;
    m_spiCnt = (m_spiCnt & ~mask) | (value & mask);
}

void Spi::WriteSpiData(uint8_t value) {
    if (!(m_spiCnt & BIT(15))) {
        m_spiData = 0;
        return;
    }
    if (m_writeCount == 0) {
        m_command = value;
        m_address = 0;
        m_spiData = 0;
        if (((m_spiCnt & 0x0300) >> 8) == 1) {
            // Commandes de la flash du firmware sans octet de données.
            switch (m_command) {
                case 0x06: // WREN : autorise la prochaine écriture
                    m_firmStatus |= BIT(1);
                    break;
                case 0x04: // WRDI
                    m_firmStatus = static_cast<uint8_t>(m_firmStatus & ~BIT(1));
                    break;
                case 0x05: // RDSR : l'état est renvoyé dès le premier octet lu
                    m_spiData = m_firmStatus;
                    break;
            }
        }
    } else {
        switch ((m_spiCnt & 0x0300) >> 8) {
            case 1: {
                // Flash du firmware (ST M45PE20, 256 Kio) : adresse sur 3 octets
                // après la commande, puis données. Les écritures ne sont
                // acceptées qu'après WREN (WEL), qu'elles consomment.
                const bool addressPhase = m_writeCount < 4;
                if (addressPhase && m_command != 0x05 && m_command != 0x9F) {
                    m_address |= value << ((3 - m_writeCount) * 8);
                    m_spiData = 0;
                    break;
                }
                const bool writable = m_firmStatus & BIT(1);
                switch (m_command) {
                    case 0x03: // READ
                        m_spiData = (m_address < m_firmSize) ? m_firmware[m_address] : 0;
                        m_address += (m_spiCnt & BIT(10)) ? 2 : 1;
                        break;
                    case 0x0B: // FAST READ : un octet factice après l'adresse
                        if (m_writeCount == 4) {
                            m_spiData = 0;
                        } else {
                            m_spiData = (m_address < m_firmSize) ? m_firmware[m_address] : 0;
                            m_address++;
                        }
                        break;
                    case 0x05: // RDSR
                        m_spiData = m_firmStatus;
                        break;
                    case 0x9F: { // RDID (JEDEC) : ST, M45PE, 2 Mbit
                        static const uint8_t id[3] = {0x20, 0x40, 0x12};
                        m_spiData = id[(m_writeCount - 1) % 3];
                        break;
                    }
                    case 0x0A: // PW : écriture de page (remplace les octets)
                    case 0x02: // PP : programmation de page (ne fait que passer des bits à 0)
                        if (writable && m_address < m_firmSize) {
                            m_firmware[m_address] = (m_command == 0x0A) ? value : uint8_t(m_firmware[m_address] & value);
                            // Reste dans la page de 256 octets, comme la puce.
                            m_address = (m_address & ~0xFFu) | ((m_address + 1) & 0xFFu);
                        }
                        m_spiData = 0;
                        break;
                    default:
                        LOG_VERBOSE("Write to SPI with unknown firmware command: 0x%X\n", command);
                        m_spiData = 0;
                        break;
                }
                break;
            }
            case 2: {
                switch ((m_command & 0x70) >> 4) {
                    case 1:
                        m_spiData = static_cast<uint8_t>((m_writeCount & 1) ? (m_touchY >> 5) : (m_touchY << 3));
                        break;
                    case 5:
                        m_spiData = static_cast<uint8_t>((m_writeCount & 1) ? (m_touchX >> 5) : (m_touchX << 3));
                        break;
                    case 6:
                        if (m_writeCount & 1 && m_micStep) {
                            m_mutex.lock();
                            size_t index =
                                std::min<size_t>((m_core->GetGlobalCycles() - m_micCycles) / m_micStep, m_micBufSize);
                            m_micSample = static_cast<uint16_t>((m_micBufSize > 0) ? ((m_micBuffer[index] >> 4) + 0x800) : 0);
                            m_mutex.unlock();
                            m_spiData = static_cast<uint8_t>(m_micSample >> 5);
                            break;
                        }
                        m_spiData = static_cast<uint8_t>(m_micSample << 3);
                        break;
                    default:
                        LOG_VERBOSE("Write to SPI with unknown touchscreen channel: %d\n", (command & 0x70) >> 4);
                        m_spiData = 0;
                        break;
                }
                break;
            }
            default:
                LOG_VERBOSE("Write to SPI with unknown device: %d\n", (spiCnt & 0x0300) >> 8);
                m_spiData = 0;
                break;
        }
    }
    if (m_spiCnt & BIT(11)) {
        m_writeCount++;
    } else {
        // Fin du transfert (CS relâché) : les effacements s'exécutent une fois
        // l'adresse complète reçue, et toute écriture consomme WEL.
        if (((m_spiCnt & 0x0300) >> 8) == 1 && m_writeCount >= 3) {
            if ((m_command == 0xDB || m_command == 0xD8) && (m_firmStatus & BIT(1))) {
                const uint32_t span  = (m_command == 0xDB) ? 0x100 : 0x10000; // page / secteur
                const uint32_t start = m_address & ~(span - 1);
                for (uint32_t a = start; a < start + span && a < m_firmSize; a++)
                    m_firmware[a] = 0xFF;
            }
            if (m_command == 0x0A || m_command == 0x02 || m_command == 0xDB || m_command == 0xD8)
                m_firmStatus = static_cast<uint8_t>(m_firmStatus & ~BIT(1));
        }
        m_writeCount = 0;
    }
    if (m_spiCnt & BIT(14))
        m_core->interpreter[1].SendInterrupt(23);
}

} // namespace consoles::arm

