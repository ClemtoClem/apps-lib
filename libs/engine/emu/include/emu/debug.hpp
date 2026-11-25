#pragma once
/**
 * @file debug.hpp
 * @brief Interface de débogage d'un processeur émulé (`device_state_interface` et
 *        `device_disasm_interface` de MAME) : registres nommés, compteur ordinal,
 *        désassemblage, bus du programme. Un débogueur générique s'en contente.
 */
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "memory.hpp"

namespace emu {

struct RegisterValue {
	std::string name;
	uint64_t value = 0;
	unsigned bits = 8; ///< largeur d'affichage
};

struct DisassembledLine {
	uint32_t address = 0;
	uint32_t size = 1;	   ///< octets de l'instruction (≥ 1)
	std::string mnemonic;  ///< « LD »
	std::string operands;  ///< « A,(HL) »
};

class Debuggable {
public:
	virtual ~Debuggable() = default;
	[[nodiscard]] virtual std::vector<RegisterValue> DebugRegisters() const = 0;
	[[nodiscard]] virtual uint32_t ProgramCounter() const = 0;
	/// Désassemble l'instruction à `address` (lecture du bus sans effet de bord notable).
	[[nodiscard]] virtual DisassembledLine Disassemble(uint32_t address) const = 0;
	[[nodiscard]] virtual const AddressSpace &ProgramSpace() const = 0;
	/// Drapeaux lisibles (« Z N H C ») ; vide si le processeur n'en a pas.
	[[nodiscard]] virtual std::string FlagsText() const { return {}; }
	/// Régions mémoire remarquables (raccourcis du débogueur) : nom et adresse de début.
	[[nodiscard]] virtual std::vector<std::pair<std::string, uint32_t>> MemoryRegions() const { return {}; }
};

} // namespace emu
