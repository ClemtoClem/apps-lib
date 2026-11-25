#pragma once
#include <memory>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <queue>
#include "consoles/arm/defines.hpp"
#include "consoles/arm/state_archive.hpp"

namespace consoles::arm {

class Core;
class Gpu {
  private:
    Core *m_core;

    struct Buffers {
        std::vector<uint32_t> framebuffer;
        std::vector<uint32_t> hiRes3D; ///< vide : pas d'image 3D en haute résolution
        bool      top3D       = false;
    };

    std::queue<Buffers> m_framebuffers;
    std::atomic<bool>   m_ready;
    std::mutex          m_mutex;
    std::atomic<bool>   m_running{false}; // lu en boucle par le fil de rendu : doit être atomique
    std::atomic<int>    m_drawing;
    std::unique_ptr<std::thread> m_thread;
    bool                m_gbaBlock       = true;
    bool                m_displayCapture = false;
    uint8_t             m_dirty3D        = 0;
    uint16_t            m_dispStat[2]    = {};
    uint16_t            m_vCount         = 0;
    uint32_t            m_dispCapCnt     = 0;
    uint16_t            m_powCnt1        = 0;

  public:
    Gpu(Core *core);
    ~Gpu();

    void ScheduleInit();
    void GbaScheduleInit();
    bool GetFrame(uint32_t *out, bool gbaCrop);

    void Invalidate3D() {
        m_dirty3D |= BIT(0);
    }
    
    uint16_t ReadDispStat(bool cpu) {
        return m_dispStat[cpu];
    }

    uint16_t ReadVCount() {
        return m_vCount;
    }

    uint32_t ReadDispCapCnt() {
        return m_dispCapCnt;
    }

    uint16_t ReadPowCnt1() {
        return m_powCnt1;
    }

    void WriteDispStat(bool cpu, uint16_t mask, uint16_t value);
    void WriteDispCapCnt(uint32_t mask, uint32_t value);
    void WritePowCnt1(uint16_t mask, uint16_t value);

    /**
     * @brief Sérialise/désérialise l'état du GPU (registres 2D communs) pour les save states.
     *
     * Le thread de rendu 2D en tâche de fond (thread/running/drawing) et la
     * file de framebuffers prêts (framebuffers) ne sont pas des données
     * persistantes : ils sont recalculés à chaque frame. Le thread est donc
     * arrêté proprement (comme le fait déjà le destructeur) avant toute
     * sauvegarde ou tout chargement, pour éviter qu'il ne touche à des
     * données en cours de modification (VRAM, registres gpu2D...).
     * @param archive Archive de sauvegarde/chargement.
     */
    void IoState(StateArchive &archive) {
        if (m_thread) {
            m_running = false;
            m_thread->join();
            m_thread.reset();
        }
        archive.Io(m_gbaBlock);
        archive.Io(m_displayCapture);
        archive.Io(m_dirty3D);
        archive.Io(m_dispStat);
        archive.Io(m_vCount);
        archive.Io(m_dispCapCnt);
        archive.Io(m_powCnt1);
    }

  private:
    std::function<void()> m_gbaScanline240Task;
    std::function<void()> m_gbaScanline308Task;
    std::function<void()> m_scanline256Task;
    std::function<void()> m_scanline355Task;

    static uint32_t Rgb5ToRgb8(uint32_t color);
    static uint32_t Rgb6ToRgb8(uint32_t color);
    static uint16_t Rgb6ToRgb5(uint32_t color);

    void GbaScanline240();
    void GbaScanline308();
    void Scanline256();
    void Scanline355();
    void DrawGbaThreaded();
    void DrawThreaded();
};

} // namespace consoles::arm

