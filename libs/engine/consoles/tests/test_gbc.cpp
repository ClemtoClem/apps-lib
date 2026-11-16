#include <cpp-maker/test.hpp>

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>

#include <consoles/consoles.hpp>
#include <consoles/gbc/gbc.hpp>
#include <emu/emu.hpp>

namespace {

/// Processeur seul sur un bus de 64 Ko de RAM : le programme est placé en 0x0100.
struct CpuBench {
	emu::Machine machine{"bench", gbc::kMasterClock};
	emu::AddressSpace bus{"ram", 16, 8};
	std::vector<uint8_t> ram = std::vector<uint8_t>(0x10000, 0);
	gbc::Lr35902 *cpu = nullptr;

	explicit CpuBench(std::initializer_list<uint8_t> program) {
		bus.MapRam(0x0000, 0xFFFF, ram);
		cpu = &machine.Root().Add<gbc::Lr35902>("cpu", bus);
		gbc::Lr35902::Registers regs;
		regs.pc = 0x0100;
		regs.sp = 0xFFFE;
		cpu->SetResetState(regs);
		machine.Start();
		std::copy(program.begin(), program.end(), ram.begin() + 0x100);
	}
	int Step(int count = 1) {
		int cycles = 0;
		for (int i = 0; i < count; ++i)
			cycles += cpu->Step();
		return cycles;
	}
	gbc::Lr35902::Registers &R() { return cpu->Regs(); }
};

/// Image de cartouche minimale : en-tête valide, `banks` banques de 16 Ko dont le premier
/// octet (après l'en-tête pour la banque 0) est le numéro de banque.
std::vector<uint8_t> MakeRom(uint8_t type, uint8_t romSizeCode, uint8_t ramSizeCode, bool color = false,
							 std::initializer_list<uint8_t> program = {}) {
	const size_t banks = size_t(2) << romSizeCode;
	std::vector<uint8_t> rom(banks * 0x4000, 0);
	for (size_t b = 0; b < banks; ++b) {
		rom[b * 0x4000 + 0x2000] = uint8_t(b);
		rom[b * 0x4000 + 0x2001] = uint8_t(b >> 8);
	}
	const char title[] = "TEST";
	std::copy(title, title + 4, rom.begin() + 0x134);
	rom[0x143] = color ? 0x80 : 0x00;
	rom[0x147] = type;
	rom[0x148] = romSizeCode;
	rom[0x149] = ramSizeCode;
	uint8_t sum = 0;
	for (size_t i = 0x134; i <= 0x14C; ++i)
		sum = uint8_t(sum - rom[i] - 1);
	rom[0x14D] = sum;
	// Point d'entrée : saut vers le programme en 0x0150.
	rom[0x100] = 0x00;
	rom[0x101] = 0xC3;
	rom[0x102] = 0x50;
	rom[0x103] = 0x01;
	std::copy(program.begin(), program.end(), rom.begin() + 0x150);
	return rom;
}

std::unique_ptr<emu::Machine> Boot(std::vector<uint8_t> rom, std::string_view model = "auto", std::filesystem::path romPath = {}) {
	emu::Media media;
	media.rom = std::move(rom);
	media.romPath = std::move(romPath);
	media.options["model"] = std::string(model);
	auto result = gbc::CreateMachine(media);
	return result.IsOk() ? std::move(result.Value()) : nullptr;
}

} // namespace

