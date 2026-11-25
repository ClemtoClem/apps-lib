// Analyseur ELF64 x86-64 petit-boutiste.
#include "rea/elf.hpp"

#include <algorithm>
#include <fstream>

namespace rea {

namespace {

// Constantes ELF (partagées entre les plates-formes, sur 64 bits).
constexpr uint8_t kMagic[4] = {0x7f, 'E', 'L', 'F'};
constexpr uint8_t kClass64 = 2;
constexpr uint8_t kDataLsb = 1;
constexpr uint16_t kMachineX86_64 = 62;
constexpr uint16_t kEtExec = 2, kEtDyn = 3;
constexpr uint32_t kPtLoad = 1, kPtDynamic = 2, kPtNote = 4;
constexpr uint32_t kPtGnuStack = 0x6474e551, kPtGnuRelro = 0x6474e552;
constexpr uint32_t kPfX = 1;
constexpr uint32_t kShtSymtab = 2, kShtStrtab = 3, kShtDynsym = 11;
// Entrées de la section dynamique (Elf64_Dyn).
constexpr uint64_t kDtNull = 0, kDtBindNow = 24, kDtFlags = 30, kDtFlags1 = 0x6ffffffb;
constexpr uint64_t kDfBindNow = 0x8, kDf1Now = 0x8;
// Notes GNU.
constexpr uint32_t kGnuAbiTag = 1, kGnuBuildId = 3;

// Lecture petit-boutiste bornée.
struct Reader {
	const std::vector<uint8_t> &b;

	[[nodiscard]] bool Ok(size_t off, size_t n) const noexcept { return off <= b.size() && n <= b.size() - off; }
	[[nodiscard]] uint8_t U8(size_t off) const { return b[off]; }
	[[nodiscard]] uint16_t U16(size_t off) const { return uint16_t(b[off]) | uint16_t(uint16_t(b[off + 1]) << 8); }
	[[nodiscard]] uint32_t U32(size_t off) const {
		return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) | (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24);
	}
	[[nodiscard]] uint64_t U64(size_t off) const { return uint64_t(U32(off)) | (uint64_t(U32(off + 4)) << 32); }
};

// Lit la chaîne C à `off` dans une table des chaînes `strtab` (bornée).
std::string ReadName(std::string_view strtab, uint32_t off) {
	if (off >= strtab.size())
		return {};
	const char *p = strtab.data() + off;
	const size_t max = strtab.size() - off;
	const size_t len = strnlen(p, max);
	return std::string(p, len);
}

// Rend les octets en hexadécimal minuscule (« 1a2b3c… »).
std::string ToHex(const std::vector<uint8_t> &bytes) {
	std::string out;
	out.reserve(bytes.size() * 2);
	for (uint8_t b : bytes)
		out += std::format("{:02x}", b);
	return out;
}

} // namespace

