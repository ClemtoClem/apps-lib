#include "consoles/arm/settings.hpp"

#include <algorithm>
#include <cstring>
#include <thread>
#include "consoles/arm/core.hpp"

namespace consoles::arm {

Core::Core(RomType type, std::vector<uint8_t> rom, Settings settings_, int id_) : Core(type, std::string(), std::move(settings_), id_, std::move(rom)) {}

Core::Core(RomType type, std::string path, Settings settings_, int id_, std::vector<uint8_t> rom)
    : settings(std::move(settings_)), romImage(std::move(rom)), actionReplay(this), ndsBios9(this), ndsBios7(this), cartridgeNds(this), cartridgeGba(this), cp15(this),
      divSqrt(this), dldi(this), dma{Dma(this, 0), Dma(this, 1)}, gpu(this), gpu2D{Gpu2D(this, 0), Gpu2D(this, 1)}, gpu3D(this),
      gpu3DRenderer(this), hleArm7(this), input(this), interpreter{CPU(this, 0), CPU(this, 1)}, ipc(this), memory(this), rtc(this),
      spi(this), spu(this), timers{Timers(this, 0), Timers(this, 1)}, wifi(this), m_id(id_) {
    const bool hasRom = !path.empty() || !romImage.empty();
    // BIOS et micrologiciel de la DS : facultatifs en démarrage direct sur une cartouche DS
    // (BIOS logiciel), inutiles pour une cartouche GBA (seul son BIOS sert).
    const bool ndsFirmwareRequired = type == ROM_GBA ? !hasRom : (!settings.directBoot || !hasRom);
    if (!memory.LoadNdsBios9() && ndsFirmwareRequired)
        { m_bootError = ERROR_BIOS; return; }
    if (!memory.LoadNdsBios7() && ndsFirmwareRequired)
        { m_bootError = ERROR_BIOS; return; }
    if (!spi.LoadFirmware() && ndsFirmwareRequired)
        { m_bootError = ERROR_FIRM; return; }
    // Amont (NooDS) : une session GBA SEULE exige le BIOS GBA, boot direct ou
    // non — le cœur n'a pas de BIOS GBA en HLE (seulement ARM9/ARM7 NDS), et
    // le premier SWI ou la première interruption sauterait dans une zone
    // vide. La réécriture précédente de cette condition l'autorisait en boot
    // direct : la ROM « démarrait », puis l'émulation se figeait.
    const bool gbaBiosLoaded = memory.LoadGbaBios();
    if (!gbaBiosLoaded && type == ROM_GBA && hasRom)
        { m_bootError = ERROR_GBA_BIOS; return; }

    m_resetCyclesTask = std::bind(&Core::ResetCycles, this);
    Schedule(Task(&m_resetCyclesTask, 0x7FFFFFFF));
    gpu.ScheduleInit();
    spu.ScheduleInit();
    memory.UpdateMap9(0x00000000, 0xFFFFFFFF);
    memory.UpdateMap7(0x00000000, 0xFFFFFFFF);
    interpreter[0].Init();
    interpreter[1].Init();

    if (type == ROM_GBA && hasRom) {
        if (!cartridgeGba.LoadRom(path))
            { m_bootError = ERROR_ROM; return; }
        // Upstream NooDS also checks "no NDS ROM was given" here, since its
        // constructor can load an NDS ROM *and* a GBA slot-2 ROM together
        // (defaulting to NDS mode unless there's no NDS ROM at all). This
        // fork's constructor takes a single RomType + path, so type ==
        // ROM_GBA already means "GBA-only session" — that upstream check
        // has no equivalent here.
        // Le mode GBA est toujours activé (auparavant seulement en démarrage direct : sans
        // lui, une ROM GBA démarrait comme une cartouche DS). Le démarrage direct saute
        // l'introduction du BIOS GBA et reprend dans l'état où le BIOS rend la main.
        memory.Write<uint16_t>(0, 0x4000304, 0x8003);
        EnterGbaMode();
        if (settings.directBoot) {
            memory.Write<uint16_t>(1, 0x4000088, 0x0200); // SOUNDBIAS
            memory.Write<uint8_t>(1, 0x4000300, 0x01);    // POSTFLG
            for (const uint32_t reg : {0x4000020u, 0x4000026u, 0x4000030u, 0x4000036u})
                memory.Write<uint16_t>(1, reg, 0x0100);   // BG2PA/PD, BG3PA/PD : matrices identité
            interpreter[1].DirectBootGba();
        }
    }

    if (type == ROM_NDS && hasRom) {
        if (!cartridgeNds.LoadRom(path))
            { m_bootError = ERROR_ROM; return; }

        if (settings.directBoot) {
            cp15.Write(1, 0, 0, 0x0005707D);
            cp15.Write(9, 1, 0, 0x0300000A);
            cp15.Write(9, 1, 1, 0x00000020);
            memory.Write<uint8_t>(0, 0x4000247, 0x03);
            memory.Write<uint8_t>(0, 0x4000300, 0x01);
            memory.Write<uint8_t>(1, 0x4000300, 0x01);
            memory.Write<uint16_t>(0, 0x4000304, 0x0001);
            memory.Write<uint16_t>(1, 0x4000504, 0x0200);
            memory.Write<uint32_t>(0, 0x27FF800, 0x00001FC2);
            memory.Write<uint32_t>(0, 0x27FF804, 0x00001FC2);
            memory.Write<uint16_t>(0, 0x27FF850, 0x5835);
            memory.Write<uint16_t>(0, 0x27FF880, 0x0007);
            memory.Write<uint16_t>(0, 0x27FF884, 0x0006);
            memory.Write<uint32_t>(0, 0x27FFC00, 0x00001FC2);
            memory.Write<uint32_t>(0, 0x27FFC04, 0x00001FC2);
            memory.Write<uint16_t>(0, 0x27FFC10, 0x5835);
            memory.Write<uint16_t>(0, 0x27FFC40, 0x0001);
            cartridgeNds.DirectBoot();
            interpreter[0].DirectBoot();
            interpreter[1].DirectBoot();
            spi.DirectBoot();
        }
    }

    // Opt-in, EXPÉRIMENTAL : l'ARM7 est arrêté et seuls l'initialisation IPC
    // et le tactile sont simulés (HleArm7). Le programme ARM7 du jeu (son,
    // RTC, NVRAM, Wi-Fi…) ne s'exécute donc pas : la plupart des jeux du
    // commerce attendent une réponse qui ne vient jamais (écran fixe, aucun
    // son). Inutile pour démarrer sans dump de BIOS : le BIOS HLE (Bios)
    // exécute alors les SWI du vrai code ARM7. Sans effet en mode GBA.
    if (!m_gbaMode && settings.arm7Hle) {
        LOG("ARM7 en HLE (expérimental) : pas de son, la plupart des jeux du commerce ne démarreront pas\n");
        arm7Hle = true;
        hleArm7.Init();
    }

    m_running.store(true);
}


void Core::RunFrame() {
    (this->*m_runFunc)();
}


void Core::ResetCycles() {
    for (unsigned int i = 0; i < m_tasks.size(); i++)
        m_tasks[i].cycles -= m_globalCycles;

    m_arm9Cycles -= std::min(m_globalCycles, m_arm9Cycles);
    m_arm7Cycles -= std::min(m_globalCycles, m_arm7Cycles);
    timers[0].ResetCycles();
    timers[1].ResetCycles();
    m_globalCycles -= m_globalCycles;
    Schedule(Task(&m_resetCyclesTask, 0x7FFFFFFF));
}

void Core::RunGbaFrame() {
    while (m_running.exchange(true)) {
        if (m_arm7Cycles > m_globalCycles)
            m_globalCycles = m_arm7Cycles;

        while (interpreter[1].ShouldRun() && m_tasks[0].cycles > m_arm7Cycles)
            m_arm7Cycles = (m_globalCycles += static_cast<uint32_t>(interpreter[1].RunOpcode()));

        m_globalCycles = m_tasks[0].cycles;
        while (m_tasks[0].cycles <= m_globalCycles) {
            (*m_tasks[0].task)();
            m_tasks.erase(m_tasks.begin());
        }
    }
}

void Core::RunNdsFrame() {
    while (m_running.exchange(true)) {
        while (m_tasks[0].cycles > m_globalCycles) {
            if (interpreter[0].ShouldRun() && m_globalCycles >= m_arm9Cycles)
                m_arm9Cycles = m_globalCycles + static_cast<uint32_t>(interpreter[0].RunOpcode());

            if (interpreter[1].ShouldRun() && m_globalCycles >= m_arm7Cycles)
                m_arm7Cycles = m_globalCycles + static_cast<uint32_t>((interpreter[1].RunOpcode() << 1));

            m_globalCycles = std::min<uint32_t>((interpreter[0].ShouldRun() ? m_arm9Cycles : UINT32_MAX),
                                              (interpreter[1].ShouldRun() ? m_arm7Cycles : UINT32_MAX));
        }

        m_globalCycles = m_tasks[0].cycles;
        while (m_tasks[0].cycles <= m_globalCycles) {
            (*m_tasks[0].task)();
            m_tasks.erase(m_tasks.begin());
        }
    }
}

void Core::Schedule(Task task) {
    task.cycles += m_globalCycles;
    auto it = std::upper_bound(m_tasks.cbegin(), m_tasks.cend(), task);
    m_tasks.insert(it, task);
}

void Core::EnterGbaMode() {
    m_gbaMode = true;
    m_runFunc = &Core::RunGbaFrame;
    m_running.store(false);
    m_tasks.clear();

    Schedule(Task(&m_resetCyclesTask, 1));
    gpu.GbaScheduleInit();
    spu.GbaScheduleInit();

    memory.UpdateMap7(0x00000000, 0xFFFFFFFF);
    interpreter[1].Init();
    interpreter[1].SetBios(nullptr);
    rtc.Reset();
    memory.Write<uint8_t>(0, 0x4000240, 0x80);
    memory.Write<uint8_t>(0, 0x4000241, 0x80);
}

void Core::EndFrame() {
    m_running.store(false);
    m_fpsCount++;

    if (arm7Hle)
        hleArm7.RunFrame();
    std::chrono::duration<double> fpsTime = std::chrono::steady_clock::now() - m_lastFpsTime;
    if (fpsTime.count() >= 1.0) {
        m_fps = m_fpsCount;
        m_fpsCount = 0;
        m_lastFpsTime = std::chrono::steady_clock::now();
    }

    if (wifi.ShouldSchedule())
        wifi.ScheduleInit();
}

void Core::IoState(StateArchive &archive) {
    // Marqueur interne à l'archive (en plus de l'en-tête de fichier géré par
    // SaveStates) : identifie le mode au moment de la sauvegarde et permet un
    // contrôle de cohérence supplémentaire au chargement.
    char tag[4];
    if (archive.saving)
        std::memcpy(tag, m_gbaMode ? "GBAS" : "NDSS", 4);
    archive.Io(tag);
    uint32_t version = 1;
    archive.Io(version);
    if (!archive.saving && (version != 1 || (std::memcmp(tag, "NDSS", 4) != 0 && std::memcmp(tag, "GBAS", 4) != 0)))
        LOG("Unexpected save state marker/version inside archive (0x%.4s, v%u)\n", tag, version);

    archive.Io(m_globalCycles);
    archive.Io(m_arm9Cycles);
    archive.Io(m_arm7Cycles);
    archive.Io(m_gbaMode);

    ndsBios9.IoState(archive);
    ndsBios7.IoState(archive);
    // Les deux cartouches sont toujours sérialisées, même si une seule est
    // pertinente selon le mode courant : cela garde une disposition de
    // l'archive fixe, indépendante de gbaMode.
    cartridgeNds.IoState(archive);
    cartridgeGba.IoState(archive);
    cp15.IoState(archive);
    divSqrt.IoState(archive);
    dma[0].IoState(archive);
    dma[1].IoState(archive);
    gpu.IoState(archive);
    gpu2D[0].IoState(archive);
    gpu2D[1].IoState(archive);
    gpu3D.IoState(archive);
    gpu3DRenderer.IoState(archive);
    hleArm7.IoState(archive);
    input.IoState(archive);
    interpreter[0].IoState(archive);
    interpreter[1].IoState(archive);
    ipc.IoState(archive);
    memory.IoState(archive);
    rtc.IoState(archive);
    spi.IoState(archive);
    spu.IoState(archive);
    timers[0].IoState(archive);
    timers[1].IoState(archive);
    wifi.IoState(archive);

    if (archive.saving)
        return;

    // Tout ce qui suit ne concerne qu'un chargement : reconstruction de ce
    // qui n'est pas (et ne peut pas être) sérialisé directement.
    m_runFunc = m_gbaMode ? &Core::RunGbaFrame : &Core::RunNdsFrame;

    // Le scheduler n'est jamais sérialisé (il contient des pointeurs vers des
    // std::function internes à chaque composant). On repart d'une file vide
    // et on ré-arme uniquement ce qui doit l'être, exactement comme le fait
    // le constructeur pour un démarrage à froid.
    m_tasks.clear();
    Schedule(Task(&m_resetCyclesTask, 0x7FFFFFFF));

    // Reconstruit les caches de pointeurs mémoire/VRAM à partir des registres
    // bruts qui viennent d'être restaurés.
    memory.UpdateMap9(0x00000000, 0xFFFFFFFF);
    memory.UpdateMap7(0x00000000, 0xFFFFFFFF);
    memory.RebuildVram();

    if (m_gbaMode) {
        gpu.GbaScheduleInit();
        spu.GbaScheduleInit();
    } else {
        gpu.ScheduleInit();
        spu.ScheduleInit();
    }

    dma[0].Rearm();
    dma[1].Rearm();
    timers[0].Rearm();
    timers[1].Rearm();

    wifi.ResetSchedule();
    if (wifi.ShouldSchedule())
        wifi.ScheduleInit();
}

FORCE_INLINE int CPU::RunOpcode() {
    uint32_t opcode = m_pipeline[0];
    m_pipeline[0] = m_pipeline[1];
    if (m_cpsr & BIT(5)) {
        m_pipeline[1] = m_core->memory.Read<uint16_t>(m_cpu, *m_registers[15] += 2);
        return (this->*m_thumbInstrs[(opcode >> 6) & 0x3FF])(static_cast<uint16_t>(opcode));
    }
    else {
        m_pipeline[1] = m_core->memory.Read<uint32_t>(m_cpu, *m_registers[15] += 4);
        switch (condition[((opcode >> 24) & 0xF0) | (m_cpsr >> 28)]) {
        case 0:
            return 1;
        case 2:
            return HandleReserved(opcode);
        default:
            return (this->*m_armInstrs[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 0xF)])(opcode);
        }
    }
}

} // namespace consoles::arm

