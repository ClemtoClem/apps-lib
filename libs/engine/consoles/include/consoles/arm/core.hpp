
#pragma once
#include <chrono>
#include <map>
#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include <string>

#include <core/core.hpp>

#include "settings.hpp"
#include "cpu/cp15.hpp"
#include "cpu/cpu.hpp"
#include "cpu/timers.hpp"
#include "defines.hpp"
#include "gpu/gpu.hpp"
#include "gpu/gpu_2d.hpp"
#include "gpu/gpu_3d.hpp"
#include "gpu/gpu_3d_renderer.hpp"
#include "hle/action_replay.hpp"
#include "hle/bios.hpp"
#include "hle/dldi.hpp"
#include "hle/hle_arm7.hpp"
#include "io/div_sqrt.hpp"
#include "io/input.hpp"
#include "io/ipc.hpp"
#include "io/rtc.hpp"
#include "io/spi.hpp"
#include "io/spu.hpp"
#include "io/wifi.hpp"
#include "memory/cartridge.hpp"
#include "memory/dma.hpp"
#include "memory/memory.hpp"
#include "state_archive.hpp"

namespace consoles::arm {

enum RomType {
    ROM_NDS,
    ROM_GBA
};

enum CoreError {
    ERROR_NONE = 0,
    ERROR_BIOS,
    ERROR_GBA_BIOS,
    ERROR_FIRM,
    ERROR_ROM
};

struct Task {
    Task(std::function<void()> *task_, uint32_t cycles_) : task(task_), cycles(cycles_) {
    }
    std::function<void()> *task;
    uint32_t cycles;
    bool operator<(const Task &other) const {
        return cycles < other.cycles;
    }
};


class Core {
public:
    /// Construit et démarre un cœur. Préférer Create() : le constructeur ne
    /// peut pas échouer autrement qu'en renseignant GetBootError() (ce dépôt
    /// n'utilise pas d'exception — l'original levait un CoreError).
    Core(RomType type, std::string path, Settings settings, int id = 0, std::vector<uint8_t> rom = {});
    /// Cœur sur une image de ROM déjà en mémoire (extraite d'une archive, par exemple).
    Core(RomType type, std::vector<uint8_t> rom, Settings settings, int id = 0);

    /// Démarre un cœur sur le tas (ses composants gardent un pointeur vers
    /// lui : son adresse doit rester stable). Err si BIOS/firmware/ROM
    /// illisibles.
    [[nodiscard]] static Result<std::unique_ptr<Core>, CoreError> Create(RomType type, const std::string &path, Settings settings,
                                                                         int id = 0) {
        auto core = std::make_unique<Core>(type, path, std::move(settings), id);
        if (core->m_bootError != ERROR_NONE)
            return Err(core->m_bootError);
        return Ok(std::move(core));
    }

    /// Démarre un cœur sur une image de ROM en mémoire.
    [[nodiscard]] static Result<std::unique_ptr<Core>, CoreError> Create(RomType type, std::vector<uint8_t> rom, Settings settings,
                                                                         int id = 0) {
        auto core = std::make_unique<Core>(type, std::move(rom), std::move(settings), id);
        if (core->m_bootError != ERROR_NONE)
            return Err(core->m_bootError);
        return Ok(std::move(core));
    }

    [[nodiscard]] static const char *ErrorText(CoreError error) {
        switch (error) {
        case ERROR_NONE: return "aucune erreur";
        case ERROR_BIOS: return "BIOS NDS introuvable ou illisible (boot direct ou --arm7-hle pour s'en passer)";
        case ERROR_GBA_BIOS: return "BIOS GBA (gbaBios.bin) requis : le cœur n'émule pas le BIOS GBA (voir --bios-dir)";
        case ERROR_FIRM: return "firmware introuvable ou illisible";
        case ERROR_ROM: return "ROM introuvable ou illisible";
        }
        return "erreur inconnue";
    }

    [[nodiscard]] CoreError GetBootError() const {
        return m_bootError;
    }

    void RunFrame();

    bool IsGbaMode() {
        return m_gbaMode;
    }

    int GetId() {
        return m_id;
    }

    int GetFps() {
        return m_fps;
    }

    uint32_t GetGlobalCycles() {
        return m_globalCycles;
    }

    void Schedule(Task task);
    void EnterGbaMode();
    void EndFrame();

    /**
     * @brief Sérialise/désérialise l'état complet de l'émulateur (save state).
     *
     * Orchestre l'appel à IoState() de chaque composant, dans l'ordre de
     * déclaration des membres, puis (lors d'un chargement uniquement)
     * reconstruit tout ce qui n'est pas sérialisable directement : caches de
     * pointeurs mémoire/VRAM, threads de rendu/audio, et tâches du
     * scheduler (Core::tasks n'est jamais sérialisé : il contient des
     * pointeurs vers des std::function internes, qui n'ont de sens que dans
     * le processus courant).
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive);

    /// True once Init() below has actually taken effect (NDS mode + opted in
    /// via core->settings.arm7Hle at construction time) — checked by Ipc to
    /// redirect ARM9-side IPCSYNC/IPCFIFO writes to hleArm7 instead of the
    /// (halted, never-running) real ARM7.
    bool arm7Hle = false;

    /// Always false: this fork does not implement DSi mode. Kept as a real
    /// member (rather than removed) so DSi-only peripherals ported from
    /// upstream (e.g. Aes) can gate their behavior the same way upstream
    /// does, and compile/serialize correctly, without pretending DSi
    /// hardware is actually emulated.
    bool dsiMode = false;

    /// Réglages de l'instance (aucun état global : chaque cœur a les siens).
    Settings settings;
    /// Image de la ROM fournie en mémoire, cédée à la cartouche au chargement.
    std::vector<uint8_t> romImage;

    ActionReplay actionReplay;
    NdsBios9 ndsBios9;
    NdsBios7 ndsBios7;
    CartridgeNds cartridgeNds;
    CartridgeGba cartridgeGba;
    Cp15 cp15;
    DivSqrt divSqrt;
    Dldi dldi;
    Dma dma[2];
    Gpu gpu;
    Gpu2D gpu2D[2];
    Gpu3D gpu3D;
    Gpu3DRenderer gpu3DRenderer;
    HleArm7 hleArm7;
    Input input;
    CPU interpreter[2];
    Ipc ipc;
    Memory memory;
    Rtc rtc;
    Spi spi;
    Spu spu;
    Timers timers[2];
    Wifi wifi;

private:
    void (Core::*m_runFunc)(void) = &Core::RunNdsFrame;
    bool m_gbaMode = false;
    CoreError m_bootError = ERROR_NONE;
    int m_id = 0;
    std::vector<Task> m_tasks;
    uint32_t m_globalCycles = 0;
    uint32_t m_arm9Cycles = 0, m_arm7Cycles = 0;
    std::atomic<bool> m_running;
    int m_fps = 0, m_fpsCount = 0;

    std::chrono::steady_clock::time_point m_lastFpsTime;
    std::function<void()> m_resetCyclesTask;
    void ResetCycles();
    void RunNdsFrame();
    void RunGbaFrame();
};

} // namespace consoles::arm

