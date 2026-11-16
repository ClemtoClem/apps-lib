
#include <cstring>
#include "consoles/arm/hle/action_replay.hpp"
#include "consoles/arm/core.hpp"
#include "consoles/arm/defines.hpp"

namespace consoles::arm {

void ActionReplay::SetPath(std::string newPath) {
    this->m_path = newPath;
}

std::optional<File> ActionReplay::OpenFile(const std::string &forPath, const char *mode) {
    if (forPath == "")
        return std::nullopt;
    return File::Open(forPath, mode);
}

bool ActionReplay::LoadCheats(const std::string &fromPath) {
    auto file = OpenFile(fromPath != "" ? fromPath : m_path, "r");
    if (!file)
        return false;
    File f = std::move(*file);

    std::vector<uint8_t> bytes = f.ReadAll();
    String content(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    // split('\n') strips the delimiter, unlike the old fgets()-based loop
    // which kept it — the trim lengths below are adjusted accordingly (one
    // byte shorter than the fgets version at each spot).
    std::vector<String> lines = content.Split('\n');

    m_mutex.lock();

    // Reload cheats from the file
    cheats.clear();
    for (size_t i = 0; i < lines.size(); i++) {
        // Create a new cheat when one is found
        const String &data = lines[i];
        if (data.empty() || data[0] != '[')
            continue;
        cheats.push_back(ARCheat());
        ARCheat &cheat = cheats[cheats.size() - 1];

        // Parse the cheat name and enabled state from the file
        cheat.name    = data.substr(1);
        cheat.enabled = (cheat.name[cheat.name.size() - 1] == '+');
        cheat.name    = cheat.name.substr(0, cheat.name.size() - 2);
        LOG("Loaded cheat: %s (%s)\n", cheat.name.c_str(), cheat.enabled ? "enabled" : "disabled");

        // Load the cheat code up until an empty line
        while (i + 1 < lines.size() && !lines[i + 1].empty()) {
            ++i;
            cheat.code.push_back(uint32_t(strtoll(lines[i].c_str(), nullptr, 16)));
            cheat.code.push_back(uint32_t(strtoll(lines[i].c_str() + 8, nullptr, 16)));
        }
    }

    m_mutex.unlock();
    return true;
}

bool ActionReplay::SaveCheats() {
    auto file = OpenFile(m_path, "w");
    if (!file)
        return false;
    File f = std::move(*file);
    m_mutex.lock();

    String out;
    for (size_t i = 0; i < cheats.size(); i++) {
        out += '[';
        out += cheats[i].name;
        out += ']';
        out += cheats[i].enabled ? '+' : '-';
        out += '\n';
        for (size_t j = 0; j < cheats[i].code.size(); j += 2) {
            char buf[32]; // "%08X %08X\n" = 18 caractères + NUL
            snprintf(buf, sizeof(buf), "%08X %08X\n", cheats[i].code[j], cheats[i].code[j + 1]);
            out += buf;
        }
        out += '\n';
    }

    m_mutex.unlock();
    f.Write(out.c_str(), out.GetSize());
    return true;
}

void ActionReplay::ApplyCheats() {
    // Execute the code of enabled cheats
    m_mutex.lock();
    for (size_t i = 0; i < cheats.size(); i++) {
        // Define registers for executing a cheat
        if (!cheats[i].enabled)
            continue;
        uint32_t offset      = 0;
        uint32_t dataReg     = 0;
        uint32_t counter     = 0;
        uint32_t loopCount   = 0;
        uint32_t loopAddress = 0;
        bool     condFlag    = false;

        // Loop through lines of a cheat's code
        for (uint32_t address = 0; address < cheats[i].code.size(); address += 2) {
            // Check the condition flag
            uint32_t *line = &cheats[i].code[address];
            if (condFlag) {
                // Handle adjustments that happen regardless of condition
                uint8_t op = uint8_t(line[0] >> 24);
                if ((op >> 4) == 0xE) // Parameter copy
                    address += ((line[1] + 0x7) & ~0x7u) >> 2;
                else if (op == 0xC5) // If counter
                    counter++;

                // Skip non-control opcodes if the flag is set
                if (op != 0xD0 && op != 0xD1 && op != 0xD2)
                    continue;
            }

            // Interpret a line of the code
            switch (line[0] >> 28) {
                case 0x0: // Write word
                    m_core->memory.Write<uint32_t>(true, (line[0] & 0xFFFFFFF) + offset, line[1]);
                    continue;

                case 0x1: // Write half
                    m_core->memory.Write<uint16_t>(true, (line[0] & 0xFFFFFFF) + offset, uint16_t(line[1]));
                    continue;

                case 0x2: // Write byte
                    m_core->memory.Write<uint8_t>(true, (line[0] & 0xFFFFFFF) + offset, uint8_t(line[1]));
                    continue;

                case 0x3: { // If greater than word
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag      = (line[1] <= m_core->memory.Read<uint32_t>(true, addr));
                    continue;
                }

                case 0x4: { // If less than word
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag      = (line[1] >= m_core->memory.Read<uint32_t>(true, addr));
                    continue;
                }

                case 0x5: { // If equal to word
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag      = (line[1] != m_core->memory.Read<uint32_t>(true, addr));
                    continue;
                }

                case 0x6: { // If not equal to word
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag      = (line[1] == m_core->memory.Read<uint32_t>(true, addr));
                    continue;
                }

                case 0x7: { // If greater than half
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag = (line[1] & 0xFFFF) <= (m_core->memory.Read<uint16_t>(true, addr) & ~(line[1] >> 16));
                    continue;
                }

                case 0x8: { // If less than half
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag = (line[1] & 0xFFFF) >= (m_core->memory.Read<uint16_t>(true, addr) & ~(line[1] >> 16));
                    continue;
                }

                case 0x9: { // If equal to half
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag = (line[1] & 0xFFFF) != (m_core->memory.Read<uint16_t>(true, addr) & ~(line[1] >> 16));
                    continue;
                }

                case 0xA: { // If not equal to half
                    uint32_t addr = (line[0] & 0xFFFFFFF) ? (line[0] & 0xFFFFFFF) : offset;
                    condFlag = (line[1] & 0xFFFF) == (m_core->memory.Read<uint16_t>(true, addr) & ~(line[1] >> 16));
                    continue;
                }

                case 0xB: // Load offset
                    offset = m_core->memory.Read<uint32_t>(true, (line[0] & 0xFFFFFFF) + offset);
                    continue;

                case 0xC:
                    switch (line[0] >> 24) {
                        case 0xC0: // For loop
                            loopCount   = line[1];
                            loopAddress = address;
                            continue;

                        case 0xC5: // If counter
                            condFlag = (++counter & line[1] & 0xFFFF) != (line[1] >> 16);
                            continue;

                        case 0xC6: // Write offset
                            m_core->memory.Write<uint32_t>(true, line[1], offset);
                            continue;
                    }
                    LOG("Invalid AR code: %08X %08X\n", line[0], line[1]);
                    continue;

                case 0xD:
                    switch (line[0] >> 24) {
                        case 0xD0: // End if
                            condFlag = false;
                            continue;

                        case 0xD1: // Next loop
                            if (loopCount) {
                                loopCount--;
                                address = loopAddress;
                                continue;
                            }
                            condFlag = false;
                            continue;

                        case 0xD2: // Next loop and flush
                            if (loopCount) {
                                loopCount--;
                                address = loopAddress;
                                continue;
                            }
                            offset   = 0;
                            dataReg  = 0;
                            condFlag = false;
                            continue;

                        case 0xD3: // Set offset
                            offset = line[1];
                            continue;

                        case 0xD4: // Add data
                            dataReg += line[1];
                            continue;

                        case 0xD5: // Set data
                            dataReg = line[1];
                            continue;

                        case 0xD6: // Write data word
                            m_core->memory.Write<uint32_t>(true, line[1] + offset, dataReg);
                            offset += 4;
                            continue;

                        case 0xD7: // Write data half
                            m_core->memory.Write<uint16_t>(true, line[1] + offset, uint16_t(dataReg));
                            offset += 2;
                            continue;

                        case 0xD8: // Write data byte
                            m_core->memory.Write<uint8_t>(true, line[1] + offset, uint8_t(dataReg));
                            offset += 1;
                            continue;

                        case 0xD9: // Read data word
                            dataReg = m_core->memory.Read<uint32_t>(true, line[1] + offset);
                            continue;

                        case 0xDA: // Read data half
                            dataReg = m_core->memory.Read<uint16_t>(true, line[1] + offset);
                            continue;

                        case 0xDB: // Read data byte
                            dataReg = m_core->memory.Read<uint8_t>(true, line[1] + offset);
                            continue;

                        case 0xDC: // Add offset
                            offset += line[1];
                            continue;
                    }
                    LOG("Invalid AR code: %08X %08X\n", line[0], line[1]);
                    continue;

                case 0xE: // Parameter copy
                    for (uint32_t j = 0; j < line[1]; j++) {
                        uint8_t value = uint8_t(line[(j >> 2) + 2] >> ((j & 0x3) * 8));
                        m_core->memory.Write<uint8_t>(true, (line[0] & 0xFFFFFFF) + offset + j, value);
                    }
                    address += ((line[1] + 0x7) & ~0x7u) >> 2;
                    continue;

                case 0xF: // Memory copy
                    for (uint32_t j = 0; j < line[1]; j++) {
                        uint8_t value = m_core->memory.Read<uint8_t>(true, offset + j);
                        m_core->memory.Write<uint8_t>(true, (line[0] & 0xFFFFFFF) + j, value);
                    }
                    continue;

                default:
                    LOG("Invalid AR code: %08X %08X\n", line[0], line[1]);
                    continue;
            }
        }
    }
    m_mutex.unlock();
}

} // namespace consoles::arm
