// Lifting « au mieux » d'une fonction x86-64 vers du C lisible.
#include "rea/lifter.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace rea {

namespace {

/// Nettoie un nom pour en faire un identifiant C valide.
std::string Sanitize(std::string_view s) {
	std::string out;
	out.reserve(s.size());
	for (char c : s) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
			out += c;
		else
			out += '_';
	}
	if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())))
		out.insert(out.begin(), '_');
	return out;
}

/// Vrai si `tok` ressemble à un registre (premier caractère alphabétique).
bool IsRegister(std::string_view tok) {
	return !tok.empty() && std::isalpha(static_cast<unsigned char>(tok.front()));
}

/// Découpe la chaîne d'opérandes Intel en jetons (« rax, 0x2A » → {rax, 0x2A}),
/// en supprimant les espaces autour des virgules.
std::vector<std::string> SplitOperands(const std::string &operands) {
	std::vector<std::string> out;
	std::string cur;
	auto flush = [&] {
		while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t'))
			cur.pop_back();
		if (!cur.empty())
			out.push_back(cur);
		cur.clear();
	};
	for (char c : operands) {
		if (c == ',') {
			flush();
		} else {
			if (cur.empty() && (c == ' ' || c == '\t'))
				continue; // saute l'espace qui suit une virgule
			cur += c;
		}
	}
	flush();
	return out;
}

/// Vrai si l'instruction peut être levée sans danger (opérandes registres / immédiats).
bool Liftable(const Instruction &insn) {
	if (insn.operands.empty())
		return true;
	for (const std::string &tok : SplitOperands(insn.operands))
		if (tok.find_first_of("[]") != std::string::npos || tok.find("ptr") != std::string::npos ||
			tok.find(':') != std::string::npos)
			return false;
	return true;
}

/// Opérateur C pour une mnémonique binaire (vide si non reconnue).
const char *BinaryOp(const std::string &m) {
	if (m == "add") return "+=";
	if (m == "sub") return "-=";
	if (m == "and") return "&=";
	if (m == "or") return "|=";
	if (m == "xor") return "^=";
	if (m == "shl" || m == "sal") return "<<=";
	if (m == "shr") return ">>=";
	if (m == "sar") return ">>=";
	if (m == "imul" || m == "mul") return "*=";
	if (m == "mov") return "=";
	if (m == "lea") return "=";
	return nullptr;
}

/// Rend la liste ordonnée des registres utilisés par les instructions levables.
std::vector<std::string> CollectRegisters(const std::vector<Instruction> &insns) {
	std::vector<std::string> regs;
	std::set<std::string> seen;
	for (const Instruction &insn : insns) {
		if (!Liftable(insn))
			continue;
		for (const std::string &tok : SplitOperands(insn.operands))
			if (IsRegister(tok) && seen.insert(tok).second)
				regs.push_back(tok);
	}
	return regs;
}

/// Ensemble des adresses cibles de sauts (pour les étiquettes goto).
std::set<uint64_t> BranchTargets(const std::vector<Instruction> &insns) {
	std::set<uint64_t> out;
	for (const Instruction &insn : insns)
		if (insn.isBranch && insn.hasTarget)
			out.insert(insn.target);
	return out;
}

// ── Registres GPR (analyse de signature) ────────────────────────────────────

/// Registre GPR canonique + largeur (8/16/32/64 bits). `canonical == nullptr` :
/// pas un GPR.
struct GprInfo {
	const char *canonical;
	int width;
};