TEST(Gbc_CpuArithmeticFlagsAndTiming) {
	// LD A,0x3C ; ADD A,0xC6 → 0x02, retenue et demi-retenue.
	CpuBench b{0x3E, 0x3C, 0xC6, 0xC6};
	CHECK_EQ(b.Step(), 8);
	CHECK_EQ(b.Step(), 8);
	CHECK_EQ(b.R().a, 0x02);
	CHECK_EQ(b.R().f, gbc::kFlagH | gbc::kFlagC);

	// SUB, SBC, CP, AND, XOR, OR.
	CpuBench c{0x3E, 0x10, 0xD6, 0x01, 0xDE, 0x0F, 0xFE, 0x00, 0xE6, 0xF0, 0xEE, 0xFF, 0xF6, 0x01};
	c.Step(2); // 0x10 − 1 = 0x0F : demi-emprunt
	CHECK_EQ(c.R().a, 0x0F);
	CHECK_EQ(c.R().f, gbc::kFlagN | gbc::kFlagH);
	c.Step(); // SBC 0x0F sans retenue : 0
	CHECK_EQ(c.R().a, 0x00);
	CHECK_EQ(c.R().f, gbc::kFlagZ | gbc::kFlagN);
	c.Step(); // CP 0 : Z, N ; A inchangé
	CHECK_EQ(c.R().f, gbc::kFlagZ | gbc::kFlagN);
	c.Step(); // AND 0xF0 : Z, H
	CHECK_EQ(c.R().f, gbc::kFlagZ | gbc::kFlagH);
	c.Step(); // XOR 0xFF
	CHECK_EQ(c.R().a, 0xFF);
	CHECK_EQ(c.R().f, 0);
	c.Step();
	CHECK_EQ(c.R().a, 0xFF);

	// DAA : 0x45 + 0x38 = 0x7D → 0x83 en décimal codé binaire ; puis 0x83 − 0x38 → 0x45.
	CpuBench d{0x3E, 0x45, 0xC6, 0x38, 0x27, 0xD6, 0x38, 0x27};
	d.Step(3);
	CHECK_EQ(d.R().a, 0x83);
	d.Step(2);
	CHECK_EQ(d.R().a, 0x45);

	// INC/DEC 8 bits (la retenue est préservée), 16 bits (aucun drapeau), ADD HL.
	CpuBench e{0x37, 0x06, 0xFF, 0x04, 0x05, 0x21, 0xFF, 0x0F, 0x23, 0x01, 0x01, 0xF0, 0x09};
	e.Step(3); // SCF ; LD B,0xFF ; INC B
	CHECK_EQ(e.R().b, 0x00);
	CHECK_EQ(e.R().f, gbc::kFlagZ | gbc::kFlagH | gbc::kFlagC);
	e.Step(); // DEC B → 0xFF
	CHECK_EQ(e.R().f, gbc::kFlagN | gbc::kFlagH | gbc::kFlagC);
	e.Step(); // LD HL,0x0FFF
	CHECK_EQ(e.Step(), 8); // INC HL
	CHECK_EQ(e.R().Hl(), 0x1000);
	e.Step(); // LD BC,0xF001
	CHECK_EQ(e.Step(), 8); // ADD HL,BC : 0x1000 + 0xF001 = 0x0001, retenue
	CHECK_EQ(e.R().Hl(), 0x0001);
	CHECK(e.R().f & gbc::kFlagC);

	// Rotations de l'accumulateur et préfixe CB (dont l'accès à (HL) : 16 cycles, BIT : 12).
	CpuBench f{0x3E, 0x85, 0x07, 0x1F, 0x21, 0x00, 0xC0, 0x36, 0x81, 0xCB, 0x06, 0xCB, 0x7E, 0xCB, 0x86, 0xCB, 0x37, 0xCB, 0x3F};
	f.Step(2); // RLCA : 0x85 → 0x0B, retenue
	CHECK_EQ(f.R().a, 0x0B);
	CHECK_EQ(f.R().f, gbc::kFlagC);
	f.Step(); // RRA : la retenue entre par le haut
	CHECK_EQ(f.R().a, 0x85);
	CHECK_EQ(f.R().f, gbc::kFlagC);
	f.Step();
	CHECK_EQ(f.Step(), 12); // LD (HL),n
	CHECK_EQ(f.Step(), 16); // RLC (HL) : 0x81 → 0x03
	CHECK_EQ(f.ram[0xC000], 0x03);
	CHECK_EQ(f.Step(), 12); // BIT 7,(HL) : bit nul → Z
	CHECK(f.R().f & gbc::kFlagZ);
	CHECK_EQ(f.Step(), 16); // RES 0,(HL)
	CHECK_EQ(f.ram[0xC000], 0x02);
	f.Step(); // SWAP A : 0x85 → 0x58
	CHECK_EQ(f.R().a, 0x58);
	CHECK_EQ(f.Step(), 8); // SRL A
	CHECK_EQ(f.R().a, 0x2C);
}

