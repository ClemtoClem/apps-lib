#include "consoles/arm/memory/cartridge.hpp"

#include "consoles/arm/core.hpp"
#include "consoles/arm/settings.hpp"
#include "consoles/arm/memory/save_database.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace consoles::arm {

Cartridge::~Cartridge() { WriteSave(); }

bool Cartridge::LoadRom(const std::string &path) {
    m_romName = path;
    if (path.empty()) { // image en mémoire (Core::romImage)
        if (core->romImage.empty())
            return false;
        romSize = int(core->romImage.size());
    } else {
        auto file = File::Open(m_romName, "rb");
        if (!file)
            return false;
        romFile = std::move(*file);
        romSize = int(romFile.GetSize());
    }
    m_saveName = core->settings.saveBasePath.empty() ? std::string() : core->settings.saveBasePath + ".sav";
    if (auto saveFile = File::Open(m_saveName, "rb")) {
        saveData = saveFile->ReadAll();
        saveSize = int(saveData.size());
        save = saveData.empty() ? nullptr : saveData.data();
    }
    return true;
}

bool Cartridge::LoadRomSection(size_t offset, size_t size) {
    if (!core->romImage.empty() && offset == 0 && size == core->romImage.size()) {
        romData = std::move(core->romImage); // image entière en mémoire : aucune copie
        core->romImage.clear();
    } else if (!core->romImage.empty()) {
        romData.assign(size, 0);
        const size_t available = offset < core->romImage.size() ? std::min(size, core->romImage.size() - offset) : 0;
        std::copy_n(core->romImage.begin() + std::ptrdiff_t(offset), available, romData.begin());
    } else {
        romData.assign(size, 0);
        romFile.Seek(int64_t(offset));
        (void)romFile.Read(romData.data(), size);
    }
    rom = romData.data();
    // Harmless no-op for GBA ROMs / homebrew without a DLDI header (patchRom
    // just scans for the magic signature and returns if it isn't found).
    core->dldi.PatchRom(rom, uint32_t(offset), uint32_t(size));
    return true;
}

void Cartridge::WriteSave() {
    const std::scoped_lock lock(mutex);
    if (!saveDirty || m_saveName.empty() || saveSize <= 0)
        return;
    if (auto file = File::Open(m_saveName, "wb")) {
        file->Write(save, size_t(saveSize));
        saveDirty = false;
    }
}

void Cartridge::ResizeSave(int newSize, bool dirty) {
    // newSize <= 0 (e.g. the "no save chip detected yet" sentinel -1) isn't a
    // real size: just drop any existing buffer.
    const std::scoped_lock lock(mutex);
    if (newSize <= 0) {
        saveData.clear();
        save = nullptr;
    } else {
        saveData.resize(size_t(newSize), 0xFF); // une mémoire agrandie est vierge (0xFF)
        save = saveData.data();
    }
    saveSize = newSize;
    if (dirty)
        saveDirty = true;
}

void Cartridge::IoState(StateArchive &archive) {
    uint32_t nameLen = uint32_t(m_romName.size());
    archive.Io(nameLen);
    if (archive.saving) {
        if (nameLen)
            archive.Io(const_cast<char *>(m_romName.c_str()), nameLen);
    } else {
        // std::string has no public resize()/mutable data() (assign() is
        // private) — read the raw bytes into a scratch buffer first, then
        // rebuild the std::string from it via the (char*, len) constructor.
        std::vector<char> buf(nameLen);
        if (nameLen)
            archive.Io(buf.data(), nameLen);
        m_romName = std::string(buf.data(), size_t(nameLen));
        if (archive.failed)
            m_romName.clear();
    }

    // Le contenu de la ROM n'est pas dans l'archive : on la recharge depuis le
    // disque (LoadRom() est virtuelle, elle appelle donc la bonne version
    // dérivée) avant de restaurer les champs qui suivent, afin qu'ils
    // écrasent les valeurs par défaut calculées par le rechargement. On libère
    // d'abord `save` : LoadRom() peut le réallouer sans le libérer lui-même
    // s'il trouve un fichier de sauvegarde sur le disque.
    if (!archive.saving && !m_romName.empty()) {
        saveData.clear();
        save     = nullptr;
        saveSize = 0;
        LoadRom(m_romName);
    }

    archive.Io(romSize);

    int oldSaveSize = saveSize;
    archive.Io(saveSize);
    archive.Io(saveDirty);

    if (!archive.saving && saveSize != oldSaveSize)
        ResizeSave(saveSize, false);
    if (saveSize > 0)
        archive.Io(save, size_t(saveSize));
}