GprInfo GprOf(std::string_view name) {
	static constexpr struct {
		std::string_view name;
		const char *canonical;
		int width;
	} kTable[] = {
		{"rax", "rax", 64}, {"eax", "rax", 32}, {"ax", "rax", 16}, {"al", "rax", 8}, {"ah", "rax", 8},
		{"rbx", "rbx", 64}, {"ebx", "rbx", 32}, {"bx", "rbx", 16}, {"bl", "rbx", 8}, {"bh", "rbx", 8},
		{"rcx", "rcx", 64}, {"ecx", "rcx", 32}, {"cx", "rcx", 16}, {"cl", "rcx", 8}, {"ch", "rcx", 8},
		{"rdx", "rdx", 64}, {"edx", "rdx", 32}, {"dx", "rdx", 16}, {"dl", "rdx", 8}, {"dh", "rdx", 8},
		{"rsi", "rsi", 64}, {"esi", "rsi", 32}, {"si", "rsi", 16}, {"sil", "rsi", 8},
		{"rdi", "rdi", 64}, {"edi", "rdi", 32}, {"di", "rdi", 16}, {"dil", "rdi", 8},
		{"rbp", "rbp", 64}, {"ebp", "rbp", 32}, {"bp", "rbp", 16}, {"bpl", "rbp", 8},
		{"rsp", "rsp", 64}, {"esp", "rsp", 32}, {"sp", "rsp", 16}, {"spl", "rsp", 8},
		{"r8", "r8", 64}, {"r8d", "r8", 32}, {"r8w", "r8", 16}, {"r8b", "r8", 8},
		{"r9", "r9", 64}, {"r9d", "r9", 32}, {"r9w", "r9", 16}, {"r9b", "r9", 8},
		{"r10", "r10", 64}, {"r10d", "r10", 32}, {"r10w", "r10", 16}, {"r10b", "r10", 8},
		{"r11", "r11", 64}, {"r11d", "r11", 32}, {"r11w", "r11", 16}, {"r11b", "r11", 8},
		{"r12", "r12", 64}, {"r12d", "r12", 32}, {"r12w", "r12", 16}, {"r12b", "r12", 8},
		{"r13", "r13", 64}, {"r13d", "r13", 32}, {"r13w", "r13", 16}, {"r13b", "r13", 8},
		{"r14", "r14", 64}, {"r14d", "r14", 32}, {"r14w", "r14", 16}, {"r14b", "r14", 8},
		{"r15", "r15", 64}, {"r15d", "r15", 32}, {"r15w", "r15", 16}, {"r15b", "r15", 8},
	};
	for (const auto &e : kTable)
		if (e.name == name)
			return {e.canonical, e.width};
	return {nullptr, 0};
}

/// Extrait les registres GPR d'un opérande (y compris dans `[base+index*scale]`).
std::vector<GprInfo> OperandGprs(std::string_view tok) {
	std::vector<GprInfo> out;
	size_t i = 0;
	while (i < tok.size()) {
		if (!std::isalpha(static_cast<unsigned char>(tok[i]))) {
			++i;
			continue;
		}
		size_t j = i;
		while (j < tok.size() && std::isalnum(static_cast<unsigned char>(tok[j])))
			++j;
		if (GprInfo g = GprOf(tok.substr(i, j - i)); g.canonical)
			out.push_back(g);
		i = j;
	}
	return out;
}

/// Type C correspondant à une largeur de registre.
const char *TypeForWidth(int w) {
	if (w == 8) return "char";
	if (w == 16) return "short";
	if (w == 64) return "long";
	return "int";
}

/// Nom du sous-registre de rax portant la valeur de retour d'une largeur donnée.
const char *ReturnRegForWidth(int w) {
	if (w == 8) return "al";
	if (w == 16) return "ax";
	if (w == 64) return "rax";
	return "eax";
}

/// Convertit un jeton d'opérande Intel en expression C, en renommant les
/// registres d'argument par leur nom de paramètre.
std::string LiftToken(const std::string &tok, const Signature *sig) {
	if (sig && IsRegister(tok)) {
		GprInfo g = GprOf(tok);
		if (g.canonical)
			for (size_t i = 0; i < sig->paramRegisters.size(); ++i)
				if (sig->paramRegisters[i] == g.canonical)
					return sig->paramNames[i];
	}
	return tok;
}

} // namespace

// ── Signature (convention System V AMD64) ────────────────────────────────────

std::string Signature::Params() const {
	if (paramNames.empty())
		return "void";
	std::string out;
	for (size_t i = 0; i < paramNames.size(); ++i) {
		if (i)
			out += ", ";
		out += paramTypes[i];
		out += ' ';
		out += paramNames[i];
	}
	return out;
}

