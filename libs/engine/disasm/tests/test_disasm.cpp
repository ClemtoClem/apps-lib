#include <cpp-maker/test.hpp>

#include <string>

#include <disasm/disasm.hpp>

// ── MIPS32 ───────────────────────────────────────────────────────────────────

TEST(Disasm_Mips32) {
	// addiu sp, sp, -16  (prologue classique)
	{
		const uint8_t b[] = {0xF0, 0xFF, 0xBD, 0x27}; // 0x27BDFFF0
		auto r = rea::Decode(b, sizeof(b), 0x1000, rea::Arch::Mips32);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("addiu"));
		CHECK(r.Value().operands.find("sp") != std::string::npos);
		CHECK_EQ(r.Value().size, uint8_t(4));
	}

	// jr ra  (retour)
	{
		const uint8_t b[] = {0x08, 0x00, 0xE0, 0x03}; // 0x03E00008
		auto r = rea::Decode(b, sizeof(b), 0x1000, rea::Arch::Mips32);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("jr"));
		CHECK(r.Value().operands.find("ra") != std::string::npos);
	}

	// nop (= sll zero, zero, 0)
	{
		const uint8_t b[] = {0x00, 0x00, 0x00, 0x00};
		auto r = rea::Decode(b, sizeof(b), 0x1000, rea::Arch::Mips32);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("sll"));
	}

	// lw ra, 0x14(sp)
	{
		const uint8_t b[] = {0x14, 0x00, 0xBF, 0x8F}; // 0x8FBF0014
		auto r = rea::Decode(b, sizeof(b), 0x1000, rea::Arch::Mips32);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().mnemonic, std::string("lw"));
		CHECK(r.Value().operands.find("sp") != std::string::npos);
	}
}

TEST(Disasm_Mips_Linear) {
	// addiu sp, sp, -16 ; sw ra, 0x14(sp) ; jr ra ; nop
	const uint8_t code[] = {0xF0, 0xFF, 0xBD, 0x27, 0x14, 0x00, 0xBF, 0xAF, 0x08, 0x00, 0xE0, 0x03, 0x00, 0x00, 0x00, 0x00};
	auto lines = rea::Disassemble(code, sizeof(code), 0x80000000, rea::Arch::Mips32);
	CHECK_EQ(lines.size(), size_t(4));
	CHECK_EQ(lines[0].mnemonic, std::string("addiu"));
	CHECK_EQ(lines[1].mnemonic, std::string("sw"));
	CHECK_EQ(lines[2].mnemonic, std::string("jr"));
	CHECK_EQ(lines[3].mnemonic, std::string("sll"));
	CHECK_EQ(lines[1].address, uint64_t(0x80000004));
}

// ── ARM / Thumb (noyau consoles) ────────────────────────────────────────────

TEST(Disasm_ArmThumb) {
	// mov r0, #0 (ARM) = 0xE3A00000
	{
		const uint8_t b[] = {0x00, 0x00, 0xA0, 0xE3};
		auto r = rea::Decode(b, sizeof(b), 0x2000, rea::Arch::Arm);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().size, uint8_t(4));
		CHECK_FALSE(r.Value().mnemonic.empty());
	}

	// movs r0, #0 (Thumb) = 0x2000
	{
		const uint8_t b[] = {0x00, 0x20};
		auto r = rea::Decode(b, sizeof(b), 0x2000, rea::Arch::Thumb);
		CHECK(r.IsOk());
		CHECK_EQ(r.Value().size, uint8_t(2));
		CHECK_FALSE(r.Value().mnemonic.empty());
	}
}

// ── x86-64 (délégation à Zydis) ─────────────────────────────────────────────

TEST(Disasm_X86_64) {
	const uint8_t b[] = {0xC3}; // ret
	auto r = rea::Decode(b, sizeof(b), 0x3000, rea::Arch::X86_64);
	CHECK(r.IsOk());
	CHECK_EQ(r.Value().mnemonic, std::string("ret"));
}
