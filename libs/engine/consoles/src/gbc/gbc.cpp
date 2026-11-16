#include "consoles/gbc/gbc.hpp"

#include <algorithm>

#include <emu/machine.hpp>
#include <emu/state.hpp>

namespace gbc {

// ── Timer ───────────────────────────────────────────────────────────────────

namespace {
/// Période de TIMA en cycles selon TAC (4096, 262144, 65536, 16384 Hz).
constexpr uint32_t kTimaPeriods[4] = {1024, 16, 64, 256};
constexpr emu::Time kSerialTicks = 8 * 512 * 2; // 8 bits à 8192 Hz
} // namespace

Timer::Timer(emu::Machine &machine, std::string tag, emu::Device *owner, Lr35902 &cpu) : Device(machine, std::move(tag), owner), m_cpu(cpu) {}

void Timer::Start() {
	m_timer = GetMachine().GetScheduler().AllocTimer("tima", [this] { OnOverflowTick(); });
}

void Timer::Reset() {
	m_ticksPerCycle = 2;
	m_counterBase = 0xABCC; // valeur laissée par le programme de démarrage
	m_baseTime = 0;
	m_tima = m_tma = 0;
	m_tac = 0;
}

void Timer::State(emu::StateArchive &archive) {
	archive.Io(m_ticksPerCycle);
	archive.Io(m_counterBase);
	archive.Io(m_baseTime);
	archive.Io(m_tima);
	archive.Io(m_tma);
	archive.Io(m_tac);
}

uint16_t Timer::Counter() const {
	const emu::Time now = GetMachine().GetScheduler().Now();
	return uint16_t(m_counterBase + (now - m_baseTime) / m_ticksPerCycle);
}

void Timer::Sync() {
	const emu::Time now = GetMachine().GetScheduler().Now();
	const emu::Time cycles = (now - m_baseTime) / m_ticksPerCycle;
	m_counterBase = uint16_t(m_counterBase + cycles);
	m_baseTime += cycles * m_ticksPerCycle;
}

void Timer::Rearm() {
	emu::Scheduler &scheduler = GetMachine().GetScheduler();
	if (!(m_tac & 4)) {
		scheduler.Cancel(m_timer);
		return;
	}
	// TIMA avance quand le compteur système franchit un multiple de la période.
	const uint32_t period = kTimaPeriods[m_tac & 3];
	const emu::Time now = scheduler.Now();
	const emu::Time phase = (now - m_baseTime) % m_ticksPerCycle;
	const uint32_t toNext = period - (Counter() % period);
	scheduler.Adjust(m_timer, emu::Time(toNext) * m_ticksPerCycle - phase, emu::Time(period) * m_ticksPerCycle);
}

void Timer::OnOverflowTick() {
	if (++m_tima == 0) {
		m_tima = m_tma;
		m_cpu.RequestInterrupt(kIntTimer);
	}
}

void Timer::SetTicksPerCycle(uint32_t ticks) {
	Sync();
	m_ticksPerCycle = ticks ? ticks : 1;
	m_baseTime = GetMachine().GetScheduler().Now();
	Rearm();
}

uint8_t Timer::Read(uint32_t port) const {
	switch (port) {
	case 0xFF04: return uint8_t(Counter() >> 8);
	case 0xFF05: return m_tima;
	case 0xFF06: return m_tma;
	default: return uint8_t(m_tac | 0xF8);
	}
}

void Timer::Write(uint32_t port, uint8_t value) {
	switch (port) {
	case 0xFF04: {
		// Remise à zéro du compteur : si le bit observé par TIMA était à 1, son front
		// descendant fait avancer TIMA (comportement du matériel).
		const uint16_t before = Counter();
		m_counterBase = 0;
		m_baseTime = GetMachine().GetScheduler().Now();
		if ((m_tac & 4) && (before & (kTimaPeriods[m_tac & 3] / 2)))
			OnOverflowTick();
		Rearm();
		break;
	}
	case 0xFF05: m_tima = value; break;
	case 0xFF06: m_tma = value; break;
	default:
		m_tac = value & 7;
		Rearm();
		break;
	}
}

// ── Board ───────────────────────────────────────────────────────────────────

Board::Board(emu::Machine &machine, std::string tag, emu::Device *owner, std::vector<uint8_t> rom, const CartridgeHeader &header, bool color)
	: Device(machine, std::move(tag), owner), m_bus("gbc", 16, 8), m_color(color), m_wram(0x8000, 0) {
	m_cpu = &Add<Lr35902>("cpu", m_bus);
	m_cart = &Add<Cartridge>("cart", std::move(rom), header, m_bus);
	m_screen = &Add<emu::Screen>("screen", kScreenWidth, kScreenHeight);
	m_sound = &Add<emu::SoundStream>("sound", 48000);
	m_ppu = &Add<Ppu>("ppu", m_bus, *m_cpu, *m_screen, color);
	m_apu = &Add<Apu>("apu", *m_sound);
	m_timer = &Add<Timer>("timer", *m_cpu);
	m_pad = &Add<emu::InputPort>("pad");
	m_pad->AddField("right", 0x01, "Droite");
	m_pad->AddField("left", 0x02, "Gauche");
	m_pad->AddField("up", 0x04, "Haut");
	m_pad->AddField("down", 0x08, "Bas");
	m_pad->AddField("a", 0x10, "A");
	m_pad->AddField("b", 0x20, "B");
	m_pad->AddField("select", 0x40, "Select");
	m_pad->AddField("start", 0x80, "Start");
}

void Board::Start() {
	m_serialTimer = GetMachine().GetScheduler().AllocTimer("serial", [this] {
		m_serialData = 0xFF; // personne à l'autre bout du câble
		m_serialControl = uint8_t(m_serialControl & ~0x80);
		m_cpu->RequestInterrupt(kIntSerial);
	});
	m_cpu->OnStop([this] { ToggleSpeed(); });
	m_ppu->OnHBlank([this] {
		if (m_hdmaActive)
			HdmaBlock();
	});
	m_ppu->OnFrame([this] {
		// La RAM sur pile modifiée est enregistrée environ toutes les deux secondes.
		if (++m_framesSinceFlush >= 120) {
			m_framesSinceFlush = 0;
			if (emu::Nvram *ram = m_cart->Ram())
				(void)ram->Flush();
		}
	});
	m_pad->OnChange([this](uint32_t before, uint32_t after) {
		if (after & ~before) // un bouton vient d'être pressé
			m_cpu->RequestInterrupt(kIntJoypad);
	});
	// Projection fixe : registres d'E/S, petite RAM haute, registre IE. Le reste (ROM, RAM
	// externe, VRAM, OAM, RAM de travail) est projeté par chaque périphérique à son Reset.
	m_bus.MapHandlers(0xFF00, 0xFF7F, [this](uint32_t address) { return ReadIo(address); },
					  [this](uint32_t address, uint8_t value) { WriteIo(address, value); });
	m_bus.MapRam(0xFF80, 0xFFFE, m_hram);
	m_bus.MapHandlers(0xFFFF, 0xFFFF, [this](uint32_t) { return m_cpu->ReadIe(); },
					  [this](uint32_t, uint8_t value) { m_cpu->WriteIe(value); });
}

void Board::Reset() {
	std::ranges::fill(m_wram, uint8_t(0));
	m_hram.fill(0);
	m_svbk = 1;
	m_joypadSelect = 0x30;
	m_serialData = m_serialControl = 0;
	m_dma = 0xFF;
	m_doubleSpeed = m_speedArmed = false;
	m_hdmaSource[0] = m_hdmaSource[1] = m_hdmaTarget[0] = m_hdmaTarget[1] = 0xFF;
	m_hdmaRemaining = 0xFF;
	m_hdmaActive = false;
	m_hdmaNextSource = m_hdmaNextTarget = 0;
	m_framesSinceFlush = 0;
	m_serialOutput.clear();
	MapWorkRam();
	// Démarrage direct : registres laissés par le programme de démarrage de la console.
	// (Le Reset du processeur passe après celui de la carte : il les reçoit en configuration.)
	Lr35902::Registers regs;
	if (m_color) {
		regs.a = 0x11;
		regs.f = 0x80;
		regs.d = 0xFF;
		regs.e = 0x56;
		regs.l = 0x0D;
	} else {
		regs.a = 0x01;
		regs.f = 0xB0;
		regs.c = 0x13;
		regs.e = 0xD8;
		regs.h = 0x01;
		regs.l = 0x4D;
	}
	regs.sp = 0xFFFE;
	regs.pc = 0x0100;
	m_cpu->SetResetState(regs);
	m_cpu->SetClockDivider(2);
}

void Board::State(emu::StateArchive &archive) {
	archive.Io(std::span<uint8_t>(m_wram));
	archive.Io(m_hram);
	archive.Io(m_svbk);
	archive.Io(m_joypadSelect);
	archive.Io(m_serialData);
	archive.Io(m_serialControl);
	archive.Io(m_dma);
	archive.Io(m_doubleSpeed);
	archive.Io(m_speedArmed);
	archive.Io(m_hdmaSource);
	archive.Io(m_hdmaTarget);
	archive.Io(m_hdmaRemaining);
	archive.Io(m_hdmaActive);
	archive.Io(m_hdmaNextSource);
	archive.Io(m_hdmaNextTarget);
	if (!archive.Saving())
		MapWorkRam();
}

void Board::MapWorkRam() {
	const std::span<uint8_t> ram(m_wram);
	const std::span<uint8_t> low = ram.subspan(0, 0x1000), high = ram.subspan(size_t(m_svbk) * 0x1000, 0x1000);
	m_bus.MapRam(0xC000, 0xCFFF, low);
	m_bus.MapRam(0xD000, 0xDFFF, high);
	// Écho de la RAM de travail (0xE000-0xFDFF).
	m_bus.MapRam(0xE000, 0xEFFF, low);
	m_bus.MapRam(0xF000, 0xFDFF, high.subspan(0, 0xE00));
}

void Board::ToggleSpeed() {
	if (!m_color || !m_speedArmed)
		return;
	m_speedArmed = false;
	m_doubleSpeed = !m_doubleSpeed;
	const uint32_t ticks = m_doubleSpeed ? 1 : 2;
	m_cpu->SetClockDivider(ticks);
	m_timer->SetTicksPerCycle(ticks);
}

void Board::OamDma(uint8_t page) {
	m_dma = page;
	const uint16_t source = uint16_t(page << 8);
	const std::span<uint8_t> oam = m_ppu->Oam();
	for (size_t i = 0; i < oam.size(); ++i)
		oam[i] = m_bus.Read8(uint32_t(source + i));
}

void Board::StartHdma(uint8_t value) {
	const bool hblank = (value & 0x80) != 0;
	if (m_hdmaActive && !hblank) { // écriture avec le bit 7 à 0 : annule le transfert en cours
		m_hdmaActive = false;
		m_hdmaRemaining = uint8_t(0x80 | m_hdmaRemaining);
		return;
	}
	m_hdmaNextSource = uint16_t(((m_hdmaSource[0] << 8) | m_hdmaSource[1]) & 0xFFF0);
	m_hdmaNextTarget = uint16_t((((m_hdmaTarget[0] << 8) | m_hdmaTarget[1]) & 0x1FF0) | 0x8000);
	m_hdmaRemaining = value & 0x7F;
	if (hblank) {
		m_hdmaActive = true; // un bloc de 16 octets à chaque retour horizontal
		return;
	}
	const unsigned blocks = unsigned(m_hdmaRemaining) + 1;
	for (unsigned i = 0; i < blocks; ++i)
		HdmaBlock();
}

void Board::HdmaBlock() {
	for (int i = 0; i < 0x10; ++i) {
		m_bus.Write8(m_hdmaNextTarget, m_bus.Read8(m_hdmaNextSource));
		++m_hdmaNextSource;
		m_hdmaNextTarget = uint16_t(0x8000 | ((m_hdmaNextTarget + 1) & 0x1FFF));
	}
	if (m_hdmaRemaining-- == 0) { // dernier bloc : 0xFF = terminé
		m_hdmaRemaining = 0xFF;
		m_hdmaActive = false;
	}
}

uint8_t Board::ReadIo(uint32_t address) {
	switch (address) {
	case 0xFF00: {
		const uint32_t state = m_pad->Read();
		uint8_t low = 0x0F;
		if (!(m_joypadSelect & 0x10))
			low = uint8_t(low & ~(state & 0x0F)); // directions
		if (!(m_joypadSelect & 0x20))
			low = uint8_t(low & ~((state >> 4) & 0x0F)); // boutons
		return uint8_t(0xC0 | m_joypadSelect | low);
	}
	case 0xFF01: return m_serialData;
	case 0xFF02: return uint8_t(m_serialControl | 0x7E);
	case 0xFF04:
	case 0xFF05:
	case 0xFF06:
	case 0xFF07: return m_timer->Read(address);
	case 0xFF0F: return m_cpu->ReadIf();
	case 0xFF46: return m_dma;
	case 0xFF4D: return m_color ? uint8_t((m_doubleSpeed ? 0x80 : 0) | (m_speedArmed ? 1 : 0) | 0x7E) : 0xFF;
	case 0xFF55: return m_color ? m_hdmaRemaining : 0xFF;
	case 0xFF70: return m_color ? uint8_t(m_svbk | 0xF8) : 0xFF;
	default: break;
	}
	if ((address >= 0xFF10 && address <= 0xFF26) || (address >= 0xFF30 && address <= 0xFF3F))
		return m_apu->Read(address);
	if ((address >= 0xFF40 && address <= 0xFF4B) || address == 0xFF4F || (address >= 0xFF68 && address <= 0xFF6B))
		return m_ppu->Read(address);
	return 0xFF;
}

void Board::WriteIo(uint32_t address, uint8_t value) {
	switch (address) {
	case 0xFF00: m_joypadSelect = value & 0x30; return;
	case 0xFF01: m_serialData = value; return;
	case 0xFF02:
		m_serialControl = value;
		if ((value & 0x81) == 0x81) { // transfert lancé sur l'horloge interne
			if (m_serialOutput.size() < 65536)
				m_serialOutput.push_back(char(m_serialData));
			GetMachine().GetScheduler().Adjust(m_serialTimer, kSerialTicks);
		}
		return;
	case 0xFF04:
	case 0xFF05:
	case 0xFF06:
	case 0xFF07: m_timer->Write(address, value); return;
	case 0xFF0F: m_cpu->WriteIf(value); return;
	case 0xFF46: OamDma(value); return;
	case 0xFF4D:
		if (m_color)
			m_speedArmed = (value & 1) != 0;
		return;
	case 0xFF51:
	case 0xFF52:
		if (m_color)
			m_hdmaSource[address - 0xFF51] = value;
		return;
	case 0xFF53:
	case 0xFF54:
		if (m_color)
			m_hdmaTarget[address - 0xFF53] = value;
		return;
	case 0xFF55:
		if (m_color)
			StartHdma(value);
		return;
	case 0xFF70:
		if (m_color) {
			m_svbk = (value & 7) ? (value & 7) : 1;
			MapWorkRam();
		}
		return;
	default: break;
	}
	if ((address >= 0xFF10 && address <= 0xFF26) || (address >= 0xFF30 && address <= 0xFF3F))
		m_apu->Write(address, value);
	else if ((address >= 0xFF40 && address <= 0xFF4B) || address == 0xFF4F || (address >= 0xFF68 && address <= 0xFF6B))
		m_ppu->Write(address, value);
}

// ── Fabrique ────────────────────────────────────────────────────────────────

emu::MachineResult CreateMachine(const emu::Media &media) {
	const CartridgeHeader header = ParseHeader(media.rom);
	if (!header.valid)
		return Err(std::string("cartouche Game Boy invalide : ") + header.error);
	const std::string_view model = media.Option("model", "auto");
	if (model != "auto" && model != "dmg" && model != "cgb")
		return Err(std::string("option « model » inconnue (auto, dmg, cgb)"));
	if (model == "dmg" && header.cgbOnly)
		return Err(std::string("cette cartouche exige une Game Boy Color"));
	const bool color = model == "cgb" || (model == "auto" && header.cgb);

	auto machine = std::make_unique<emu::Machine>("gbc", kMasterClock);
	machine->SetFrameTicks(emu::Time(kFrameDots) * kTicksPerDot);
	Board &board = machine->Root().Add<Board>("board", media.rom, header, color);
	machine->Start();
	if (emu::Nvram *ram = board.Cart().Ram(); ram && header.battery && !media.romPath.empty())
		(void)ram->SetPath(media.SavePath(".sav"));
	return Ok(std::move(machine));
}

emu::SystemInfo System() {
	emu::SystemInfo info;
	info.name = "gbc";
	info.description = "Game Boy / Game Boy Color";
	info.manufacturer = "Nintendo";
	info.year = 1998;
	info.extensions = {".gb", ".gbc"};
	info.create = &CreateMachine;
	return info;
}

} // namespace gbc
