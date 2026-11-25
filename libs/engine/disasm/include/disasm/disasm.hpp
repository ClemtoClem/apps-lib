#pragma once
/**
 * @file disasm.hpp
 * @brief Désassembleur multi-architectures : une interface unique pour x86-64
 *        (Zydis), ARM/Thumb (noyau `consoles`) et MIPS32 (interprétation d'opcodes).
 *
 * Destiné aux ROM console et aux binaires bruts : `Disassemble` balaye linéairement
 * un tampon d'octets dans l'architecture demandée. Les instructions non reconnues
 * sont rendues en données brutes (`db`, `dcw`, `dcd`).
 */
#include <cstdint>
#include <string>
#include <vector>

#include <core/core.hpp>

#include <rea/arch.hpp>

namespace rea {

/// Une ligne de désassemblage (architecture neutre).
struct Line {
	uint64_t address = 0; ///< adresse (runtime)
	uint8_t size = 1;	  ///< taille en octets de l'instruction
	std::string mnemonic; ///< « mov », « ldr », « addiu »…
	std::string operands; ///< opérandes formatés
};

/// Désassemble une instruction unique.
Result<Line, String> Decode(const uint8_t *data, size_t size, uint64_t address, Arch arch);

/// Désassemble linéairement `data` (taille `size`) depuis `address`, jusqu'à
/// épuisement du tampon ou rencontre d'un octet indécodable.
std::vector<Line> Disassemble(const uint8_t *data, size_t size, uint64_t address, Arch arch);

} // namespace rea
