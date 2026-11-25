// Tests de la Nintendo DS : en-tête, fabrique, deux processeurs (ARM946E-S et ARM7TDMI) sur
// des cartouches synthétiques, affichage de la VRAM et inversion des écrans, touches (dont
// X / Y et le stylet), états, réinitialisation, débogueur ; jeux du dépôt quand ils sont présents.
#include <cpp-maker/test.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <consoles/arm/board.hpp>
#include <consoles/arm/hle/dldi.hpp>
#include <consoles/consoles.hpp>
#include <consoles/nds/nds.hpp>

#include "arm_rom.hpp"

using armtest::Program;

namespace {

constexpr uint32_t kResults = 0x02100000; ///< résultats rangés par les programmes (mémoire principale)

const std::filesystem::path kNdsRom = "assets/roms/tests/checknds.nds";
const std::filesystem::path kNdsZip = "assets/roms/nds/Mario Kart DS (Europe) (En,Fr,De,Es,It).zip";

/// Boucle sans fin : copie du registre `keyRegister` en `kResults + keyOffset` et compteur
/// incrémenté en `kResults + counterOffset`.
void KeyLoop(Program &p, uint32_t keyRegister, uint32_t keyOffset, uint32_t counterOffset) {
	p.MovConst(8, keyRegister);
	const uint32_t loop = p.Here();
	p.Ldrh(9, 8, 0);
	p.Str(9, 0, keyOffset);
	p.Ldr(10, 0, counterOffset);
	p.AddImm(10, 10, 1);
	p.Str(10, 0, counterOffset);
	p.B(loop);
}

/// ARM9 : calculs (dont CLZ, propre à l'ARMv5), moteur A en affichage direct de la VRAM A
/// (un point rouge en haut à gauche, un bleu en bas à droite), écran du moteur A en haut si
/// `swap`, puis boucle sur KEYINPUT. ARM7 : boucle sur EXTKEYIN (X, Y, stylet).
std::vector<uint8_t> TestRom(bool swap = true) {
	Program arm9(armtest::kNdsArm9Base);
	arm9.MovConst(0, kResults);
	arm9.MovImm(1, 7);
	arm9.MovImm(2, 6);
	arm9.Mul(3, 1, 2);
	arm9.Str(3, 0, 0); // 42
	arm9.Clz(4, 1);
	arm9.Str(4, 0, 4); // 29
	arm9.MovConst(5, 0x04000000);
	arm9.MovConst(6, 0x00020000); // DISPCNT A : affichage de la VRAM (bloc A)
	arm9.Str(6, 5, 0);
	arm9.MovConst(7, 0x04000240);
	arm9.MovImm(8, 0x80); // VRAMCNT_A : activé, mode LCDC
	arm9.Strb(8, 7, 0);
	arm9.MovConst(9, 0x04000304);
	arm9.MovConst(10, swap ? 0x8003 : 0x0003); // POWCNT1 : écrans, moteur A, inversion (bit 15)
	arm9.Strh(10, 9, 0);
	arm9.MovConst(11, 0x06800000);
	arm9.MovConst(12, 0x001F); // rouge
	arm9.Strh(12, 11, 0);
	arm9.MovConst(11, 0x06800000 + (191 * 256 + 255) * 2);
	arm9.MovConst(12, 0x7C00); // bleu
	arm9.Strh(12, 11, 0);
	KeyLoop(arm9, 0x04000130, 8, 12);

	Program arm7(armtest::kNdsArm7Base);
	arm7.MovConst(0, kResults);
	KeyLoop(arm7, 0x04000136, 16, 20);
	return armtest::NdsRom(arm9, arm7);
}

struct Session {
	std::unique_ptr<emu::Machine> machine;
	consoles::arm::Board *board = nullptr;
	emu::Debuggable *debug = nullptr;
};

Session Boot(std::vector<uint8_t> image) {
	Session s;
	auto result = nds::CreateMachine(armtest::MediaFromImage(std::move(image)));
	if (result.IsError())
		return s;
	s.machine = std::move(result.Value());
	s.board = s.machine->Find<consoles::arm::Board>("board");
	s.debug = s.machine->Debugger();
	return s;
}

bool IsRed(uint32_t pixel) {
	const armtest::Color c = armtest::Split(pixel);
	return c.r >= 0xF0 && c.g == 0 && c.b == 0;
}

bool IsBlue(uint32_t pixel) {
	const armtest::Color c = armtest::Split(pixel);
	return c.b >= 0xF0 && c.r == 0 && c.g == 0;
}

} // namespace

