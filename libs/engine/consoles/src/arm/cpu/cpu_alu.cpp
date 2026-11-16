#include <bit>
#include "consoles/arm/cpu/cpu.hpp"
#include "consoles/arm/core.hpp"

namespace consoles::arm {


FORCE_INLINE uint32_t CPU::Lli(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return value << shift;
}

FORCE_INLINE uint32_t CPU::Llr(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    return (shift < 32) ? (value << shift) : 0;
}

FORCE_INLINE uint32_t CPU::Lri(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::Lrr(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    return (shift < 32) ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::Ari(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? static_cast<unsigned int>(((int32_t)value >> shift)) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::Arr(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    return (shift < 32) ? static_cast<unsigned int>(((int32_t)value >> shift)) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::Rri(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    return shift ? ((value << (32 - shift)) | (value >> shift)) : (((m_cpsr & BIT(29)) << 2) | (value >> 1));
}

// Rotations : `std::rotr` et non `(v << (32 - s)) | (v >> s)`, qui décale de
// 32 bits quand s == 0 (comportement indéfini, relevé par UBSan ; x86 donnait
// le bon résultat par hasard).
FORCE_INLINE uint32_t CPU::Rrr(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    return std::rotr(value, shift % 32);
}

FORCE_INLINE uint32_t CPU::Imm(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    return std::rotr(value, shift);
}

FORCE_INLINE uint32_t CPU::LliS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    if (shift > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT(32 - shift)) << 29);
    return value << shift;
}

FORCE_INLINE uint32_t CPU::LlrS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    if (shift > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((shift <= 32 && (value & BIT(32 - shift))) << 29);
    return (shift < 32) ? (value << shift) : 0;
}

FORCE_INLINE uint32_t CPU::LriS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    m_cpsr           = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT(shift ? (shift - 1) : 31)) << 29);
    return shift ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::LrrS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    if (shift > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((shift <= 32 && (value & BIT(shift - 1))) << 29);
    return (shift < 32) ? (value >> shift) : 0;
}

FORCE_INLINE uint32_t CPU::AriS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    m_cpsr           = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT(shift ? (shift - 1) : 31)) << 29);
    return shift ? static_cast<unsigned int>(((int32_t)value >> shift)) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::ArrS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    if (shift > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT((shift <= 32) ? (shift - 1) : 31)) << 29);
    return (shift < 32) ? static_cast<unsigned int>(((int32_t)value >> shift)) : ((value & BIT(31)) ? 0xFFFFFFFF : 0);
}

FORCE_INLINE uint32_t CPU::RriS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F];
    uint8_t  shift = (opcode & 0x00000F80) >> 7;
    uint32_t res   = shift ? ((value << (32 - shift)) | (value >> shift)) : (((m_cpsr & BIT(29)) << 2) | (value >> 1));
    m_cpsr           = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT(shift ? (shift - 1) : 0)) << 29);
    return res;
}

FORCE_INLINE uint32_t CPU::RrrS(uint32_t opcode) {
    uint32_t value = *m_registers[opcode & 0x0000000F] + (((opcode & 0x0000000F) == 0x0000000F) ? 4 : 0);
    uint8_t  shift = static_cast<uint8_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    if (shift > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT((shift - 1) % 32)) << 29);
    return std::rotr(value, shift % 32);
}

FORCE_INLINE uint32_t CPU::ImmS(uint32_t opcode) {
    uint32_t value = opcode & 0x000000FF;
    uint8_t  shift = (opcode & 0x00000F00) >> 7;
    if (shift > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(value & BIT(shift - 1)) << 29);
    return std::rotr(value, shift);
}

