// Désassembleur x86-64 bâti sur Zydis (amalgame vendu dans src/zydis/).
#include "rea/disasm.hpp"

#include <cctype>

// Zydis est un en-tête tiers : on désactive ses avertissements (macros
// d'endianness non définies, membre flexible C99) pour un build silencieux.
#if defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wundef"
#	pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <Zydis.h>
#if defined(__GNUC__)
#	pragma GCC diagnostic pop
#endif

namespace rea {

namespace {

/// Contexte Zydis partagé (décodeur + formateur, sans état entre deux appels).
struct ZydisCtx {
	ZydisDecoder decoder;
	ZydisFormatter formatter;

	ZydisCtx() {
		ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
		ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);
	}
};

ZydisCtx &Ctx() {
	static ZydisCtx ctx;
	return ctx;
}

/// Remplit `out` depuis une instruction Zydis décodée. Retourne faux si le
/// décodage échoue (données invalides ou tronquées).
bool DecodeInto(const uint8_t *data, size_t size, uint64_t address, Instruction &out) {
	ZydisDecodedInstruction insn;
	ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
	if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&Ctx().decoder, data, size, &insn, operands)))
		return false;

	out = Instruction{};
	out.address = address;
	out.length = insn.length;
	out.bytes.assign(data, data + insn.length);

	const char *mnem = ZydisMnemonicGetString(insn.mnemonic);
	out.mnemonic = mnem ? mnem : "???";
	for (char &c : out.mnemonic)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

	std::string ops;
	for (ZyanU8 i = 0; i < insn.operand_count_visible; ++i) {
		char buf[160];
		if (ZYAN_SUCCESS(ZydisFormatterFormatOperand(&Ctx().formatter, &insn, &operands[i], buf, sizeof(buf), address, nullptr))) {
			if (i)
				ops += ", ";
			ops += buf;
		}
	}
	out.operands = ops;

	switch (insn.meta.category) {
	case ZYDIS_CATEGORY_CALL: out.category = "call"; break;
	case ZYDIS_CATEGORY_RET: out.category = "ret"; break;
	case ZYDIS_CATEGORY_UNCOND_BR: out.category = "jmp"; break;
	case ZYDIS_CATEGORY_COND_BR: out.category = "jcc"; break;
	default: {
		const char *cat = ZydisCategoryGetString(insn.meta.category);
		out.category = cat ? cat : "?";
		break;
	}
	}

	out.isCall = insn.meta.category == ZYDIS_CATEGORY_CALL;
	out.isReturn = insn.meta.category == ZYDIS_CATEGORY_RET;
	out.isConditional = insn.meta.category == ZYDIS_CATEGORY_COND_BR;
	out.isBranch = insn.meta.category == ZYDIS_CATEGORY_UNCOND_BR || out.isConditional || out.isCall;

	if (out.isBranch)
		for (ZyanU8 i = 0; i < insn.operand_count; ++i) {
			ZyanU64 target = 0;
			if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&insn, &operands[i], address, &target))) {
				out.hasTarget = true;
				out.target = target;
				break;
			}
		}

	// Références de données : opérandes mémoire RIP-relatifs ou à adresse absolue.
	for (ZyanU8 i = 0; i < insn.operand_count; ++i) {
		if (operands[i].type != ZYDIS_OPERAND_TYPE_MEMORY)
			continue;
		const ZydisRegister base = operands[i].mem.base;
		if (base != ZYDIS_REGISTER_RIP && base != ZYDIS_REGISTER_NONE)
			continue;
		ZyanU64 addr = 0;
		if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&insn, &operands[i], address, &addr)))
			out.dataRefs.push_back(addr);
	}

	return true;
}

} // namespace

Result<Instruction, String> DecodeInstruction(const uint8_t *data, size_t size, uint64_t address) {
	Instruction insn;
	if (!DecodeInto(data, size, address, insn))
		return Err(String(std::format("instruction indécodable à 0x{:x}", address)));
	return Ok(std::move(insn));
}

std::vector<Instruction> Disassemble(const uint8_t *data, size_t size, uint64_t address) {
	std::vector<Instruction> out;
	size_t off = 0;
	while (off < size) {
		Instruction insn;
		if (!DecodeInto(data + off, size - off, address + off, insn))
			break;
		off += insn.length;
		out.push_back(std::move(insn));
	}
	return out;
}

} // namespace rea