TEST(Gbc_CpuControlFlowStackAndInterrupts) {
	// CALL / RET / PUSH / POP / JR / JP cc / RST : adresses et durées.
	CpuBench b{0xCD, 0x10, 0x01,			   // 0100 CALL 0x0110
			   0x18, 0x02,					   // 0103 JR +2
			   0x00, 0x00,					   // 0105
			   0xC2, 0x00, 0x02,			   // 0107 JP NZ,0x0200
			   0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			   0x01, 0x34, 0x12,			   // 0110 LD BC,0x1234
			   0xC5,						   // 0113 PUSH BC
			   0xF1,						   // 0114 POP AF (les 4 bits bas de F restent nuls)
			   0xC9};						   // 0115 RET
	CHECK_EQ(b.Step(), 24);
	CHECK_EQ(b.R().pc, 0x0110);
	CHECK_EQ(b.R().sp, 0xFFFC);
	CHECK_EQ(b.ram[0xFFFC], 0x03);
	b.Step();
	CHECK_EQ(b.Step(), 16); // PUSH
	CHECK_EQ(b.Step(), 12); // POP AF
	CHECK_EQ(b.R().a, 0x12);
	CHECK_EQ(b.R().f, 0x30);
	CHECK_EQ(b.Step(), 16); // RET
	CHECK_EQ(b.R().pc, 0x0103);
	CHECK_EQ(b.Step(), 12); // JR pris
	CHECK_EQ(b.R().pc, 0x0107);
	CHECK_EQ(b.Step(), 16); // JP NZ pris (Z nul)
	CHECK_EQ(b.R().pc, 0x0200);

	// Sauts non pris, RET cc, LD (nn),SP, ADD SP,d, LD HL,SP+d.
	CpuBench c{0xAF,			 // XOR A → Z
			   0x20, 0x05,		 // JR NZ non pris : 8 cycles
			   0xC0,			 // RET NZ non pris : 8 cycles
			   0x08, 0x00, 0xC0, // LD (0xC000),SP : 20 cycles
			   0xE8, 0xFE,		 // ADD SP,−2 : 16 cycles
			   0xF8, 0x02,		 // LD HL,SP+2 : 12 cycles
			   0xF9,			 // LD SP,HL : 8 cycles
			   0xE9};			 // JP HL : 4 cycles
	c.Step();
	CHECK_EQ(c.Step(), 8);
	CHECK_EQ(c.Step(), 8);
	CHECK_EQ(c.Step(), 20);
	CHECK_EQ(c.ram[0xC000], 0xFE);
	CHECK_EQ(c.ram[0xC001], 0xFF);
	CHECK_EQ(c.Step(), 16);
	CHECK_EQ(c.R().sp, 0xFFFC);
	CHECK_EQ(c.Step(), 12);
	CHECK_EQ(c.R().Hl(), 0xFFFE);
	CHECK_EQ(c.Step(), 8);
	CHECK_EQ(c.Step(), 4);
	CHECK_EQ(c.R().pc, 0xFFFE);

	// Interruptions : EI ne prend effet qu'après l'instruction suivante ; HALT se réveille.
	CpuBench d{0xFB, 0x00, 0x00, 0x76, 0x00};
	d.cpu->WriteIe(gbc::kIntTimer);
	d.cpu->RequestInterrupt(gbc::kIntTimer);
	d.Step(); // EI
	CHECK(!d.cpu->InterruptsEnabled());
	d.Step(); // NOP : l'interruption attend encore
	CHECK_EQ(d.R().pc, 0x0102);
	CHECK(d.cpu->InterruptsEnabled());
	CHECK_EQ(d.Step(), 20); // prise en compte : vecteur 0x50
	CHECK_EQ(d.R().pc, 0x0050);
	CHECK(!d.cpu->InterruptsEnabled());
	CHECK_EQ(d.cpu->ReadIf() & 0x1F, 0);
	CHECK_EQ(d.ram[0xFFFC], 0x02); // adresse de retour empilée
	// RETI réactive les interruptions ; la priorité va au bit le plus bas.
	d.ram[0x50] = 0xD9;
	d.Step();
	CHECK(d.cpu->InterruptsEnabled());
	CHECK_EQ(d.R().pc, 0x0102);
	d.Step(); // NOP
	d.Step(); // HALT
	CHECK(d.cpu->Halted());
	CHECK_EQ(d.Step(), 4); // rien ne se passe
	CHECK(d.cpu->Halted());
	d.cpu->WriteIe(gbc::kIntTimer | gbc::kIntVBlank);
	d.cpu->RequestInterrupt(gbc::kIntTimer | gbc::kIntVBlank);
	d.Step();
	CHECK(!d.cpu->Halted());
	CHECK_EQ(d.R().pc, 0x0040); // VBlank d'abord
	// Interruptions masquées (DI) : HALT se réveille sans appeler le vecteur.
	CpuBench e{0xF3, 0x76, 0x3C};
	e.cpu->WriteIe(gbc::kIntJoypad);
	e.Step(2);
	CHECK(e.cpu->Halted());
	e.cpu->RequestInterrupt(gbc::kIntJoypad);
	e.Step();
	CHECK_EQ(e.R().a, 1);
	CHECK_EQ(e.R().pc, 0x0103);
}

