// Tests du cœur ARM commun à la Game Boy Advance et à la Nintendo DS (`consoles/arm/`) :
// désassembleur ARM / Thumb, construction du cœur et ses erreurs, registre des systèmes.
#include <cpp-maker/test.hpp>

#include <set>
#include <string>
#include <vector>

#include <consoles/arm/core.hpp>
#include <consoles/arm/disasm.hpp>
#include <consoles/consoles.hpp>

#include "arm_rom.hpp"

TEST(Arm_DisassemblerArm) {
	using consoles::arm::DisassembleArm;
	struct Case {
		uint32_t address, opcode;
		const char *mnemonic, *operands;
	};
	const Case cases[] = {
		{0x08000000, 0xEA00002E, "B", "0x080000C0"},			 // B +0xB8
		{0x08000100, 0xEBFFFFFE, "BL", "0x08000100"},			 // BL sur elle-même
		{0x0, 0xE12FFF1E, "BX", "LR"},
		{0x0, 0xE3A00012, "MOV", "R0,#0x12"},
		{0x0, 0xE3A0DC01, "MOV", "SP,#0x100"},					 // immédiat tourné
		{0x0, 0xE0811002, "ADD", "R1,R1,R2"},
		{0x0, 0xE1B00080, "MOVS", "R0,R0,LSL #1"},
		{0x0, 0xE3530000, "CMP", "R3,#0"},
		{0x0, 0x13A00001, "MOVNE", "R0,#1"},
		{0x0, 0xE5910004, "LDR", "R0,[R1,#4]"},
		{0x0, 0xE5A10004, "STR", "R0,[R1,#4]!"},
		{0x0, 0xE4D10001, "LDRB", "R0,[R1],#1"},
		{0x0, 0xE1D100B2, "LDRH", "R0,[R1,#2]"},
		{0x0, 0xE92D4010, "STMDB", "SP!,{R4,LR}"},
		{0x0, 0xE8BD8FF0, "LDMIA", "SP!,{R4-R11,PC}"},
		{0x0, 0xE0000291, "MUL", "R0,R1,R2"},
		{0x0, 0xE10F0000, "MRS", "R0,CPSR"},
		{0x0, 0xE129F000, "MSR", "CPSR_cf,R0"},
		{0x0, 0xEF000005, "SWI", "#5"},
		{0x0, 0xEE190F11, "MRC", "p15,0,R0,c9,c1,0"},
		{0x0, 0xE16F0F11, "CLZ", "R0,R1"},
		{0x0, 0xFFFFFFFF, "DCD", "0xFFFFFFFF"},
	};
	for (const Case &c : cases) {
		const emu::DisassembledLine line = DisassembleArm(c.address, c.opcode);
		CHECK_EQ(line.size, 4u);
		CHECK_EQ(line.mnemonic, std::string(c.mnemonic));
		CHECK_EQ(line.operands, std::string(c.operands));
	}
}

TEST(Arm_DisassemblerThumb) {
	using consoles::arm::DisassembleThumb;
	struct Case {
		uint32_t address;
		uint16_t opcode, next;
		const char *mnemonic, *operands;
		uint32_t size;
	};
	const Case cases[] = {
		{0x0, 0x2012, 0, "MOV", "R0,#0x12", 2},
		{0x0, 0x0048, 0, "LSL", "R0,R1,#1", 2},
		{0x0, 0x1888, 0, "ADD", "R0,R1,R2", 2},
		{0x0, 0x1E48, 0, "SUB", "R0,R1,#1", 2},
		{0x0, 0x4008, 0, "AND", "R0,R1", 2},
		{0x0, 0x4770, 0, "BX", "LR", 2},
		{0x0, 0x46C0, 0, "MOV", "R8,R8", 2},
		{0x08000100, 0x4801, 0, "LDR", "R0,[PC,#0x4] ; 0x08000108", 2},
		{0x0, 0x6848, 0, "LDR", "R0,[R1,#4]", 2},
		{0x0, 0x5888, 0, "LDR", "R0,[R1,R2]", 2},
		{0x0, 0xB510, 0, "PUSH", "{R4,LR}", 2},
		{0x0, 0xBD10, 0, "POP", "{R4,PC}", 2},
		{0x0, 0xB082, 0, "SUB", "SP,#8", 2},
		{0x08000000, 0xD0FE, 0, "BEQ", "0x08000000", 2},
		{0x08000000, 0xE7FE, 0, "B", "0x08000000", 2},
		{0x0, 0xDF05, 0, "SWI", "#5", 2},
		{0x08000000, 0xF000, 0xF802, "BL", "0x08000008", 4},
		{0x0, 0xC103, 0, "STMIA", "R1!,{R0,R1}", 2},
	};
	for (const Case &c : cases) {
		const emu::DisassembledLine line = DisassembleThumb(c.address, c.opcode, c.next);
		CHECK_EQ(line.size, c.size);
		CHECK_EQ(line.mnemonic, std::string(c.mnemonic));
		CHECK_EQ(line.operands, std::string(c.operands));
	}
}