CartridgeNds::CartridgeNds(Core *core_) : Cartridge(core_) {
    m_wordReadyTasks[0] = std::bind(&CartridgeNds::WordReady, this, 0);
    m_wordReadyTasks[1] = std::bind(&CartridgeNds::WordReady, this, 1);
}

bool CartridgeNds::LoadRom(const std::string& path) {
    bool res = Cartridge::LoadRom(path);

    // Action Replay cheats live in a `.cht` file next to the ROM (same
    // basename convention as the `.sav` save file above).
    core->actionReplay.SetPath(core->settings.saveBasePath.empty() ? std::string() : core->settings.saveBasePath + ".cht");
    core->actionReplay.LoadCheats();

    if (romSize <= 0x20000000) {
        if (LoadRomSection(0, size_t(romSize)))
            romFile.Reset();
        else
            LoadRomSection(0, 0x5000);
    } else {
        LoadRomSection(0, 0x5000);
    }
    romMask = 1;
    while (romMask < uint32_t(romSize)) { romMask <<= 1; }
    romMask -= 1;
    m_romCode = U8TO32(rom, 0x0C);
    if (romSize >= 0x8000) {
        uint64_t data = U8TO64(rom, 0x4000);
        InitKeycode(2);
        data = Decrypt64(data);
        InitKeycode(3);
        data = Decrypt64(data);
        if (data == 0x6A624F7972636E65) {
            LOG("Detected an encrypted ROM!\n");
            m_romEncrypted = true;
        }
    }
    if (!save) {
        // No .sav file on disk yet: try the known-game save database before
        // falling back to WriteAuxSpiData()'s runtime SPI-command sniffing,
        // which can only ever distinguish 3 of the ~9 real save chip sizes
        // (see save_database.hpp for why). Recognized commercial games get
        // the correct size immediately; everything else (homebrew, obscure
        // ROMs) behaves exactly as before (saveSize stays -1).
        int knownSize = LookupSaveSize(m_romCode);
        if (knownSize >= 0) {
            LOG("Save database: detected save size 0x%X for game code 0x%08X\n", knownSize, m_romCode);
            ResizeSave(knownSize, false);
        } else {
            saveSize = -1;
        }
    }
    return res;
}

void CartridgeNds::DirectBoot() {
    if (romFile)
        LoadRomSection(0, 0x170);

    uint32_t offset9    = U8TO32(rom, 0x20);
    uint32_t entryAddr9 = U8TO32(rom, 0x24);
    uint32_t ramAddr9   = U8TO32(rom, 0x28);
    uint32_t size9      = U8TO32(rom, 0x2C);

    LOG("ARM9 code ROM offset:    0x%X\n", offset9);
    LOG("ARM9 code entry address: 0x%X\n", entryAddr9);
    LOG("ARM9 RAM address:        0x%X\n", ramAddr9);
    LOG("ARM9 code size:          0x%X\n", size9);

    uint32_t offset7    = U8TO32(rom, 0x30);
    uint32_t entryAddr7 = U8TO32(rom, 0x34);
    uint32_t ramAddr7   = U8TO32(rom, 0x38);
    uint32_t size7      = U8TO32(rom, 0x3C);

    LOG("ARM7 code ROM offset:    0x%X\n", offset7);
    LOG("ARM7 code entry address: 0x%X\n", entryAddr7);
    LOG("ARM7 RAM address:        0x%X\n", ramAddr7);
    LOG("ARM7 code size:          0x%X\n", size7);

    for (uint32_t i = 0; i < 0x170; i += 4)
        core->memory.Write<uint32_t>(0, 0x27FFE00 + i, U8TO32(rom, i));

    uint32_t offset;
    if (romFile) {
        LoadRomSection(offset9, size9);
        offset = 0;
    } else {
        offset = offset9;
    }

    for (uint32_t i = 0; i < size9; i += 4) {
        if (m_romEncrypted && offset9 + i >= 0x4000 && offset9 + i < 0x4800) {
            if (offset9 + i < 0x4008) {
                core->memory.Write<uint32_t>(0, ramAddr9 + i, 0xE7FFDEFF);
            } else {
                InitKeycode(3);
                uint64_t data = Decrypt64(U8TO64(rom, (offset + i) & ~7u));
                core->memory.Write<uint32_t>(0, ramAddr9 + i, static_cast<unsigned int>(data >> (((offset + i) & 4) ? 32 : 0)));
            }
        } else {
            core->memory.Write<uint32_t>(0, ramAddr9 + i, U8TO32(rom, offset + i));
        }
    }

    if (romFile) {
        LoadRomSection(offset7, size7);
        offset = 0;
    } else {
        offset = offset7;
    }

    for (uint32_t i = 0; i < size7; i += 4) {
        if (m_romEncrypted && offset7 + i >= 0x4000 && offset7 + i < 0x4800) {
            if (offset7 + i < 0x4008) {
                core->memory.Write<uint32_t>(1, ramAddr7 + i, 0xE7FFDEFF);
            } else {
                InitKeycode(3);
                uint64_t data = Decrypt64(U8TO64(rom, (offset + i) & ~7u));
                core->memory.Write<uint32_t>(1, ramAddr7 + i, static_cast<unsigned int>(data >> (((offset + i) & 4) ? 32 : 0)));
            }
        } else {
            core->memory.Write<uint32_t>(1, ramAddr7 + i, U8TO32(rom, offset + i));
        }
    }
}