TEST(Gbc_HeaderAndBankControllers) {
	// En-tête.
	gbc::CartridgeHeader h = gbc::ParseHeader(MakeRom(0x1B, 3, 3, true));
	CHECK(h.valid);
	CHECK(h.title == "TEST");
	CHECK(h.cgb && !h.cgbOnly);
	CHECK(h.mbc == gbc::Mbc::Mbc5);
	CHECK(h.battery);
	CHECK_EQ(h.romSize, 0x40000u);
	CHECK_EQ(h.ramSize, 0x8000u);
	CHECK(h.checksumOk);
	CHECK(!gbc::ParseHeader(std::vector<uint8_t>(0x100)).valid); // trop petite
	std::vector<uint8_t> unknown = MakeRom(0x00, 0, 0);
	unknown[0x147] = 0xFC; // caméra : non prise en charge
	CHECK(!gbc::ParseHeader(unknown).valid);
	CHECK(Boot(unknown) == nullptr);
	std::vector<uint8_t> corrupt = MakeRom(0x00, 0, 0);
	corrupt[0x134] ^= 0xFF;
	CHECK(!gbc::ParseHeader(corrupt).checksumOk);

	// MBC1 : banque 0 → 1, bits hauts, mode 1 (banque 0 commutée et banque de RAM).
	{
		auto machine = Boot(MakeRom(0x03, 6, 3)); // 128 banques, 32 Ko de RAM
		REQUIRE(machine != nullptr);
		auto &bus = machine->Find<gbc::Board>("board")->Bus();
		CHECK_EQ(bus.Read8(0x6000), 1);
		bus.Write8(0x2000, 0x00);
		CHECK_EQ(bus.Read8(0x6000), 1); // 0 est remplacé par 1
		bus.Write8(0x2000, 0x05);
		CHECK_EQ(bus.Read8(0x6000), 5);
		bus.Write8(0x4000, 0x02);
		CHECK_EQ(bus.Read8(0x6000), 0x45);
		CHECK_EQ(bus.Read8(0x2000), 0); // banque basse fixe en mode 0
		bus.Write8(0x6000, 0x01);
		CHECK_EQ(bus.Read8(0x2000), 0x40); // mode 1 : la banque basse suit les bits hauts
		// RAM : fermée par défaut (0xFF), ouverte par 0x0A, banques distinctes en mode 1.
		CHECK_EQ(bus.Read8(0xA000), 0xFF);
		bus.Write8(0xA000, 0x11); // ignorée
		bus.Write8(0x0000, 0x0A);
		CHECK_EQ(bus.Read8(0xA000), 0x00);
		bus.Write8(0xA000, 0x22); // banque 2
		bus.Write8(0x4000, 0x00);
		CHECK_EQ(bus.Read8(0xA000), 0x00); // banque 0
		bus.Write8(0xA000, 0x33);
		bus.Write8(0x4000, 0x02);
		CHECK_EQ(bus.Read8(0xA000), 0x22);
		bus.Write8(0x0000, 0x00);
		CHECK_EQ(bus.Read8(0xA000), 0xFF);
		CHECK(machine->Find<emu::Nvram>("board:cart:ram")->Dirty());
	}
	// MBC2 : sélection par le bit 8 de l'adresse, RAM de 512 quartets répétée.
	{
		auto machine = Boot(MakeRom(0x06, 3, 0));
		REQUIRE(machine != nullptr);
		auto &bus = machine->Find<gbc::Board>("board")->Bus();
		bus.Write8(0x2100, 0x07);
		CHECK_EQ(bus.Read8(0x6000), 7);
		bus.Write8(0x0000, 0x0A);
		bus.Write8(0xA005, 0xAB);
		CHECK_EQ(bus.Read8(0xA005), 0xFB);
		CHECK_EQ(bus.Read8(0xA205), 0xFB); // miroir
	}
	// MBC3 : 7 bits de banque, 4 banques de RAM, horloge verrouillée en temps émulé.
	{
		auto machine = Boot(MakeRom(0x10, 6, 3));
		REQUIRE(machine != nullptr);
		auto &bus = machine->Find<gbc::Board>("board")->Bus();
		bus.Write8(0x2000, 0x7F);
		CHECK_EQ(bus.Read8(0x6000), 0x7F);
		bus.Write8(0x0000, 0x0A);
		bus.Write8(0x4000, 0x03);
		bus.Write8(0xA000, 0x99);
		bus.Write8(0x4000, 0x00);
		CHECK_EQ(bus.Read8(0xA000), 0x00);
		machine->Run(gbc::kMasterClock * 3 + 100); // un peu plus de 3 secondes émulées
		bus.Write8(0x6000, 0x00);
		bus.Write8(0x6000, 0x01); // verrouillage
		bus.Write8(0x4000, 0x08);
		CHECK_EQ(bus.Read8(0xA000), 3); // secondes
		bus.Write8(0x4000, 0x09);
		CHECK_EQ(bus.Read8(0xA000), 0); // minutes
		bus.Write8(0x4000, 0x03);
		CHECK_EQ(bus.Read8(0xA000), 0x99);
	}
	// MBC5 : 9 bits de banque, la banque 0 est sélectionnable.
	{
		auto machine = Boot(MakeRom(0x1B, 8, 3)); // 512 banques
		REQUIRE(machine != nullptr);
		auto &bus = machine->Find<gbc::Board>("board")->Bus();
		bus.Write8(0x2000, 0x00);
		CHECK_EQ(bus.Read8(0x6000), 0);
		bus.Write8(0x2000, 0x34);
		bus.Write8(0x3000, 0x01);
		CHECK_EQ(bus.Read16(0x6000), 0x134);
		CHECK_EQ(machine->Find<gbc::Cartridge>("board:cart")->RomBank(), 0x134u);
	}
}