FORCE_INLINE int CPU::And(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Eor(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 ^ op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Sub(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Rsb(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Add(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Adc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2 + ((m_cpsr & BIT(29)) >> 29);
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Sbc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2 - 1 + ((m_cpsr & BIT(29)) >> 29);
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Rsc(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1 - 1 + ((m_cpsr & BIT(29)) >> 29);
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Tst(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 & op2;
    m_cpsr         = (m_cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
    return 1;
}

FORCE_INLINE int CPU::Teq(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 ^ op2;
    m_cpsr         = (m_cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
    return 1;
}

FORCE_INLINE int CPU::Cmp(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 - op2;
    m_cpsr         = (m_cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

FORCE_INLINE int CPU::Cmn(uint32_t opcode, uint32_t op2) {
    uint32_t op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    uint32_t res = op1 + op2;
    m_cpsr         = (m_cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 > res) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (res & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

FORCE_INLINE int CPU::Orr(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 | op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Mov(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Bic(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & ~op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Mvn(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = ~op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Ands(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Eors(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 ^ op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Subs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Rsbs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op2 >= *op0) << 29) |
           (((op1 & BIT(31)) != (op2 & BIT(31)) && (*op0 & BIT(31)) == (op1 & BIT(31))) << 28);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Adds(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Adcs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 + op2 + ((m_cpsr & BIT(29)) >> 29);
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 > *op0 || (op2 == 0xFFFFFFFF && (m_cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Sbcs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 - op2 - 1 + ((m_cpsr & BIT(29)) >> 29);
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 >= *op0 && (op2 != 0xFFFFFFFF || (m_cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Rscs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op2 - op1 - 1 + ((m_cpsr & BIT(29)) >> 29);
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op2 >= *op0 && (op1 != 0xFFFFFFFF || (m_cpsr & BIT(29)))) << 29) |
           (((op1 & BIT(31)) != (op2 & BIT(31)) && (*op0 & BIT(31)) == (op1 & BIT(31))) << 28);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Orrs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 | op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Movs(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Bics(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1 = *m_registers[(opcode & 0x000F0000) >> 16] + (((opcode & 0x020F0010) == 0x000F0010) ? 4 : 0);
    *op0          = op1 & ~op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

FORCE_INLINE int CPU::Mvns(uint32_t opcode, uint32_t op2) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = ~op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op0 != m_registers[15])
        return 1;
    if (m_spsr)
        SetCpsr(*m_spsr);
    FlushPipeline();
    return 3;
}

int CPU::AndLli(uint32_t opcode) {
    return And(opcode, Lli(opcode));
}

int CPU::AndLlr(uint32_t opcode) {
    return And(opcode, Llr(opcode)) + 1;
}

int CPU::AndLri(uint32_t opcode) {
    return And(opcode, Lri(opcode));
}

int CPU::AndLrr(uint32_t opcode) {
    return And(opcode, Lrr(opcode)) + 1;
}

int CPU::AndAri(uint32_t opcode) {
    return And(opcode, Ari(opcode));
}

int CPU::AndArr(uint32_t opcode) {
    return And(opcode, Arr(opcode)) + 1;
}

int CPU::AndRri(uint32_t opcode) {
    return And(opcode, Rri(opcode));
}

int CPU::AndRrr(uint32_t opcode) {
    return And(opcode, Rrr(opcode)) + 1;
}

int CPU::AndImm(uint32_t opcode) {
    return And(opcode, Imm(opcode));
}

int CPU::AndsLli(uint32_t opcode) {
    return Ands(opcode, LliS(opcode));
}

int CPU::AndsLlr(uint32_t opcode) {
    return Ands(opcode, LlrS(opcode)) + 1;
}

int CPU::AndsLri(uint32_t opcode) {
    return Ands(opcode, LriS(opcode));
}

int CPU::AndsLrr(uint32_t opcode) {
    return Ands(opcode, LrrS(opcode)) + 1;
}

int CPU::AndsAri(uint32_t opcode) {
    return Ands(opcode, AriS(opcode));
}

int CPU::AndsArr(uint32_t opcode) {
    return Ands(opcode, ArrS(opcode)) + 1;
}

int CPU::AndsRri(uint32_t opcode) {
    return Ands(opcode, RriS(opcode));
}

int CPU::AndsRrr(uint32_t opcode) {
    return Ands(opcode, RrrS(opcode)) + 1;
}

int CPU::AndsImm(uint32_t opcode) {
    return Ands(opcode, ImmS(opcode));
}

int CPU::EorLli(uint32_t opcode) {
    return Eor(opcode, Lli(opcode));
}

int CPU::EorLlr(uint32_t opcode) {
    return Eor(opcode, Llr(opcode)) + 1;
}

int CPU::EorLri(uint32_t opcode) {
    return Eor(opcode, Lri(opcode));
}

int CPU::EorLrr(uint32_t opcode) {
    return Eor(opcode, Lrr(opcode)) + 1;
}

int CPU::EorAri(uint32_t opcode) {
    return Eor(opcode, Ari(opcode));
}

int CPU::EorArr(uint32_t opcode) {
    return Eor(opcode, Arr(opcode)) + 1;
}

int CPU::EorRri(uint32_t opcode) {
    return Eor(opcode, Rri(opcode));
}

int CPU::EorRrr(uint32_t opcode) {
    return Eor(opcode, Rrr(opcode)) + 1;
}

int CPU::EorImm(uint32_t opcode) {
    return Eor(opcode, Imm(opcode));
}

int CPU::EorsLli(uint32_t opcode) {
    return Eors(opcode, LliS(opcode));
}

int CPU::EorsLlr(uint32_t opcode) {
    return Eors(opcode, LlrS(opcode)) + 1;
}

int CPU::EorsLri(uint32_t opcode) {
    return Eors(opcode, LriS(opcode));
}

int CPU::EorsLrr(uint32_t opcode) {
    return Eors(opcode, LrrS(opcode)) + 1;
}

int CPU::EorsAri(uint32_t opcode) {
    return Eors(opcode, AriS(opcode));
}

int CPU::EorsArr(uint32_t opcode) {
    return Eors(opcode, ArrS(opcode)) + 1;
}

int CPU::EorsRri(uint32_t opcode) {
    return Eors(opcode, RriS(opcode));
}

int CPU::EorsRrr(uint32_t opcode) {
    return Eors(opcode, RrrS(opcode)) + 1;
}

int CPU::EorsImm(uint32_t opcode) {
    return Eors(opcode, ImmS(opcode));
}

int CPU::SubLli(uint32_t opcode) {
    return Sub(opcode, Lli(opcode));
}

int CPU::SubLlr(uint32_t opcode) {
    return Sub(opcode, Llr(opcode)) + 1;
}

int CPU::SubLri(uint32_t opcode) {
    return Sub(opcode, Lri(opcode));
}

int CPU::SubLrr(uint32_t opcode) {
    return Sub(opcode, Lrr(opcode)) + 1;
}

int CPU::SubAri(uint32_t opcode) {
    return Sub(opcode, Ari(opcode));
}

int CPU::SubArr(uint32_t opcode) {
    return Sub(opcode, Arr(opcode)) + 1;
}

int CPU::SubRri(uint32_t opcode) {
    return Sub(opcode, Rri(opcode));
}

int CPU::SubRrr(uint32_t opcode) {
    return Sub(opcode, Rrr(opcode)) + 1;
}

int CPU::SubImm(uint32_t opcode) {
    return Sub(opcode, Imm(opcode));
}

int CPU::SubsLli(uint32_t opcode) {
    return Subs(opcode, Lli(opcode));
}

int CPU::SubsLlr(uint32_t opcode) {
    return Subs(opcode, Llr(opcode)) + 1;
}

int CPU::SubsLri(uint32_t opcode) {
    return Subs(opcode, Lri(opcode));
}

int CPU::SubsLrr(uint32_t opcode) {
    return Subs(opcode, Lrr(opcode)) + 1;
}

int CPU::SubsAri(uint32_t opcode) {
    return Subs(opcode, Ari(opcode));
}

int CPU::SubsArr(uint32_t opcode) {
    return Subs(opcode, Arr(opcode)) + 1;
}

int CPU::SubsRri(uint32_t opcode) {
    return Subs(opcode, Rri(opcode));
}

int CPU::SubsRrr(uint32_t opcode) {
    return Subs(opcode, Rrr(opcode)) + 1;
}

int CPU::SubsImm(uint32_t opcode) {
    return Subs(opcode, Imm(opcode));
}

int CPU::RsbLli(uint32_t opcode) {
    return Rsb(opcode, Lli(opcode));
}

int CPU::RsbLlr(uint32_t opcode) {
    return Rsb(opcode, Llr(opcode)) + 1;
}

int CPU::RsbLri(uint32_t opcode) {
    return Rsb(opcode, Lri(opcode));
}

int CPU::RsbLrr(uint32_t opcode) {
    return Rsb(opcode, Lrr(opcode)) + 1;
}

int CPU::RsbAri(uint32_t opcode) {
    return Rsb(opcode, Ari(opcode));
}

int CPU::RsbArr(uint32_t opcode) {
    return Rsb(opcode, Arr(opcode)) + 1;
}

int CPU::RsbRri(uint32_t opcode) {
    return Rsb(opcode, Rri(opcode));
}

int CPU::RsbRrr(uint32_t opcode) {
    return Rsb(opcode, Rrr(opcode)) + 1;
}

int CPU::RsbImm(uint32_t opcode) {
    return Rsb(opcode, Imm(opcode));
}

int CPU::RsbsLli(uint32_t opcode) {
    return Rsbs(opcode, Lli(opcode));
}

int CPU::RsbsLlr(uint32_t opcode) {
    return Rsbs(opcode, Llr(opcode)) + 1;
}

int CPU::RsbsLri(uint32_t opcode) {
    return Rsbs(opcode, Lri(opcode));
}

int CPU::RsbsLrr(uint32_t opcode) {
    return Rsbs(opcode, Lrr(opcode)) + 1;
}

int CPU::RsbsAri(uint32_t opcode) {
    return Rsbs(opcode, Ari(opcode));
}

int CPU::RsbsArr(uint32_t opcode) {
    return Rsbs(opcode, Arr(opcode)) + 1;
}

int CPU::RsbsRri(uint32_t opcode) {
    return Rsbs(opcode, Rri(opcode));
}

int CPU::RsbsRrr(uint32_t opcode) {
    return Rsbs(opcode, Rrr(opcode)) + 1;
}

int CPU::RsbsImm(uint32_t opcode) {
    return Rsbs(opcode, Imm(opcode));
}

int CPU::AddLli(uint32_t opcode) {
    return Add(opcode, Lli(opcode));
}

int CPU::AddLlr(uint32_t opcode) {
    return Add(opcode, Llr(opcode)) + 1;
}

int CPU::AddLri(uint32_t opcode) {
    return Add(opcode, Lri(opcode));
}

int CPU::AddLrr(uint32_t opcode) {
    return Add(opcode, Lrr(opcode)) + 1;
}

int CPU::AddAri(uint32_t opcode) {
    return Add(opcode, Ari(opcode));
}

int CPU::AddArr(uint32_t opcode) {
    return Add(opcode, Arr(opcode)) + 1;
}

int CPU::AddRri(uint32_t opcode) {
    return Add(opcode, Rri(opcode));
}

int CPU::AddRrr(uint32_t opcode) {
    return Add(opcode, Rrr(opcode)) + 1;
}

int CPU::AddImm(uint32_t opcode) {
    return Add(opcode, Imm(opcode));
}

int CPU::AddsLli(uint32_t opcode) {
    return Adds(opcode, Lli(opcode));
}

int CPU::AddsLlr(uint32_t opcode) {
    return Adds(opcode, Llr(opcode)) + 1;
}

int CPU::AddsLri(uint32_t opcode) {
    return Adds(opcode, Lri(opcode));
}

int CPU::AddsLrr(uint32_t opcode) {
    return Adds(opcode, Lrr(opcode)) + 1;
}

int CPU::AddsAri(uint32_t opcode) {
    return Adds(opcode, Ari(opcode));
}

int CPU::AddsArr(uint32_t opcode) {
    return Adds(opcode, Arr(opcode)) + 1;
}

int CPU::AddsRri(uint32_t opcode) {
    return Adds(opcode, Rri(opcode));
}

int CPU::AddsRrr(uint32_t opcode) {
    return Adds(opcode, Rrr(opcode)) + 1;
}

int CPU::AddsImm(uint32_t opcode) {
    return Adds(opcode, Imm(opcode));
}

int CPU::AdcLli(uint32_t opcode) {
    return Adc(opcode, Lli(opcode));
}

int CPU::AdcLlr(uint32_t opcode) {
    return Adc(opcode, Llr(opcode)) + 1;
}

int CPU::AdcLri(uint32_t opcode) {
    return Adc(opcode, Lri(opcode));
}

int CPU::AdcLrr(uint32_t opcode) {
    return Adc(opcode, Lrr(opcode)) + 1;
}

int CPU::AdcAri(uint32_t opcode) {
    return Adc(opcode, Ari(opcode));
}

int CPU::AdcArr(uint32_t opcode) {
    return Adc(opcode, Arr(opcode)) + 1;
}

int CPU::AdcRri(uint32_t opcode) {
    return Adc(opcode, Rri(opcode));
}

int CPU::AdcRrr(uint32_t opcode) {
    return Adc(opcode, Rrr(opcode)) + 1;
}

int CPU::AdcImm(uint32_t opcode) {
    return Adc(opcode, Imm(opcode));
}

int CPU::AdcsLli(uint32_t opcode) {
    return Adcs(opcode, Lli(opcode));
}

int CPU::AdcsLlr(uint32_t opcode) {
    return Adcs(opcode, Llr(opcode)) + 1;
}

int CPU::AdcsLri(uint32_t opcode) {
    return Adcs(opcode, Lri(opcode));
}

int CPU::AdcsLrr(uint32_t opcode) {
    return Adcs(opcode, Lrr(opcode)) + 1;
}

int CPU::AdcsAri(uint32_t opcode) {
    return Adcs(opcode, Ari(opcode));
}

int CPU::AdcsArr(uint32_t opcode) {
    return Adcs(opcode, Arr(opcode)) + 1;
}

int CPU::AdcsRri(uint32_t opcode) {
    return Adcs(opcode, Rri(opcode));
}

int CPU::AdcsRrr(uint32_t opcode) {
    return Adcs(opcode, Rrr(opcode)) + 1;
}

int CPU::AdcsImm(uint32_t opcode) {
    return Adcs(opcode, Imm(opcode));
}

int CPU::SbcLli(uint32_t opcode) {
    return Sbc(opcode, Lli(opcode));
}

int CPU::SbcLlr(uint32_t opcode) {
    return Sbc(opcode, Llr(opcode)) + 1;
}

int CPU::SbcLri(uint32_t opcode) {
    return Sbc(opcode, Lri(opcode));
}

int CPU::SbcLrr(uint32_t opcode) {
    return Sbc(opcode, Lrr(opcode)) + 1;
}

int CPU::SbcAri(uint32_t opcode) {
    return Sbc(opcode, Ari(opcode));
}

int CPU::SbcArr(uint32_t opcode) {
    return Sbc(opcode, Arr(opcode)) + 1;
}

int CPU::SbcRri(uint32_t opcode) {
    return Sbc(opcode, Rri(opcode));
}

int CPU::SbcRrr(uint32_t opcode) {
    return Sbc(opcode, Rrr(opcode)) + 1;
}

int CPU::SbcImm(uint32_t opcode) {
    return Sbc(opcode, Imm(opcode));
}

int CPU::SbcsLli(uint32_t opcode) {
    return Sbcs(opcode, Lli(opcode));
}

int CPU::SbcsLlr(uint32_t opcode) {
    return Sbcs(opcode, Llr(opcode)) + 1;
}

int CPU::SbcsLri(uint32_t opcode) {
    return Sbcs(opcode, Lri(opcode));
}

int CPU::SbcsLrr(uint32_t opcode) {
    return Sbcs(opcode, Lrr(opcode)) + 1;
}

int CPU::SbcsAri(uint32_t opcode) {
    return Sbcs(opcode, Ari(opcode));
}

int CPU::SbcsArr(uint32_t opcode) {
    return Sbcs(opcode, Arr(opcode)) + 1;
}

int CPU::SbcsRri(uint32_t opcode) {
    return Sbcs(opcode, Rri(opcode));
}

int CPU::SbcsRrr(uint32_t opcode) {
    return Sbcs(opcode, Rrr(opcode)) + 1;
}

int CPU::SbcsImm(uint32_t opcode) {
    return Sbcs(opcode, Imm(opcode));
}

int CPU::RscLli(uint32_t opcode) {
    return Rsc(opcode, Lli(opcode));
}

int CPU::RscLlr(uint32_t opcode) {
    return Rsc(opcode, Llr(opcode)) + 1;
}

int CPU::RscLri(uint32_t opcode) {
    return Rsc(opcode, Lri(opcode));
}

int CPU::RscLrr(uint32_t opcode) {
    return Rsc(opcode, Lrr(opcode)) + 1;
}

int CPU::RscAri(uint32_t opcode) {
    return Rsc(opcode, Ari(opcode));
}

int CPU::RscArr(uint32_t opcode) {
    return Rsc(opcode, Arr(opcode)) + 1;
}

int CPU::RscRri(uint32_t opcode) {
    return Rsc(opcode, Rri(opcode));
}

int CPU::RscRrr(uint32_t opcode) {
    return Rsc(opcode, Rrr(opcode)) + 1;
}

int CPU::RscImm(uint32_t opcode) {
    return Rsc(opcode, Imm(opcode));
}

int CPU::RscsLli(uint32_t opcode) {
    return Rscs(opcode, Lli(opcode));
}

int CPU::RscsLlr(uint32_t opcode) {
    return Rscs(opcode, Llr(opcode)) + 1;
}

int CPU::RscsLri(uint32_t opcode) {
    return Rscs(opcode, Lri(opcode));
}

int CPU::RscsLrr(uint32_t opcode) {
    return Rscs(opcode, Lrr(opcode)) + 1;
}

int CPU::RscsAri(uint32_t opcode) {
    return Rscs(opcode, Ari(opcode));
}

int CPU::RscsArr(uint32_t opcode) {
    return Rscs(opcode, Arr(opcode)) + 1;
}

int CPU::RscsRri(uint32_t opcode) {
    return Rscs(opcode, Rri(opcode));
}

int CPU::RscsRrr(uint32_t opcode) {
    return Rscs(opcode, Rrr(opcode)) + 1;
}

int CPU::RscsImm(uint32_t opcode) {
    return Rscs(opcode, Imm(opcode));
}

int CPU::TstLli(uint32_t opcode) {
    return Tst(opcode, LliS(opcode));
}

int CPU::TstLlr(uint32_t opcode) {
    return Tst(opcode, LlrS(opcode)) + 1;
}

int CPU::TstLri(uint32_t opcode) {
    return Tst(opcode, LriS(opcode));
}

int CPU::TstLrr(uint32_t opcode) {
    return Tst(opcode, LrrS(opcode)) + 1;
}

int CPU::TstAri(uint32_t opcode) {
    return Tst(opcode, AriS(opcode));
}

int CPU::TstArr(uint32_t opcode) {
    return Tst(opcode, ArrS(opcode)) + 1;
}

int CPU::TstRri(uint32_t opcode) {
    return Tst(opcode, RriS(opcode));
}

int CPU::TstRrr(uint32_t opcode) {
    return Tst(opcode, RrrS(opcode)) + 1;
}

int CPU::TstImm(uint32_t opcode) {
    return Tst(opcode, ImmS(opcode));
}

int CPU::TeqLli(uint32_t opcode) {
    return Teq(opcode, LliS(opcode));
}

int CPU::TeqLlr(uint32_t opcode) {
    return Teq(opcode, LlrS(opcode)) + 1;
}

int CPU::TeqLri(uint32_t opcode) {
    return Teq(opcode, LriS(opcode));
}

int CPU::TeqLrr(uint32_t opcode) {
    return Teq(opcode, LrrS(opcode)) + 1;
}

int CPU::TeqAri(uint32_t opcode) {
    return Teq(opcode, AriS(opcode));
}

int CPU::TeqArr(uint32_t opcode) {
    return Teq(opcode, ArrS(opcode)) + 1;
}

int CPU::TeqRri(uint32_t opcode) {
    return Teq(opcode, RriS(opcode));
}

int CPU::TeqRrr(uint32_t opcode) {
    return Teq(opcode, RrrS(opcode)) + 1;
}

int CPU::TeqImm(uint32_t opcode) {
    return Teq(opcode, ImmS(opcode));
}

int CPU::CmpLli(uint32_t opcode) {
    return Cmp(opcode, LliS(opcode));
}

int CPU::CmpLlr(uint32_t opcode) {
    return Cmp(opcode, LlrS(opcode)) + 1;
}

int CPU::CmpLri(uint32_t opcode) {
    return Cmp(opcode, LriS(opcode));
}

int CPU::CmpLrr(uint32_t opcode) {
    return Cmp(opcode, LrrS(opcode)) + 1;
}

int CPU::CmpAri(uint32_t opcode) {
    return Cmp(opcode, AriS(opcode));
}

int CPU::CmpArr(uint32_t opcode) {
    return Cmp(opcode, ArrS(opcode)) + 1;
}

int CPU::CmpRri(uint32_t opcode) {
    return Cmp(opcode, RriS(opcode));
}

int CPU::CmpRrr(uint32_t opcode) {
    return Cmp(opcode, RrrS(opcode)) + 1;
}

int CPU::CmpImm(uint32_t opcode) {
    return Cmp(opcode, ImmS(opcode));
}

int CPU::CmnLli(uint32_t opcode) {
    return Cmn(opcode, LliS(opcode));
}

int CPU::CmnLlr(uint32_t opcode) {
    return Cmn(opcode, LlrS(opcode)) + 1;
}

int CPU::CmnLri(uint32_t opcode) {
    return Cmn(opcode, LriS(opcode));
}

int CPU::CmnLrr(uint32_t opcode) {
    return Cmn(opcode, LrrS(opcode)) + 1;
}

int CPU::CmnAri(uint32_t opcode) {
    return Cmn(opcode, AriS(opcode));
}

int CPU::CmnArr(uint32_t opcode) {
    return Cmn(opcode, ArrS(opcode)) + 1;
}

int CPU::CmnRri(uint32_t opcode) {
    return Cmn(opcode, RriS(opcode));
}

int CPU::CmnRrr(uint32_t opcode) {
    return Cmn(opcode, RrrS(opcode)) + 1;
}

int CPU::CmnImm(uint32_t opcode) {
    return Cmn(opcode, ImmS(opcode));
}

int CPU::OrrLli(uint32_t opcode) {
    return Orr(opcode, Lli(opcode));
}

int CPU::OrrLlr(uint32_t opcode) {
    return Orr(opcode, Llr(opcode)) + 1;
}

int CPU::OrrLri(uint32_t opcode) {
    return Orr(opcode, Lri(opcode));
}

int CPU::OrrLrr(uint32_t opcode) {
    return Orr(opcode, Lrr(opcode)) + 1;
}

int CPU::OrrAri(uint32_t opcode) {
    return Orr(opcode, Ari(opcode));
}

int CPU::OrrArr(uint32_t opcode) {
    return Orr(opcode, Arr(opcode)) + 1;
}

int CPU::OrrRri(uint32_t opcode) {
    return Orr(opcode, Rri(opcode));
}

int CPU::OrrRrr(uint32_t opcode) {
    return Orr(opcode, Rrr(opcode)) + 1;
}

int CPU::OrrImm(uint32_t opcode) {
    return Orr(opcode, Imm(opcode));
}

int CPU::OrrsLli(uint32_t opcode) {
    return Orrs(opcode, LliS(opcode));
}

int CPU::OrrsLlr(uint32_t opcode) {
    return Orrs(opcode, LlrS(opcode)) + 1;
}

int CPU::OrrsLri(uint32_t opcode) {
    return Orrs(opcode, LriS(opcode));
}

int CPU::OrrsLrr(uint32_t opcode) {
    return Orrs(opcode, LrrS(opcode)) + 1;
}

int CPU::OrrsAri(uint32_t opcode) {
    return Orrs(opcode, AriS(opcode));
}

int CPU::OrrsArr(uint32_t opcode) {
    return Orrs(opcode, ArrS(opcode)) + 1;
}

int CPU::OrrsRri(uint32_t opcode) {
    return Orrs(opcode, RriS(opcode));
}

int CPU::OrrsRrr(uint32_t opcode) {
    return Orrs(opcode, RrrS(opcode)) + 1;
}

int CPU::OrrsImm(uint32_t opcode) {
    return Orrs(opcode, ImmS(opcode));
}

int CPU::MovLli(uint32_t opcode) {
    return Mov(opcode, Lli(opcode));
}

int CPU::MovLlr(uint32_t opcode) {
    return Mov(opcode, Llr(opcode)) + 1;
}

int CPU::MovLri(uint32_t opcode) {
    return Mov(opcode, Lri(opcode));
}

int CPU::MovLrr(uint32_t opcode) {
    return Mov(opcode, Lrr(opcode)) + 1;
}

int CPU::MovAri(uint32_t opcode) {
    return Mov(opcode, Ari(opcode));
}

int CPU::MovArr(uint32_t opcode) {
    return Mov(opcode, Arr(opcode)) + 1;
}

int CPU::MovRri(uint32_t opcode) {
    return Mov(opcode, Rri(opcode));
}

int CPU::MovRrr(uint32_t opcode) {
    return Mov(opcode, Rrr(opcode)) + 1;
}

int CPU::MovImm(uint32_t opcode) {
    return Mov(opcode, Imm(opcode));
}

int CPU::MovsLli(uint32_t opcode) {
    return Movs(opcode, LliS(opcode));
}

int CPU::MovsLlr(uint32_t opcode) {
    return Movs(opcode, LlrS(opcode)) + 1;
}

int CPU::MovsLri(uint32_t opcode) {
    return Movs(opcode, LriS(opcode));
}

int CPU::MovsLrr(uint32_t opcode) {
    return Movs(opcode, LrrS(opcode)) + 1;
}

int CPU::MovsAri(uint32_t opcode) {
    return Movs(opcode, AriS(opcode));
}

int CPU::MovsArr(uint32_t opcode) {
    return Movs(opcode, ArrS(opcode)) + 1;
}

int CPU::MovsRri(uint32_t opcode) {
    return Movs(opcode, RriS(opcode));
}

int CPU::MovsRrr(uint32_t opcode) {
    return Movs(opcode, RrrS(opcode)) + 1;
}

int CPU::MovsImm(uint32_t opcode) {
    return Movs(opcode, ImmS(opcode));
}

int CPU::BicLli(uint32_t opcode) {
    return Bic(opcode, Lli(opcode));
}

int CPU::BicLlr(uint32_t opcode) {
    return Bic(opcode, Llr(opcode)) + 1;
}

int CPU::BicLri(uint32_t opcode) {
    return Bic(opcode, Lri(opcode));
}

int CPU::BicLrr(uint32_t opcode) {
    return Bic(opcode, Lrr(opcode)) + 1;
}

int CPU::BicAri(uint32_t opcode) {
    return Bic(opcode, Ari(opcode));
}

int CPU::BicArr(uint32_t opcode) {
    return Bic(opcode, Arr(opcode)) + 1;
}

int CPU::BicRri(uint32_t opcode) {
    return Bic(opcode, Rri(opcode));
}

int CPU::BicRrr(uint32_t opcode) {
    return Bic(opcode, Rrr(opcode)) + 1;
}

int CPU::BicImm(uint32_t opcode) {
    return Bic(opcode, Imm(opcode));
}

int CPU::BicsLli(uint32_t opcode) {
    return Bics(opcode, LliS(opcode));
}

int CPU::BicsLlr(uint32_t opcode) {
    return Bics(opcode, LlrS(opcode)) + 1;
}

int CPU::BicsLri(uint32_t opcode) {
    return Bics(opcode, LriS(opcode));
}

int CPU::BicsLrr(uint32_t opcode) {
    return Bics(opcode, LrrS(opcode)) + 1;
}

int CPU::BicsAri(uint32_t opcode) {
    return Bics(opcode, AriS(opcode));
}

int CPU::BicsArr(uint32_t opcode) {
    return Bics(opcode, ArrS(opcode)) + 1;
}

int CPU::BicsRri(uint32_t opcode) {
    return Bics(opcode, RriS(opcode));
}

int CPU::BicsRrr(uint32_t opcode) {
    return Bics(opcode, RrrS(opcode)) + 1;
}

int CPU::BicsImm(uint32_t opcode) {
    return Bics(opcode, ImmS(opcode));
}

int CPU::MvnLli(uint32_t opcode) {
    return Mvn(opcode, Lli(opcode));
}

int CPU::MvnLlr(uint32_t opcode) {
    return Mvn(opcode, Llr(opcode)) + 1;
}

int CPU::MvnLri(uint32_t opcode) {
    return Mvn(opcode, Lri(opcode));
}

int CPU::MvnLrr(uint32_t opcode) {
    return Mvn(opcode, Lrr(opcode)) + 1;
}

int CPU::MvnAri(uint32_t opcode) {
    return Mvn(opcode, Ari(opcode));
}

int CPU::MvnArr(uint32_t opcode) {
    return Mvn(opcode, Arr(opcode)) + 1;
}

int CPU::MvnRri(uint32_t opcode) {
    return Mvn(opcode, Rri(opcode));
}

int CPU::MvnRrr(uint32_t opcode) {
    return Mvn(opcode, Rrr(opcode)) + 1;
}

int CPU::MvnImm(uint32_t opcode) {
    return Mvn(opcode, Imm(opcode));
}

int CPU::MvnsLli(uint32_t opcode) {
    return Mvns(opcode, LliS(opcode));
}

int CPU::MvnsLlr(uint32_t opcode) {
    return Mvns(opcode, LlrS(opcode)) + 1;
}

int CPU::MvnsLri(uint32_t opcode) {
    return Mvns(opcode, LriS(opcode));
}

int CPU::MvnsLrr(uint32_t opcode) {
    return Mvns(opcode, LrrS(opcode)) + 1;
}

int CPU::MvnsAri(uint32_t opcode) {
    return Mvns(opcode, AriS(opcode));
}

int CPU::MvnsArr(uint32_t opcode) {
    return Mvns(opcode, ArrS(opcode)) + 1;
}

int CPU::MvnsRri(uint32_t opcode) {
    return Mvns(opcode, RriS(opcode));
}

int CPU::MvnsRrr(uint32_t opcode) {
    return Mvns(opcode, RrrS(opcode)) + 1;
}

int CPU::MvnsImm(uint32_t opcode) {
    return Mvns(opcode, ImmS(opcode));
}

int CPU::Mul(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *m_registers[opcode & 0x0000000F];
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    *op0          = op1 * static_cast<uint32_t>(op2);
    if (m_cpu == 0)
        return 2;
    int m;
    // `m < 4` testé EN PREMIER : sinon la condition évaluait `1 << 32` (comportement
    // indéfini, relevé par UBSan) avant de constater que la boucle devait s'arrêter.
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 1;
}

int CPU::Mla(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *m_registers[opcode & 0x0000000F];
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = op1 * static_cast<uint32_t>(op2) + op3;
    if (m_cpu == 0)
        return 2;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::Umull(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *m_registers[opcode & 0x0000000F];
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    *op1          = static_cast<uint32_t>(res >> 32);
    *op0          = static_cast<uint32_t>(res);
    if (m_cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::Umlal(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *m_registers[opcode & 0x0000000F];
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    res += ((uint64_t)*op1 << 32) | *op0;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    if (m_cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::Smull(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    int64_t   res = (int64_t)op2 * op3;
    *op1          = static_cast<uint32_t>(res >> 32);
    *op0          = static_cast<uint32_t>(res);
    if (m_cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::Smlal(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    int64_t   res = (int64_t)op2 * op3;
    res += ((int64_t)*op1 << 32) | *op0;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    if (m_cpu == 0)
        return 3;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::Muls(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *m_registers[opcode & 0x0000000F];
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    *op0          = op1 * static_cast<uint32_t>(op2);
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (m_cpu == 0)
        return 4;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 1;
}

int CPU::Mlas(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op1 = *m_registers[opcode & 0x0000000F];
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    *op0          = op1 * static_cast<uint32_t>(op2) + op3;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (m_cpu == 0)
        return 4;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::Umulls(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *m_registers[opcode & 0x0000000F];
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    *op1          = static_cast<uint32_t>(res >> 32);
    *op0          = static_cast<uint32_t>(res);
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (m_cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::Umlals(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    uint32_t  op2 = *m_registers[opcode & 0x0000000F];
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint64_t  res = (uint64_t)op2 * (uint32_t)op3;
    res += ((uint64_t)*op1 << 32) | *op0;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    m_cpsr = (m_cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (m_cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::Smulls(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    int64_t   res = (int64_t)op2 * op3;
    *op1          = static_cast<uint32_t>(res >> 32);
    *op0          = static_cast<uint32_t>(res);
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (m_cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 2;
}

int CPU::Smlals(uint32_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op2 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op3 = static_cast<int32_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    int64_t   res = (int64_t)op2 * op3;
    res += ((int64_t)*op1 << 32) | *op0;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    m_cpsr = (m_cpsr & ~0xC0000000) | (*op1 & BIT(31)) | ((*op1 == 0) << 30);
    if (m_cpu == 0)
        return 5;
    int m;
    for (m = 1; m < 4 && (op3 < (-1 << (m * 8)) || op3 >= (1 << (m * 8))); m++)
        ;
    return m + 3;
}

int CPU::Smulbb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    *op0          = static_cast<uint32_t>(op1 * op2);
    return 1;
}

int CPU::Smulbt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    *op0          = static_cast<uint32_t>(op1 * op2);
    return 1;
}

int CPU::Smultb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F] >> 16);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    *op0          = static_cast<uint32_t>(op1 * op2);
    return 1;
}

int CPU::Smultt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F] >> 16);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    *op0          = static_cast<uint32_t>(op1 * op2);
    return 1;
}

int CPU::Smulwb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    *op0          = static_cast<uint32_t>(((int64_t)op1 * op2) >> 16);
    return 1;
}

int CPU::Smulwt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    *op0          = static_cast<uint32_t>(((int64_t)op1 * op2) >> 16);
    return 1;
}

int CPU::Smlabb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = static_cast<uint32_t>(op1 * op2);
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        m_cpsr |= BIT(27);
    return 1;
}

int CPU::Smlabt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = static_cast<uint32_t>(op1 * op2);
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        m_cpsr |= BIT(27);
    return 1;
}

int CPU::Smlatb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F] >> 16);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = static_cast<uint32_t>(op1 * op2);
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        m_cpsr |= BIT(27);
    return 1;
}

int CPU::Smlatt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op1 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F] >> 16);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = static_cast<uint32_t>(op1 * op2);
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        m_cpsr |= BIT(27);
    return 1;
}

int CPU::Smlawb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = static_cast<uint32_t>(((int64_t)op1 * op2) >> 16);
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        m_cpsr |= BIT(27);
    return 1;
}

int CPU::Smlawt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x000F0000) >> 16];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op2 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    uint32_t  op3 = *m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  res = static_cast<uint32_t>(((int64_t)op1 * op2) >> 16);
    *op0          = res + op3;
    if ((*op0 & BIT(31)) != (res & BIT(31)))
        m_cpsr |= BIT(27);
    return 1;
}

int CPU::Smlalbb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op3 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    return 2;
}

int CPU::Smlalbt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F]);
    int16_t   op3 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    return 2;
}

int CPU::Smlaltb(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F] >> 16);
    int16_t   op3 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8]);
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    return 2;
}

int CPU::Smlaltt(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t *op1 = m_registers[(opcode & 0x000F0000) >> 16];
    int16_t   op2 = static_cast<int16_t>(*m_registers[opcode & 0x0000000F] >> 16);
    int16_t   op3 = static_cast<int16_t>(*m_registers[(opcode & 0x00000F00) >> 8] >> 16);
    int64_t   res = ((int64_t)*op1 << 32) | *op0;
    res += op2 * op3;
    *op1 = static_cast<uint32_t>(res >> 32);
    *op0 = static_cast<uint32_t>(res);
    return 2;
}

int CPU::Qadd(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x000F0000) >> 16]);
    int64_t   res = (int64_t)op1 + op2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        m_cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        m_cpsr |= BIT(27);
    }
    *op0 = static_cast<uint32_t>(res);
    return 1;
}