uint64_t CartridgeNds::Encrypt64(uint64_t value) {
    uint32_t y = static_cast<uint32_t>(value);
    uint32_t x = static_cast<uint32_t>(value >> 32);
    for (int i = 0x00; i <= 0x0F; i++) {
        uint32_t z = m_encTable[i] ^ x;
        x          = m_encTable[0x012 + ((z >> 24) & 0xFF)];
        x          = m_encTable[0x112 + ((z >> 16) & 0xFF)] + x;
        x          = m_encTable[0x212 + ((z >> 8) & 0xFF)] ^ x;
        x          = m_encTable[0x312 + ((z >> 0) & 0xFF)] + x;
        x ^= y;
        y = z;
    }
    return ((uint64_t)(y ^ m_encTable[0x11]) << 32) | (x ^ m_encTable[0x10]);
}

uint64_t CartridgeNds::Decrypt64(uint64_t value) {
    uint32_t y = static_cast<uint32_t>(value);
    uint32_t x = static_cast<uint32_t>(value >> 32);
    for (int i = 0x11; i >= 0x02; i--) {
        uint32_t z = m_encTable[i] ^ x;
        x          = m_encTable[0x012 + ((z >> 24) & 0xFF)];
        x          = m_encTable[0x112 + ((z >> 16) & 0xFF)] + x;
        x          = m_encTable[0x212 + ((z >> 8) & 0xFF)] ^ x;
        x          = m_encTable[0x312 + ((z >> 0) & 0xFF)] + x;
        x ^= y;
        y = z;
    }
    return ((uint64_t)(y ^ m_encTable[0x00]) << 32) | (x ^ m_encTable[0x01]);
}

void CartridgeNds::InitKeycode(int level) {
    for (int i = 0; i < 0x412; i++)
        m_encTable[i] = core->memory.Read<uint32_t>(1, static_cast<uint32_t>(0x30 + i * 4));
    m_encCode[0] = m_romCode;
    m_encCode[1] = m_romCode / 2;
    m_encCode[2] = m_romCode * 2;
    if (level >= 1)
        ApplyKeycode();
    if (level >= 2)
        ApplyKeycode();
    m_encCode[1] *= 2;
    m_encCode[2] /= 2;
    if (level >= 3)
        ApplyKeycode();
}

void CartridgeNds::ApplyKeycode() {
    uint64_t enc1 = Encrypt64(((uint64_t)m_encCode[2] << 32) | m_encCode[1]);
    m_encCode[1]    = static_cast<uint32_t>(enc1);
    m_encCode[2]    = static_cast<uint32_t>(enc1 >> 32);
    uint64_t enc2 = Encrypt64(((uint64_t)m_encCode[1] << 32) | m_encCode[0]);
    m_encCode[0]    = static_cast<uint32_t>(enc2);
    m_encCode[1]    = static_cast<uint32_t>(enc2 >> 32);
    for (int i = 0; i <= 0x11; i++) {
        uint32_t byteReverse = 0;
        for (int j = 0; j < 4; j++)
            byteReverse |= ((m_encCode[i % 2] >> (j * 8)) & 0xFF) << ((3 - j) * 8);
        m_encTable[i] ^= byteReverse;
    }
    uint64_t scratch = 0;
    for (int i = 0; i <= 0x410; i += 2) {
        scratch         = Encrypt64(scratch);
        m_encTable[i + 0] = static_cast<uint32_t>(scratch >> 32);
        m_encTable[i + 1] = static_cast<uint32_t>(scratch);
    }
}

