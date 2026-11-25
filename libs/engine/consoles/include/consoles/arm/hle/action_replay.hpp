#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

#include <string>
#include <core/core.hpp>
#include "consoles/arm/file.hpp"

namespace consoles::arm {

class Core;

struct ARCheat {
    std::string           name;
    std::vector<uint32_t> code;
    bool                  enabled;
};

/**
 * @brief Action Replay DS cheat engine: loads/data a `.cht` cheat file next
 * to the ROM and, once per frame (right before Core::EndFrame(), see
 * Gpu::DrawScanline case 262), interprets the enabled cheats' opcodes to
 * patch live memory. NDS-only (no GBA Action Replay format).
 */
class ActionReplay {
  public:
    std::vector<ARCheat> cheats;

    ActionReplay(Core *core_) : m_core(core_) {
    }

    void SetPath(std::string newPath);

    /// Loads (replacing `cheats`) from `fromPath`, or from the path set via
    /// SetPath() when `fromPath` is empty (the normal per-ROM `.cht` file).
    /// Used by the Cheats UI to import a cheat database file without losing
    /// track of where SaveCheats() should persist afterwards.
    bool LoadCheats(const std::string &fromPath = "");
    bool SaveCheats();
    void ApplyCheats();

  private:
    Core       *m_core;
    std::mutex  m_mutex;
    std::string m_path;

    std::optional<File> OpenFile(const std::string &forPath, const char *mode);
};

} // namespace consoles::arm

