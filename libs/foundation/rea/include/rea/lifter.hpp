#pragma once
/**
 * @file lifter.hpp
 * @brief Lifting « au mieux » d'une fonction désassemblée vers du C.
 *
 * Le résultat est un pseudo-code C *lisible* (et recompilable pour les
 * fonctions simples) : chaque instruction est traduite en une instruction C
 * quand c'est possible, sinon en un commentaire d'assemblage. Les sauts
 * deviennent des `goto` sur des étiquettes d'adresse ; les appels deviennent
 * des appels de fonction ; `ret` devient `return`.
 */
#include <cstdint>
#include <string>
#include <vector>

#include <core/core.hpp>

#include "disasm.hpp"

namespace rea {

/// Une fonction : un nom, une adresse de début et son désassemblage.
struct Function {
	std::string name;						///< nom (symbole) ou « sub_<adresse> »
	uint64_t address = 0;					///< adresse du premier octet
	std::vector<Instruction> instructions;	///< corps désassemblé (linéaire)
};

/// Options du lifting (contrôle la fidélité / lisibilité du C produit).
struct LiftOptions {
	/// Émettre l'assemblage d'origine en commentaire au-dessus de chaque ligne C.
	bool annotate = true;
	/// Déclarer les registres (`uint64_t rax, rbx…`) en tête de fonction : le C
	/// produit est alors autonome et recompilable.
	bool declareRegisters = true;
	/// Pour une instruction non levée, émettre un commentaire (vrai) plutôt
	/// qu'un `__asm__` inline (faux). Les commentaires restent lisibles mais ne
	/// reproduisent pas la sémantique à la recompilation.
	bool commentsForUnlifted = true;
};

/// Signature C inférée (convention d'appel System V AMD64).
struct Signature {
	std::string returnType = "void";	  ///< « void », « int », « long », « char »…
	std::string returnRegister;			  ///< « eax », « rax », « al »… (vide si void)
	std::vector<std::string> paramTypes;  ///< types des paramètres, dans l'ordre
	std::vector<std::string> paramNames;  ///< noms proposés (« a », « b », « c »…)
	std::vector<std::string> paramRegisters; ///< registres canoniques (« rdi », « rsi »…), parallèle à paramNames
	[[nodiscard]] std::string Params() const; ///< « int a, int b » (ou « void »)
};

/// Infère la signature d'une fonction depuis son désassemblage : les paramètres
/// sont les registres d'argument lus avant d'être écrits (rdi, rsi, rdx, rcx,
/// r8, r9) ; le type de retour découle des écritures dans rax (hors appels).
Signature InferSignature(const Function &function);

/// Lève une fonction vers un corps de fonction C (sans la ligne de signature).
/// `sig` (optionnel) pilote l'instruction `return` : `return eax;` au lieu de
/// `return;`, et déclare le registre de retour.
String LiftFunctionBody(const Function &function, const LiftOptions &options = {}, const Signature *sig = nullptr);

/// Lève une fonction complète : signature inférée (`int f(int a, int b)`) +
/// corps + accolades.
String LiftFunction(const Function &function, const LiftOptions &options = {});

} // namespace rea