TEST(Nds_HeaderParsing) {
	std::vector<uint8_t> rom = TestRom();
	const nds::Header header = nds::ParseHeader(rom);
	CHECK(header.valid);
	CHECK_EQ(header.title, std::string("SYNTHETIC"));
	CHECK_EQ(header.gameCode, std::string("ZTST"));
	CHECK(header.checksumOk);
	rom[0x10] ^= 0xFF; // octet couvert par le CRC-16
	CHECK(!nds::ParseHeader(rom).checksumOk);
	const nds::Header tooSmall = nds::ParseHeader(std::vector<uint8_t>(0x100));
	CHECK(!tooSmall.valid);
	CHECK(!tooSmall.error.empty());
}

TEST(Nds_SystemInfoAndRejectedImages) {
	const emu::SystemInfo info = nds::System();
	CHECK_EQ(info.name, std::string("nds"));
	CHECK_EQ(info.year, 2004);
	CHECK(info.readsRomFromPath);
	CHECK(info.extensions == std::vector<std::string>({".nds", ".srl"}));
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	CHECK(registry.FindByExtension(".srl") == registry.Find("nds"));
	CHECK(nds::CreateMachine(emu::Media{}).IsError());
	CHECK(nds::CreateMachine(armtest::MediaFromImage(std::vector<uint8_t>(0x20))).IsError());
	emu::Media missing;
	missing.romPath = "absent/jeu.nds";
	CHECK(nds::CreateMachine(missing).IsError());
}

TEST(Nds_MachineLayout) {
	Session s = Boot(TestRom());
	REQUIRE(s.machine != nullptr);
	REQUIRE(s.board != nullptr);
	CHECK(s.board->GetMode() == consoles::arm::Mode::Nds);
	CHECK(s.debug == s.board);
	REQUIRE(s.machine->Screens().size() == 1u);
	CHECK_EQ(s.machine->Screens()[0]->Width(), 256);
	CHECK_EQ(s.machine->Screens()[0]->Height(), 384); // deux écrans empilés, celui du haut d'abord
	REQUIRE(s.machine->TouchPanels().size() == 1u);
	const emu::TouchPanel &touch = *s.machine->TouchPanels()[0];
	CHECK_EQ(touch.AreaX(), 0);
	CHECK_EQ(touch.AreaY(), 192);
	CHECK_EQ(touch.AreaWidth(), 256);
	CHECK_EQ(touch.AreaHeight(), 192);
	CHECK_EQ(s.machine->Inputs()[0]->Fields().size(), 12u); // X et Y en plus
	CHECK_EQ(s.machine->Sounds()[0]->SampleRate(), 32728);
	CHECK_EQ(s.debug->MemoryRegions().size(), 6u);
}

TEST(Nds_BothProcessorsRun) {
	Session s = Boot(TestRom());
	REQUIRE(s.machine != nullptr);
	emu::Machine &machine = *s.machine;
	armtest::RunFrames(machine, 2);
	CHECK_EQ(machine.GetScheduler().Now(), 2u * nds::kFrameTicks);
	const emu::AddressSpace &bus = s.debug->ProgramSpace(); // vue de l'ARM9
	CHECK_EQ(bus.Read32(kResults + 0), 42u);
	CHECK_EQ(bus.Read32(kResults + 4), 29u); // CLZ(7)
	const uint32_t arm9 = bus.Read32(kResults + 12), arm7 = bus.Read32(kResults + 20);
	CHECK(arm9 > 1000u);
	CHECK(arm7 > 1000u);
	CHECK(arm9 > arm7); // l'ARM9 tourne deux fois plus vite
	armtest::RunFrames(machine, 1);
	CHECK(bus.Read32(kResults + 12) > arm9);
	CHECK(bus.Read32(kResults + 20) > arm7);

	// Débogueur : processeur principal = ARM9, état ARM, mode système (démarrage direct).
	const auto regs = s.debug->DebugRegisters();
	REQUIRE(regs.size() == 18u);
	CHECK_EQ(regs[0].value, kResults);
	CHECK_EQ(regs[16].value & 0x3Fu, 0x1Fu);
	const uint32_t pc = s.debug->ProgramCounter();
	CHECK(pc >= armtest::kNdsArm9Base && pc < armtest::kNdsArm9Base + 0x100);
	CHECK(s.debug->FlagsText().find("SYS") != std::string::npos);
	const emu::DisassembledLine clz = s.debug->Disassemble(armtest::kNdsArm9Base + 6 * 4); // après MOV+ORR, MOV, MOV, MUL, STR
	CHECK_EQ(clz.mnemonic, std::string("CLZ"));
	CHECK_EQ(clz.operands, std::string("R4,R1"));
}