int CPU::Qsub(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x000F0000) >> 16]);
    int64_t   res = (int64_t)op1 - op2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        m_cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        m_cpsr |= BIT(27);
    }
    *op0 = static_cast<uint32_t>(res);
    return 1;
}

int CPU::Qdadd(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x000F0000) >> 16]);
    int64_t   res = (int64_t)op2 * 2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        m_cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        m_cpsr |= BIT(27);
    }
    res += op1;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        m_cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        m_cpsr |= BIT(27);
    }
    *op0 = static_cast<uint32_t>(res);
    return 1;
}

int CPU::Qdsub(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0 = m_registers[(opcode & 0x0000F000) >> 12];
    int32_t   op1 = static_cast<int32_t>(*m_registers[opcode & 0x0000000F]);
    int32_t   op2 = static_cast<int32_t>(*m_registers[(opcode & 0x000F0000) >> 16]);
    int64_t   res = (int64_t)op2 * 2;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        m_cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        m_cpsr |= BIT(27);
    }
    res -= op1;
    if (res > 0x7FFFFFFF) {
        res = 0x7FFFFFFF;
        m_cpsr |= BIT(27);
    } else if (res < -0x80000000) {
        res = -0x80000000;
        m_cpsr |= BIT(27);
    }
    *op0 = static_cast<uint32_t>(res);
    return 1;
}