TEST(Gbc_MachineBootVideoTimerInputSerial) {
	// Programme : écrit « OK » sur la liaison série, charge une tuile pleine (couleur 3),
	// active l'interruption de la minuterie (TIMA à 262144 Hz, rechargée à 0xF0) et compte
	// les interruptions en 0xC000 ; lit la manette en 0xC001.
	const std::vector<uint8_t> rom = MakeRom(0x00, 0, 0, false,
		{0x31, 0xFE, 0xFF,		 // LD SP,0xFFFE
		 0x3E, 'O', 0xE0, 0x01,	 // SB = 'O'
		 0x3E, 0x81, 0xE0, 0x02, // SC = 0x81 : envoi
		 0x3E, 'K', 0xE0, 0x01, 0x3E, 0x81, 0xE0, 0x02,
		 0x21, 0x00, 0x80,		 // LD HL,0x8000 : tuile 0
		 0x06, 0x10,			 // LD B,16
		 0x3E, 0xFF, 0x22, 0x05, // boucle : LD A,0xFF ; LD (HL+),A ; DEC B
		 0x20, 0xFA,			 // JR NZ,boucle
		 0x3E, 0xE4, 0xE0, 0x47, // BGP = 0xE4 (identité)
		 0x3E, 0xF0, 0xE0, 0x06, // TMA = 0xF0
		 0x3E, 0x05, 0xE0, 0x07, // TAC = 5 : actif, 262144 Hz
		 0x3E, 0x04, 0xE0, 0xFF, // IE = minuterie
		 0x3E, 0x10, 0xE0, 0x00, // P1 : sélection des boutons (bit 5 à 0)
		 0xFB,					 // EI
		 0xF0, 0x00, 0xEA, 0x01, 0xC0, // attente : A = P1 ; (0xC001) = A
		 0x18, 0xF9});			 // JR attente
	std::vector<uint8_t> image = rom;
	// Vecteur de la minuterie (0x50) : INC (0xC000) ; RETI.
	const uint8_t handler[] = {0xF5, 0xFA, 0x00, 0xC0, 0x3C, 0xEA, 0x00, 0xC0, 0xF1, 0xD9};
	std::copy(std::begin(handler), std::end(handler), image.begin() + 0x50);

	auto machine = Boot(image, "dmg");
	REQUIRE(machine != nullptr);
	auto *board = machine->Find<gbc::Board>("board");
	REQUIRE(board != nullptr);
	CHECK(!board->Color());
	// Arbre de la machine.
	CHECK(machine->Find<gbc::Lr35902>("board:cpu") != nullptr);
	CHECK(machine->Find<gbc::Ppu>("board:ppu") != nullptr);
	CHECK_EQ(machine->Screens().size(), 1u);
	CHECK_EQ(machine->Sounds().size(), 1u);
	CHECK_EQ(machine->Inputs().size(), 1u);
	CHECK_EQ(board->Cpu().Regs().pc, 0x0100); // démarrage direct
	CHECK_EQ(board->Cpu().Regs().a, 0x01);

	// Une image dure exactement 70224 cycles (140448 tops) à partir de la deuxième.
	machine->RunFrame();
	const emu::Time first = machine->GetScheduler().Now();
	machine->RunFrame();
	CHECK_EQ(machine->GetScheduler().Now() - first, emu::Time(gbc::kFrameDots) * gbc::kTicksPerDot);
	CHECK_EQ(machine->FrameCount(), 2u);
	CHECK(board->SerialOutput() == "OK");

	// Vidéo : la carte du fond est vide (tuile 0), la tuile 0 est pleine → couleur la plus foncée.
	const emu::Screen &screen = *machine->Screens()[0];
	CHECK_EQ(screen.Width(), 160);
	CHECK_EQ(screen.Height(), 144);
	const uint32_t darkest = emu::Rgb(0x0F, 0x38, 0x0F);
	CHECK_EQ(screen.Frame()[0], darkest);
	CHECK_EQ(screen.Frame()[size_t(143) * 160 + 159], darkest);

	// Minuterie : 262144 Hz / 16 pas par débordement = 16384 interruptions/s ≈ 274 par image.
	const uint8_t before = board->Bus().Read8(0xC000);
	machine->RunFrame();
	const int perFrame = uint8_t(board->Bus().Read8(0xC000) - before);
	CHECK(perFrame >= 16 && perFrame <= 20); // 274 mod 256 = 18
	// DIV avance à 16384 Hz : 70224 cycles / 256 ≈ 274 pas par image.
	const uint8_t div0 = board->Bus().Read8(0xFF04);
	machine->RunFrame();
	const int divStep = uint8_t(board->Bus().Read8(0xFF04) - div0);
	CHECK(divStep >= 17 && divStep <= 19);

	// Manette : A pressé → bit 0 à 0 quand les boutons sont sélectionnés.
	CHECK_EQ(board->Bus().Read8(0xC001) & 0x0F, 0x0F);
	CHECK(board->Pad().SetPressed("a", true));
	board->Pad().SetPressed("left", true); // direction : invisible dans cette sélection
	machine->RunFrame();
	CHECK_EQ(board->Bus().Read8(0xC001) & 0x0F, 0x0E);
	board->Pad().SetPressed("a", false);
	machine->RunFrame();
	CHECK_EQ(board->Bus().Read8(0xC001) & 0x0F, 0x0F);

	// Son : une sortie à 48 kHz produit ~803 trames par image.
	const uint64_t frames0 = machine->Sounds()[0]->TotalFrames();
	machine->RunFrame();
	const uint64_t produced = machine->Sounds()[0]->TotalFrames() - frames0;
	CHECK(produced >= 800 && produced <= 806);

	// Écho de la RAM, zone non projetée, registre IE.
	board->Bus().Write8(0xC123, 0x5A);
	CHECK_EQ(board->Bus().Read8(0xE123), 0x5A);
	CHECK_EQ(board->Bus().Read8(0xFEA0), 0xFF);
	CHECK_EQ(board->Bus().Read8(0xFFFF), 0x04);
}