TEST(Nds_VramDisplayAndScreenSwap) {
	{
		Session s = Boot(TestRom(true)); // moteur A sur l'écran du haut
		REQUIRE(s.machine != nullptr);
		armtest::RunFrames(*s.machine, 3);
		const emu::Screen &screen = *s.machine->Screens()[0];
		CHECK(IsRed(armtest::Pixel(screen, 0, 0)));
		CHECK(IsBlue(armtest::Pixel(screen, 255, 191)));
		CHECK(!IsRed(armtest::Pixel(screen, 1, 0)));
		CHECK(!IsRed(armtest::Pixel(screen, 0, 192)));
	}
	{
		Session s = Boot(TestRom(false)); // moteur A sur l'écran du bas
		REQUIRE(s.machine != nullptr);
		armtest::RunFrames(*s.machine, 3);
		const emu::Screen &screen = *s.machine->Screens()[0];
		CHECK(IsRed(armtest::Pixel(screen, 0, 192)));
		CHECK(IsBlue(armtest::Pixel(screen, 255, 383)));
		CHECK(!IsRed(armtest::Pixel(screen, 0, 0)));
	}
}

TEST(Nds_KeysExtraKeysAndPen) {
	Session s = Boot(TestRom());
	REQUIRE(s.machine != nullptr);
	emu::Machine &machine = *s.machine;
	emu::InputPort &pad = *machine.Inputs()[0];
	emu::TouchPanel &touch = *machine.TouchPanels()[0];
	const emu::AddressSpace &bus = s.debug->ProgramSpace();
	armtest::RunFrames(machine, 2);
	CHECK_EQ(bus.Read32(kResults + 8) & 0x3FFu, 0x3FFu);			// KEYINPUT : rien d'enfoncé
	CHECK_EQ(bus.Read32(kResults + 16) & 0x43u, 0x43u);			// EXTKEYIN : X, Y, stylet levés
	CHECK(pad.SetPressed("b", true));
	CHECK(pad.SetPressed("r", true));
	CHECK(pad.SetPressed("x", true));
	touch.Set(128, 96, true);
	CHECK(touch.Pressed());
	armtest::RunFrames(machine, 1);
	CHECK_EQ(bus.Read32(kResults + 8) & 0x3FFu, 0x3FFu & ~((1u << 1) | (1u << 8))); // B, R
	CHECK_EQ(bus.Read32(kResults + 16) & 0x43u, 0x02u);							// X et stylet à 0, Y à 1
	pad.SetPressed("x", false);
	CHECK(pad.SetPressed("y", true));
	touch.Set(0, 0, false);
	armtest::RunFrames(machine, 1);
	CHECK_EQ(bus.Read32(kResults + 16) & 0x43u, 0x41u); // Y à 0
}

