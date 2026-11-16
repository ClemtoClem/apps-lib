#pragma once
/**
 * @file disasm.hpp
 * @brief Désassembleur ARM (ARMv5TE) et Thumb : les classes d'instructions des processeurs
 *        ARM7TDMI (GBA, NDS) et ARM946E-S (NDS). Les codes non reconnus sont rendus en
 *        données brutes (`DCD`, `DCW`).
 */
#include <cstdint>

#include <emu/debug.hpp>

namespace consoles::arm {

/// Instruction ARM de 32 bits `opcode` située à `address`.
[[nodiscard]] emu::DisassembledLine DisassembleArm(uint32_t address, uint32_t opcode);
/// Instruction Thumb de 16 bits ; `next` : demi-mot suivant (seconde moitié de BL).
[[nodiscard]] emu::DisassembledLine DisassembleThumb(uint32_t address, uint16_t opcode, uint16_t next = 0);

} // namespace consoles::arm
