// Tests de la Game Boy Advance : en-tête, fabrique, processeur ARM7TDMI (ARM et Thumb) sur
// des cartouches synthétiques, vidéo (mode bitmap 3), manette, états, réinitialisation,
// débogueur ; jeux du dépôt (assets/roms) quand ils sont présents.
#include <cpp-maker/test.hpp>

#include <string>

#include <consoles/arm/board.hpp>
#include <consoles/consoles.hpp>
#include <consoles/gba/gba.hpp>

#include "arm_rom.hpp"

using armtest::Cond;
using armtest::Program;

namespace {

constexpr uint32_t kRomBase = 0x08000000, kCodeStart = 0x080000C0, kIwram = 0x03000000, kThumbCode = 0x08000200;

const std::filesystem::path kGbaRom = "assets/roms/gba/Metroid Fusion (Europe) (En,Fr,De,Es,It).gba";
const std::filesystem::path kGbaZip = "assets/roms/gba/Wario Land 4 (UE).zip";

/// En-tête sauté puis code ARM (en 0x080000C0).
Program StartProgram() {
	Program p(kRomBase);
	p.B(kCodeStart);
	p.PadTo(kCodeStart);
	return p;
}

/// Calculs ARM rangés en IWRAM, appel de sous-programme, passage en Thumb, puis compteur
/// incrémenté sans fin (en IWRAM + 32).
std::vector<uint8_t> CpuRom() {
	Program p = StartProgram();
	p.MovConst(0, kIwram);
	p.MovImm(1, 7);
	p.MovImm(2, 6);
	p.Mul(3, 1, 2);
	p.Str(3, 0, 0); // 42
	p.MovConst(4, 0x12345678);
	p.Str(4, 0, 4);
	p.Add(5, 4, 3);
	p.Str(5, 0, 8);
	p.Sub(6, 4, 3);
	p.Str(6, 0, 12);
	p.MovConst(7, 0xBEEF);
	p.Strh(7, 0, 16);
	p.CmpImm(1, 7);
	p.MovImm(8, 1, Cond::Eq);
	p.MovImm(8, 2, Cond::Ne);
	p.Str(8, 0, 20);
	const uint32_t subroutine = p.Here() + 4;
	p.B(subroutine + 8); // par-dessus le sous-programme (deux instructions)
	p.MovImm(9, 0x55);
	p.Bx(14);
	p.Bl(subroutine);
	p.Str(9, 0, 24);
	p.MovConst(10, kThumbCode | 1);
	p.Bx(10);
	p.PadTo(kThumbCode);
	p.TMov(1, 3);
	p.TMov(2, 4);
	p.TAdd(3, 1, 2);	// 7
	p.TLsl(3, 3, 4);	// 0x70
	p.TMov(4, 3);
	p.TMul(3, 4); // 0x150
	p.TStr(3, 0, 28);
	const uint32_t loop = p.Here();
	p.TLdr(4, 0, 32);
	p.TAddImm(4, 1);
	p.TStr(4, 0, 32);
	p.TB(loop);
	return armtest::GbaRom(p);
}

/// Mode 3 (bitmap 240 × 160) : un point rouge en haut à gauche, un bleu en bas à droite ;
/// puis copie sans fin de KEYINPUT en IWRAM.
std::vector<uint8_t> VideoInputRom() {
	Program p = StartProgram();
	p.MovConst(0, 0x04000000);
	p.MovConst(1, 0x0403); // DISPCNT : mode 3, BG2
	p.Strh(1, 0, 0);
	p.MovConst(2, 0x06000000);
	p.MovConst(3, 0x001F); // rouge (BGR555)
	p.Strh(3, 2, 0);
	p.MovConst(5, 0x06000000 + (159 * 240 + 239) * 2);
	p.MovConst(4, 0x7C00); // bleu
	p.Strh(4, 5, 0);
	p.MovConst(6, kIwram);
	p.MovConst(8, 0x04000130); // KEYINPUT
	const uint32_t loop = p.Here();
	p.Ldrh(7, 8, 0);
	p.Str(7, 6, 0);
	p.B(loop);
	return armtest::GbaRom(p);
}

struct Session {
	std::unique_ptr<emu::Machine> machine;
	consoles::arm::Board *board = nullptr;
	emu::Debuggable *debug = nullptr;
};

Session Boot(std::vector<uint8_t> image, bool directBoot = true) {
	Session s;
	auto result = gba::CreateMachine(armtest::MediaFromImage(std::move(image), directBoot));
	if (result.IsError())
		return s;
	s.machine = std::move(result.Value());
	s.board = s.machine->Find<consoles::arm::Board>("board");
	s.debug = s.machine->Debugger();
	return s;
}

} // namespace

