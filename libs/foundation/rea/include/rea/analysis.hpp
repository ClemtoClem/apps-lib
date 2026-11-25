#pragma once
/**
 * @file analysis.hpp
 * @brief Analyses de haut niveau inspirées du flux « REA » : extraction des
 *        chaînes, désassemblage de toutes les fonctions, graphe d'appels
 *        (références croisées), identité du binaire et export du C reconstruit.
 */
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <core/core.hpp>

#include "disasm.hpp"
#include "elf.hpp"

namespace rea {

/// Une chaîne ASCII imprimable trouvée dans une section.
struct StringRef {
	std::string section; ///< nom de la section (« .rodata », « .strtab »…)
	uint64_t address = 0; ///< adresse virtuelle (0 si la section n'est pas allouée)
	uint64_t offset = 0;  ///< décalage dans le fichier
	std::string text;	  ///< contenu (sans le NUL final)
};

/// Extrait les chaînes ASCII imprimables (longueur ≥ `minLength`) de toutes les
/// sections du fichier. Les chaînes sont dédupliquées par (section, offset).
std::vector<StringRef> ExtractStrings(const Elf &elf, size_t minLength = 4);

/// Bloc de base : séquence maximale d'instructions sans branche interne.
struct BasicBlock {
	uint64_t address = 0;					///< adresse du premier octet
	std::vector<Instruction> instructions;	///< instructions du bloc
	std::vector<uint64_t> successors;		///< adresses des blocs suivants (sauts / retombée)
};

/// Référence de données : une instruction référence une chaîne ou une globale.
struct DataRef {
	uint64_t from = 0;   ///< adresse de l'instruction qui référence
	uint64_t target = 0; ///< adresse référencée (chaîne / globale)
	std::string what;	 ///< texte pointé (entre guillemets), nom de symbole, ou « data_xxxx »
};

/// Identité du binaire : langage, chaîne d'outils, liaison et présence des symboles.
struct BinaryIdentity {
	std::string language;		  ///< « Go », « Rust », « C++ », « C », « ? »
	std::string toolchain;		  ///< « go1.22.0 », « rustc 1.100.0 », « GCC 15.2.0 », « Clang 21.1.8 »…
	bool stripped = false;		  ///< pas de table .symtab
	bool staticallyLinked = false; ///< pas d'interpréteur PT_INTERP
};

/// Analyse d'une fonction : désassemblage, appels, blocs et références de données.
struct FunctionAnalysis {
	const ElfSymbol *symbol = nullptr;	///< symbole ELF (ou nul pour l'entrée)
	std::vector<Instruction> instructions; ///< corps désassemblé
	std::vector<BasicBlock> blocks;		   ///< blocs de base (graphe de flot de contrôle)
	std::vector<uint64_t> calls;		   ///< adresses cibles des appels (résolues)
	std::vector<std::string> callNames;	   ///< noms résolus (ou « sub_xxxx »)
	size_t xrefs = 0;					   ///< nombre de fonctions qui appellent celle-ci
	std::vector<uint64_t> callers;		   ///< adresses des fonctions appelantes
	std::vector<std::string> callerNames;  ///< noms des appelants (ou « sub_xxxx »)
	std::vector<DataRef> dataRefs;		   ///< chaînes / globales référencées par le corps
};

/// Désassemble toutes les fonctions du binaire et construit le graphe d'appels
/// (chaque fonction connaît ses appels ; `xrefs` compte les appelants).
std::vector<FunctionAnalysis> AnalyzeFunctions(const Elf &elf);

/// Détecte le langage, la chaîne d'outils et la liaison d'un binaire à partir de
/// ses marqueurs embarqués (.comment, sections, symboles, segments).
BinaryIdentity DetectIdentity(const Elf &elf);

/// Exporte le C reconstruit de toutes les fonctions : un fichier `<nom>.c` par
/// fonction dans `directory` (créé au besoin). Rend le nombre de fichiers écrits.
Result<size_t, String> ExportC(const std::vector<FunctionAnalysis> &functions, const std::filesystem::path &directory);

} // namespace rea
