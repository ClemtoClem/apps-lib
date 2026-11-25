#include "consoles/arm/cpu/cpu.hpp"
#include "consoles/arm/core.hpp"

namespace consoles::arm {


int CPU::Bx(uint32_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0000000F];
    if (op0 & BIT(0))
        m_cpsr |= BIT(5);
    *m_registers[15] = op0;
    FlushPipeline();
    return 3;
}

int CPU::BlxReg(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t op0 = *m_registers[opcode & 0x0000000F];
    if (op0 & BIT(0))
        m_cpsr |= BIT(5);
    *m_registers[14] = *m_registers[15] - 4;
    *m_registers[15] = op0;
    FlushPipeline();
    return 3;
}

int CPU::B(uint32_t opcode) {
    uint32_t op0 = ((opcode & BIT(23)) ? 0xFC000000 : 0) | ((opcode & 0x00FFFFFF) << 2);
    *m_registers[15] += op0;
    FlushPipeline();
    return 3;
}

int CPU::Bl(uint32_t opcode) {
    uint32_t op0   = ((opcode & BIT(23)) ? 0xFC000000 : 0) | ((opcode & 0x00FFFFFF) << 2);
    *m_registers[14] = *m_registers[15] - 4;
    *m_registers[15] += op0;
    FlushPipeline();
    return 3;
}

int CPU::Blx(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t op0 = ((opcode & BIT(23)) ? 0xFC000000 : 0) | ((opcode & 0x00FFFFFF) << 2) | ((opcode & BIT(24)) >> 23);
    m_cpsr |= BIT(5);
    *m_registers[14] = *m_registers[15] - 4;
    *m_registers[15] += op0;
    FlushPipeline();
    return 3;
}

int CPU::Swi(uint32_t /*opcode*/) {
    *m_registers[15] -= 4;
    return Exception(0x08);
}

int CPU::BxRegT(uint16_t opcode) {
    uint32_t op0 = *m_registers[(opcode & 0x0078) >> 3];
    if (!(op0 & BIT(0)))
        m_cpsr &= ~BIT(5);
    *m_registers[15] = op0;
    FlushPipeline();
    return 3;
}

int CPU::BlxRegT(uint16_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t op0 = *m_registers[(opcode & 0x0078) >> 3];
    if (!(op0 & BIT(0)))
        m_cpsr &= ~BIT(5);
    *m_registers[14] = *m_registers[15] - 1;
    *m_registers[15] = op0;
    FlushPipeline();
    return 3;
}

int CPU::BeqT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (m_cpsr & BIT(30)) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BneT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(m_cpsr & BIT(30))) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BcsT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (m_cpsr & BIT(29)) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BccT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(m_cpsr & BIT(29))) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BmiT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (m_cpsr & BIT(31)) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BplT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(m_cpsr & BIT(31))) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BvsT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (m_cpsr & BIT(28)) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BvcT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(m_cpsr & BIT(28))) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BhiT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((m_cpsr & BIT(29)) && !(m_cpsr & BIT(30))) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BlsT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(m_cpsr & BIT(29)) || (m_cpsr & BIT(30))) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BgeT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((m_cpsr & BIT(31)) == (m_cpsr & BIT(28)) << 3) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BltT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((m_cpsr & BIT(31)) != (m_cpsr & BIT(28)) << 3) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BgtT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if (!(m_cpsr & BIT(30)) && (m_cpsr & BIT(31)) == (m_cpsr & BIT(28)) << 3) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BleT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(7)) ? 0xFFFFFE00 : 0) | ((opcode & 0x00FF) << 1);
    if ((m_cpsr & BIT(30)) || (m_cpsr & BIT(31)) != (m_cpsr & BIT(28)) << 3) {
        *m_registers[15] += op0;
        FlushPipeline();
        return 3;
    }
    return 1;
}

int CPU::BT(uint16_t opcode) {
    uint32_t op0 = ((opcode & BIT(10)) ? 0xFFFFF000 : 0) | ((opcode & 0x07FF) << 1);
    *m_registers[15] += op0;
    FlushPipeline();
    return 3;
}

int CPU::BlSetupT(uint16_t opcode) {
    uint32_t op0   = ((opcode & BIT(10)) ? 0xFFFFF000 : 0) | ((opcode & 0x07FF) << 1);
    *m_registers[14] = *m_registers[15] + (op0 << 11);
    return 1;
}

int CPU::BlOffT(uint16_t opcode) {
    uint32_t op0   = (opcode & 0x07FF) << 1;
    uint32_t ret   = *m_registers[15] - 1;
    *m_registers[15] = *m_registers[14] + op0;
    *m_registers[14] = ret;
    FlushPipeline();
    return 3;
}

int CPU::BlxOffT(uint16_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t op0 = (opcode & 0x07FF) << 1;
    m_cpsr &= ~BIT(5);
    uint32_t ret   = *m_registers[15] - 1;
    *m_registers[15] = *m_registers[14] + op0;
    *m_registers[14] = ret;
    FlushPipeline();
    return 3;
}

int CPU::SwiT(uint16_t /*opcode*/) {
    *m_registers[15] -= 4;
    return Exception(0x08);
}

} // namespace consoles::arm

