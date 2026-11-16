#include "consoles/arm/cpu/cpu.hpp"
#include "consoles/arm/core.hpp"

namespace consoles::arm {

CPU::CPU(Core *core_, bool cpu_) : m_core(core_), m_cpu(cpu_) {
    for (int i = 0; i < 16; i++)
        m_registers[i] = &m_registersUsr[i];
    m_interruptTask = std::bind(&CPU::Interrupt, this);
}

void CPU::Init() {
    SetCpsr(0x000000D3);
    m_registersUsr[15] = (m_cpu == 0) ? 0xFFFF0000 : 0x00000000;
    FlushPipeline();
    m_ime = 0;
    m_ie = m_irf = 0;
    m_postFlg  = 0;
}

void CPU::DirectBoot() {
    uint32_t entryAddr;
    if (m_cpu == 0) {
        entryAddr        = m_core->memory.Read<uint32_t>(0, 0x27FFE24);
        m_registersUsr[13] = 0x03002F7C;
        m_registersIrq[0]  = 0x03003F80;
        m_registersSvc[0]  = 0x03003FC0;
    } else {
        entryAddr        = m_core->memory.Read<uint32_t>(0, 0x27FFE34);
        m_registersUsr[13] = 0x0380FD80;
        m_registersIrq[0]  = 0x0380FF80;
        m_registersSvc[0]  = 0x0380FFC0;
    }
    SetCpsr(0x000000DF);
    m_registersUsr[12] = entryAddr;
    m_registersUsr[14] = entryAddr;
    m_registersUsr[15] = entryAddr;
    FlushPipeline();
}

void CPU::DirectBootGba() {
    m_registersUsr[13] = 0x03007F00;
    m_registersIrq[0]  = 0x03007FA0;
    m_registersSvc[0]  = 0x03007FE0;
    SetCpsr(0x0000001F);
    m_registersUsr[15] = 0x08000000;
    FlushPipeline();
}

void CPU::SendInterrupt(int bit) {
    m_irf |= BIT(bit);
    if (m_ie & m_irf) {
        if (m_ime && !(m_cpsr & BIT(7)))
            m_core->Schedule(Task(&m_interruptTask, (m_cpu == 1 && !m_core->IsGbaMode()) ? 2 : 1));
        else if (m_ime || m_cpu == 1)
            m_halted = static_cast<uint8_t>(m_halted & ~BIT(0));
    }
}

void CPU::Interrupt() {
    if (m_ime && (m_ie & m_irf) && !(m_cpsr & BIT(7))) {
        Exception(0x18);
        m_halted = static_cast<uint8_t>(m_halted & ~BIT(0));
    }
}

int CPU::Exception(uint8_t vector) {
    if (m_bios && (m_cpu || m_core->cp15.GetExceptionAddr()))
        return m_bios->Execute(vector, m_cpu, m_registers);

    static uint8_t modes[] = {0x13, 0x1B, 0x13, 0x17, 0x17, 0x13, 0x12, 0x11};
    SetCpsr((m_cpsr & ~0x3Fu) | BIT(7) | modes[vector >> 2], true);
    *m_registers[14] = *m_registers[15] + ((*m_spsr & BIT(5)) ? 2 : 0);
    *m_registers[15] = (m_cpu ? 0 : m_core->cp15.GetExceptionAddr()) + vector;
    FlushPipeline();
    return 3;
}

void CPU::FlushPipeline() {
    if (m_cpsr & BIT(5)) {
        *m_registers[15] = (*m_registers[15] & ~1u) + 2;
        m_pipeline[0]    = m_core->memory.Read<uint16_t>(m_cpu, *m_registers[15] - 2);
        m_pipeline[1]    = m_core->memory.Read<uint16_t>(m_cpu, *m_registers[15]);
    } else {
        *m_registers[15] = (*m_registers[15] & ~3u) + 4;
        m_pipeline[0]    = m_core->memory.Read<uint32_t>(m_cpu, *m_registers[15] - 4);
        m_pipeline[1]    = m_core->memory.Read<uint32_t>(m_cpu, *m_registers[15]);
    }
}

void CPU::SetCpsr(uint32_t value, bool save) {
    if ((value & 0x1F) != (m_cpsr & 0x1F)) {
        switch (value & 0x1F) {
            case 0x10:
            case 0x1F:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersUsr[13];
                m_registers[14] = &m_registersUsr[14];
                m_spsr          = nullptr;
                break;
            case 0x11:
                m_registers[8]  = &m_registersFiq[0];
                m_registers[9]  = &m_registersFiq[1];
                m_registers[10] = &m_registersFiq[2];
                m_registers[11] = &m_registersFiq[3];
                m_registers[12] = &m_registersFiq[4];
                m_registers[13] = &m_registersFiq[5];
                m_registers[14] = &m_registersFiq[6];
                m_spsr          = &m_spsrFiq;
                break;
            case 0x12:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersIrq[0];
                m_registers[14] = &m_registersIrq[1];
                m_spsr          = &m_spsrIrq;
                break;
            case 0x13:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersSvc[0];
                m_registers[14] = &m_registersSvc[1];
                m_spsr          = &m_spsrSvc;
                break;
            case 0x17:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersAbt[0];
                m_registers[14] = &m_registersAbt[1];
                m_spsr          = &m_spsrAbt;
                break;
            case 0x1B:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersUnd[0];
                m_registers[14] = &m_registersUnd[1];
                m_spsr          = &m_spsrUnd;
                break;
            default:
                LOG_VERBOSE("Unknown ARM%d CPU mode: 0x%X\n", ((cpu == 0) ? 9 : 7), value & 0x1F);
                break;
        }
    }
    if (save && m_spsr)
        *m_spsr = m_cpsr;
    m_cpsr = value;
    if (m_ime && (m_ie & m_irf) && !(m_cpsr & BIT(7)))
        m_core->Schedule(Task(&m_interruptTask, (m_cpu == 1 && !m_core->IsGbaMode()) ? 2 : 1));
}

void CPU::IoState(StateArchive &archive) {
    archive.Io(m_pipeline);
    archive.Io(m_registersUsr);
    archive.Io(m_registersFiq);
    archive.Io(m_registersSvc);
    archive.Io(m_registersAbt);
    archive.Io(m_registersIrq);
    archive.Io(m_registersUnd);
    archive.Io(m_cpsr);
    archive.Io(m_spsrFiq);
    archive.Io(m_spsrSvc);
    archive.Io(m_spsrAbt);
    archive.Io(m_spsrIrq);
    archive.Io(m_spsrUnd);
    archive.Io(m_halted);
    archive.Io(m_ime);
    archive.Io(m_ie);
    archive.Io(m_irf);
    archive.Io(m_postFlg);

    if (!archive.saving) {
        // registers[0..7] et registers[15] pointent toujours vers registersUsr
        // (jamais banqués, donc déjà corrects depuis la construction du CPU) ;
        // seuls registers[8..14] et spsr dépendent du mode courant. On ne peut
        // pas simplement rappeler SetCpsr(cpsr) : cette fonction ne repointe
        // les registres banqués que lorsque le mode change par rapport à la
        // valeur ACTUELLE de cpsr, qui est déjà celle qu'on vient de
        // restaurer (donc "inchangée"). On reproduit donc ici le switch de
        // SetCpsr() directement à partir du cpsr restauré.
        switch (m_cpsr & 0x1F) {
            case 0x10:
            case 0x1F:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersUsr[13];
                m_registers[14] = &m_registersUsr[14];
                m_spsr          = nullptr;
                break;
            case 0x11:
                m_registers[8]  = &m_registersFiq[0];
                m_registers[9]  = &m_registersFiq[1];
                m_registers[10] = &m_registersFiq[2];
                m_registers[11] = &m_registersFiq[3];
                m_registers[12] = &m_registersFiq[4];
                m_registers[13] = &m_registersFiq[5];
                m_registers[14] = &m_registersFiq[6];
                m_spsr          = &m_spsrFiq;
                break;
            case 0x12:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersIrq[0];
                m_registers[14] = &m_registersIrq[1];
                m_spsr          = &m_spsrIrq;
                break;
            case 0x13:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersSvc[0];
                m_registers[14] = &m_registersSvc[1];
                m_spsr          = &m_spsrSvc;
                break;
            case 0x17:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersAbt[0];
                m_registers[14] = &m_registersAbt[1];
                m_spsr          = &m_spsrAbt;
                break;
            case 0x1B:
                m_registers[8]  = &m_registersUsr[8];
                m_registers[9]  = &m_registersUsr[9];
                m_registers[10] = &m_registersUsr[10];
                m_registers[11] = &m_registersUsr[11];
                m_registers[12] = &m_registersUsr[12];
                m_registers[13] = &m_registersUnd[0];
                m_registers[14] = &m_registersUnd[1];
                m_spsr          = &m_spsrUnd;
                break;
            default:
                LOG_VERBOSE("Unknown ARM%d CPU mode: 0x%X\n", ((cpu == 0) ? 9 : 7), cpsr & 0x1F);
                break;
        }

        // Le scheduler n'étant pas sérialisé, on reprogramme l'IRQ en attente
        // s'il y en avait une au moment de la sauvegarde (même condition que
        // dans SetCpsr()/SendInterrupt()).
        if (m_ime && (m_ie & m_irf) && !(m_cpsr & BIT(7)))
            m_core->Schedule(Task(&m_interruptTask, (m_cpu == 1 && !m_core->IsGbaMode()) ? 2 : 1));
    }
}

int CPU::HandleReserved(uint32_t opcode) {
    if ((opcode & 0x0E000000) == 0x0A000000)
        return Blx(opcode);
    if (m_bios && opcode == 0xFF000000)
        return FinishHleIrq();

    // A branch to one of the DLDI sentinel addresses (see dldi.h) — HLE the
    // driver call, then return to the caller as a real BX LR would.
    if (m_core->dldi.IsPatched()) {
        uint32_t **r = m_registers;
        switch (opcode) {
            case DLDI_START:  *r[0] = uint32_t(m_core->dldi.Startup()); break;
            case DLDI_INSERT: *r[0] = uint32_t(m_core->dldi.IsInserted()); break;
            case DLDI_READ:   *r[0] = uint32_t(m_core->dldi.ReadSectors(m_cpu, *r[0], *r[1], *r[2])); break;
            case DLDI_WRITE:  *r[0] = uint32_t(m_core->dldi.WriteSectors(m_cpu, *r[0], *r[1], *r[2])); break;
            case DLDI_CLEAR:  *r[0] = uint32_t(m_core->dldi.ClearStatus()); break;
            case DLDI_STOP:   *r[0] = uint32_t(m_core->dldi.Shutdown()); break;
            default: return UnkArm(opcode);
        }
        return Bx(14);
    }

    return UnkArm(opcode);
}

int CPU::HandleHleIrq() {
    SetCpsr((m_cpsr & ~0x3Fu) | BIT(7) | 0x12, true);
    *m_registers[14] = *m_registers[15] + ((*m_spsr & BIT(5)) ? 2 : 0);
    StmdbW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *m_registers[14] = m_cpu ? 0x00000000 : 0xFFFF0000;
    *m_registers[15] = m_core->memory.Read<uint32_t>(m_cpu, m_cpu ? 0x3FFFFFC : (m_core->cp15.GetDtcmAddr() + 0x3FFC));
    FlushPipeline();
    return 3;
}

int CPU::FinishHleIrq() {
    if (m_bios->ShouldCheck())
        m_bios->CheckWaitFlags(m_cpu);
    LdmiaW((13 << 16) | BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(12) | BIT(14));
    *m_registers[15] = *m_registers[14] - 4;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

int CPU::UnkArm([[maybe_unused]] uint32_t opcode) {
    LOG_VERBOSE("Unknown ARM%d ARM opcode: 0x%X\n", ((cpu == 0) ? 9 : 7), opcode);
    return 1;
}

int CPU::UnkThumb([[maybe_unused]] uint16_t opcode) {
    LOG_VERBOSE("Unknown ARM%d THUMB opcode: 0x%X\n", ((cpu == 0) ? 9 : 7), opcode);
    return 1;
}

void CPU::WriteIme(uint8_t value) {
    m_ime = value & 0x01;
    if (m_ime && (m_ie & m_irf) && !(m_cpsr & BIT(7)))
        m_core->Schedule(Task(&m_interruptTask, (m_cpu == 1 && !m_core->IsGbaMode()) ? 2 : 1));
}

void CPU::WriteIe(uint32_t mask, uint32_t value) {
    mask &= ((m_cpu == 0) ? 0x003F3F7Fu : (m_core->IsGbaMode() ? 0x3FFFu : 0x01FF3FFFu));
    m_ie = (m_ie & ~mask) | (value & mask);
    if (m_ime && (m_ie & m_irf) && !(m_cpsr & BIT(7)))
        m_core->Schedule(Task(&m_interruptTask, (m_cpu == 1 && !m_core->IsGbaMode()) ? 2 : 1));
}

void CPU::WriteIrf(uint32_t mask, uint32_t value) {
    m_irf &= ~(value & mask);
}

void CPU::WritePostFlg(uint8_t value) {
    m_postFlg |= value & 0x01;
    if (m_cpu == 0)
        m_postFlg = static_cast<uint8_t>((m_postFlg & ~0x02) | (value & 0x02));
}

} // namespace consoles::arm

