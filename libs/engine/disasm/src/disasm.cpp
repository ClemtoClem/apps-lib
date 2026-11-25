// Désassembleur multi-architectures : x86-64, ARM/Thumb, MIPS32.
#include "disasm/disasm.hpp"

#include <format>

#include <consoles/arm/disasm.hpp>
#include <rea/disasm.hpp>

namespace rea {

namespace {

// ── MIPS32 (petit-boutiste, style assembleur MIPS) ───────────────────────────

const char *MipsReg(unsigned r) {
	static constexpr const char *kNames[32] = {
		"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
		"t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
		"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
		"t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
	return r < 32 ? kNames[r] : "?";
}

Line Mips(uint64_t addr, std::string mnemonic, std::string operands) {
	return {addr, 4, std::move(mnemonic), std::move(operands)};
}

/// Cible d'un branchement relatif (offset signé en mots, PC+4 + offset×4).
uint64_t MipsBranch(uint64_t address, int32_t imm) {
	return address + 4 + uint64_t(int64_t(imm) << 2);
}

Line DisassembleMips(uint64_t address, uint32_t op) {
	const unsigned opcode = op >> 26;
	const unsigned rs = (op >> 21) & 31u;
	const unsigned rt = (op >> 16) & 31u;
	const unsigned rd = (op >> 11) & 31u;
	const unsigned shamt = (op >> 6) & 31u;
	const unsigned funct = op & 63u;
	const int32_t imm = int32_t(int16_t(op & 0xffffu));
	const uint32_t target = op & 0x03ffffffu;
	const unsigned base = (address & 0xf0000000ull);

	// R-type (SPECIAL).
	if (opcode == 0x00)
		switch (funct) {
		case 0x20: return Mips(address, "add", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x21: return Mips(address, "addu", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x22: return Mips(address, "sub", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x23: return Mips(address, "subu", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x24: return Mips(address, "and", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x25: return Mips(address, "or", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x26: return Mips(address, "xor", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x27: return Mips(address, "nor", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x2a: return Mips(address, "slt", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x2b: return Mips(address, "sltu", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rs), MipsReg(rt)));
		case 0x00: return Mips(address, "sll", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rt), shamt));
		case 0x02: return Mips(address, "srl", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rt), shamt));
		case 0x03: return Mips(address, "sra", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rt), shamt));
		case 0x04: return Mips(address, "sllv", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rt), MipsReg(rs)));
		case 0x06: return Mips(address, "srlv", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rt), MipsReg(rs)));
		case 0x07: return Mips(address, "srav", std::format("{}, {}, {}", MipsReg(rd), MipsReg(rt), MipsReg(rs)));
		case 0x08: return Mips(address, "jr", std::format("{}", MipsReg(rs)));
		case 0x09: return Mips(address, "jalr", std::format("{}, {}", MipsReg(rd), MipsReg(rs)));
		case 0x0c: return Mips(address, "syscall", "");
		case 0x0d: return Mips(address, "break", "");
		case 0x10: return Mips(address, "mfhi", std::format("{}", MipsReg(rd)));
		case 0x11: return Mips(address, "mthi", std::format("{}", MipsReg(rs)));
		case 0x12: return Mips(address, "mflo", std::format("{}", MipsReg(rd)));
		case 0x13: return Mips(address, "mtlo", std::format("{}", MipsReg(rs)));
		case 0x18: return Mips(address, "mult", std::format("{}, {}", MipsReg(rs), MipsReg(rt)));
		case 0x19: return Mips(address, "multu", std::format("{}, {}", MipsReg(rs), MipsReg(rt)));
		case 0x1a: return Mips(address, "div", std::format("{}, {}", MipsReg(rs), MipsReg(rt)));
		case 0x1b: return Mips(address, "divu", std::format("{}, {}", MipsReg(rs), MipsReg(rt)));
		default: return Mips(address, "???", std::format("funct 0x{:02x}", funct));
		}

