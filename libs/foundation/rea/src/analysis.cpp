// Analyses : chaînes, désassemblage complet, graphe d'appels et références de données.
#include "rea/analysis.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <unordered_map>

#include "rea/lifter.hpp"

namespace rea {

namespace {

bool Printable(uint8_t b) {
	return b >= 0x20 && b <= 0x7e;
}

/// Rend le premier jeton de version (`X.Y[.Z]`) trouvé après `marker` dans `text`.
std::string FindVersion(std::string_view text, std::string_view marker) {
	const size_t p = text.find(marker);
	if (p == std::string_view::npos)
		return {};
	const size_t limit = std::min(text.size(), p + marker.size() + 64);
	size_t i = p + marker.size();
	while (i < limit) {
		if (std::isdigit(static_cast<unsigned char>(text[i]))) {
			size_t e = i;
			while (e < limit && (std::isdigit(static_cast<unsigned char>(text[e])) || text[e] == '.'))
				++e;
			return std::string(text.substr(i, e - i));
		}
		++i;
	}
	return {};
}

/// Découpe une séquence d'instructions en blocs de base et calcule les
/// successeurs (sauts, retombées) pour reconstruire le graphe de flot.
std::vector<BasicBlock> BuildBlocks(const std::vector<Instruction> &insns) {
	std::vector<BasicBlock> out;
	if (insns.empty())
		return out;

	const uint64_t lo = insns.front().address;
	const uint64_t hi = insns.back().address + insns.back().length;

	// Blocs leaders : début, cibles de saut internes, retombées de saut.
	std::set<uint64_t> leaders{insns.front().address};
	for (const Instruction &i : insns) {
		if (i.isBranch && i.hasTarget && i.target >= lo && i.target < hi)
			leaders.insert(i.target);
		if ((i.isBranch || i.isReturn) && i.address + i.length < hi)
			leaders.insert(i.address + i.length);
	}

	// Découpe.
	size_t start = 0;
	for (size_t idx = 1; idx < insns.size(); ++idx) {
		if (!leaders.contains(insns[idx].address))
			continue;
		BasicBlock b;
		b.address = insns[start].address;
		for (size_t k = start; k < idx; ++k)
			b.instructions.push_back(insns[k]);
		out.push_back(std::move(b));
		start = idx;
	}
	BasicBlock last;
	last.address = insns[start].address;
	for (size_t k = start; k < insns.size(); ++k)
		last.instructions.push_back(insns[k]);
	out.push_back(std::move(last));

	// Index adresse → bloc.
	std::unordered_map<uint64_t, size_t> index;
	for (size_t i = 0; i < out.size(); ++i)
		index[out[i].address] = i;

	// Successeurs.
	for (size_t i = 0; i < out.size(); ++i) {
		const Instruction &tail = out[i].instructions.back();
		auto succ = [&](uint64_t addr) {
			if (index.contains(addr))
				out[i].successors.push_back(addr);
		};
		if (tail.isReturn)
			continue;
		if (tail.isBranch && tail.hasTarget && tail.target >= lo && tail.target < hi) {
			succ(tail.target);
			if (tail.isConditional)
				succ(tail.address + tail.length);
		} else {
			succ(tail.address + tail.length);
		}
	}

	return out;
}

} // namespace

std::vector<StringRef> ExtractStrings(const Elf &elf, size_t minLength) {
	std::vector<StringRef> out;
	for (const ElfSection &s : elf.Sections()) {
		if (!s.HasFileBytes())
			continue;
		const auto bytes = elf.Bytes(s);
		size_t i = 0;
		while (i < bytes.size()) {
			if (!Printable(uint8_t(bytes[i]))) {
				++i;
				continue;
			}
			const size_t start = i;
			while (i < bytes.size() && Printable(uint8_t(bytes[i])))
				++i;
			if (i - start >= minLength) {
				StringRef ref;
				ref.section = s.name;
				ref.offset = s.offset + start;
				ref.address = s.address + start;
				ref.text.assign(bytes.substr(start, i - start));
				out.push_back(std::move(ref));
			}
		}
	}
	return out;
}

std::vector<FunctionAnalysis> AnalyzeFunctions(const Elf &elf) {
	// Index adresse → symbole de fonction (pour résoudre noms et xrefs).
	std::unordered_map<uint64_t, const ElfSymbol *> byAddr;
	std::vector<FunctionAnalysis> result;

	for (const ElfSymbol *sym : elf.Functions()) {
		byAddr[sym->value] = sym;
		FunctionAnalysis fa;
		fa.symbol = sym;
		result.push_back(std::move(fa));
	}

	// Désassemble chaque fonction.
	for (FunctionAnalysis &fa : result) {
		const uint64_t addr = fa.symbol->value;
		uint64_t off = 0;
		if (elf.AddressToOffset(addr, off)) {
			const auto &raw = elf.Raw();
			const size_t size = std::min<uint64_t>(fa.symbol->size, raw.size() - off);
			fa.instructions = Disassemble(raw.data() + off, size, addr);
		}
		fa.blocks = BuildBlocks(fa.instructions);
		for (const Instruction &insn : fa.instructions) {
			if (!insn.isCall || !insn.hasTarget)
				continue;
			fa.calls.push_back(insn.target);
			if (const auto it = byAddr.find(insn.target); it != byAddr.end())
				fa.callNames.push_back(it->second->name);
			else
				fa.callNames.push_back(std::format("sub_{:x}", insn.target));
		}
	}

	// Références croisées : construit la liste (dédoublonnée) des appelants.
	std::unordered_map<uint64_t, std::set<uint64_t>> callersOf;
	for (const FunctionAnalysis &fa : result)
		for (uint64_t target : fa.calls)
			callersOf[target].insert(fa.symbol->value);
	for (FunctionAnalysis &fa : result) {
		const auto it = callersOf.find(fa.symbol->value);
		if (it == callersOf.end())
			continue;
		fa.xrefs = it->second.size();
		for (uint64_t caller : it->second) {
			fa.callers.push_back(caller);
			if (const auto c = byAddr.find(caller); c != byAddr.end())
				fa.callerNames.push_back(c->second->name);
			else
				fa.callerNames.push_back(std::format("sub_{:x}", caller));
		}
	}

	// Références de données : relie chaque fonction aux chaînes / globales qu'elle
	// charge via des opérandes mémoire RIP-relatifs (lea/mov rax, [rip+…]).
	std::map<uint64_t, std::string> stringAt; // adresse de début → texte
	for (const StringRef &s : ExtractStrings(elf, 4))
		if (s.address != 0)
			stringAt.emplace(s.address, s.text);
	std::map<uint64_t, std::string> objectAt; // adresse → nom de globale (STT_OBJECT)
	for (const ElfSymbol &s : elf.Symbols())
		if (s.Type() == 1 /* STT_OBJECT */ && s.value != 0 && !s.name.empty())
			objectAt.emplace(s.value, s.name);

	auto resolve = [&](uint64_t addr) -> std::string {
		// Chaîne contenant l'adresse (début exact ou milieu).
		auto it = stringAt.upper_bound(addr);
		if (it != stringAt.begin()) {
			--it;
			if (addr < it->first + it->second.size())
				return std::format("\"{}\"", it->second);
		}
		if (auto o = objectAt.find(addr); o != objectAt.end())
			return o->second;
		return std::format("data_{:x}", addr);
	};

	for (FunctionAnalysis &fa : result) {
		for (const Instruction &insn : fa.instructions) {
			for (uint64_t addr : insn.dataRefs) {
				const std::string what = resolve(addr);
				const bool dup = std::any_of(fa.dataRefs.begin(), fa.dataRefs.end(),
											 [&](const DataRef &r) { return r.from == insn.address && r.target == addr; });
				if (!dup)
					fa.dataRefs.push_back({insn.address, addr, what});
			}
		}
	}

	return result;
}

BinaryIdentity DetectIdentity(const Elf &elf) {
	BinaryIdentity id;

	// Liaison statique : absence d'interpréteur PT_INTERP.
	id.staticallyLinked = true;
	for (const ElfSegment &p : elf.Segments())
		if (p.type == 3 /* PT_INTERP */) {
			id.staticallyLinked = false;
			break;
		}

	// Symboles : absence de .symtab (binaire « stripé »).
	id.stripped = elf.FindSection(".symtab") == nullptr;

	// Marqueurs du compilateur (section .comment, entrées séparées par NUL).
	std::string comment;
	if (const ElfSection *c = elf.FindSection(".comment"))
		comment.assign(elf.Bytes(*c).begin(), elf.Bytes(*c).end());

	const auto imports = elf.Imports();
	// Go : marqueurs structurels (section .go.buildinfo, note « Go » du build ID).
	// On évite toute recherche de chaîne (le code de ce détecteur contient lui-même
	// des littéraux « Go… » qui fausseraient l'analyse de son propre binaire).
	bool isGo = elf.FindSection(".go.buildinfo") != nullptr;
	if (!isGo)
		for (const ElfNote &n : elf.Notes())
			if (n.owner == "Go") {
				isGo = true;
				break;
			}

	bool isRust = comment.find("rustc") != std::string::npos;
	for (const ElfSymbol *s : imports)
		if (s->name == "rust_eh_personality") {
			isRust = true;
			break;
		}
	bool hasCxx = false;
	for (const ElfSymbol &s : elf.Symbols())
		if (s.name.starts_with("_Z")) {
			hasCxx = true;
			break;
		}

	if (isGo) {
		id.language = "Go";
		for (const StringRef &s : ExtractStrings(elf, 8))
			if (s.text.starts_with("go1.")) {
				id.toolchain = s.text;
				if (const size_t sp = id.toolchain.find(' '); sp != std::string::npos)
					id.toolchain.resize(sp);
				break;
			}
	} else if (isRust) {
		id.language = "Rust";
		if (const std::string v = FindVersion(comment, "rustc version "); !v.empty())
			id.toolchain = "rustc " + v;
		else
			id.toolchain = "rustc";
	} else {
		if (comment.find("GCC:") != std::string::npos) {
			id.language = hasCxx ? "C++" : "C";
			if (const std::string v = FindVersion(comment, "GCC:"); !v.empty())
				id.toolchain = "GCC " + v;
			else
				id.toolchain = "GCC";
		} else if (comment.find("clang version") != std::string::npos) {
			id.language = hasCxx ? "C++" : "C";
			if (const std::string v = FindVersion(comment, "clang version "); !v.empty())
				id.toolchain = "Clang " + v;
			else
				id.toolchain = "Clang";
		} else {
			id.language = hasCxx ? "C++" : "C";
		}
	}

	return id;
}

Result<size_t, String> ExportC(const std::vector<FunctionAnalysis> &functions, const std::filesystem::path &directory) {
	std::error_code ec;
	std::filesystem::create_directories(directory, ec);
	if (ec)
		return Err(String(std::format("création de « {} » impossible : {}", directory.string(), ec.message())));

	size_t count = 0;
	for (const FunctionAnalysis &f : functions) {
		Function fn;
		fn.name = f.symbol->name;
		fn.address = f.symbol->value;
		fn.instructions = f.instructions;
		const String code = LiftFunction(fn);

		// Nom de fichier sûr (identifiant + adresse pour lever les collisions).
		std::string base;
		for (char c : f.symbol->name) {
			if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')
				base += c;
			else
				base += '_';
		}
		if (base.empty())
			base = std::format("sub_{:x}", f.symbol->value);

		std::ofstream out(directory / (base + ".c"));
		if (!out)
			continue;
		out << "// " << f.symbol->name << " @ 0x" << std::hex << f.symbol->value << std::dec
			<< " — " << f.instructions.size() << " instruction(s)\n";
		out << "// reconstruit par REA (rétro-ingénierie)\n";
		out << "#include <stdint.h>\n\n";
		out << code.View();
		++count;
	}
	return Ok(count);
}

} // namespace rea