int CPU::Clz(uint32_t opcode) {
    if (m_cpu == 1)
        return 1;
    uint32_t *op0   = m_registers[(opcode & 0x0000F000) >> 12];
    uint32_t  op1   = *m_registers[opcode & 0x0000000F];
    int       count = 0;
    while (op1 != 0) {
        op1 >>= 1;
        count++;
    }
    *op0 = static_cast<uint32_t>(32 - count);
    return 1;
}

int CPU::AddRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = op1 + op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::SubRegT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = *m_registers[(opcode & 0x01C0) >> 6];
    *op0          = op1 - op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::AddHT(uint16_t opcode) {
    uint32_t *op0 = m_registers[((opcode & 0x0080) >> 4) | (opcode & 0x0007)];
    uint32_t  op2 = *m_registers[(opcode & 0x0078) >> 3];
    *op0 += op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

int CPU::CmpHT(uint16_t opcode) {
    uint32_t op1 = *m_registers[((opcode & 0x0080) >> 4) | (opcode & 0x0007)];
    uint32_t op2 = *m_registers[(opcode & 0x0078) >> 3];
    uint32_t res = op1 - op2;
    m_cpsr         = (m_cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::MovHT(uint16_t opcode) {
    uint32_t *op0 = m_registers[((opcode & 0x0080) >> 4) | (opcode & 0x0007)];
    uint32_t  op2 = *m_registers[(opcode & 0x0078) >> 3];
    *op0          = op2;
    if (op0 != m_registers[15])
        return 1;
    FlushPipeline();
    return 3;
}

int CPU::AddPcT(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *m_registers[15] & ~3u;
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = op1 + op2;
    return 1;
}

int CPU::AddSpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *m_registers[13];
    uint32_t  op2 = (opcode & 0x00FF) << 2;
    *op0          = op1 + op2;
    return 1;
}

int CPU::AddSpImmT(uint16_t opcode) {
    uint32_t *op0 = m_registers[13];
    uint32_t  op2 = static_cast<uint32_t>(((opcode & BIT(7)) ? (0 - (opcode & 0x007F)) : (opcode & 0x007F)) << 2);
    *op0 += op2;
    return 1;
}

int CPU::LslImmT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint8_t   op2 = (opcode & 0x07C0) >> 6;
    *op0          = op1 << op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(op1 & BIT(32 - op2)) << 29);
    return 1;
}