TEST(Arm_CoreCreationErrors) {
	using namespace consoles::arm;
	armtest::Program loop(armtest::kNdsArm9Base);
	loop.B(loop.Here());
	armtest::Program loop7(armtest::kNdsArm7Base);
	loop7.B(loop7.Here());
	const std::vector<uint8_t> nds = armtest::NdsRom(loop, loop7);
	armtest::Program gbaCode(0x08000000);
	gbaCode.B(gbaCode.Here());
	const std::vector<uint8_t> gba = armtest::GbaRom(gbaCode);

	// GBA : le BIOS est indispensable (le cœur n'en a pas d'équivalent logiciel).
	auto noBios = Core::Create(ROM_GBA, gba, Settings{});
	REQUIRE(noBios.IsError());
	CHECK(noBios.Error() == ERROR_GBA_BIOS);
	// NDS sans démarrage direct : BIOS ARM9/ARM7 exigés.
	Settings firmwareBoot;
	firmwareBoot.directBoot = false;
	auto noNdsBios = Core::Create(ROM_NDS, nds, firmwareBoot);
	REQUIRE(noNdsBios.IsError());
	CHECK(noNdsBios.Error() == ERROR_BIOS);
	// Sans ROM, rien à démarrer directement : BIOS exigés aussi.
	auto noRom = Core::Create(ROM_NDS, std::vector<uint8_t>{}, Settings{});
	REQUIRE(noRom.IsError());
	CHECK(noRom.Error() == ERROR_BIOS);
	// Textes d'erreur distincts et non vides.
	std::set<std::string> texts;
	for (const CoreError error : {ERROR_BIOS, ERROR_GBA_BIOS, ERROR_FIRM, ERROR_ROM})
		texts.insert(Core::ErrorText(error));
	CHECK_EQ(texts.size(), 4u);
	CHECK(!texts.contains(""));

	// NDS en démarrage direct : BIOS logiciel du cœur, aucun fichier requis.
	auto direct = Core::Create(ROM_NDS, nds, Settings{});
	REQUIRE(direct.IsOk());
	CHECK(direct.Value()->GetBootError() == ERROR_NONE);
	CHECK(!direct.Value()->IsGbaMode());
	direct.Value()->RunFrame();
	// B sur elle-même : r15 reste une instruction en avance sur l'adresse de la boucle.
	CHECK_EQ(direct.Value()->interpreter[0].GetRegister(15) - 4u, armtest::kNdsArm9Base);
	CHECK_EQ(direct.Value()->interpreter[1].GetRegister(15) - 4u, armtest::kNdsArm7Base);

	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	Settings withBios;
	withBios.gbaBiosPath = armtest::kGbaBios.string();
	auto missingRom = Core::Create(ROM_GBA, std::string("absent/jeu.gba"), withBios);
	REQUIRE(missingRom.IsError());
	CHECK(missingRom.Error() == ERROR_ROM);
	auto gbaCore = Core::Create(ROM_GBA, gba, withBios);
	REQUIRE(gbaCore.IsOk());
	CHECK(gbaCore.Value()->IsGbaMode()); // mode GBA même sans démarrage direct explicite
}

TEST(Consoles_RegistryAndArchives) {
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	for (const char *name : {"gbc", "gba", "nds"})
		CHECK(registry.Find(name) != nullptr);
	CHECK(registry.FindByExtension(".gba") == registry.Find("gba"));
	CHECK(registry.FindByExtension("NDS") == registry.Find("nds"));
	CHECK(registry.FindByExtension(".gbc") == registry.Find("gbc"));
	CHECK(registry.Find("gba")->readsRomFromPath);
	CHECK(registry.Find("nds")->readsRomFromPath);
	CHECK(!registry.Find("gbc")->readsRomFromPath);
	CHECK(registry.IsRomFile("jeu.zip"));
	CHECK(registry.IsRomFile("jeu.7z"));
	CHECK(!registry.IsRomFile("notes.txt"));
	CHECK(registry.Identify("absent.gba").IsOk()); // un fichier simple est identifié par son extension
	CHECK(registry.Identify("notes.txt").IsError());
	CHECK(registry.Identify("absent.zip").IsError());
}