void CartridgeNds::IoState(StateArchive &archive) {
    Cartridge::IoState(archive);
    archive.Io(m_romCode);
    archive.Io(m_romEncrypted);
    archive.Io(m_cmdMode);
    archive.Io(m_encTable);
    archive.Io(m_encCode);
    archive.Io(m_romAddrReal);
    archive.Io(m_romAddrVirt);
    archive.Io(m_blockSize);
    archive.Io(m_readCount);
    archive.Io(m_wordCycles);
    archive.Io(m_encrypted);
    archive.Io(m_auxCommand);
    archive.Io(m_auxAddress);
    archive.Io(m_auxWriteCount);
    archive.Io(m_auxSpiCnt);
    archive.Io(m_auxSpiData);
    archive.Io(m_romCtrl);
    archive.Io(m_romCmdOut);
}

void CartridgeNds::WriteAuxSpiCnt(bool cpu, uint16_t mask, uint16_t value) {
    mask &= 0xE043;
    m_auxSpiCnt[cpu] = (m_auxSpiCnt[cpu] & ~mask) | (value & mask);
}

void CartridgeNds::WriteRomCmdOutL(bool cpu, uint32_t mask, uint32_t value) {
    m_romCmdOut[cpu] = (m_romCmdOut[cpu] & ~((uint64_t)mask)) | (value & mask);
}

void CartridgeNds::WriteRomCmdOutH(bool cpu, uint32_t mask, uint32_t value) {
    m_romCmdOut[cpu] = (m_romCmdOut[cpu] & ~((uint64_t)mask << 32)) | ((uint64_t)(value & mask) << 32);
}