TEST(Gbc_ColorModeSpeedSwitchBanksAndHdma) {
	// Programme couleur : arme le changement de vitesse puis STOP ; boucle.
	const std::vector<uint8_t> rom = MakeRom(0x00, 0, 0, true,
		{0x3E, 0x01, 0xE0, 0x4D, // KEY1 = 1
		 0x10, 0x00,			 // STOP
		 0x18, 0xFE});			 // JR −2
	auto machine = Boot(rom);
	REQUIRE(machine != nullptr);
	auto *board = machine->Find<gbc::Board>("board");
	CHECK(board->Color());
	CHECK_EQ(board->Cpu().Regs().a, 0x11); // signature de la Game Boy Color
	auto &bus = board->Bus();
	CHECK_EQ(bus.Read8(0xFF4D) & 0x80, 0);
	machine->RunFrame();
	CHECK(board->DoubleSpeed());
	CHECK_EQ(bus.Read8(0xFF4D) & 0x81, 0x80);
	// En vitesse double, le processeur exécute deux fois plus de cycles par image.
	const uint64_t c0 = board->Cpu().TotalCycles();
	machine->RunFrame();
	const uint64_t cycles = board->Cpu().TotalCycles() - c0;
	CHECK(cycles >= 2u * 70224u - 32u && cycles <= 2u * 70224u + 32u);

	// Banques de RAM de travail (SVBK) et de VRAM (VBK).
	bus.Write8(0xFF70, 2);
	bus.Write8(0xD000, 0x22);
	bus.Write8(0xFF70, 3);
	bus.Write8(0xD000, 0x33);
	bus.Write8(0xFF70, 2);
	CHECK_EQ(bus.Read8(0xD000), 0x22);
	CHECK_EQ(bus.Read8(0xF000), 0x22); // écho de la banque courante
	bus.Write8(0xFF70, 0); // 0 désigne la banque 1
	CHECK_EQ(bus.Read8(0xFF70) & 7, 1);
	bus.Write8(0xFF4F, 1);
	bus.Write8(0x8000, 0xB1);
	bus.Write8(0xFF4F, 0);
	CHECK_EQ(bus.Read8(0x8000), 0x00);
	CHECK_EQ(board->Video().Vram(1)[0], 0xB1);

	// Palettes couleur : écriture avec incrément automatique ; rendu d'un fond rouge.
	bus.Write8(0xFF68, 0x80);
	bus.Write8(0xFF69, 0x1F); // couleur 0 de la palette 0 : rouge
	bus.Write8(0xFF69, 0x00);
	CHECK_EQ(bus.Read8(0xFF68) & 0x3F, 2);
	machine->RunFrame();
	CHECK_EQ(machine->Screens()[0]->Frame()[80 * 160 + 80], emu::Rgb(255, 0, 0));

	// HDMA général : copie immédiate de 32 octets de la RAM vers la VRAM.
	for (uint32_t i = 0; i < 32; ++i)
		bus.Write8(0xC100 + i, uint8_t(0x40 + i));
	bus.Write8(0xFF51, 0xC1);
	bus.Write8(0xFF52, 0x00);
	bus.Write8(0xFF53, 0x10); // 0x9000
	bus.Write8(0xFF54, 0x00);
	bus.Write8(0xFF55, 0x01); // 2 blocs
	CHECK_EQ(bus.Read8(0x9000), 0x40);
	CHECK_EQ(bus.Read8(0x901F), 0x5F);
	CHECK_EQ(bus.Read8(0xFF55), 0xFF);
	// HDMA au retour horizontal : un bloc par ligne visible.
	bus.Write8(0xFF53, 0x18); // 0x9800
	bus.Write8(0xFF55, 0x83); // 4 blocs
	CHECK_EQ(bus.Read8(0xFF55), 0x03);
	machine->RunFrame();
	CHECK_EQ(bus.Read8(0xFF55), 0xFF);
	CHECK_EQ(bus.Read8(0x981F), 0x5F);
	// DMA de l'OAM.
	bus.Write8(0xFF46, 0xC1);
	CHECK_EQ(board->Video().Oam()[5], 0x45);

	// Mode imposé : une cartouche couleur peut tourner en Game Boy d'origine, pas l'inverse.
	auto dmg = Boot(rom, "dmg");
	REQUIRE(dmg != nullptr);
	CHECK(!dmg->Find<gbc::Board>("board")->Color());
	std::vector<uint8_t> only = rom;
	only[0x143] = 0xC0;
	CHECK(Boot(only, "dmg") == nullptr);
	CHECK(Boot(rom, "inconnu") == nullptr);
}