Result<Elf, String> Elf::Load(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return Err(String(std::format("impossible d'ouvrir « {} »", path.string())));
	std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	if (raw.size() < 64)
		return Err("fichier trop petit pour un en-tête ELF");
	if (raw[0] != kMagic[0] || raw[1] != kMagic[1] || raw[2] != kMagic[2] || raw[3] != kMagic[3])
		return Err("pas un fichier ELF (magic manquant)");
	if (raw[4] != kClass64 || raw[5] != kDataLsb)
		return Err("seuls les ELF 64 bits petit-boutiste sont pris en charge");

	Reader r{raw};
	const uint16_t type = r.U16(16);
	if (type != kEtExec && type != kEtDyn)
		return Err("ELF ni exécutable ni partagé (ET_EXEC / ET_DYN attendu)");
	const uint16_t machine = r.U16(18);
	if (machine != kMachineX86_64)
		return Err(std::format("machine non prise en charge (e_machine = {})", machine));

	// L'objet `Elf` possède les octets ; le lecteur référence ensuite ce tampon
	// (et non la variable locale, invalidée par le déplacement).
	Elf elf;
	elf.m_raw = std::move(raw);
	Reader rw{elf.m_raw};
	elf.m_machine = machine;
	elf.m_type = type;
	elf.m_entry = rw.U64(24);

	const uint64_t phoff = rw.U64(32), shoff = rw.U64(40);
	const uint16_t phentsize = rw.U16(54), phnum = rw.U16(56);
	const uint16_t shentsize = rw.U16(58), shnum = rw.U16(60), shstrndx = rw.U16(62);

	// Segments de programme.
	if (phentsize >= 56)
		for (uint16_t i = 0; i < phnum; ++i) {
			const size_t off = size_t(phoff) + size_t(i) * phentsize;
			if (!rw.Ok(off, 56))
				break;
			ElfSegment seg;
			seg.type = rw.U32(off);
			seg.flags = rw.U32(off + 4);
			seg.offset = rw.U64(off + 8);
			seg.vaddr = rw.U64(off + 16);
			seg.filesz = rw.U64(off + 32);
			seg.memsz = rw.U64(off + 40);
			elf.m_segments.push_back(seg);
		}

	// Sections (les noms sont résolus après coup via la table shstrtab).
	std::vector<uint32_t> nameOffsets;
	if (shentsize >= 64)
		for (uint16_t i = 0; i < shnum; ++i) {
			const size_t off = size_t(shoff) + size_t(i) * shentsize;
			if (!rw.Ok(off, 64))
				break;
			ElfSection s;
			nameOffsets.push_back(rw.U32(off));
			s.type = rw.U32(off + 4);
			s.flags = rw.U64(off + 8);
			s.address = rw.U64(off + 16);
			s.offset = rw.U64(off + 24);
			s.size = rw.U64(off + 32);
			s.link = rw.U32(off + 40);
			s.info = rw.U32(off + 44);
			s.align = rw.U64(off + 48);
			s.entsize = rw.U64(off + 56);
			elf.m_sections.push_back(std::move(s));
		}

	// Résolution des noms de sections.
	if (shstrndx < elf.m_sections.size()) {
		const auto strtab = elf.Bytes(elf.m_sections[shstrndx]);
		for (size_t i = 0; i < elf.m_sections.size() && i < nameOffsets.size(); ++i)
			elf.m_sections[i].name = ReadName(strtab, nameOffsets[i]);
	}

	// Symboles : tables SHT_SYMTAB / SHT_DYNSYM pointant vers leur strtab (sh_link).
	for (const ElfSection &symtab : elf.m_sections) {
		if (symtab.type != kShtSymtab && symtab.type != kShtDynsym)
			continue;
		if (symtab.link >= elf.m_sections.size())
			continue;
		const auto strtab = elf.Bytes(elf.m_sections[symtab.link]);
		const size_t entsize = symtab.entsize >= 24 ? symtab.entsize : 24;
		const size_t count = entsize ? symtab.size / entsize : 0;
		for (size_t i = 0; i < count; ++i) {
			const size_t off = size_t(symtab.offset) + i * entsize;
			if (!rw.Ok(off, 24))
				break;
			ElfSymbol sym;
			sym.name = ReadName(strtab, rw.U32(off));
			sym.info = rw.U8(off + 4);
			sym.section = rw.U16(off + 6);
			sym.value = rw.U64(off + 8);
			sym.size = rw.U64(off + 16);
			elf.m_symbols.push_back(std::move(sym));
		}
	}

	// Trie stable des symboles par adresse (pratique pour l'affichage).
	std::stable_sort(elf.m_symbols.begin(), elf.m_symbols.end(),
					 [](const ElfSymbol &a, const ElfSymbol &b) { return a.value < b.value; });

	return Ok(std::move(elf));
}

const ElfSection *Elf::FindSection(std::string_view name) const {
	for (const ElfSection &s : m_sections)
		if (s.name == name)
			return &s;
	return nullptr;
}

const ElfSymbol *Elf::FindSymbol(std::string_view name) const {
	for (const ElfSymbol &s : m_symbols)
		if (s.name == name)
			return &s;
	return nullptr;
}

std::vector<const ElfSection *> Elf::ExecutableSections() const {
	std::vector<const ElfSection *> out;
	for (const ElfSection &s : m_sections)
		if (s.IsExecutable() && s.HasFileBytes() && s.size > 0)
			out.push_back(&s);
	return out;
}

std::vector<const ElfSymbol *> Elf::Functions() const {
	std::vector<const ElfSymbol *> out;
	for (const ElfSymbol &s : m_symbols)
		if (s.IsFunction() && s.size > 0 && s.value != 0)
			out.push_back(&s);
	return out;
}