int CPU::LsrImmT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint8_t   op2 = (opcode & 0x07C0) >> 6;
    *op0          = op2 ? (op1 >> op2) : 0;
    m_cpsr =
        (m_cpsr & ~0xE0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((bool)(op1 & BIT(op2 ? (op2 - 1) : 31)) << 29);
    return 1;
}

int CPU::AsrImmT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint8_t   op2 = (opcode & 0x07C0) >> 6;
    *op0          = op2 ? static_cast<unsigned int>(((int32_t)op1 >> op2)) : ((op1 & BIT(31)) ? 0xFFFFFFFF : 0);
    m_cpsr =
        (m_cpsr & ~0xE0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((bool)(op1 & BIT(op2 ? (op2 - 1) : 31)) << 29);
    return 1;
}

int CPU::AddImm3T(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x01C0) >> 6;
    *op0          = op1 + op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::SubImm3T(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t  op2 = (opcode & 0x01C0) >> 6;
    *op0          = op1 - op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::AddImm8T(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op2 = opcode & 0x00FF;
    *op0 += op2;
    m_cpsr = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 > *op0) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::SubImm8T(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op1 = *m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op2 = opcode & 0x00FF;
    *op0 -= op2;
    m_cpsr = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::CmpImm8T(uint16_t opcode) {
    uint32_t op1 = *m_registers[(opcode & 0x0700) >> 8];
    uint32_t op2 = opcode & 0x00FF;
    uint32_t res = op1 - op2;
    m_cpsr         = (m_cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::MovImm8T(uint16_t opcode) {
    uint32_t *op0 = m_registers[(opcode & 0x0700) >> 8];
    uint32_t  op2 = opcode & 0x00FF;
    *op0          = op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::LslDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[opcode & 0x0007];
    uint8_t   op2 = static_cast<uint8_t>(*m_registers[(opcode & 0x0038) >> 3]);
    *op0          = (op2 < 32) ? (*op0 << op2) : 0;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((op2 <= 32 && (op1 & BIT(32 - op2))) << 29);
    return 1;
}

int CPU::LsrDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[opcode & 0x0007];
    uint8_t   op2 = static_cast<uint8_t>(*m_registers[(opcode & 0x0038) >> 3]);
    *op0          = (op2 < 32) ? (*op0 >> op2) : 0;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((op2 <= 32 && (op1 & BIT(op2 - 1))) << 29);
    return 1;
}

int CPU::AsrDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[opcode & 0x0007];
    uint8_t   op2 = static_cast<uint8_t>(*m_registers[(opcode & 0x0038) >> 3]);
    *op0          = (op2 < 32) ? static_cast<unsigned int>(((int32_t)(*op0) >> op2)) : ((*op0 & BIT(31)) ? 0xFFFFFFFF : 0);
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(op1 & BIT((op2 <= 32) ? (op2 - 1) : 31)) << 29);
    return 1;
}

int CPU::RorDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[opcode & 0x0007];
    uint8_t   op2 = static_cast<uint8_t>(*m_registers[(opcode & 0x0038) >> 3]);
    *op0          = (*op0 << (32 - op2 % 32)) | (*op0 >> (op2 % 32));
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (op2 > 0)
        m_cpsr = (m_cpsr & ~BIT(29)) | ((bool)(op1 & BIT((op2 - 1) % 32)) << 29);
    return 1;
}