TEST(Gbc_SaveStateDeterminismAndBatteryRam) {
	// Programme : incrémente un compteur 16 bits en RAM de travail et en RAM externe.
	const std::vector<uint8_t> rom = MakeRom(0x03, 1, 2, false,
		{0x3E, 0x0A, 0xEA, 0x00, 0x00, // RAM externe ouverte
		 0x21, 0x00, 0xC0,			   // LD HL,0xC000
		 0x34,						   // boucle : INC (HL)
		 0x20, 0x04,				   // JR NZ,+4
		 0x23, 0x34, 0x2B,			   // INC HL ; INC (HL) ; DEC HL
		 0x00,
		 0x7E, 0xEA, 0x00, 0xA0,	   // LD A,(HL) ; LD (0xA000),A
		 0x18, 0xF3});				   // JR boucle
	const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gbc_test_save";
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	const std::filesystem::path romPath = dir / "compteur.gb";
	{
		auto machine = Boot(rom, "auto", romPath);
		REQUIRE(machine != nullptr);
		auto *board = machine->Find<gbc::Board>("board");
		for (int i = 0; i < 5; ++i)
			machine->RunFrame();
		const std::vector<uint8_t> state = machine->SaveState();
		const uint16_t counter = board->Bus().Read16(0xC000);
		CHECK(counter > 1000);
		// Suite de référence.
		for (int i = 0; i < 7; ++i)
			machine->RunFrame();
		const uint16_t expected = board->Bus().Read16(0xC000);
		const uint16_t pc = board->Cpu().Regs().pc;
		const std::vector<uint32_t> frame(machine->Screens()[0]->Frame().begin(), machine->Screens()[0]->Frame().end());
		// Restauration puis même durée : résultat identique au cycle près.
		CHECK(machine->LoadState(state));
		CHECK_EQ(board->Bus().Read16(0xC000), counter);
		for (int i = 0; i < 7; ++i)
			machine->RunFrame();
		CHECK_EQ(board->Bus().Read16(0xC000), expected);
		CHECK_EQ(board->Cpu().Regs().pc, pc);
		CHECK(std::equal(frame.begin(), frame.end(), machine->Screens()[0]->Frame().begin()));
		// Un état d'une autre cartouche (autre structure de RAM) est refusé.
		auto other = Boot(MakeRom(0x00, 0, 0));
		REQUIRE(other != nullptr);
		CHECK(!other->LoadState(state));
		// La RAM sur pile est écrite à la destruction de la machine.
		CHECK(machine->Find<emu::Nvram>("board:cart:ram")->Dirty());
	}
	CHECK(std::filesystem::exists(dir / "compteur.sav"));
	CHECK_EQ(std::filesystem::file_size(dir / "compteur.sav"), 0x2000u);
	{
		// Au démarrage suivant, la sauvegarde est rechargée avant la première instruction.
		auto machine = Boot(rom, "auto", romPath);
		REQUIRE(machine != nullptr);
		auto *board = machine->Find<gbc::Board>("board");
		board->Bus().Write8(0x0000, 0x0A);
		CHECK(board->Bus().Read8(0xA000) != 0);
	}
	std::filesystem::remove_all(dir);
}

