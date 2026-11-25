#pragma once
/**
 * @file disasm.hpp
 * @brief Désassembleur x86-64 (mode long, Intel) bâti sur Zydis (vendu en interne).
 *
 * L'interface est volontairement minimale : décodage d'une instruction et
 * désassemblage linéaire d'un tampon. L'analyse de flot de contrôle (découverte
 * de fonctions, blocs de base) est réalisée à l'étage supérieur (lifter).
 */
#include <cstdint>
#include <string>
#include <vector>

#include <core/core.hpp>

namespace rea {

/// Une instruction x86-64 décodée.
struct Instruction {
	uint64_t address = 0;				///< adresse virtuelle (runtime)
	uint8_t length = 0;					///< taille en octets
	std::vector<uint8_t> bytes;			///< octets d'encodage
	std::string mnemonic;				///< « mov », « call », « jmp »… (minuscules)
	std::string operands;				///< opérandes formatés (syntaxe Intel)
	std::string category;				///< « call », « ret », « jmp », « jcc », « invalid »…

	bool isBranch = false;				///< saut (inconditionnel ou conditionnel)
	bool isConditional = false;			///< saut conditionnel (jcc)
	bool isCall = false;				///< appel de fonction
	bool isReturn = false;				///< retour (ret)
	bool hasTarget = false;				///< cible directe résolue (branche relative/absolue)
	uint64_t target = 0;				///< adresse absolue de la cible (si hasTarget)
	/// Adresses absolues référencées par un opérande mémoire (RIP-relative ou
	/// absolu) — chaînes, variables globales, tables… (références de données).
	std::vector<uint64_t> dataRefs{};
};

/// Décode une seule instruction à `address`. `data`/`size` décrivent les octets
/// disponibles à partir de cette adresse. Rend `Err` si le décodage échoue
/// (octet invalide, tampon trop court).
Result<Instruction, String> DecodeInstruction(const uint8_t *data, size_t size, uint64_t address);

/// Désassemble linéairement `data` (de taille `size`) à partir de `address`,
/// jusqu'à épuisement du tampon ou rencontre d'un octet indécodable.
std::vector<Instruction> Disassemble(const uint8_t *data, size_t size, uint64_t address);

} // namespace rea
