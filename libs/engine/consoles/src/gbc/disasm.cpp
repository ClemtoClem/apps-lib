// Désassembleur LR35902 : même découpage de l'octet d'instruction que l'interpréteur
// (x = bits 7-6, y = bits 5-3, z = bits 2-0).
#include <format>

#include "consoles/gbc/cpu.hpp"

namespace gbc {

namespace {
constexpr const char *kR[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
constexpr const char *kRp[4] = {"BC", "DE", "HL", "SP"};
constexpr const char *kRp2[4] = {"BC", "DE", "HL", "AF"};
constexpr const char *kCc[4] = {"NZ", "Z", "NC", "C"};
constexpr const char *kAlu[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};
constexpr const char *kRot[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"};
constexpr const char *kAcc[8] = {"RLCA", "RRCA", "RLA", "RRA", "DAA", "CPL", "SCF", "CCF"};
} // namespace

emu::DisassembledLine Disassemble(const emu::AddressSpace &bus, uint16_t address) {
	emu::DisassembledLine line;
	line.address = address;
	const uint8_t op = bus.Read8(address);
	const uint8_t n = bus.Read8(uint16_t(address + 1));
	const uint16_t nn = uint16_t(n | (bus.Read8(uint16_t(address + 2)) << 8));
	const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
	auto set = [&](uint32_t size, std::string mnemonic, std::string operands = {}) {
		line.size = size;
		line.mnemonic = std::move(mnemonic);
		line.operands = std::move(operands);
	};
	const std::string imm8 = std::format("${:02X}", n), imm16 = std::format("${:04X}", nn);
	const std::string target = std::format("${:04X}", uint16_t(address + 2 + int8_t(n)));
	if (x == 1) {
		if (op == 0x76)
			set(1, "HALT");
		else
			set(1, "LD", std::format("{},{}", kR[y], kR[z]));
		return line;
	}
	if (x == 2) {
		set(1, kAlu[y], std::format("A,{}", kR[z]));
		return line;
	}
	if (x == 0) {
		switch (z) {
		case 0:
			if (y == 0)
				set(1, "NOP");
			else if (y == 1)
				set(3, "LD", std::format("({}),SP", imm16));
			else if (y == 2)
				set(2, "STOP");
			else if (y == 3)
				set(2, "JR", target);
			else
				set(2, "JR", std::format("{},{}", kCc[y - 4], target));
			break;
		case 1:
			if (q == 0)
				set(3, "LD", std::format("{},{}", kRp[p], imm16));
			else
				set(1, "ADD", std::format("HL,{}", kRp[p]));
			break;
		case 2: {
			static constexpr const char *kIndirect[4] = {"(BC)", "(DE)", "(HL+)", "(HL-)"};
			set(1, "LD", q == 0 ? std::format("{},A", kIndirect[p]) : std::format("A,{}", kIndirect[p]));
			break;
		}
		case 3: set(1, q == 0 ? "INC" : "DEC", kRp[p]); break;
		case 4: set(1, "INC", kR[y]); break;
		case 5: set(1, "DEC", kR[y]); break;
		case 6: set(2, "LD", std::format("{},{}", kR[y], imm8)); break;
		default: set(1, kAcc[y]); break;
		}
		return line;
	}
	switch (z) {
	case 0:
		if (y < 4)
			set(1, "RET", kCc[y]);
		else if (y == 4)
			set(2, "LDH", std::format("($FF{:02X}),A", n));
		else if (y == 5)
			set(2, "ADD", std::format("SP,{}", int(int8_t(n))));
		else if (y == 6)
			set(2, "LDH", std::format("A,($FF{:02X})", n));
		else
			set(2, "LD", std::format("HL,SP{:+}", int(int8_t(n))));
		break;
	case 1:
		if (q == 0)
			set(1, "POP", kRp2[p]);
		else if (p == 0)
			set(1, "RET");
		else if (p == 1)
			set(1, "RETI");
		else if (p == 2)
			set(1, "JP", "HL");
		else
			set(1, "LD", "SP,HL");
		break;
	case 2:
		if (y < 4)
			set(3, "JP", std::format("{},{}", kCc[y], imm16));
		else if (y == 4)
			set(1, "LD", "($FF00+C),A");
		else if (y == 5)
			set(3, "LD", std::format("({}),A", imm16));
		else if (y == 6)
			set(1, "LD", "A,($FF00+C)");
		else
			set(3, "LD", std::format("A,({})", imm16));
		break;
	case 3:
		if (y == 0)
			set(3, "JP", imm16);
		else if (y == 1) {
			const int cy = (n >> 3) & 7, cz = n & 7;
			switch (n >> 6) {
			case 0: set(2, kRot[cy], kR[cz]); break;
			case 1: set(2, "BIT", std::format("{},{}", cy, kR[cz])); break;
			case 2: set(2, "RES", std::format("{},{}", cy, kR[cz])); break;
			default: set(2, "SET", std::format("{},{}", cy, kR[cz])); break;
			}
		} else if (y == 6)
			set(1, "DI");
		else if (y == 7)
			set(1, "EI");
		else
			set(1, "DB", std::format("${:02X}", op));
		break;
	case 4:
		if (y < 4)
			set(3, "CALL", std::format("{},{}", kCc[y], imm16));
		else
			set(1, "DB", std::format("${:02X}", op));
		break;
	case 5:
		if (q == 0)
			set(1, "PUSH", kRp2[p]);
		else if (p == 0)
			set(3, "CALL", imm16);
		else
			set(1, "DB", std::format("${:02X}", op));
		break;
	case 6: set(2, kAlu[y], std::format("A,{}", imm8)); break;
	default: set(1, "RST", std::format("${:02X}", y * 8)); break;
	}
	return line;
}

std::vector<emu::RegisterValue> Lr35902::DebugRegisters() const {
	return {{"PC", m_r.pc, 16}, {"SP", m_r.sp, 16}, {"A", m_r.a, 8},   {"F", m_r.f, 8},	  {"B", m_r.b, 8},	 {"C", m_r.c, 8},
			{"D", m_r.d, 8},	{"E", m_r.e, 8},	{"H", m_r.h, 8},   {"L", m_r.l, 8},	  {"IF", m_if, 8},	 {"IE", m_ie, 8},
			{"IME", m_ime, 1},	{"HALT", m_halted, 1}};
}

std::string Lr35902::FlagsText() const {
	return std::format("{}{}{}{}", (m_r.f & kFlagZ) ? 'Z' : '-', (m_r.f & kFlagN) ? 'N' : '-', (m_r.f & kFlagH) ? 'H' : '-',
					   (m_r.f & kFlagC) ? 'C' : '-');
}

} // namespace gbc