TEST(Gba_HeaderParsing) {
	std::vector<uint8_t> rom = armtest::GbaRom(StartProgram());
	const gba::Header header = gba::ParseHeader(rom);
	CHECK(header.valid);
	CHECK_EQ(header.title, std::string("SYNTHETIC"));
	CHECK_EQ(header.gameCode, std::string("ZTST"));
	CHECK(header.checksumOk);
	rom[0xBD] ^= 1;
	CHECK(!gba::ParseHeader(rom).checksumOk);
	rom[0xA3] = 0x01; // octet non imprimable dans le titre
	CHECK_EQ(gba::ParseHeader(rom).title, std::string("SYN?HETIC"));
	const gba::Header tooSmall = gba::ParseHeader(std::vector<uint8_t>(0x40));
	CHECK(!tooSmall.valid);
	CHECK(!tooSmall.error.empty());
}

TEST(Gba_SystemInfoAndRejectedImages) {
	const emu::SystemInfo info = gba::System();
	CHECK_EQ(info.name, std::string("gba"));
	CHECK_EQ(info.year, 2001);
	CHECK(info.readsRomFromPath);
	CHECK(info.create != nullptr);
	CHECK(info.extensions == std::vector<std::string>({".gba", ".agb"}));
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	CHECK(registry.FindByExtension(".agb") == registry.Find("gba"));
	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	CHECK(gba::CreateMachine(emu::Media{}).IsError());						 // ni image ni chemin
	CHECK(gba::CreateMachine(armtest::MediaFromImage(std::vector<uint8_t>(0x20))).IsError()); // en-tête tronqué
	emu::Media missing;
	missing.romPath = "absent/jeu.gba";
	CHECK(gba::CreateMachine(missing).IsError());
}

TEST(Gba_MachineLayout) {
	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	Session s = Boot(CpuRom());
	REQUIRE(s.machine != nullptr);
	REQUIRE(s.board != nullptr);
	CHECK(s.board->GetMode() == consoles::arm::Mode::Gba);
	CHECK(s.board->GetCore() != nullptr);
	CHECK(s.debug == s.board);
	REQUIRE(s.machine->Screens().size() == 1u);
	CHECK_EQ(s.machine->Screens()[0]->Width(), 240);
	CHECK_EQ(s.machine->Screens()[0]->Height(), 160);
	REQUIRE(s.machine->Inputs().size() == 1u);
	CHECK_EQ(s.machine->Inputs()[0]->Fields().size(), 10u); // sans X ni Y
	CHECK(s.machine->TouchPanels().empty());
	REQUIRE(s.machine->Sounds().size() == 1u);
	CHECK_EQ(s.machine->Sounds()[0]->SampleRate(), 32768);
	CHECK_EQ(s.debug->MemoryRegions().size(), 7u);
}

