#include "consoles/arm/cpu/cpu.hpp"
#include "consoles/arm/core.hpp"

namespace consoles::arm {



FORCE_INLINE uint32_t CPU::Ip(uint32_t opcode) {
    return opcode & 0x00000FFF;
}

FORCE_INLINE uint32_t CPU::IpH(uint32_t opcode) {
    return ((opcode & 0x00000F00) >> 4) | (opcode & 0x0000000F);
}

FORCE_INLINE uint32_t CPU::Rp(uint32_t opcode) {
    return *m_registers[opcode & 0x0000000F];
}

FORCE_INLINE uint32_t CPU::Rpll(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return value << shift;
}

FORCE_INLINE uint32_t CPU::Rplr(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::Rpar(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? static_cast<unsigned int>(((int32_t)value >> shift)) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::Rprr(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? ((value << (32 - shift)) | (value >> shift)) : (((m_cpsr & BIT(29)) << 2) | (value >> 1));
}

FORCE_INLINE int CPU::LdrsbOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = (uint32_t)(int8_t)m_core->memory.Read<uint8_t>(m_cpu, op1 + op2);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::LdrshOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = (uint32_t)(int16_t)m_core->memory.Read<uint16_t>(m_cpu, op1 += op2);
    if (m_cpu == 1 && (op1 & 1))
        *op0 = static_cast<uint32_t>((int16_t)*op0 >> 8);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::LdrbOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = m_core->memory.Read<uint8_t>(m_cpu, op1 + op2);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    if (m_cpu == 0 && (*op0 & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrbOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint8_t>(m_cpu, op1 + op2, static_cast<unsigned char>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrhOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = m_core->memory.Read<uint16_t>(m_cpu, op1 += op2);
    if (m_cpu == 1 && (op1 & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrhOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint16_t>(m_cpu, op1 + op2, static_cast<unsigned short>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrOf(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = m_core->memory.Read<uint32_t>(m_cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    if (m_cpu == 0 && (*op0 & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrOf(uint32_t opcode, uint32_t op2) {
    uint32_t op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint32_t>(m_cpu, op1 + op2, op0);
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrdOf(uint32_t opcode, uint32_t op2) {
    if (m_cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t op1        = *m_registers[(opcode & 0x000F0000) >> 16];
    *m_registers[op0]     = m_core->memory.Read<uint32_t>(m_cpu, op1 + op2);
    *m_registers[op0 + 1] = m_core->memory.Read<uint32_t>(m_cpu, op1 + op2 + 4);
    return 2;
}

FORCE_INLINE int CPU::StrdOf(uint32_t opcode, uint32_t op2) {
    if (m_cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint32_t>(m_cpu, op1 + op2, *m_registers[op0]);
    m_core->memory.Write<uint32_t>(m_cpu, op1 + op2 + 4, *m_registers[op0 + 1]);
    return 2;
}

FORCE_INLINE int CPU::LdrsbPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    *op0 = (uint32_t)(int8_t)m_core->memory.Read<uint8_t>(m_cpu, *op1);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::LdrshPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op1 += op2;
    *op0 = (uint32_t)(int16_t)m_core->memory.Read<uint16_t>(m_cpu, address = *op1);
    if (m_cpu == 1 && (address & 1))
        *op0 = static_cast<uint32_t>((int16_t)*op0 >> 8);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::LdrbPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    *op0 = m_core->memory.Read<uint8_t>(m_cpu, *op1);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    if (m_cpu == 0 && (*op0 & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrbPr(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    m_core->memory.Write<uint8_t>(m_cpu, *op1, static_cast<unsigned char>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrhPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op1 += op2;
    *op0 = m_core->memory.Read<uint16_t>(m_cpu, address = *op1);
    if (m_cpu == 1 && (address & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrhPr(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    m_core->memory.Write<uint16_t>(m_cpu, *op1, static_cast<unsigned short>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrPr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op1 += op2;
    *op0 = m_core->memory.Read<uint32_t>(m_cpu, address = *op1);
    if (address & 3) {
        int shift = (address & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    if (m_cpu == 0 && (*op0 & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrPr(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    m_core->memory.Write<uint32_t>(m_cpu, *op1, op0);
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrdPr(uint32_t opcode, uint32_t op2) {
    if (m_cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    *m_registers[op0]     = m_core->memory.Read<uint32_t>(m_cpu, *op1);
    *m_registers[op0 + 1] = m_core->memory.Read<uint32_t>(m_cpu, *op1 + 4);
    return 2;
}

FORCE_INLINE int CPU::StrdPr(uint32_t opcode, uint32_t op2) {
    if (m_cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op1 += op2;
    m_core->memory.Write<uint32_t>(m_cpu, *op1, *m_registers[op0]);
    m_core->memory.Write<uint32_t>(m_cpu, *op1 + 4, *m_registers[op0 + 1]);
    return 2;
}

FORCE_INLINE int CPU::LdrsbPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = (uint32_t)(int8_t)m_core->memory.Read<uint8_t>(m_cpu, *op1);
    if (op0 != op1)
        *op1 += op2;
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::LdrshPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op0 = (uint32_t)(int16_t)m_core->memory.Read<uint16_t>(m_cpu, address = *op1);
    if (op0 != op1)
        *op1 += op2;
    if (m_cpu == 1 && (address & 1))
        *op0 = static_cast<uint32_t>((int16_t)*op0 >> 8);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::LdrbPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = m_core->memory.Read<uint8_t>(m_cpu, *op1);
    if (op0 != op1)
        *op1 += op2;
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    if (m_cpu == 0 && (*op0 & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrbPt(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint8_t>(m_cpu, *op1, static_cast<unsigned char>(op0));
    *op1 += op2;
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrhPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op0 = m_core->memory.Read<uint16_t>(m_cpu, address = *op1);
    *op1 += op2;
    if (m_cpu == 1 && (address & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrhPt(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint16_t>(m_cpu, *op1, static_cast<unsigned short>(op0));
    *op1 += op2;
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrPt(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  address;
    *op0 = m_core->memory.Read<uint32_t>(m_cpu, address = *op1);
    if (address & 3) {
        int shift = (address & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    if (op0 != op1)
        *op1 += op2;
    if (op0 != m_registers[15])
        return ((m_cpu == 0) ? 1 : 3);
    if (m_cpu == 0 && (*op0 & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return 5;
}

FORCE_INLINE int CPU::StrPt(uint32_t opcode, uint32_t op2) {
    uint32_t  op0 = *m_registers[(opcode & 0x0000F000) >> 12] + (((opcode & 0x0000F000) == 0x0000F000) ? 4 : 0);
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint32_t>(m_cpu, *op1, op0);
    *op1 += op2;
    return ((m_cpu == 0) ? 1 : 2);
}

FORCE_INLINE int CPU::LdrdPt(uint32_t opcode, uint32_t op2) {
    if (m_cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1       = m_registers[(opcode & 0x000F0000) >> 16];
    *m_registers[op0]     = m_core->memory.Read<uint32_t>(m_cpu, *op1);
    *m_registers[op0 + 1] = m_core->memory.Read<uint32_t>(m_cpu, *op1 + 4);
    *op1 += op2;
    return 2;
}

FORCE_INLINE int CPU::StrdPt(uint32_t opcode, uint32_t op2) {
    if (m_cpu == 1)
        return 1;
    uint8_t op0 = (opcode & 0x0000F000) >> 12;
    if (op0 == 15)
        return 1;
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    m_core->memory.Write<uint32_t>(m_cpu, *op1, *m_registers[op0]);
    m_core->memory.Write<uint32_t>(m_cpu, *op1 + 4, *m_registers[op0 + 1]);
    *op1 += op2;
    return 2;
}

int CPU::LdrsbOfrm(uint32_t opcode) {
    return LdrsbOf(opcode, -Rp(opcode));
}

int CPU::LdrsbOfim(uint32_t opcode) {
    return LdrsbOf(opcode, -IpH(opcode));
}

int CPU::LdrsbOfrp(uint32_t opcode) {
    return LdrsbOf(opcode, Rp(opcode));
}

int CPU::LdrsbOfip(uint32_t opcode) {
    return LdrsbOf(opcode, IpH(opcode));
}

int CPU::LdrsbPrrm(uint32_t opcode) {
    return LdrsbPr(opcode, -Rp(opcode));
}

int CPU::LdrsbPrim(uint32_t opcode) {
    return LdrsbPr(opcode, -IpH(opcode));
}

int CPU::LdrsbPrrp(uint32_t opcode) {
    return LdrsbPr(opcode, Rp(opcode));
}

int CPU::LdrsbPrip(uint32_t opcode) {
    return LdrsbPr(opcode, IpH(opcode));
}

int CPU::LdrsbPtrm(uint32_t opcode) {
    return LdrsbPt(opcode, -Rp(opcode));
}

int CPU::LdrsbPtim(uint32_t opcode) {
    return LdrsbPt(opcode, -IpH(opcode));
}

int CPU::LdrsbPtrp(uint32_t opcode) {
    return LdrsbPt(opcode, Rp(opcode));
}

int CPU::LdrsbPtip(uint32_t opcode) {
    return LdrsbPt(opcode, IpH(opcode));
}

int CPU::LdrshOfrm(uint32_t opcode) {
    return LdrshOf(opcode, -Rp(opcode));
}

int CPU::LdrshOfim(uint32_t opcode) {
    return LdrshOf(opcode, -IpH(opcode));
}

int CPU::LdrshOfrp(uint32_t opcode) {
    return LdrshOf(opcode, Rp(opcode));
}

int CPU::LdrshOfip(uint32_t opcode) {
    return LdrshOf(opcode, IpH(opcode));
}

int CPU::LdrshPrrm(uint32_t opcode) {
    return LdrshPr(opcode, -Rp(opcode));
}

int CPU::LdrshPrim(uint32_t opcode) {
    return LdrshPr(opcode, -IpH(opcode));
}

int CPU::LdrshPrrp(uint32_t opcode) {
    return LdrshPr(opcode, Rp(opcode));
}

int CPU::LdrshPrip(uint32_t opcode) {
    return LdrshPr(opcode, IpH(opcode));
}

int CPU::LdrshPtrm(uint32_t opcode) {
    return LdrshPt(opcode, -Rp(opcode));
}

int CPU::LdrshPtim(uint32_t opcode) {
    return LdrshPt(opcode, -IpH(opcode));
}

int CPU::LdrshPtrp(uint32_t opcode) {
    return LdrshPt(opcode, Rp(opcode));
}

int CPU::LdrshPtip(uint32_t opcode) {
    return LdrshPt(opcode, IpH(opcode));
}

int CPU::LdrbOfim(uint32_t opcode) {
    return LdrbOf(opcode, -Ip(opcode));
}

int CPU::LdrbOfip(uint32_t opcode) {
    return LdrbOf(opcode, Ip(opcode));
}

int CPU::LdrbOfrmll(uint32_t opcode) {
    return LdrbOf(opcode, -Rpll(opcode));
}

int CPU::LdrbOfrmlr(uint32_t opcode) {
    return LdrbOf(opcode, -Rplr(opcode));
}

int CPU::LdrbOfrmar(uint32_t opcode) {
    return LdrbOf(opcode, -Rpar(opcode));
}

int CPU::LdrbOfrmrr(uint32_t opcode) {
    return LdrbOf(opcode, -Rprr(opcode));
}

int CPU::LdrbOfrpll(uint32_t opcode) {
    return LdrbOf(opcode, Rpll(opcode));
}

int CPU::LdrbOfrplr(uint32_t opcode) {
    return LdrbOf(opcode, Rplr(opcode));
}

int CPU::LdrbOfrpar(uint32_t opcode) {
    return LdrbOf(opcode, Rpar(opcode));
}

int CPU::LdrbOfrprr(uint32_t opcode) {
    return LdrbOf(opcode, Rprr(opcode));
}

int CPU::LdrbPrim(uint32_t opcode) {
    return LdrbPr(opcode, -Ip(opcode));
}

int CPU::LdrbPrip(uint32_t opcode) {
    return LdrbPr(opcode, Ip(opcode));
}

int CPU::LdrbPrrmll(uint32_t opcode) {
    return LdrbPr(opcode, -Rpll(opcode));
}

int CPU::LdrbPrrmlr(uint32_t opcode) {
    return LdrbPr(opcode, -Rplr(opcode));
}

int CPU::LdrbPrrmar(uint32_t opcode) {
    return LdrbPr(opcode, -Rpar(opcode));
}

int CPU::LdrbPrrmrr(uint32_t opcode) {
    return LdrbPr(opcode, -Rprr(opcode));
}

int CPU::LdrbPrrpll(uint32_t opcode) {
    return LdrbPr(opcode, Rpll(opcode));
}

int CPU::LdrbPrrplr(uint32_t opcode) {
    return LdrbPr(opcode, Rplr(opcode));
}

int CPU::LdrbPrrpar(uint32_t opcode) {
    return LdrbPr(opcode, Rpar(opcode));
}

int CPU::LdrbPrrprr(uint32_t opcode) {
    return LdrbPr(opcode, Rprr(opcode));
}

int CPU::LdrbPtim(uint32_t opcode) {
    return LdrbPt(opcode, -Ip(opcode));
}

int CPU::LdrbPtip(uint32_t opcode) {
    return LdrbPt(opcode, Ip(opcode));
}

int CPU::LdrbPtrmll(uint32_t opcode) {
    return LdrbPt(opcode, -Rpll(opcode));
}

int CPU::LdrbPtrmlr(uint32_t opcode) {
    return LdrbPt(opcode, -Rplr(opcode));
}

int CPU::LdrbPtrmar(uint32_t opcode) {
    return LdrbPt(opcode, -Rpar(opcode));
}

int CPU::LdrbPtrmrr(uint32_t opcode) {
    return LdrbPt(opcode, -Rprr(opcode));
}

int CPU::LdrbPtrpll(uint32_t opcode) {
    return LdrbPt(opcode, Rpll(opcode));
}

int CPU::LdrbPtrplr(uint32_t opcode) {
    return LdrbPt(opcode, Rplr(opcode));
}

int CPU::LdrbPtrpar(uint32_t opcode) {
    return LdrbPt(opcode, Rpar(opcode));
}

int CPU::LdrbPtrprr(uint32_t opcode) {
    return LdrbPt(opcode, Rprr(opcode));
}

int CPU::StrbOfim(uint32_t opcode) {
    return StrbOf(opcode, -Ip(opcode));
}

int CPU::StrbOfip(uint32_t opcode) {
    return StrbOf(opcode, Ip(opcode));
}

int CPU::StrbOfrmll(uint32_t opcode) {
    return StrbOf(opcode, -Rpll(opcode));
}

int CPU::StrbOfrmlr(uint32_t opcode) {
    return StrbOf(opcode, -Rplr(opcode));
}

int CPU::StrbOfrmar(uint32_t opcode) {
    return StrbOf(opcode, -Rpar(opcode));
}

int CPU::StrbOfrmrr(uint32_t opcode) {
    return StrbOf(opcode, -Rprr(opcode));
}

int CPU::StrbOfrpll(uint32_t opcode) {
    return StrbOf(opcode, Rpll(opcode));
}

int CPU::StrbOfrplr(uint32_t opcode) {
    return StrbOf(opcode, Rplr(opcode));
}

int CPU::StrbOfrpar(uint32_t opcode) {
    return StrbOf(opcode, Rpar(opcode));
}

int CPU::StrbOfrprr(uint32_t opcode) {
    return StrbOf(opcode, Rprr(opcode));
}

int CPU::StrbPrim(uint32_t opcode) {
    return StrbPr(opcode, -Ip(opcode));
}

int CPU::StrbPrip(uint32_t opcode) {
    return StrbPr(opcode, Ip(opcode));
}

int CPU::StrbPrrmll(uint32_t opcode) {
    return StrbPr(opcode, -Rpll(opcode));
}

int CPU::StrbPrrmlr(uint32_t opcode) {
    return StrbPr(opcode, -Rplr(opcode));
}

int CPU::StrbPrrmar(uint32_t opcode) {
    return StrbPr(opcode, -Rpar(opcode));
}

int CPU::StrbPrrmrr(uint32_t opcode) {
    return StrbPr(opcode, -Rprr(opcode));
}

int CPU::StrbPrrpll(uint32_t opcode) {
    return StrbPr(opcode, Rpll(opcode));
}

int CPU::StrbPrrplr(uint32_t opcode) {
    return StrbPr(opcode, Rplr(opcode));
}

int CPU::StrbPrrpar(uint32_t opcode) {
    return StrbPr(opcode, Rpar(opcode));
}

int CPU::StrbPrrprr(uint32_t opcode) {
    return StrbPr(opcode, Rprr(opcode));
}

int CPU::StrbPtim(uint32_t opcode) {
    return StrbPt(opcode, -Ip(opcode));
}

int CPU::StrbPtip(uint32_t opcode) {
    return StrbPt(opcode, Ip(opcode));
}

int CPU::StrbPtrmll(uint32_t opcode) {
    return StrbPt(opcode, -Rpll(opcode));
}

int CPU::StrbPtrmlr(uint32_t opcode) {
    return StrbPt(opcode, -Rplr(opcode));
}

int CPU::StrbPtrmar(uint32_t opcode) {
    return StrbPt(opcode, -Rpar(opcode));
}

int CPU::StrbPtrmrr(uint32_t opcode) {
    return StrbPt(opcode, -Rprr(opcode));
}

int CPU::StrbPtrpll(uint32_t opcode) {
    return StrbPt(opcode, Rpll(opcode));
}

int CPU::StrbPtrplr(uint32_t opcode) {
    return StrbPt(opcode, Rplr(opcode));
}

int CPU::StrbPtrpar(uint32_t opcode) {
    return StrbPt(opcode, Rpar(opcode));
}

int CPU::StrbPtrprr(uint32_t opcode) {
    return StrbPt(opcode, Rprr(opcode));
}

int CPU::LdrhOfrm(uint32_t opcode) {
    return LdrhOf(opcode, -Rp(opcode));
}

int CPU::LdrhOfim(uint32_t opcode) {
    return LdrhOf(opcode, -IpH(opcode));
}

int CPU::LdrhOfrp(uint32_t opcode) {
    return LdrhOf(opcode, Rp(opcode));
}

int CPU::LdrhOfip(uint32_t opcode) {
    return LdrhOf(opcode, IpH(opcode));
}

int CPU::LdrhPrrm(uint32_t opcode) {
    return LdrhPr(opcode, -Rp(opcode));
}

int CPU::LdrhPrim(uint32_t opcode) {
    return LdrhPr(opcode, -IpH(opcode));
}

int CPU::LdrhPrrp(uint32_t opcode) {
    return LdrhPr(opcode, Rp(opcode));
}

int CPU::LdrhPrip(uint32_t opcode) {
    return LdrhPr(opcode, IpH(opcode));
}

int CPU::LdrhPtrm(uint32_t opcode) {
    return LdrhPt(opcode, -Rp(opcode));
}

int CPU::LdrhPtim(uint32_t opcode) {
    return LdrhPt(opcode, -IpH(opcode));
}

int CPU::LdrhPtrp(uint32_t opcode) {
    return LdrhPt(opcode, Rp(opcode));
}

int CPU::LdrhPtip(uint32_t opcode) {
    return LdrhPt(opcode, IpH(opcode));
}

int CPU::StrhOfrm(uint32_t opcode) {
    return StrhOf(opcode, -Rp(opcode));
}

int CPU::StrhOfim(uint32_t opcode) {
    return StrhOf(opcode, -IpH(opcode));
}

int CPU::StrhOfrp(uint32_t opcode) {
    return StrhOf(opcode, Rp(opcode));
}

int CPU::StrhOfip(uint32_t opcode) {
    return StrhOf(opcode, IpH(opcode));
}

int CPU::StrhPrrm(uint32_t opcode) {
    return StrhPr(opcode, -Rp(opcode));
}

int CPU::StrhPrim(uint32_t opcode) {
    return StrhPr(opcode, -IpH(opcode));
}

int CPU::StrhPrrp(uint32_t opcode) {
    return StrhPr(opcode, Rp(opcode));
}

int CPU::StrhPrip(uint32_t opcode) {
    return StrhPr(opcode, IpH(opcode));
}

int CPU::StrhPtrm(uint32_t opcode) {
    return StrhPt(opcode, -Rp(opcode));
}

int CPU::StrhPtim(uint32_t opcode) {
    return StrhPt(opcode, -IpH(opcode));
}

int CPU::StrhPtrp(uint32_t opcode) {
    return StrhPt(opcode, Rp(opcode));
}

int CPU::StrhPtip(uint32_t opcode) {
    return StrhPt(opcode, IpH(opcode));
}

int CPU::LdrOfim(uint32_t opcode) {
    return LdrOf(opcode, -Ip(opcode));
}

int CPU::LdrOfip(uint32_t opcode) {
    return LdrOf(opcode, Ip(opcode));
}

int CPU::LdrOfrmll(uint32_t opcode) {
    return LdrOf(opcode, -Rpll(opcode));
}

int CPU::LdrOfrmlr(uint32_t opcode) {
    return LdrOf(opcode, -Rplr(opcode));
}

int CPU::LdrOfrmar(uint32_t opcode) {
    return LdrOf(opcode, -Rpar(opcode));
}

int CPU::LdrOfrmrr(uint32_t opcode) {
    return LdrOf(opcode, -Rprr(opcode));
}

int CPU::LdrOfrpll(uint32_t opcode) {
    return LdrOf(opcode, Rpll(opcode));
}

int CPU::LdrOfrplr(uint32_t opcode) {
    return LdrOf(opcode, Rplr(opcode));
}

int CPU::LdrOfrpar(uint32_t opcode) {
    return LdrOf(opcode, Rpar(opcode));
}

int CPU::LdrOfrprr(uint32_t opcode) {
    return LdrOf(opcode, Rprr(opcode));
}

int CPU::LdrPrim(uint32_t opcode) {
    return LdrPr(opcode, -Ip(opcode));
}

int CPU::LdrPrip(uint32_t opcode) {
    return LdrPr(opcode, Ip(opcode));
}

int CPU::LdrPrrmll(uint32_t opcode) {
    return LdrPr(opcode, -Rpll(opcode));
}

int CPU::LdrPrrmlr(uint32_t opcode) {
    return LdrPr(opcode, -Rplr(opcode));
}

int CPU::LdrPrrmar(uint32_t opcode) {
    return LdrPr(opcode, -Rpar(opcode));
}

int CPU::LdrPrrmrr(uint32_t opcode) {
    return LdrPr(opcode, -Rprr(opcode));
}

int CPU::LdrPrrpll(uint32_t opcode) {
    return LdrPr(opcode, Rpll(opcode));
}

int CPU::LdrPrrplr(uint32_t opcode) {
    return LdrPr(opcode, Rplr(opcode));
}

int CPU::LdrPrrpar(uint32_t opcode) {
    return LdrPr(opcode, Rpar(opcode));
}

int CPU::LdrPrrprr(uint32_t opcode) {
    return LdrPr(opcode, Rprr(opcode));
}

int CPU::LdrPtim(uint32_t opcode) {
    return LdrPt(opcode, -Ip(opcode));
}

int CPU::LdrPtip(uint32_t opcode) {
    return LdrPt(opcode, Ip(opcode));
}

int CPU::LdrPtrmll(uint32_t opcode) {
    return LdrPt(opcode, -Rpll(opcode));
}

int CPU::LdrPtrmlr(uint32_t opcode) {
    return LdrPt(opcode, -Rplr(opcode));
}

int CPU::LdrPtrmar(uint32_t opcode) {
    return LdrPt(opcode, -Rpar(opcode));
}

int CPU::LdrPtrmrr(uint32_t opcode) {
    return LdrPt(opcode, -Rprr(opcode));
}

int CPU::LdrPtrpll(uint32_t opcode) {
    return LdrPt(opcode, Rpll(opcode));
}

int CPU::LdrPtrplr(uint32_t opcode) {
    return LdrPt(opcode, Rplr(opcode));
}

int CPU::LdrPtrpar(uint32_t opcode) {
    return LdrPt(opcode, Rpar(opcode));
}

int CPU::LdrPtrprr(uint32_t opcode) {
    return LdrPt(opcode, Rprr(opcode));
}

int CPU::StrOfim(uint32_t opcode) {
    return StrOf(opcode, -Ip(opcode));
}

int CPU::StrOfip(uint32_t opcode) {
    return StrOf(opcode, Ip(opcode));
}

int CPU::StrOfrmll(uint32_t opcode) {
    return StrOf(opcode, -Rpll(opcode));
}

int CPU::StrOfrmlr(uint32_t opcode) {
    return StrOf(opcode, -Rplr(opcode));
}

int CPU::StrOfrmar(uint32_t opcode) {
    return StrOf(opcode, -Rpar(opcode));
}

int CPU::StrOfrmrr(uint32_t opcode) {
    return StrOf(opcode, -Rprr(opcode));
}

int CPU::StrOfrpll(uint32_t opcode) {
    return StrOf(opcode, Rpll(opcode));
}

int CPU::StrOfrplr(uint32_t opcode) {
    return StrOf(opcode, Rplr(opcode));
}

int CPU::StrOfrpar(uint32_t opcode) {
    return StrOf(opcode, Rpar(opcode));
}

int CPU::StrOfrprr(uint32_t opcode) {
    return StrOf(opcode, Rprr(opcode));
}

int CPU::StrPrim(uint32_t opcode) {
    return StrPr(opcode, -Ip(opcode));
}

int CPU::StrPrip(uint32_t opcode) {
    return StrPr(opcode, Ip(opcode));
}

int CPU::StrPrrmll(uint32_t opcode) {
    return StrPr(opcode, -Rpll(opcode));
}

int CPU::StrPrrmlr(uint32_t opcode) {
    return StrPr(opcode, -Rplr(opcode));
}

int CPU::StrPrrmar(uint32_t opcode) {
    return StrPr(opcode, -Rpar(opcode));
}

int CPU::StrPrrmrr(uint32_t opcode) {
    return StrPr(opcode, -Rprr(opcode));
}

int CPU::StrPrrpll(uint32_t opcode) {
    return StrPr(opcode, Rpll(opcode));
}

int CPU::StrPrrplr(uint32_t opcode) {
    return StrPr(opcode, Rplr(opcode));
}

int CPU::StrPrrpar(uint32_t opcode) {
    return StrPr(opcode, Rpar(opcode));
}

int CPU::StrPrrprr(uint32_t opcode) {
    return StrPr(opcode, Rprr(opcode));
}

int CPU::StrPtim(uint32_t opcode) {
    return StrPt(opcode, -Ip(opcode));
}

int CPU::StrPtip(uint32_t opcode) {
    return StrPt(opcode, Ip(opcode));
}

int CPU::StrPtrmll(uint32_t opcode) {
    return StrPt(opcode, -Rpll(opcode));
}

int CPU::StrPtrmlr(uint32_t opcode) {
    return StrPt(opcode, -Rplr(opcode));
}

int CPU::StrPtrmar(uint32_t opcode) {
    return StrPt(opcode, -Rpar(opcode));
}

int CPU::StrPtrmrr(uint32_t opcode) {
    return StrPt(opcode, -Rprr(opcode));
}

int CPU::StrPtrpll(uint32_t opcode) {
    return StrPt(opcode, Rpll(opcode));
}

int CPU::StrPtrplr(uint32_t opcode) {
    return StrPt(opcode, Rplr(opcode));
}

int CPU::StrPtrpar(uint32_t opcode) {
    return StrPt(opcode, Rpar(opcode));
}

int CPU::StrPtrprr(uint32_t opcode) {
    return StrPt(opcode, Rprr(opcode));
}

int CPU::LdrdOfrm(uint32_t opcode) {
    return LdrdOf(opcode, -Rp(opcode));
}

int CPU::LdrdOfim(uint32_t opcode) {
    return LdrdOf(opcode, -IpH(opcode));
}

int CPU::LdrdOfrp(uint32_t opcode) {
    return LdrdOf(opcode, Rp(opcode));
}

int CPU::LdrdOfip(uint32_t opcode) {
    return LdrdOf(opcode, IpH(opcode));
}

int CPU::LdrdPrrm(uint32_t opcode) {
    return LdrdPr(opcode, -Rp(opcode));
}

int CPU::LdrdPrim(uint32_t opcode) {
    return LdrdPr(opcode, -IpH(opcode));
}

int CPU::LdrdPrrp(uint32_t opcode) {
    return LdrdPr(opcode, Rp(opcode));
}

int CPU::LdrdPrip(uint32_t opcode) {
    return LdrdPr(opcode, IpH(opcode));
}

int CPU::LdrdPtrm(uint32_t opcode) {
    return LdrdPt(opcode, -Rp(opcode));
}

int CPU::LdrdPtim(uint32_t opcode) {
    return LdrdPt(opcode, -IpH(opcode));
}

int CPU::LdrdPtrp(uint32_t opcode) {
    return LdrdPt(opcode, Rp(opcode));
}

int CPU::LdrdPtip(uint32_t opcode) {
    return LdrdPt(opcode, IpH(opcode));
}

int CPU::StrdOfrm(uint32_t opcode) {
    return StrdOf(opcode, -Rp(opcode));
}

int CPU::StrdOfim(uint32_t opcode) {
    return StrdOf(opcode, -IpH(opcode));
}

int CPU::StrdOfrp(uint32_t opcode) {
    return StrdOf(opcode, Rp(opcode));
}

int CPU::StrdOfip(uint32_t opcode) {
    return StrdOf(opcode, IpH(opcode));
}

int CPU::StrdPrrm(uint32_t opcode) {
    return StrdPr(opcode, -Rp(opcode));
}

int CPU::StrdPrim(uint32_t opcode) {
    return StrdPr(opcode, -IpH(opcode));
}

int CPU::StrdPrrp(uint32_t opcode) {
    return StrdPr(opcode, Rp(opcode));
}

int CPU::StrdPrip(uint32_t opcode) {
    return StrdPr(opcode, IpH(opcode));
}

int CPU::StrdPtrm(uint32_t opcode) {
    return StrdPt(opcode, -Rp(opcode));
}

int CPU::StrdPtim(uint32_t opcode) {
    return StrdPt(opcode, -IpH(opcode));
}

int CPU::StrdPtrp(uint32_t opcode) {
    return StrdPt(opcode, Rp(opcode));
}

int CPU::StrdPtip(uint32_t opcode) {
    return StrdPt(opcode, IpH(opcode));
}

int CPU::Swpb(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[opcode & 0x0000000F];
    uint32_t  op2 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = m_core->memory.Read<uint8_t>(m_cpu, op2);
    m_core->memory.Write<uint8_t>(m_cpu, op2, static_cast<unsigned char>(op1));
    return (m_cpu == 0) ? 2 : 4;
}

int CPU::Swp(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[opcode & 0x0000000F];
    uint32_t  op2 = *m_registers[(opcode & 0x000F0000) >> 16];
    *op0          = m_core->memory.Read<uint32_t>(m_cpu, op2);
    m_core->memory.Write<uint32_t>(m_cpu, op2, op1);
    if (op2 & 3) {
        int shift = (op2 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return (m_cpu == 0) ? 2 : 4;
}

int CPU::Ldmda(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::Stmda(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::Ldmia(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::Stmia(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::Ldmdb(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::Stmdb(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::Ldmib(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::Stmib(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmdaW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = writeback;
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmdaW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
        }
    }
    *m_registers[m] = writeback;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmiaW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = op0;
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmiaW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = op0 + static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    *m_registers[m] = op0;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmdbW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = writeback;
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmdbW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    *m_registers[m] = writeback;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmibW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = op0;
    if (!(opcode & BIT(15)))
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmibW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = op0 + static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
        }
    }
    *m_registers[m] = op0;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmdaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            }
        }
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmdaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmiaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
                op0 += 4;
            }
        }
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmiaU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
            op0 += 4;
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmdbU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
                op0 += 4;
            }
        }
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmdbU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16] - static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
            op0 += 4;
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmibU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            }
        }
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmibU(uint32_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[(opcode & 0x000F0000) >> 16];
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmdaUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            }
        }
        if (!(opcode & BIT(m)) ||
            (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *m_registers[m] = writeback;
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = writeback;
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmdaUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
        }
    }
    *m_registers[m] = writeback;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmiaUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
                op0 += 4;
            }
        }
        if (!(opcode & BIT(m)) ||
            (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *m_registers[m] = op0;
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = op0;
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmiaUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = op0 + static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
            op0 += 4;
        }
    }
    *m_registers[m] = op0;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmdbUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
                op0 += 4;
            }
        }
        if (!(opcode & BIT(m)) ||
            (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *m_registers[m] = writeback;
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = writeback;
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmdbUW(uint32_t opcode) {
    int      m         = (opcode & 0x000F0000) >> 16;
    int      n         = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0       = *m_registers[m] - static_cast<uint32_t>(n * 4);
    uint32_t writeback = op0;
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = writeback;
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
            op0 += 4;
        }
    }
    *m_registers[m] = writeback;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::LdmibUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (!(opcode & BIT(15))) {
        for (int i = 0; i < 16; i++) {
            if (opcode & BIT(i)) {
                op0 += 4;
                m_registersUsr[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            }
        }
        if (!(opcode & BIT(m)) ||
            (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
            *m_registers[m] = op0;
        return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
    }
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
        }
    }
    if (!(opcode & BIT(m)) ||
        (m_cpu == 0 && ((opcode & 0x0000FFFF) == uint32_t(BIT(m)) || (opcode & 0x0000FFFF & ~(BIT(m + 1) - 1)))))
        *m_registers[m] = op0;
    if (m_spsr)
        SetCpsr(*m_spsr);
    if (m_cpu == 0 && (*m_registers[15] & BIT(0)))
        m_cpsr |= BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::StmibUW(uint32_t opcode) {
    int      m   = (opcode & 0x000F0000) >> 16;
    int      n   = bitCount[opcode & 0xFF] + bitCount[(opcode >> 8) & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = op0 + static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 16; i++) {
        if (opcode & BIT(i)) {
            op0 += 4;
            m_core->memory.Write<uint32_t>(m_cpu, op0, m_registersUsr[i]);
        }
    }
    *m_registers[m] = op0;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::MsrRc(uint32_t opcode) {
    uint32_t op1 = *m_registers[opcode & 0x0000000F];
    if (opcode & BIT(16)) {
        uint8_t mask = ((m_cpsr & 0x1F) == 0x10) ? 0xE0 : 0xFF;
        SetCpsr((m_cpsr & ~mask) | (op1 & mask));
    }
    for (int i = 0; i < 3; i++) {
        if (opcode & BIT(17 + i))
            m_cpsr = (m_cpsr & ~(0x0000FF00u << (i * 8))) | (op1 & (0x0000FF00u << (i * 8)));
    }
    return 1;
}

int CPU::MsrRs(uint32_t opcode) {
    uint32_t op1 = *m_registers[opcode & 0x0000000F];
    if (m_spsr) {
        for (int i = 0; i < 4; i++) {
            if (opcode & BIT(16 + i))
                *m_spsr = (*m_spsr & ~(0x000000FFu << (i * 8))) | (op1 & (0x000000FFu << (i * 8)));
        }
    }
    return 1;
}

int CPU::MsrIc(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    uint32_t op1   = (value << (32 - shift)) | (value >> shift);
    if (opcode & BIT(16)) {
        uint8_t mask = ((m_cpsr & 0x1F) == 0x10) ? 0xE0 : 0xFF;
        SetCpsr((m_cpsr & ~mask) | (op1 & mask));
    }
    for (int i = 0; i < 3; i++) {
        if (opcode & BIT(17 + i))
            m_cpsr = (m_cpsr & ~(0x0000FF00u << (i * 8))) | (op1 & (0x0000FF00u << (i * 8)));
    }
    return 1;
}

int CPU::MsrIs(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    uint32_t op1   = (value << (32 - shift)) | (value >> shift);
    if (m_spsr) {
        for (int i = 0; i < 4; i++) {
            if (opcode & BIT(16 + i))
                *m_spsr = (*m_spsr & ~(0x000000FFu << (i * 8))) | (op1 & (0x000000FFu << (i * 8)));
        }
    }
    return 1;
}

int CPU::MrsRc(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = m_cpsr;
    return (m_cpu == 0 ? 2 : 1);
}

int CPU::MrsRs(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    if (m_spsr)
        *op0 = *m_spsr;
    return (m_cpu == 0 ? 2 : 1);
}

int CPU::Mrc(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op2 = m_registers[(opcode & 0x0000F000) >> 12];
    int       op3 = (opcode & 0x000F0000) >> 16;
    int       op4 = opcode & 0x0000000F;
    int       op5 = (opcode & 0x000000E0) >> 5;
    *op2          = m_core->cp15.Read(op3, op4, op5);
    return 1;
}

int CPU::Mcr(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t op2 = *m_registers[(opcode & 0x0000F000) >> 12];
    int      op3 = (opcode & 0x000F0000) >> 16;
    int      op4 = opcode & 0x0000000F;
    int      op5 = (opcode & 0x000000E0) >> 5;
    m_core->cp15.Write(op3, op4, op5, op2);
    return 1;
}

int CPU::LdrsbRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = (uint32_t)(int8_t)m_core->memory.Read<uint8_t>(m_cpu, op1 + op2);
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::LdrshRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = (uint32_t)(int16_t)m_core->memory.Read<uint16_t>(m_cpu, op1 += op2);
    if (m_cpu == 1 && (op1 & 1))
        *op0 = static_cast<uint32_t>((int16_t)*op0 >> 8);
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::LdrbRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = m_core->memory.Read<uint8_t>(m_cpu, op1 + op2);
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrbRegT(uint16_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0007];
    uint32_t op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = *m_registers[(opcode & 0x01C0) >> 6];
    m_core->memory.Write<uint8_t>(m_cpu, op1 + op2, static_cast<unsigned char>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdrhRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = m_core->memory.Read<uint16_t>(m_cpu, op1 += op2);
    if (m_cpu == 1 && (op1 & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrhRegT(uint16_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0007];
    uint32_t op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = *m_registers[(opcode & 0x01C0) >> 6];
    m_core->memory.Write<uint16_t>(m_cpu, op1 + op2, static_cast<unsigned short>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdrRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = m_core->memory.Read<uint32_t>(m_cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrRegT(uint16_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0007];
    uint32_t op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = *m_registers[(opcode & 0x01C0) >> 6];
    m_core->memory.Write<uint32_t>(m_cpu, op1 + op2, op0);
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdrbImm5T(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x07C0) >> 6;
    *op0          = m_core->memory.Read<uint8_t>(m_cpu, op1 + op2);
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrbImm5T(uint16_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0007];
    uint32_t op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = (opcode & 0x07C0) >> 6;
    m_core->memory.Write<uint8_t>(m_cpu, op1 + op2, static_cast<unsigned char>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdrhImm5T(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x07C0) >> 5;
    *op0          = m_core->memory.Read<uint16_t>(m_cpu, op1 += op2);
    if (m_cpu == 1 && (op1 & 1))
        *op0 = (*op0 << 24) | (*op0 >> 8);
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrhImm5T(uint16_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0007];
    uint32_t op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = (opcode & 0x07C0) >> 5;
    m_core->memory.Write<uint16_t>(m_cpu, op1 + op2, static_cast<unsigned short>(op0));
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdrImm5T(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x07C0) >> 4;
    *op0          = m_core->memory.Read<uint32_t>(m_cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrImm5T(uint16_t opcode) {
    uint32_t op0 = *m_registers[opcode & 0x0007];
    uint32_t op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t op2 = (opcode & 0x07C0) >> 4;
    m_core->memory.Write<uint32_t>(m_cpu, op1 + op2, op0);
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdrPcT(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *m_registers[15] & ~3u;
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = m_core->memory.Read<uint32_t>(m_cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::LdrSpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *m_registers[13];
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = m_core->memory.Read<uint32_t>(m_cpu, op1 += op2);
    if (op1 & 3) {
        int shift = (op1 & 3) * 8;
        *op0      = (*op0 << (32 - shift)) | (*op0 >> shift);
    }
    return ((m_cpu == 0) ? 1 : 3);
}

int CPU::StrSpT(uint16_t opcode) {
    uint32_t op0 = *m_registers[(opcode & 0x0700) >> 8];
    uint32_t op1 = *m_registers[13];
    uint32_t op2 = (opcode & 0x00FF) << 2;
    m_core->memory.Write<uint32_t>(m_cpu, op1 + op2, op0);
    return ((m_cpu == 0) ? 1 : 2);
}

int CPU::LdmiaT(uint16_t opcode) {
    int      m   = (opcode & 0x0700) >> 8;
    int      n   = bitCount[opcode & 0xFF];
    uint32_t op0 = *m_registers[m];
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    if (!(opcode & BIT(m)))
        *m_registers[m] = op0;
    return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
}

int CPU::StmiaT(uint16_t opcode) {
    int      m   = (opcode & 0x0700) >> 8;
    int      n   = bitCount[opcode & 0xFF];
    uint32_t op0 = *m_registers[m];
    if (m_cpu == 1 && (opcode & BIT(m)) && (opcode & (BIT(m) - 1)))
        *m_registers[m] = op0 + static_cast<uint32_t>(n * 4);
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    *m_registers[m] = op0;
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::PopT(uint16_t opcode) {
    int      n   = bitCount[opcode & 0xFF];
    uint32_t op0 = *m_registers[13];
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    *m_registers[13] = op0;
    return n + ((m_cpu == 0) ? ((n > 1) ? 0 : 1) : 2);
}

int CPU::PushT(uint16_t opcode) {
    int      n     = bitCount[opcode & 0xFF];
    uint32_t op0   = *m_registers[13] - static_cast<uint32_t>(n * 4);
    *m_registers[13] = op0;
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

int CPU::PopPcT(uint16_t opcode) {
    int      n   = bitCount[opcode & 0xFF] + 1;
    uint32_t op0 = *m_registers[13];
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            *m_registers[i] = m_core->memory.Read<uint32_t>(m_cpu, op0);
            op0 += 4;
        }
    }
    *m_registers[15] = m_core->memory.Read<uint32_t>(m_cpu, op0);
    *m_registers[13] = op0 + 4;
    if (m_cpu == 0 && !(*m_registers[15] & BIT(0)))
        m_cpsr &= ~BIT(5);
    FlushPipeline();
    return n + 4;
}

int CPU::PushLrT(uint16_t opcode) {
    int      n     = bitCount[opcode & 0xFF] + 1;
    uint32_t op0   = *m_registers[13] - static_cast<uint32_t>(n * 4);
    *m_registers[13] = op0;
    for (int i = 0; i < 8; i++) {
        if (opcode & BIT(i)) {
            m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[i]);
            op0 += 4;
        }
    }
    m_core->memory.Write<uint32_t>(m_cpu, op0, *m_registers[14]);
    return n + ((m_cpu == 0 && n > 1) ? 0 : 1);
}

} // namespace consoles::arm

