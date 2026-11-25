#pragma once
/**
 * @file elf.hpp
 * @brief Analyseur ELF64 (x86-64, petit-boutiste) : en-têtes, segments, sections,
 *        symboles et accès aux octets d'une section.
 *
 * Le parseur lit le fichier en mémoire et expose des vues (`std::string_view`)
 * sur les octets bruts — aucune copie des sections. Il ne dépend que de
 * `foundation.core` (vocabulaire `Result` / `String`) et de la bibliothèque
 * standard, si bien que le module reste au niveau « foundation ».
 */
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <core/core.hpp>

namespace rea {

/// Section ELF (en-tête `sh_*` résolu, nom déjà extrait de la table des chaînes).
struct ElfSection {
	std::string name;	///< nom (table `shstrtab`), vide si absent
	uint32_t type = 0;	///< SHT_* (PROGBITS, SYMTAB, STRTAB, NOBITS…)
	uint64_t flags = 0; ///< SHF_* (WRITE, ALLOC, EXECINSTR…)
	uint64_t address = 0; ///< adresse virtuelle (sh_addr)
	uint64_t offset = 0;  ///< décalage dans le fichier (sh_offset)
	uint64_t size = 0;	  ///< taille (sh_size)
	uint64_t link = 0, info = 0, align = 0, entsize = 0;

	[[nodiscard]] bool HasFileBytes() const noexcept { return type != 8 /* SHT_NOBITS */; }
	[[nodiscard]] bool IsExecutable() const noexcept { return (flags & 0x4u) != 0; }
};

/// Symbole ELF (entrée de `.symtab` ou `.dynsym`).
struct ElfSymbol {
	std::string name; ///< nom (table des chaînes liée), vide si absent
	uint64_t value = 0; ///< adresse (st_value)
	uint64_t size = 0;	///< taille (st_size)
	uint8_t info = 0;	///< type (4 bits bas) + bind (4 bits hauts)
	uint16_t section = 0; ///< index de section (st_shndx)

	[[nodiscard]] uint8_t Type() const noexcept { return info & 0xfu; }
	[[nodiscard]] uint8_t Bind() const noexcept { return info >> 4; }
	[[nodiscard]] bool IsFunction() const noexcept { return Type() == 2 /* STT_FUNC */; }
};

/// Segment de programme (en-tête `ph_*` résolu) — principalement les PT_LOAD.
struct ElfSegment {
	uint32_t type = 0;  ///< PT_* (LOAD = 1, DYNAMIC = 2, INTERP = 3…)
	uint32_t flags = 0; ///< PF_* (R = 4, W = 2, X = 1)
	uint64_t offset = 0;  ///< décalage fichier (p_offset)
	uint64_t vaddr = 0;   ///< adresse virtuelle (p_vaddr)
	uint64_t filesz = 0;  ///< taille dans le fichier
	uint64_t memsz = 0;   ///< taille en mémoire
};

/// Note ELF (segment PT_NOTE) : propriétaire, type et description brute.
struct ElfNote {
	std::string owner;				///< « GNU », « FreeBSD »… (nom, sans NUL)
	uint32_t type = 0;				///< NT_* (1 = ABI-tag, 3 = build-id…)
	std::vector<uint8_t> desc;		///< octets de description
};

/// Diagnostic de sécurité (durcissement) du binaire.
struct SecurityReport {
	bool pie = false;			///< exécutable indépendant de la position (ET_DYN)
	bool nx = false;			///< pile non exécutable (PT_GNU_STACK sans X)
	bool relro = false;			///< relocations en lecture seule (PT_GNU_RELRO)
	bool fullRelro = false;		///< RELRO complet (BIND_NOW)
	bool stackCanary = false;	///< canari de pile (__stack_chk_fail importé)
	bool fortify = false;		///< _FORTIFY_SOURCE (__*_chk importé)
	std::string buildId;		///< identifiant de build (hexadécimal NT_GNU_BUILD_ID), vide sinon
};

/// Fichier ELF64 chargé et indexé.
class Elf {
public:
	/// Charge et analyse `path`. Rend `Err(message)` si le fichier est illisible
	/// ou n'est pas un ELF64 x86-64 petit-boutiste exploitable.
	static Result<Elf, String> Load(const std::filesystem::path &path);

	[[nodiscard]] uint16_t Machine() const noexcept { return m_machine; }
	[[nodiscard]] uint16_t Type() const noexcept { return m_type; }
	[[nodiscard]] uint64_t EntryPoint() const noexcept { return m_entry; }
	[[nodiscard]] const std::vector<ElfSection> &Sections() const noexcept { return m_sections; }
	[[nodiscard]] const std::vector<ElfSymbol> &Symbols() const noexcept { return m_symbols; }
	[[nodiscard]] const std::vector<ElfSegment> &Segments() const noexcept { return m_segments; }
	/// Octets bruts du fichier entier.
	[[nodiscard]] const std::vector<uint8_t> &Raw() const noexcept { return m_raw; }

	/// Section par nom (ou `nullptr`).
	[[nodiscard]] const ElfSection *FindSection(std::string_view name) const;
	/// Symbole par nom (ou `nullptr`).
	[[nodiscard]] const ElfSymbol *FindSymbol(std::string_view name) const;
	/// Sections exécutables (SHF_EXECINSTR) ayant des octets de fichier.
	[[nodiscard]] std::vector<const ElfSection *> ExecutableSections() const;
	/// Fonctions (symboles STT_FUNC de taille non nulle), triées par adresse.
	[[nodiscard]] std::vector<const ElfSymbol *> Functions() const;
	/// Fonctions importées (symboles STT_FUNC indéfinis — via la PLT).
	[[nodiscard]] std::vector<const ElfSymbol *> Imports() const;

	/// Notes ELF (PT_NOTE) : build-id, ABI-tag, etc.
	[[nodiscard]] std::vector<ElfNote> Notes() const;
	/// Diagnostic de sécurité : PIE, NX, RELRO, canari, fortification, build-id.
	[[nodiscard]] SecurityReport Security() const;

	/// Octets de fichier d'une section (vide si NOBITS). Indices déjà bornés.
	[[nodiscard]] std::string_view Bytes(const ElfSection &section) const;
	/// Traduit une adresse virtuelle en décalage fichier (via les PT_LOAD).
	[[nodiscard]] bool AddressToOffset(uint64_t address, uint64_t &offset) const;

private:
	std::vector<uint8_t> m_raw;
	uint16_t m_machine = 0;
	uint16_t m_type = 0; ///< e_type (ET_EXEC = 2, ET_DYN = 3)
	uint64_t m_entry = 0;
	std::vector<ElfSection> m_sections;
	std::vector<ElfSymbol> m_symbols;
	std::vector<ElfSegment> m_segments;
};

} // namespace rea