TEST(Gba_CpuArmThumbAndSubroutines) {
	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	Session s = Boot(CpuRom());
	REQUIRE(s.machine != nullptr);
	emu::Machine &machine = *s.machine;
	armtest::RunFrames(machine, 2);
	CHECK_EQ(machine.FrameCount(), 2u);
	CHECK_EQ(machine.GetScheduler().Now(), 2u * gba::kFrameTicks);
	const emu::AddressSpace &bus = s.debug->ProgramSpace();
	CHECK_EQ(bus.Read32(kIwram + 0), 42u);
	CHECK_EQ(bus.Read32(kIwram + 4), 0x12345678u);
	CHECK_EQ(bus.Read32(kIwram + 8), 0x123456A2u);
	CHECK_EQ(bus.Read32(kIwram + 12), 0x1234564Eu);
	CHECK_EQ(bus.Read16(kIwram + 16), 0xBEEFu);
	CHECK_EQ(bus.Read32(kIwram + 20), 1u);	   // MOVEQ exécuté, MOVNE sauté
	CHECK_EQ(bus.Read32(kIwram + 24), 0x55u);  // BL / BX LR
	CHECK_EQ(bus.Read32(kIwram + 28), 0x150u); // calculs Thumb
	const uint32_t counter = bus.Read32(kIwram + 32);
	CHECK(counter > 1000u);
	armtest::RunFrames(machine, 1);
	CHECK(bus.Read32(kIwram + 32) > counter);

	// Débogueur : état Thumb, mode système, compteur ordinal dans la boucle.
	const auto regs = s.debug->DebugRegisters();
	REQUIRE(regs.size() == 18u);
	CHECK_EQ(regs[0].value, kIwram);
	CHECK_EQ(regs[13].name, std::string("SP"));
	CHECK_EQ(regs[16].name, std::string("CPSR"));
	CHECK_EQ(regs[16].value & 0x3Fu, 0x3Fu); // T + mode système (0x1F)
	const uint32_t pc = s.debug->ProgramCounter();
	CHECK(pc >= kThumbCode + 14 && pc < kThumbCode + 22);
	CHECK(s.debug->FlagsText().find("SYS") != std::string::npos);
	CHECK(s.debug->FlagsText().find('T') != std::string::npos);
	const emu::DisassembledLine first = s.debug->Disassemble(kThumbCode);
	CHECK_EQ(first.size, 2u);
	CHECK_EQ(first.mnemonic, std::string("MOV"));
	CHECK_EQ(first.operands, std::string("R1,#3"));
	CHECK_EQ(bus.Read8(0x04000000), 0u); // registres d'E/S jamais lus par le débogueur

	// Son : ~549 trames à 32 768 Hz par image.
	const uint64_t samples = machine.Sounds()[0]->TotalFrames();
	CHECK(samples > 3u * 500u && samples < 3u * 600u);
}

TEST(Gba_VideoMode3AndKeypad) {
	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	Session s = Boot(VideoInputRom());
	REQUIRE(s.machine != nullptr);
	emu::Machine &machine = *s.machine;
	armtest::RunFrames(machine, 3);
	const emu::Screen &screen = *machine.Screens()[0];
	const armtest::Color red = armtest::Split(armtest::Pixel(screen, 0, 0));
	CHECK(red.r >= 0xF0 && red.g == 0 && red.b == 0);
	const armtest::Color blue = armtest::Split(armtest::Pixel(screen, 239, 159));
	CHECK(blue.b >= 0xF0 && blue.r == 0 && blue.g == 0);
	const armtest::Color black = armtest::Split(armtest::Pixel(screen, 120, 80));
	CHECK(black.r == 0 && black.g == 0 && black.b == 0);

	// KEYINPUT : bits à 0 quand la touche est enfoncée (a = bit 0, start = 3, l = 9).
	emu::InputPort &pad = *machine.Inputs()[0];
	const emu::AddressSpace &bus = s.debug->ProgramSpace();
	CHECK_EQ(bus.Read32(kIwram) & 0x3FFu, 0x3FFu);
	CHECK(pad.SetPressed("a", true));
	CHECK(pad.SetPressed("start", true));
	CHECK(pad.SetPressed("l", true));
	armtest::RunFrames(machine, 1);
	CHECK_EQ(bus.Read32(kIwram) & 0x3FFu, 0x3FFu & ~((1u << 0) | (1u << 3) | (1u << 9)));
	pad.SetPressed("a", false);
	pad.SetPressed("start", false);
	pad.SetPressed("l", false);
	CHECK(!pad.SetPressed("x", true)); // pas de X sur GBA
	armtest::RunFrames(machine, 1);
	CHECK_EQ(bus.Read32(kIwram) & 0x3FFu, 0x3FFu);
}