TEST(Gbc_RegistryAndReset) {
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	const emu::SystemInfo *system = registry.FindByExtension(".gbc");
	REQUIRE(system != nullptr);
	CHECK(system->name == "gbc");
	CHECK(registry.FindByExtension("gb") == system);
	emu::Media media;
	CHECK(system->create(media).IsError()); // pas de ROM
	media.rom = MakeRom(0x00, 0, 0, false, {0x3E, 0x77, 0xEA, 0x00, 0xC0, 0x18, 0xFE});
	auto result = system->create(media);
	REQUIRE(result.IsOk());
	emu::Machine &machine = *result.Value();
	machine.RunFrame();
	auto *board = machine.Find<gbc::Board>("board");
	CHECK_EQ(board->Bus().Read8(0xC000), 0x77);
	machine.Reset();
	CHECK_EQ(board->Bus().Read8(0xC000), 0x00);
	CHECK_EQ(board->Cpu().Regs().pc, 0x0100);
	CHECK_EQ(machine.GetScheduler().Now(), 0u);
	machine.RunFrame();
	CHECK_EQ(board->Bus().Read8(0xC000), 0x77);
}

// ROM de test de Blargg (validation de référence du processeur) : exécutées si le dossier
// GBC_TEST_ROMS les contient ; elles écrivent leur verdict sur la liaison série.
TEST(Gbc_BlarggCpuTestRoms) {
	const char *dir = std::getenv("GBC_TEST_ROMS");
	if (!dir) {
		SKIP("GBC_TEST_ROMS non défini");
	}
	for (const auto &[file, frames] : {std::pair{"cpu_instrs.gb", 4000}, std::pair{"instr_timing.gb", 600}}) {
		emu::Media media;
		media.rom = emu::ReadFile(std::filesystem::path(dir) / file);
		if (media.rom.empty())
			continue;
		auto result = gbc::CreateMachine(media);
		REQUIRE(result.IsOk());
		auto *board = result.Value()->Find<gbc::Board>("board");
		for (int i = 0; i < frames && board->SerialOutput().find("Passed") == std::string::npos &&
						board->SerialOutput().find("Failed") == std::string::npos;
			 ++i)
			result.Value()->RunFrame();
		std::printf("    %s : %s\n", file, board->SerialOutput().c_str());
		CHECK(board->SerialOutput().find("Passed") != std::string::npos);
	}
}

TEST(Gbc_DisassemblerAndDebugInterface) {
	CpuBench b{0x00,			 // NOP
			   0x3E, 0x42,		 // LD A,$42
			   0x21, 0x34, 0x12, // LD HL,$1234
			   0x77,			 // LD (HL),A
			   0x20, 0xFE,		 // JR NZ,$0107 (sur elle-même)
			   0xCB, 0x7E,		 // BIT 7,(HL)
			   0xE0, 0x47,		 // LDH ($FF47),A
			   0xCD, 0x00, 0x02, // CALL $0200
			   0xF8, 0xFE,		 // LD HL,SP-2
			   0xD3,			 // code non défini
			   0xFF};			 // RST $38
	struct Expected {
		const char *mnemonic, *operands;
		uint32_t size;
	};
	const Expected expected[] = {{"NOP", "", 1},		   {"LD", "A,$42", 2},	   {"LD", "HL,$1234", 3}, {"LD", "(HL),A", 1},
								 {"JR", "NZ,$0107", 2},	   {"BIT", "7,(HL)", 2},   {"LDH", "($FF47),A", 2}, {"CALL", "$0200", 3},
								 {"LD", "HL,SP-2", 2},	   {"DB", "$D3", 1},	   {"RST", "$38", 1}};
	uint32_t address = 0x0100;
	for (const Expected &e : expected) {
		const emu::DisassembledLine line = b.cpu->Disassemble(address);
		CHECK_EQ(line.address, address);
		CHECK(line.mnemonic == e.mnemonic);
		CHECK(line.operands == e.operands);
		CHECK_EQ(line.size, e.size);
		address += line.size;
	}
	// Interface générique : le processeur est le débogueur de la machine, pas à pas par tranche.
	emu::Debuggable *debug = b.machine.Debugger();
	REQUIRE(debug != nullptr);
	CHECK_EQ(debug->ProgramCounter(), 0x0100u);
	b.machine.StepInstruction(); // NOP
	CHECK_EQ(debug->ProgramCounter(), 0x0101u);
	b.machine.StepInstruction(); // LD A,$42
	CHECK_EQ(debug->ProgramCounter(), 0x0103u);
	const auto regs = debug->DebugRegisters();
	CHECK(regs[0].name == "PC" && regs[0].value == 0x0103 && regs[0].bits == 16);
	CHECK(regs[2].name == "A" && regs[2].value == 0x42);
	CHECK(debug->FlagsText() == "----");
	CHECK_EQ(debug->ProgramSpace().Read8(0x0101), 0x3E);
}