void CartridgeNds::WriteAuxSpiData(bool cpu, uint8_t value) {
    if (saveSize == 0)
        return;
    if (m_auxWriteCount[cpu] == 0) {
        if (value == 0)
            return;
        m_auxCommand[cpu] = value;
        m_auxAddress[cpu] = 0;
        m_auxSpiData[cpu] = 0;
    } else {
        if (saveSize == -1) {
            switch (m_auxCommand[cpu]) {
                case 0x0B:
                    LOG("Detected EEPROM 0.5KB save type\n");
                    ResizeSave(0x200, false);
                    break;
                case 0x02:
                    LOG("Detected EEPROM 64KB save type\n");
                    ResizeSave(0x10000, false);
                    break;
                case 0x0A:
                    LOG("Detected FLASH 512KB save type\n");
                    ResizeSave(0x80000, false);
                    break;
                default:
                    if (!(m_auxSpiCnt[cpu] & BIT(6)))
                        m_auxWriteCount[cpu] = 0;
                    return;
            }
        }
        switch (saveSize) {
            case 0x200: {
                switch (m_auxCommand[cpu]) {
                    case 0x03: {
                        if (m_auxWriteCount[cpu] < 2) {
                            m_auxAddress[cpu] = value;
                            m_auxSpiData[cpu] = 0;
                        } else {
                            m_auxSpiData[cpu] = (m_auxAddress[cpu] < 0x200) ? save[m_auxAddress[cpu]] : 0;
                            m_auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x0B: {
                        if (m_auxWriteCount[cpu] < 2) {
                            m_auxAddress[cpu] = 0x100 + value;
                            m_auxSpiData[cpu] = 0;
                        } else {
                            m_auxSpiData[cpu] = (m_auxAddress[cpu] < 0x200) ? save[m_auxAddress[cpu]] : 0;
                            m_auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x02: {
                        if (m_auxWriteCount[cpu] < 2) {
                            m_auxAddress[cpu] = value;
                            m_auxSpiData[cpu] = 0;
                        } else {
                            if (m_auxAddress[cpu] < 0x200) {
                                mutex.lock();
                                save[m_auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            m_auxAddress[cpu]++;
                            m_auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    case 0x0A: {
                        if (m_auxWriteCount[cpu] < 2) {
                            m_auxAddress[cpu] = 0x100 + value;
                            m_auxSpiData[cpu] = 0;
                        } else {
                            if (m_auxAddress[cpu] < 0x200) {
                                mutex.lock();
                                save[m_auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            m_auxAddress[cpu]++;
                            m_auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    default: {
                        LOG_VERBOSE("Write to AUX SPI with unknown EEPROM 0.5KB command: 0x%X\n", auxCommand[cpu]);
                        m_auxSpiData[cpu] = 0;
                        break;
                    }
                }
                break;
            }
            case 0x2000:
            case 0x8000:
            case 0x10000:
            case 0x20000: {
                switch (m_auxCommand[cpu]) {
                    case 0x03: {
                        if (m_auxWriteCount[cpu] < ((saveSize == 0x20000) ? 4 : 3)) {
                            m_auxAddress[cpu] |= value << ((((saveSize == 0x20000) ? 3 : 2) - m_auxWriteCount[cpu]) * 8);
                            m_auxSpiData[cpu] = 0;
                        } else {
                            m_auxSpiData[cpu] = (saveSize > 0 && m_auxAddress[cpu] < uint32_t(saveSize)) ? save[m_auxAddress[cpu]] : 0;
                            m_auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x02: {
                        if (m_auxWriteCount[cpu] < ((saveSize == 0x20000) ? 4 : 3)) {
                            m_auxAddress[cpu] |= value << ((((saveSize == 0x20000) ? 3 : 2) - m_auxWriteCount[cpu]) * 8);
                            m_auxSpiData[cpu] = 0;
                        } else {
                            if (saveSize > 0 && m_auxAddress[cpu] < uint32_t(saveSize)) {
                                mutex.lock();
                                save[m_auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            m_auxAddress[cpu]++;
                            m_auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    default: {
                        LOG_VERBOSE("Write to AUX SPI with unknown EEPROM/FRAM command: 0x%X\n", auxCommand[cpu]);
                        m_auxSpiData[cpu] = 0;
                        break;
                    }
                }
                break;
            }
            case 0x40000:
            case 0x80000:
            case 0x100000:
            case 0x800000: {
                switch (m_auxCommand[cpu]) {
                    case 0x03: {
                        if (m_auxWriteCount[cpu] < 4) {
                            m_auxAddress[cpu] |= value << ((3 - m_auxWriteCount[cpu]) * 8);
                            m_auxSpiData[cpu] = 0;
                        } else {
                            m_auxSpiData[cpu] = (saveSize > 0 && m_auxAddress[cpu] < uint32_t(saveSize)) ? save[m_auxAddress[cpu]] : 0;
                            m_auxAddress[cpu]++;
                        }
                        break;
                    }
                    case 0x0A: {
                        if (m_auxWriteCount[cpu] < 4) {
                            m_auxAddress[cpu] |= value << ((3 - m_auxWriteCount[cpu]) * 8);
                            m_auxSpiData[cpu] = 0;
                        } else {
                            if (saveSize > 0 && m_auxAddress[cpu] < uint32_t(saveSize)) {
                                mutex.lock();
                                save[m_auxAddress[cpu]] = value;
                                saveDirty             = true;
                                mutex.unlock();
                            }
                            m_auxAddress[cpu]++;
                            m_auxSpiData[cpu] = 0;
                        }
                        break;
                    }
                    case 0x08: {
                        m_auxSpiData[cpu] = ((m_romCode & 0xFF) == 'I') ? 0xAA : 0;
                        break;
                    }
                    default: {
                        LOG_VERBOSE("Write to AUX SPI with unknown FLASH command: 0x%X\n", auxCommand[cpu]);
                        m_auxSpiData[cpu] = 0;
                        break;
                    }
                }
                break;
            }
            default: {
                LOG_VERBOSE("Write to AUX SPI with unknown save size: 0x%X\n", saveSize);
                break;
            }
        }
    }
    if (m_auxSpiCnt[cpu] & BIT(6))
        m_auxWriteCount[cpu]++;
    else
        m_auxWriteCount[cpu] = 0;
}

void CartridgeNds::WriteRomCtrl(bool cpu, uint32_t mask, uint32_t value) {
    bool transfer = false;
    m_romCtrl[cpu] |= (value & BIT(29));
    if (!(m_romCtrl[cpu] & BIT(31)) && (value & BIT(31)))
        transfer = true;
    mask &= 0xDF7F7FFF;
    m_romCtrl[cpu]    = (m_romCtrl[cpu] & ~mask) | (value & mask);
    m_wordCycles[cpu] = (m_romCtrl[cpu] & BIT(27)) ? (4 * 8) : (4 * 5);
    if (!transfer)
        return;
    uint8_t size = (m_romCtrl[cpu] & 0x07000000) >> 24;
    switch (size) {
        case 0:
            m_blockSize[cpu] = 0;
            break;
        case 7:
            m_blockSize[cpu] = 4;
            break;
        default:
            m_blockSize[cpu] = 0x100 << size;
            break;
    }
    uint64_t command = 0;
    for (int i = 0; i < 8; i++)
        command |= ((m_romCmdOut[cpu] >> (i * 8)) & 0xFF) << ((7 - i) * 8);
    if (m_encrypted[cpu]) {
        InitKeycode(2);
        command = Decrypt64(command);
    }
    m_cmdMode = CMD_NONE;
    if (rom) {
        if (command == 0x0000000000000000) {
            m_cmdMode = CMD_HEADER;
            if (romFile)
                LoadRomSection(0, m_blockSize[cpu]);
        } else if (command == 0x9000000000000000 || (command >> 60) == 0x1 || command == 0xB800000000000000) {
            m_cmdMode = CMD_CHIP;
        } else if ((command >> 56) == 0x3C) {
            m_encrypted[cpu] = true;
        } else if ((command >> 60) == 0x2) {
            m_cmdMode          = CMD_SECURE;
            m_romAddrReal[cpu] = ((command & 0x0FFFF00000000000) >> 44) * 0x1000;
            if (romFile) {
                LoadRomSection(m_romAddrReal[cpu], m_blockSize[cpu]);
                m_romAddrVirt[cpu] = 0;
            } else {
                m_romAddrVirt[cpu] = m_romAddrReal[cpu];
            }
        } else if ((command >> 60) == 0xA) {
            m_encrypted[cpu] = false;
        } else if ((command >> 56) == 0xB7) {
            m_cmdMode          = CMD_DATA;
            m_romAddrReal[cpu] = static_cast<uint32_t>((command >> 24) & romMask);
            if (romFile) {
                if (m_romAddrReal[cpu] < 0x8000) {
                    LoadRomSection(0, 0x8200 + m_blockSize[cpu]);
                    m_romAddrVirt[cpu] = m_romAddrReal[cpu];
                } else {
                    LoadRomSection(m_romAddrReal[cpu], m_blockSize[cpu]);
                    m_romAddrVirt[cpu] = 0;
                }
            } else {
                m_romAddrVirt[cpu] = m_romAddrReal[cpu];
            }
        } else if (command != 0x9F00000000000000) {
            LOG_VERBOSE("ROM transfer with unknown command: 0x%lX\n", command);
        }
    }
    if (m_blockSize[cpu] == 0) {
        m_romCtrl[cpu] &= ~BIT(23);
        m_romCtrl[cpu] &= ~BIT(31);
        if (m_auxSpiCnt[cpu] & BIT(14))
            core->interpreter[cpu].SendInterrupt(19);
    } else {
        core->Schedule(Task(&m_wordReadyTasks[cpu], m_wordCycles[cpu]));
        m_readCount[cpu] = 0;
    }
}

void CartridgeNds::WordReady(bool cpu) {
    m_romCtrl[cpu] |= BIT(23);
    core->dma[cpu].Trigger((cpu == 0) ? 5 : 2);
}

uint32_t CartridgeNds::ReadRomDataIn(bool cpu) {
    if (!(m_romCtrl[cpu] & BIT(23)))
        return 0;
    m_romCtrl[cpu] &= ~BIT(23);
    if ((m_readCount[cpu] += 4) == m_blockSize[cpu]) {
        m_romCtrl[cpu] &= ~BIT(31);
        if (m_auxSpiCnt[cpu] & BIT(14))
            core->interpreter[cpu].SendInterrupt(19);
    } else {
        core->Schedule(Task(&m_wordReadyTasks[cpu], m_wordCycles[cpu]));
    }
    switch (m_cmdMode) {
        case CMD_HEADER: {
            return U8TO32(rom, (m_readCount[cpu] - 4) & 0xFFF);
        }
        case CMD_CHIP: {
            return 0x00001FC2;
        }
        case CMD_SECURE: {
            if (!m_romEncrypted && m_romAddrReal[cpu] == 0x4000 && m_readCount[cpu] <= 0x800) {
                uint64_t data = (m_readCount[cpu] <= 8) ? 0x6A624F7972636E65
                                                      : U8TO64(rom, (m_romAddrVirt[cpu] + m_readCount[cpu] - 4) & ~7u);
                InitKeycode(3);
                data = Encrypt64(data);
                if (m_readCount[cpu] <= 8) {
                    InitKeycode(2);
                    data = Encrypt64(data);
                }
                return static_cast<uint32_t>(data >> (((m_romAddrReal[cpu] + m_readCount[cpu]) & 4) ? 0 : 32));
            }
            return U8TO32(rom, m_romAddrVirt[cpu] + m_readCount[cpu] - 4);
        }
        case CMD_DATA: {
            uint32_t address = m_romAddrVirt[cpu] + m_readCount[cpu] - 4;
            if (m_romAddrReal[cpu] + m_readCount[cpu] <= 0x8000)
                address = 0x8000 + (address & 0x1FF);
            if (address < uint32_t(romSize))
                return U8TO32(rom, address);
            [[fallthrough]]; // hors ROM : lecture de bus flottant
        }
        case CMD_NONE: {
            return 0xFFFFFFFF;
        }
    }
    return 0xFFFFFFFF;
}

void CartridgeGba::IoState(StateArchive &archive) {
    Cartridge::IoState(archive);
    archive.Io(m_eepromCount);
    archive.Io(m_eepromCmd);
    archive.Io(m_eepromData);
    archive.Io(m_eepromDone);
    archive.Io(m_flashCmd);
    archive.Io(m_bankSwap);
    archive.Io(m_flashErase);
}

bool CartridgeGba::FindString(std::string string) {
    for (int i = 0; i < romSize; i += 4) {
        bool found = true;
        for (size_t j = 0; j < string.length(); j++) {
            if (static_cast<size_t>(i) + j >= size_t(romSize) || rom[static_cast<size_t>(i) + j] != string[j]) {
                found = false;
                break;
            }
        }
        if (found)
            return true;
    }
    return false;
}

bool CartridgeGba::LoadRom(const std::string& path) {
    bool res = Cartridge::LoadRom(path);
    LoadRomSection(0, static_cast<size_t>(romSize));
    romFile.Reset();
    if (romSize > 0xAC && rom[0xAC] == 'F') {
        for (romMask = 1; romMask < uint32_t(romSize); romMask <<= 1)
            ;
        romMask--;
    } else {
        romMask = 0x1FFFFFF;
    }
    if (FindString("SIIRTC_V"))
        core->rtc.EnableGpRtc();
    core->memory.UpdateMap9(0x08000000, 0x0A000000);
    core->memory.UpdateMap7(0x08000000, 0x0D000000);
    if (!save) {
        const std::string saveStrs[] = {"EEPROM_V", "SRAM_V", "FLASH_V", "FLASH512_V", "FLASH1M_V"};
        for (int i = 0; i < 5; i++) {
            if (!FindString(saveStrs[i]))
                continue;
            switch (i) {
                case 0:
                    saveSize = -1;
                    return res;
                case 1:
                    LOG("Detected SRAM 32KB save type\n");
                    ResizeSave(0x8000, false);
                    return res;
                case 2:
                case 3:
                    LOG("Detected FLASH 64KB save type\n");
                    ResizeSave(0x10000, false);
                    return res;
                case 4:
                    LOG("Detected FLASH 128KB save type\n");
                    ResizeSave(0x20000, false);
                    return res;
            }
        }
    }
    return res;
}

uint8_t CartridgeGba::EepromRead() {
    if (saveSize == -1) {
        if (m_eepromCount == 9) {
            LOG("Detected EEPROM 0.5KB save type\n");
            ResizeSave(0x200, false);
        } else {
            LOG("Detected EEPROM 8KB save type\n");
            ResizeSave(0x2000, false);
        }
    }
    int length = (saveSize == 0x200) ? 8 : 16;
    if (((m_eepromCmd & 0xC000) >> 14) == 0x3 && m_eepromCount >= length + 1) {
        if (++m_eepromCount >= length + 6) {
            int      bit   = 63 - (m_eepromCount - (length + 6));
            uint16_t addr  = (saveSize == 0x200) ? ((m_eepromCmd & 0x3F00) >> 8) : (m_eepromCmd & 0x03FF);
            uint8_t  value = static_cast<uint8_t>((save[addr * 8 + bit / 8] & BIT(bit % 8)) >> (bit % 8));
            if (m_eepromCount >= length + 69) {
                m_eepromCount = 0;
                m_eepromCmd   = 0;
                m_eepromData  = 0;
            }
            return value;
        }
    } else if (m_eepromDone) {
        return 1;
    }
    return 0;
}

void CartridgeGba::EepromWrite(uint8_t value) {
    m_eepromDone = false;
    int length = (saveSize == 0x200) ? 8 : 16;
    if (m_eepromCount < length) {
        m_eepromCmd = static_cast<uint16_t>(m_eepromCmd | ((value & BIT(0)) << (16 - ++m_eepromCount)));
    } else if (((m_eepromCmd & 0xC000) >> 14) == 0x3) {
        if (m_eepromCount < length + 1)
            m_eepromCount++;
    } else if (((m_eepromCmd & 0xC000) >> 14) == 0x2) {
        if (++m_eepromCount <= length + 64)
            m_eepromData |= (uint64_t)(value & BIT(0)) << (length + 64 - m_eepromCount);
        if (m_eepromCount >= length + 65) {
            if (saveSize == -1) {
                LOG("Detected EEPROM 8KB save type\n");
                ResizeSave(0x2000, false);
            }
            mutex.lock();
            uint16_t addr = (saveSize == 0x200) ? ((m_eepromCmd & 0x3F00) >> 8) : (m_eepromCmd & 0x03FF);
            for (unsigned int i = 0; i < 8; i++)
                save[addr * 8 + i] = static_cast<uint8_t>(m_eepromData >> (i * 8));
            saveDirty = true;
            mutex.unlock();
            m_eepromCount = 0;
            m_eepromCmd   = 0;
            m_eepromData  = 0;
            m_eepromDone  = true;
        }
    }
}

uint8_t CartridgeGba::SramRead(uint32_t address) {
    if (saveSize == 0x8000 && address < 0xE008000) {
        return save[address - 0xE000000];
    } else if ((saveSize == 0x10000 || saveSize == 0x20000) && address < 0xE010000) {
        if (m_flashCmd == 0x90 && address == 0xE000000) {
            return 0xC2;
        } else if (m_flashCmd == 0x90 && address == 0xE000001) {
            return (saveSize == 0x10000) ? 0x1C : 0x09;
        } else {
            if (m_bankSwap)
                address += 0x10000;
            return save[address - 0xE000000];
        }
    }
    return 0xFF;
}

void CartridgeGba::SramWrite(uint32_t address, uint8_t value) {
    if (saveSize == 0x8000 && address < 0xE008000) {
        mutex.lock();
        save[address - 0xE000000] = value;
        saveDirty                 = true;
        mutex.unlock();
    } else if ((saveSize == 0x10000 || saveSize == 0x20000) && address < 0xE010000) {
        if (m_flashCmd == 0xA0) {
            if (m_bankSwap)
                address += 0x10000;
            mutex.lock();
            save[address - 0xE000000] = value;
            saveDirty                 = true;
            mutex.unlock();
            m_flashCmd = 0xF0;
        } else if (m_flashErase && (address & ~0x000F000u) == 0xE000000 && (value & 0xFF) == 0x30) {
            if (m_bankSwap)
                address += 0x10000;
            mutex.lock();
            memset(&save[address - 0xE000000], 0xFF, 0x1000 * sizeof(uint8_t));
            saveDirty = true;
            mutex.unlock();
            m_flashErase = false;
        } else if (saveSize == 0x20000 && m_flashCmd == 0xB0 && address == 0xE000000) {
            m_bankSwap = value;
            m_flashCmd = 0xF0;
        } else if (address == 0xE005555) {
            m_flashCmd = value;
            if (m_flashCmd == 0x80) {
                m_flashErase = true;
            } else if (m_flashCmd != 0xAA) {
                m_flashErase = false;
            } else if (m_flashErase && m_flashCmd == 0x10) {
                mutex.lock();
                memset(save, 0xFF, static_cast<size_t>(saveSize) * sizeof(uint8_t));
                saveDirty = true;
                mutex.unlock();
            }
        }
    }
}

} // namespace consoles::arm