TEST(Gba_StateRoundTripAndReset) {
	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	Session s = Boot(CpuRom());
	REQUIRE(s.machine != nullptr);
	emu::Machine &machine = *s.machine;
	const emu::AddressSpace &bus = s.debug->ProgramSpace();
	armtest::RunFrames(machine, 10);
	const std::vector<uint8_t> state = machine.SaveState();
	CHECK(state.size() > 100000u);
	armtest::RunFrames(machine, 20);
	const uint32_t reference = bus.Read32(kIwram + 32);
	const uint32_t pc = s.debug->ProgramCounter();
	CHECK(machine.LoadState(state));
	CHECK_EQ(machine.FrameCount(), 10u);
	CHECK(bus.Read32(kIwram + 32) < reference);
	armtest::RunFrames(machine, 20);
	CHECK_EQ(bus.Read32(kIwram + 32), reference); // exécution identique au cycle près
	CHECK_EQ(s.debug->ProgramCounter(), pc);
	// Un état tronqué est refusé sans toucher à la machine.
	std::vector<uint8_t> truncated(state.begin(), state.begin() + std::ptrdiff_t(state.size() / 2));
	CHECK(!machine.LoadState(truncated));
	CHECK_EQ(bus.Read32(kIwram + 32), reference);

	// Réinitialisation : un cœur neuf (copie de l'image en mémoire), le programme repart de zéro.
	machine.Reset();
	CHECK(s.board->LastError().empty());
	CHECK(s.board->GetCore() != nullptr);
	CHECK_EQ(machine.FrameCount(), 0u);
	armtest::RunFrames(machine, 1);
	CHECK_EQ(bus.Read32(kIwram), 42u);
	CHECK(bus.Read32(kIwram + 32) < reference);
}

TEST(Gba_BiosIntroWithoutDirectBoot) {
	if (!armtest::FileExists(armtest::kGbaBios))
		SKIP("BIOS GBA absent (assets/bios-firmware)");
	// Sans démarrage direct, le BIOS GBA s'exécute d'abord (introduction) : la machine est
	// bien en mode GBA et la cartouche n'a pas encore été lancée.
	Session s = Boot(CpuRom(), false);
	REQUIRE(s.machine != nullptr);
	armtest::RunFrames(*s.machine, 1);
	CHECK(s.debug->ProgramCounter() < 0x4000u); // dans le BIOS
	CHECK_EQ(s.debug->ProgramSpace().Read32(kIwram), 0u);
	CHECK_EQ(s.machine->Screens()[0]->Width(), 240);
}

TEST(Gba_CommercialRomBootAndInput) {
	if (!armtest::FileExists(kGbaRom) || !armtest::FileExists(armtest::kGbaBios))
		SKIP("ROM GBA ou BIOS absent (assets/)");
	auto result = gba::CreateMachine(armtest::MediaFromFile(kGbaRom));
	REQUIRE(result.IsOk());
	emu::Machine &machine = *result.Value();
	const emu::Screen &screen = *machine.Screens()[0];
	armtest::RunFrames(machine, 400);
	CHECK_EQ(machine.FrameCount(), 400u);
	CHECK(armtest::HasContent(screen.Frame()));
	CHECK_EQ(machine.Debugger()->ProgramSpace().Read8(0x080000B2), 0x96); // octet fixe de l'en-tête
	const std::vector<uint8_t> state = machine.SaveState();
	armtest::RunFrames(machine, 60);
	const uint64_t reference = armtest::FrameHash(screen);
	CHECK(machine.LoadState(state));
	armtest::RunFrames(machine, 60);
	CHECK_EQ(armtest::FrameHash(screen), reference);
	// A enfoncé : l'écran change (menu de langue → écran suivant).
	CHECK(machine.Inputs().front()->SetPressed("a", true));
	armtest::RunFrames(machine, 10);
	machine.Inputs().front()->SetPressed("a", false);
	armtest::RunFrames(machine, 60);
	CHECK(armtest::FrameHash(screen) != reference);
}

TEST(Gba_RomFromZipArchive) {
	if (!armtest::FileExists(kGbaZip) || !armtest::FileExists(armtest::kGbaBios))
		SKIP("archive GBA ou BIOS absent (assets/)");
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	auto system = registry.Identify(kGbaZip);
	REQUIRE(system.IsOk());
	CHECK(system.Value()->name == "gba");
	auto rom = registry.LoadRom(kGbaZip);
	REQUIRE(rom.IsOk());
	CHECK_EQ(rom.Value().data.size(), size_t(8) << 20);
	CHECK(gba::ParseHeader(rom.Value().data).valid);
	auto machine = system.Value()->create(armtest::MediaFromFile(kGbaZip)); // la fabrique relit l'archive
	REQUIRE(machine.IsOk());
	armtest::RunFrames(*machine.Value(), 300);
	CHECK_EQ(machine.Value()->FrameCount(), 300u);
	CHECK(armtest::HasContent(machine.Value()->Screens()[0]->Frame()));
}