	switch (opcode) {
	case 0x01: // REGIMM
		switch (rt) {
		case 0x00: return Mips(address, "bltz", std::format("{}, 0x{:x}", MipsReg(rs), MipsBranch(address, imm)));
		case 0x01: return Mips(address, "bgez", std::format("{}, 0x{:x}", MipsReg(rs), MipsBranch(address, imm)));
		default: return Mips(address, "???", std::format("regimm rt={}", rt));
		}
	case 0x02: return Mips(address, "j", std::format("0x{:x}", base | (target << 2)));
	case 0x03: return Mips(address, "jal", std::format("0x{:x}", base | (target << 2)));
	case 0x04: return Mips(address, "beq", std::format("{}, {}, 0x{:x}", MipsReg(rs), MipsReg(rt), MipsBranch(address, imm)));
	case 0x05: return Mips(address, "bne", std::format("{}, {}, 0x{:x}", MipsReg(rs), MipsReg(rt), MipsBranch(address, imm)));
	case 0x06: return Mips(address, "blez", std::format("{}, 0x{:x}", MipsReg(rs), MipsBranch(address, imm)));
	case 0x07: return Mips(address, "bgtz", std::format("{}, 0x{:x}", MipsReg(rs), MipsBranch(address, imm)));
	case 0x08: return Mips(address, "addi", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), imm));
	case 0x09: return Mips(address, "addiu", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), imm));
	case 0x0a: return Mips(address, "slti", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), imm));
	case 0x0b: return Mips(address, "sltiu", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), imm));
	case 0x0c: return Mips(address, "andi", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), uint32_t(op) & 0xffffu));
	case 0x0d: return Mips(address, "ori", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), op & 0xffffu));
	case 0x0e: return Mips(address, "xori", std::format("{}, {}, 0x{:x}", MipsReg(rt), MipsReg(rs), op & 0xffffu));
	case 0x0f: return Mips(address, "lui", std::format("{}, 0x{:x}", MipsReg(rt), (op & 0xffffu) << 16));
	case 0x20: return Mips(address, "lb", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x21: return Mips(address, "lh", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x23: return Mips(address, "lw", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x24: return Mips(address, "lbu", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x25: return Mips(address, "lhu", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x28: return Mips(address, "sb", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x29: return Mips(address, "sh", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	case 0x2b: return Mips(address, "sw", std::format("{}, 0x{:x}({})", MipsReg(rt), imm, MipsReg(rs)));
	default: return Mips(address, "???", std::format("op 0x{:02x}", opcode));
	}
}

// ── Lecture petit-boutiste ──────────────────────────────────────────────────

uint16_t ReadLE16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8); }
uint32_t ReadLE32(const uint8_t *p) {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

} // namespace

Result<Line, String> Decode(const uint8_t *data, size_t size, uint64_t address, Arch arch) {
	if (arch == Arch::X86_64) {
		const auto insn = rea::DecodeInstruction(data, size, address);
		if (insn.IsError())
			return Err(insn.Error());
		Line line;
		line.address = insn.Value().address;
		line.size = insn.Value().length;
		line.mnemonic = insn.Value().mnemonic;
		line.operands = insn.Value().operands;
		return Ok(std::move(line));
	}
	if (arch == Arch::Arm) {
		if (size < 4)
			return Err("tampon trop court pour une instruction ARM");
		const emu::DisassembledLine d = consoles::arm::DisassembleArm(uint32_t(address), ReadLE32(data));
		return Ok(Line{address, uint8_t(d.size), d.mnemonic, d.operands});
	}
	if (arch == Arch::Thumb) {
		if (size < 2)
			return Err("tampon trop court pour une instruction Thumb");
		const uint16_t op = ReadLE16(data);
		const uint16_t next = size >= 4 ? ReadLE16(data + 2) : 0;
		const emu::DisassembledLine d = consoles::arm::DisassembleThumb(uint32_t(address), op, next);
		return Ok(Line{address, uint8_t(d.size), d.mnemonic, d.operands});
	}
	if (arch == Arch::Mips32) {
		if (size < 4)
			return Err("tampon trop court pour une instruction MIPS");
		return Ok(DisassembleMips(address, ReadLE32(data)));
	}
	return Err("architecture inconnue");
}

std::vector<Line> Disassemble(const uint8_t *data, size_t size, uint64_t address, Arch arch) {
	std::vector<Line> out;
	size_t off = 0;
	while (off < size) {
		const auto line = Decode(data + off, size - off, address + off, arch);
		if (line.IsError())
			break;
		off += line.Value().size;
		out.push_back(std::move(line.Value()));
	}
	return out;
}

} // namespace rea