std::string_view Elf::Bytes(const ElfSection &section) const {
	if (!section.HasFileBytes())
		return {};
	if (section.offset >= m_raw.size())
		return {};
	const size_t n = std::min<size_t>(section.size, m_raw.size() - section.offset);
	return {reinterpret_cast<const char *>(m_raw.data() + section.offset), n};
}

bool Elf::AddressToOffset(uint64_t address, uint64_t &offset) const {
	for (const ElfSegment &p : m_segments) {
		if (p.type != kPtLoad)
			continue;
		if (address >= p.vaddr && address < p.vaddr + p.filesz) {
			offset = p.offset + (address - p.vaddr);
			return true;
		}
	}
	return false;
}

std::vector<const ElfSymbol *> Elf::Imports() const {
	std::vector<const ElfSymbol *> out;
	for (const ElfSymbol &s : m_symbols)
		if (s.section == 0 /* SHN_UNDEF */ && s.IsFunction() && !s.name.empty())
			out.push_back(&s);
	return out;
}

std::vector<ElfNote> Elf::Notes() const {
	std::vector<ElfNote> out;
	Reader r{m_raw};
	for (const ElfSegment &p : m_segments) {
		if (p.type != kPtNote)
			continue;
		const size_t start = size_t(p.offset);
		const size_t end = std::min<size_t>(start + size_t(p.filesz), m_raw.size());
		size_t off = start;
		while (off + 12 <= end && r.Ok(off, 12)) {
			const uint32_t namesz = r.U32(off);
			const uint32_t descsz = r.U32(off + 4);
			const uint32_t type = r.U32(off + 8);
			const size_t nameOff = off + 12;
			if (nameOff + namesz > end)
				break;

			std::string owner;
			owner.reserve(namesz);
			for (size_t i = 0; i < namesz; ++i) {
				const char c = static_cast<char>(m_raw[nameOff + i]);
				if (c == '\0')
					break;
				owner += c;
			}

			const size_t descOff = (nameOff + namesz + 3) & ~size_t(3);
			const size_t descEnd = descOff + descsz;
			if (descEnd > end)
				break;

			ElfNote note;
			note.owner = std::move(owner);
			note.type = type;
			note.desc.assign(m_raw.begin() + long(descOff), m_raw.begin() + long(descEnd));
			out.push_back(std::move(note));
			off = (descEnd + 3) & ~size_t(3);
		}
	}
	return out;
}

SecurityReport Elf::Security() const {
	SecurityReport r;
	r.pie = (m_type == kEtDyn);

	for (const ElfSegment &p : m_segments) {
		if (p.type == kPtGnuStack)
			r.nx = (p.flags & kPfX) == 0;
		else if (p.type == kPtGnuRelro)
			r.relro = true;
	}

	// BIND_NOW (RELRO complet) depuis la section dynamique.
	bool bindNow = false;
	for (const ElfSegment &p : m_segments) {
		if (p.type != kPtDynamic)
			continue;
		Reader rd{m_raw};
		const size_t end = std::min<size_t>(size_t(p.offset) + size_t(p.filesz), m_raw.size());
		for (size_t off = size_t(p.offset); off + 16 <= end; off += 16) {
			const uint64_t tag = rd.U64(off);
			const uint64_t val = rd.U64(off + 8);
			if (tag == kDtNull)
				break;
			if (tag == kDtBindNow || (tag == kDtFlags && (val & kDfBindNow)) || (tag == kDtFlags1 && (val & kDf1Now)))
				bindNow = true;
		}
	}
	r.fullRelro = r.relro && bindNow;

	const ElfSymbol *canary = FindSymbol("__stack_chk_fail");
	r.stackCanary = canary && canary->section == 0;
	for (const char *name : {"__printf_chk", "__memcpy_chk", "__sprintf_chk", "__strcpy_chk"})
		if (const ElfSymbol *chk = FindSymbol(name); chk && chk->section == 0) {
			r.fortify = true;
			break;
		}

	for (const ElfNote &n : Notes())
		if (n.owner == "GNU" && n.type == kGnuBuildId)
			r.buildId = ToHex(n.desc);

	return r;
}

} // namespace rea