Signature InferSignature(const Function &function) {
	std::set<std::string> written;		   // registres canoniques écrits
	std::map<std::string, int> readWidth;  // largeur de la première lecture
	std::set<std::string> inputs;		   // lus avant d'être écrits
	int raxWriteIdx = -1;				   // index de la dernière écriture « utile » dans rax
	int raxWriteWidth = 0;				   // largeur associée
	int lastCallIdx = -1;				   // index du dernier appel

	auto readReg = [&](const GprInfo &g) {
		if (!readWidth.contains(g.canonical))
			readWidth[g.canonical] = g.width;
		if (!written.contains(g.canonical))
			inputs.insert(g.canonical);
	};
	auto writeReg = [&](const GprInfo &g, int idx) {
		written.insert(g.canonical);
		if (g.canonical == std::string_view("rax")) {
			raxWriteIdx = idx;
			raxWriteWidth = g.width;
		}
	};

	const auto &insns = function.instructions;
	for (size_t idx = 0; idx < insns.size(); ++idx) {
		const Instruction &insn = insns[idx];
		const std::string &m = insn.mnemonic;
		const auto ops = SplitOperands(insn.operands);

		if (m == "call") {
			// L'appel écrit rax (retour du callee) : il « invalide » toute valeur
			// de retour calculée avant lui. Cible indirecte éventuellement lue.
			lastCallIdx = int(idx);
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0]))
					readReg(g);
			continue;
		}
		if (m == "ret" || m == "jmp") {
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0]))
					readReg(g);
			continue;
		}
		if (insn.isConditional) {
			// jcc : cible indirecte éventuelle.
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0]))
					readReg(g);
			continue;
		}
		if (m == "push") {
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0]))
					readReg(g);
			continue;
		}
		if (m == "pop") {
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0]))
					writeReg(g, int(idx));
			continue;
		}
		if (m == "inc" || m == "dec" || m == "neg" || m == "not") {
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0])) {
					readReg(g);
					writeReg(g, int(idx));
				}
			continue;
		}
		if (m == "cmp" || m == "test") {
			for (const std::string &tok : ops)
				for (const GprInfo &g : OperandGprs(tok))
					readReg(g);
			continue;
		}
		const bool rw = m == "add" || m == "sub" || m == "and" || m == "or" || m == "xor" || m == "shl" ||
						m == "sal" || m == "shr" || m == "sar" || m == "imul" || m == "mul";
		if (rw) {
			if (!ops.empty())
				for (const GprInfo &g : OperandGprs(ops[0])) {
					readReg(g);
					writeReg(g, int(idx));
				}
			if (ops.size() > 1)
				for (const GprInfo &g : OperandGprs(ops[1]))
					readReg(g);
			continue;
		}
		// mov / lea / movsx / … : op0 écrit (registre nu) ou lu (base mémoire), op1 lu.
		if (ops.size() >= 2) {
			const bool dstIsReg = ops[0].find_first_of("[]") == std::string::npos;
			for (const GprInfo &g : OperandGprs(ops[0]))
				dstIsReg ? writeReg(g, int(idx)) : readReg(g);
			for (const GprInfo &g : OperandGprs(ops[1]))
				readReg(g);
		} else if (!ops.empty()) {
			for (const GprInfo &g : OperandGprs(ops[0]))
				readReg(g);
		}
	}

	// Paramètres : registres d'argument ABI lus avant d'être écrits.
	static constexpr std::string_view kArgRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
	static constexpr const char *kArgNames[6] = {"a", "b", "c", "d", "e", "f"};

	// Détection des pointeurs : un registre d'argument utilisé comme base/index
	// d'un opérande mémoire est probablement un pointeur (→ « void* »).
	std::set<std::string> pointerUsed;
	for (const Instruction &insn : insns)
		for (const std::string &tok : SplitOperands(insn.operands))
			if (tok.find('[') != std::string::npos)
				for (const GprInfo &g : OperandGprs(tok))
					pointerUsed.insert(g.canonical);

	Signature sig;
	for (int i = 0; i < 6; ++i) {
		const std::string reg(kArgRegs[i]);
		if (!inputs.contains(reg))
			continue;
		sig.paramRegisters.push_back(reg);
		sig.paramNames.emplace_back(kArgNames[i]);
		const int w = readWidth[reg];
		sig.paramTypes.emplace_back((w == 64 && pointerUsed.contains(reg)) ? "void*" : TypeForWidth(w));
	}

	// Type de retour : une valeur n'est rendue que si rax est écrit (hors appel)
	// *après* le dernier appel — sinon le registre ne porte que le retour d'un
	// callee, et la fonction est `void`.
	if (raxWriteIdx > lastCallIdx) {
		sig.returnType = TypeForWidth(raxWriteWidth);
		sig.returnRegister = ReturnRegForWidth(raxWriteWidth);
	}

	return sig;
}