int CPU::AndDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0 &= op2;
    m_cpsr = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::EorDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0 ^= op2;
    m_cpsr = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::AdcDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0 += op2 + ((m_cpsr & BIT(29)) >> 29);
    m_cpsr = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 > *op0 || (op2 == 0xFFFFFFFF && (m_cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (*op0 & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::SbcDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0          = op1 - op2 - 1 + ((m_cpsr & BIT(29)) >> 29);
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) |
           ((op1 >= *op0 && (op2 != 0xFFFFFFFF || (m_cpsr & BIT(29)))) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::TstDpT(uint16_t opcode) {
    uint32_t op1 = *m_registers[opcode & 0x0007];
    uint32_t op2 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t res = op1 & op2;
    m_cpsr         = (m_cpsr & ~0xC0000000) | (res & BIT(31)) | ((res == 0) << 30);
    return 1;
}

int CPU::CmpDpT(uint16_t opcode) {
    uint32_t op1 = *m_registers[opcode & 0x0007];
    uint32_t op2 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t res = op1 - op2;
    m_cpsr         = (m_cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 >= res) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (res & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::CmnDpT(uint16_t opcode) {
    uint32_t op1 = *m_registers[opcode & 0x0007];
    uint32_t op2 = *m_registers[(opcode & 0x0038) >> 3];
    uint32_t res = op1 + op2;
    m_cpsr         = (m_cpsr & ~0xF0000000) | (res & BIT(31)) | ((res == 0) << 30) | ((op1 > res) << 29) |
           (((op2 & BIT(31)) == (op1 & BIT(31)) && (res & BIT(31)) != (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::OrrDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0 |= op2;
    m_cpsr = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::BicDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0 &= ~op2;
    m_cpsr = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::MvnDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0          = ~op2;
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    return 1;
}

int CPU::NegDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = 0;
    uint32_t  op2 = *m_registers[(opcode & 0x0038) >> 3];
    *op0          = op1 - op2;
    m_cpsr          = (m_cpsr & ~0xF0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30) | ((op1 >= *op0) << 29) |
           (((op2 & BIT(31)) != (op1 & BIT(31)) && (*op0 & BIT(31)) == (op2 & BIT(31))) << 28);
    return 1;
}

int CPU::MulDpT(uint16_t opcode) {
    uint32_t *op0 = m_registers[opcode & 0x0007];
    uint32_t  op1 = *m_registers[(opcode & 0x0038) >> 3];
    int32_t   op2 = static_cast<int32_t>(*m_registers[opcode & 0x0007]);
    *op0          = op1 * static_cast<uint32_t>(op2);
    m_cpsr          = (m_cpsr & ~0xC0000000) | (*op0 & BIT(31)) | ((*op0 == 0) << 30);
    if (m_cpu == 0)
        return 4;
    int m;
    for (m = 1; m < 4 && (op2 < (-1 << (m * 8)) || op2 >= (1 << (m * 8))); m++)
        ;
    return m + 1;
}

} // namespace consoles::arm