TEST(Nds_StateRoundTripAndReset) {
	Session s = Boot(TestRom());
	REQUIRE(s.machine != nullptr);
	emu::Machine &machine = *s.machine;
	const emu::AddressSpace &bus = s.debug->ProgramSpace();
	armtest::RunFrames(machine, 5);
	const std::vector<uint8_t> state = machine.SaveState();
	CHECK(state.size() > 1000000u); // 4 Mo de mémoire principale et la VRAM
	armtest::RunFrames(machine, 10);
	const uint32_t arm9 = bus.Read32(kResults + 12), arm7 = bus.Read32(kResults + 20);
	const uint64_t frame = armtest::FrameHash(*machine.Screens()[0]);
	CHECK(machine.LoadState(state));
	CHECK_EQ(machine.FrameCount(), 5u);
	armtest::RunFrames(machine, 10);
	CHECK_EQ(bus.Read32(kResults + 12), arm9); // les deux processeurs refont exactement le même chemin
	CHECK_EQ(bus.Read32(kResults + 20), arm7);
	CHECK_EQ(armtest::FrameHash(*machine.Screens()[0]), frame);

	machine.Reset();
	CHECK(s.board->LastError().empty());
	CHECK_EQ(machine.FrameCount(), 0u);
	armtest::RunFrames(machine, 1);
	CHECK_EQ(bus.Read32(kResults), 42u);
	CHECK(bus.Read32(kResults + 12) < arm9);
}

TEST(Nds_CommercialTestRomAndTouch) {
	if (!armtest::FileExists(kNdsRom))
		SKIP("ROM NDS de test absente (assets/roms/tests)");
	auto result = nds::CreateMachine(armtest::MediaFromFile(kNdsRom));
	REQUIRE(result.IsOk());
	emu::Machine &machine = *result.Value();
	const emu::Screen &screen = *machine.Screens()[0];
	emu::TouchPanel &touch = *machine.TouchPanels()[0];
	armtest::RunFrames(machine, 300);
	CHECK(armtest::HasContent(screen.Frame()));
	// Le programme affiche les coordonnées du stylet : l'image change quand on touche.
	const uint64_t before = armtest::FrameHash(screen);
	touch.Set(200, 150, true);
	armtest::RunFrames(machine, 30);
	CHECK(armtest::FrameHash(screen) != before);
	touch.Set(0, 0, false);
	const std::vector<uint8_t> state = machine.SaveState();
	CHECK(machine.LoadState(state));
}

TEST(Nds_CommercialRomFromZip3D) {
	if (!armtest::FileExists(kNdsZip))
		SKIP("archive NDS absente (assets/roms/nds)");
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	auto system = registry.Identify(kNdsZip);
	REQUIRE(system.IsOk());
	CHECK(system.Value()->name == "nds");
	auto machine = system.Value()->create(armtest::MediaFromFile(kNdsZip));
	REQUIRE(machine.IsOk());
	armtest::RunFrames(*machine.Value(), 600);
	// Écran titre en 3D (moitié haute) et menu (moitié basse) : les deux moitiés ont du contenu.
	const auto frame = machine.Value()->Screens()[0]->Frame();
	const size_t half = frame.size() / 2;
	CHECK(armtest::HasContent(frame.subspan(0, half)));
	CHECK(armtest::HasContent(frame.subspan(half)));
}

TEST(Nds_DldiPatchStaysInsideRom) {
	// Signature DLDI (magie + « Chishm ») trop près de la fin : l'en-tête de 0x98 octets ne
	// tient pas, la ROM ne doit être ni lue ni écrite au-delà de sa taille (ROM non fiable).
	consoles::arm::Dldi dldi(nullptr);
	std::vector<uint8_t> rom(0x100, 0);
	const auto place = [&](size_t at) {
		const uint8_t magic[] = {0xED, 0xA5, 0x8D, 0xBF, ' ', 'C', 'h', 'i', 's', 'h', 'm', 0};
		std::copy(std::begin(magic), std::end(magic), rom.begin() + std::ptrdiff_t(at));
		rom[at + 0x0F] = 0x0F; // place disponible annoncée
	};
	place(rom.size() - 0x20);
	dldi.PatchRom(rom.data(), 0, uint32_t(rom.size()));
	CHECK(!dldi.IsPatched());
	// En-tête complet : le correctif s'applique.
	std::fill(rom.begin(), rom.end(), uint8_t{0});
	place(0x40);
	dldi.PatchRom(rom.data(), 0, uint32_t(rom.size()));
	CHECK(dldi.IsPatched());
	CHECK_EQ(rom[0x40 + 0x60], uint8_t('N'));
}