// ── Lifting du corps ─────────────────────────────────────────────────────────

String LiftFunctionBody(const Function &function, const LiftOptions &options, const Signature *sig) {
	const auto &insns = function.instructions;
	const std::set<uint64_t> targets = BranchTargets(insns);
	std::vector<std::string> regs = CollectRegisters(insns);
	if (sig && !sig->returnRegister.empty() && std::find(regs.begin(), regs.end(), sig->returnRegister) == regs.end())
		regs.push_back(sig->returnRegister);

	std::string out;

	// Déclarations des registres observés (pseudo-état de la fonction).
	if (options.declareRegisters && !regs.empty()) {
		for (const std::string &r : regs)
			out += std::format("\tuint64_t {} = 0;\n", r);
		out += '\n';
	}

	for (const Instruction &insn : insns) {
		// Étiquette de branche.
		if (targets.contains(insn.address))
			out += std::format("L_{:x}:\n", insn.address);

		// Annotation d'assemblage.
		if (options.annotate)
			out += std::format("\t// {:08x}: {} {}\n", insn.address, insn.mnemonic, insn.operands);

		// Retour.
		if (insn.isReturn) {
			out += (sig && !sig->returnRegister.empty()) ? std::format("\treturn {};\n", sig->returnRegister) : "\treturn;\n";
			continue;
		}

		// Sauts.
		if (insn.isBranch && insn.isConditional) {
			// Branche conditionnelle : laissée en commentaire (condition inconnue).
			continue;
		}
		if (insn.isBranch && insn.hasTarget) {
			if (insn.isCall)
				out += std::format("\tsub_{:x}();\n", insn.target);
			else
				out += std::format("\tgoto L_{:x};\n", insn.target);
			continue;
		}
		if (insn.isCall) {
			// Appel indirect : commentaire.
			continue;
		}

		// Instructions à un opérande.
		const std::string &m = insn.mnemonic;
		if (m == "nop" || m == "int3" || m == "ud2") {
			out += "\t;\n";
			continue;
		}
		if ((m == "inc" || m == "dec") && Liftable(insn)) {
			out += std::format("\t{}{};\n", LiftToken(insn.operands, sig), m == "inc" ? "++" : "--");
			continue;
		}
		if ((m == "neg" || m == "not") && Liftable(insn)) {
			out += std::format("\t{} = {}{};\n", LiftToken(insn.operands, sig), m == "neg" ? "-" : "~", LiftToken(insn.operands, sig));
			continue;
		}

		// Instructions binaires levables.
		if (const char *op = BinaryOp(m); op && Liftable(insn)) {
			const auto ops = SplitOperands(insn.operands);
			if (ops.size() >= 2)
				out += std::format("\t{} {} {};\n", LiftToken(ops[0], sig), op, LiftToken(ops[1], sig));
			continue;
		}

		// Tout le reste : commentaire d'assemblage (déjà émis en annotation).
	}

	return String(std::move(out));
}

String LiftFunction(const Function &function, const LiftOptions &options) {
	const std::string name = Sanitize(function.name.empty() ? std::format("sub_{:x}", function.address) : function.name);
	const Signature sig = InferSignature(function);
	return String(std::format("{} {}({}) {{\n{}}}\n", sig.returnType, name, sig.Params(), LiftFunctionBody(function, options, &sig).View()));
}

} // namespace rea
