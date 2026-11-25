#include "consoles/arm/disasm.hpp"

#include <format>
#include <string>

namespace consoles::arm {

namespace {

constexpr const char *kCond[16] = {"EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT", "LE", "", "NV"};
constexpr const char *kDataOp[16] = {"AND", "EOR", "SUB", "RSB", "ADD", "ADC", "SBC", "RSC", "TST", "TEQ", "CMP", "CMN", "ORR", "MOV", "BIC", "MVN"};
constexpr const char *kShift[4] = {"LSL", "LSR", "ASR", "ROR"};

std::string Reg(uint32_t r) {
	switch (r & 15) {
	case 13: return "SP";
	case 14: return "LR";
	case 15: return "PC";
	default: return std::format("R{}", r & 15);
	}
}

std::string RegList(uint32_t mask, int count) {
	std::string out = "{";
	for (int r = 0; r < count; ++r) {
		if (!(mask & (1u << r)))
			continue;
		int end = r;
		while (end + 1 < count && (mask & (1u << (end + 1))))
			++end;
		if (out.size() > 1)
			out += ",";
		out += end > r + 1 ? Reg(uint32_t(r)) + "-" + Reg(uint32_t(end)) : (end == r + 1 ? Reg(uint32_t(r)) + "," + Reg(uint32_t(end)) : Reg(uint32_t(r)));
		r = end;
	}
	return out + "}";
}

std::string Hex(uint32_t value) { return value < 10 ? std::format("#{}", value) : std::format("#0x{:X}", value); }

/// Second opérande d'une instruction de traitement de données ARM.
std::string Operand2(uint32_t op) {
	if (op & (1u << 25)) {
		const uint32_t imm = op & 0xFF, rotate = ((op >> 8) & 15) * 2;
		return Hex(rotate ? (imm >> rotate) | (imm << (32 - rotate)) : imm);
	}
	const std::string rm = Reg(op);
	const uint32_t type = (op >> 5) & 3;
	if (op & 0x10)
		return std::format("{},{} {}", rm, kShift[type], Reg(op >> 8));
	uint32_t amount = (op >> 7) & 31;
	if (amount == 0) {
		if (type == 0)
			return rm;
		if (type == 3)
			return rm + ",RRX";
		amount = 32;
	}
	return std::format("{},{} #{}", rm, kShift[type], amount);
}

emu::DisassembledLine Line(uint32_t address, uint32_t size, std::string mnemonic, std::string operands = {}) {
	return {address, size, std::move(mnemonic), std::move(operands)};
}

} // namespace

emu::DisassembledLine DisassembleArm(uint32_t address, uint32_t op) {
	const uint32_t cond = op >> 28;
	const std::string c = kCond[cond];
	const std::string rn = Reg(op >> 16), rd = Reg(op >> 12);
	auto line = [&](std::string mnemonic, std::string operands = {}) { return Line(address, 4, std::move(mnemonic), std::move(operands)); };

	if (cond == 0xF && (op & 0x0E000000) == 0x0A000000) { // BLX immédiat
		const int32_t offset = (int32_t(op << 8) >> 6) | int32_t((op >> 23) & 2);
		return line("BLX", std::format("0x{:08X}", address + 8 + uint32_t(offset)));
	}
	if (cond == 0xF) // espace inconditionnel d'ARMv5 (hors BLX immédiat) : PLD, etc.
		return line("DCD", std::format("0x{:08X}", op));
	if ((op & 0x0FFFFFF0) == 0x012FFF10)
		return line("BX" + c, Reg(op));
	if ((op & 0x0FFFFFF0) == 0x012FFF30)
		return line("BLX" + c, Reg(op));
	if ((op & 0x0E000000) == 0x0A000000) {
		const int32_t offset = int32_t(op << 8) >> 6;
		return line(((op & (1u << 24)) ? "BL" : "B") + c, std::format("0x{:08X}", address + 8 + uint32_t(offset)));
	}
	if ((op & 0x0F000000) == 0x0F000000)
		return line("SWI" + c, Hex(op & 0xFFFFFF));
	if ((op & 0x0FFF0FF0) == 0x016F0F10)
		return line("CLZ" + c, std::format("{},{}", rd, Reg(op)));
	if ((op & 0x0FC000F0) == 0x00000090) { // MUL / MLA
		const bool accumulate = op & (1u << 21);
		const std::string s = (op & (1u << 20)) ? "S" : "";
		const std::string operands = std::format("{},{},{}", Reg(op >> 16), Reg(op), Reg(op >> 8));
		return line((accumulate ? "MLA" : "MUL") + c + s, accumulate ? operands + "," + Reg(op >> 12) : operands);
	}
	if ((op & 0x0F8000F0) == 0x00800090) { // multiplications longues
		static constexpr const char *kLong[4] = {"UMULL", "UMLAL", "SMULL", "SMLAL"};
		return line(kLong[(op >> 21) & 3] + c + ((op & (1u << 20)) ? "S" : ""),
					std::format("{},{},{},{}", Reg(op >> 12), Reg(op >> 16), Reg(op), Reg(op >> 8)));
	}
	if ((op & 0x0FB00FF0) == 0x01000090)
		return line(((op & (1u << 22)) ? "SWPB" : "SWP") + c, std::format("{},{},[{}]", rd, Reg(op), rn));
	if ((op & 0x0E000090) == 0x00000090 && (op & 0x60)) { // transferts de demi-mots et d'octets signés
		static constexpr const char *kKind[4] = {"", "H", "SB", "SH"};
		const bool load = op & (1u << 20), up = op & (1u << 23), pre = op & (1u << 24), writeback = op & (1u << 21);
		const std::string offset = (op & (1u << 22)) ? Hex(((op >> 4) & 0xF0) | (op & 15)) : Reg(op);
		const std::string sign = up ? "" : "-";
		const std::string addr = pre ? std::format("[{},{}{}]{}", rn, sign, offset, writeback ? "!" : "") : std::format("[{}],{}{}", rn, sign, offset);
		const uint32_t kind = (op >> 5) & 3;
		std::string name;
		if (load)
			name = std::string("LDR") + c + kKind[kind];
		else
			name = kind == 1 ? "STR" + c + "H" : (kind == 2 ? "LDR" + c + "D" : "STR" + c + "D"); // ARMv5TE : LDRD/STRD
		return line(name, rd + "," + addr);
	}
	if ((op & 0x0FBF0FFF) == 0x010F0000)
		return line("MRS" + c, std::format("{},{}", rd, (op & (1u << 22)) ? "SPSR" : "CPSR"));
	if ((op & 0x0DB0F000) == 0x0120F000) {
		std::string fields;
		for (int i = 0; i < 4; ++i)
			if (op & (1u << (16 + i)))
				fields += "cxsf"[i];
		const std::string source = (op & (1u << 25)) ? Operand2(op) : Reg(op);
		return line("MSR" + c, std::format("{}_{},{}", (op & (1u << 22)) ? "SPSR" : "CPSR", fields, source));
	}
	if ((op & 0x0C000000) == 0x00000000) { // traitement de données
		const uint32_t opcode = (op >> 21) & 15;
		const bool setFlags = op & (1u << 20);
		const std::string name = std::string(kDataOp[opcode]) + c + (setFlags && (opcode < 8 || opcode > 11) ? "S" : "");
		if (opcode >= 8 && opcode <= 11) // TST, TEQ, CMP, CMN
			return line(name, rn + "," + Operand2(op));
		if (opcode == 13 || opcode == 15) // MOV, MVN
			return line(name, rd + "," + Operand2(op));
		return line(name, std::format("{},{},{}", rd, rn, Operand2(op)));
	}
	if ((op & 0x0C000000) == 0x04000000) { // LDR / STR
		const bool load = op & (1u << 20), byte = op & (1u << 22), up = op & (1u << 23), pre = op & (1u << 24), writeback = op & (1u << 21);
		std::string offset;
		if (op & (1u << 25)) {
			offset = (up ? "" : "-") + Operand2(op & ~(1u << 25));
		} else {
			const uint32_t imm = op & 0xFFF;
			offset = imm ? std::string(up ? "" : "-") + Hex(imm) : std::string();
		}
		std::string addr;
		if (pre)
			addr = std::format("[{}{}{}]{}", rn, offset.empty() ? "" : ",", offset, writeback ? "!" : "");
		else
			addr = std::format("[{}]{}{}", rn, offset.empty() ? "" : ",", offset);
		return line(std::string(load ? "LDR" : "STR") + c + (byte ? "B" : ""), rd + "," + addr);
	}
	if ((op & 0x0E000000) == 0x08000000) { // LDM / STM
		static constexpr const char *kMode[4] = {"DA", "IA", "DB", "IB"};
		const bool load = op & (1u << 20);
		return line(std::string(load ? "LDM" : "STM") + c + kMode[(op >> 23) & 3],
					std::format("{}{},{}{}", rn, (op & (1u << 21)) ? "!" : "", RegList(op & 0xFFFF, 16), (op & (1u << 22)) ? "^" : ""));
	}
	if ((op & 0x0F000010) == 0x0E000010) // MRC / MCR
		return line(std::string((op & (1u << 20)) ? "MRC" : "MCR") + c,
					std::format("p{},{},{},c{},c{},{}", (op >> 8) & 15, (op >> 21) & 7, rd, (op >> 16) & 15, op & 15, (op >> 5) & 7));
	return line("DCD", std::format("0x{:08X}", op));
}

emu::DisassembledLine DisassembleThumb(uint32_t address, uint16_t op, uint16_t next) {
	auto line = [&](std::string mnemonic, std::string operands = {}, uint32_t size = 2) {
		return Line(address, size, std::move(mnemonic), std::move(operands));
	};
	const uint32_t rd = op & 7, rs = (op >> 3) & 7, rn = (op >> 6) & 7;
	switch (op >> 13) {
	case 0:
		if ((op & 0x1800) != 0x1800) { // décalage par immédiat
			static constexpr const char *kOps[3] = {"LSL", "LSR", "ASR"};
			return line(kOps[(op >> 11) & 3], std::format("{},{},#{}", Reg(rd), Reg(rs), (op >> 6) & 31));
		}
		{ // ADD / SUB registre ou immédiat 3 bits
			const bool immediate = op & 0x400;
			return line((op & 0x200) ? "SUB" : "ADD", std::format("{},{},{}", Reg(rd), Reg(rs), immediate ? Hex(rn) : Reg(rn)));
		}
	case 1: {
		static constexpr const char *kOps[4] = {"MOV", "CMP", "ADD", "SUB"};
		return line(kOps[(op >> 11) & 3], std::format("{},{}", Reg((op >> 8) & 7), Hex(op & 0xFF)));
	}
	case 2:
		if ((op & 0xFC00) == 0x4000) {
			static constexpr const char *kOps[16] = {"AND", "EOR", "LSL", "LSR", "ASR", "ADC", "SBC", "ROR",
													 "TST", "NEG", "CMP", "CMN", "ORR", "MUL", "BIC", "MVN"};
			return line(kOps[(op >> 6) & 15], std::format("{},{}", Reg(rd), Reg(rs)));
		}
		if ((op & 0xFC00) == 0x4400) { // registres hauts, BX / BLX
			const uint32_t hd = rd | ((op >> 4) & 8), hs = (op >> 3) & 15;
			switch ((op >> 8) & 3) {
			case 0: return line("ADD", std::format("{},{}", Reg(hd), Reg(hs)));
			case 1: return line("CMP", std::format("{},{}", Reg(hd), Reg(hs)));
			case 2: return line("MOV", std::format("{},{}", Reg(hd), Reg(hs)));
			default: return line((op & 0x80) ? "BLX" : "BX", Reg(hs));
			}
		}
		if ((op & 0xF800) == 0x4800) { // LDR relatif au PC
			const uint32_t target = ((address + 4) & ~3u) + (op & 0xFF) * 4;
			return line("LDR", std::format("{},[PC,#0x{:X}] ; 0x{:08X}", Reg((op >> 8) & 7), (op & 0xFF) * 4, target));
		}
		{ // registre de décalage : STR, STRH, STRB, LDRSB, LDR, LDRH, LDRB, LDRSH
			static constexpr const char *kOps[8] = {"STR", "STRH", "STRB", "LDRSB", "LDR", "LDRH", "LDRB", "LDRSH"};
			return line(kOps[(op >> 9) & 7], std::format("{},[{},{}]", Reg(rd), Reg(rs), Reg(rn)));
		}
	case 3: { // STR/LDR(B) immédiat
		const bool byte = op & 0x1000, load = op & 0x800;
		const uint32_t offset = ((op >> 6) & 31) * (byte ? 1u : 4u);
		return line(std::string(load ? "LDR" : "STR") + (byte ? "B" : ""), std::format("{},[{},{}]", Reg(rd), Reg(rs), Hex(offset)));
	}
	case 4:
		if (!(op & 0x1000)) // STRH / LDRH immédiat
			return line((op & 0x800) ? "LDRH" : "STRH", std::format("{},[{},{}]", Reg(rd), Reg(rs), Hex(((op >> 6) & 31) * 2)));
		return line((op & 0x800) ? "LDR" : "STR", std::format("{},[SP,{}]", Reg((op >> 8) & 7), Hex((op & 0xFF) * 4)));
	case 5:
		if (!(op & 0x1000))
			return line("ADD", std::format("{},{},{}", Reg((op >> 8) & 7), (op & 0x800) ? "SP" : "PC", Hex((op & 0xFF) * 4)));
		if ((op & 0xFF00) == 0xB000)
			return line((op & 0x80) ? "SUB" : "ADD", std::format("SP,{}", Hex((op & 0x7F) * 4)));
		if ((op & 0xF600) == 0xB400) { // PUSH / POP
			const bool pop = op & 0x800;
			uint32_t mask = op & 0xFF;
			if (op & 0x100)
				mask |= pop ? 0x8000u : 0x4000u;
			return line(pop ? "POP" : "PUSH", RegList(mask, 16));
		}
		if ((op & 0xFF00) == 0xBE00)
			return line("BKPT", Hex(op & 0xFF));
		break;
	case 6:
		if (!(op & 0x1000))
			return line((op & 0x800) ? "LDMIA" : "STMIA", std::format("{}!,{}", Reg((op >> 8) & 7), RegList(op & 0xFF, 8)));
		if ((op & 0xFF00) == 0xDF00)
			return line("SWI", Hex(op & 0xFF));
		if ((op & 0xF000) == 0xD000 && ((op >> 8) & 15) < 14) {
			const int32_t offset = int32_t(int8_t(op & 0xFF)) * 2;
			return line(std::string("B") + kCond[(op >> 8) & 15], std::format("0x{:08X}", address + 4 + uint32_t(offset)));
		}
		break;
	default:
		if ((op & 0xF800) == 0xE000) {
			const int32_t offset = (int32_t(uint32_t(op) << 21) >> 20);
			return line("B", std::format("0x{:08X}", address + 4 + uint32_t(offset)));
		}
		if ((op & 0xF800) == 0xF000 && ((next & 0xF800) == 0xF800 || (next & 0xF800) == 0xE800)) { // BL / BLX en deux moitiés
			const int32_t high = int32_t(uint32_t(op) << 21) >> 9;
			uint32_t target = address + 4 + uint32_t(high) + ((next & 0x7FF) << 1);
			const bool exchange = (next & 0xF800) == 0xE800;
			if (exchange)
				target &= ~3u;
			return line(exchange ? "BLX" : "BL", std::format("0x{:08X}", target), 4);
		}
		break;
	}
	return line("DCW", std::format("0x{:04X}", op));
}

} // namespace consoles::arm
