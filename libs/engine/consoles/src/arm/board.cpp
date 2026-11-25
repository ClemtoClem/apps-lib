#include "consoles/arm/board.hpp"

#include <format>

#include <emu/machine.hpp>
#include <emu/state.hpp>

#include "consoles/arm/core.hpp"
#include "consoles/arm/disasm.hpp"

namespace consoles::arm {

namespace {
/// Touches du cœur dans l'ordre de ses indices (registre KEYINPUT puis EXTKEYIN).
constexpr const char *kKeys[12][2] = {{"a", "A"},		  {"b", "B"},		{"select", "Select"}, {"start", "Start"},
									  {"right", "Droite"}, {"left", "Gauche"}, {"up", "Haut"},		  {"down", "Bas"},
									  {"r", "R"},		  {"l", "L"},		{"x", "X"},			  {"y", "Y"}};
} // namespace

Board::Board(emu::Machine &machine, std::string tag, emu::Device *owner, Mode mode, std::unique_ptr<Core> core, CoreFactory factory,
			 emu::Time frameTicks)
	: Device(machine, std::move(tag), owner), m_mode(mode), m_core(std::move(core)), m_factory(std::move(factory)),
	  m_frameTicks(frameTicks ? frameTicks : 1), m_debugSpace("arm", 32, 28) {
	const bool gba = mode == Mode::Gba;
	m_screen = &Add<emu::Screen>("screen", gba ? 240 : 256, gba ? 160 : 384);
	// Le son du cœur : une trame tous les 512 cycles à 16,78 MHz (GBA), 1024 à 33,51 MHz (NDS).
	m_sound = &Add<emu::SoundStream>("sound", gba ? 32768 : 32728);
	m_pad = &Add<emu::InputPort>("pad");
	for (int i = 0; i < (gba ? 10 : 12); ++i)
		m_pad->AddField(kKeys[i][0], 1u << i, kKeys[i][1]);
	if (!gba)
		m_touch = &Add<emu::TouchPanel>("touch", 0, 192, 256, 192);
	m_debugSpace.MapRead(0x00000000, 0xFFFFFFFF, [this](uint32_t address) { return DebugRead(address); });
}

Board::~Board() = default;

void Board::Start() {
	GetMachine().GetScheduler().AddExecutable(*this);
	m_pad->OnChange([this](uint32_t before, uint32_t after) {
		if (!m_core)
			return;
		for (int i = 0; i < 12; ++i) {
			const uint32_t bit = 1u << i;
			if ((before ^ after) & bit) {
				if (after & bit)
					m_core->input.PressKey(i);
				else
					m_core->input.ReleaseKey(i);
			}
		}
	});
	if (m_touch)
		m_touch->OnChange([this](bool pressed, int x, int y) {
			if (!m_core)
				return;
			if (pressed) {
				m_core->spi.SetTouch(x, y);
				m_core->input.PressScreen();
			} else {
				m_core->input.ReleaseScreen();
				m_core->spi.ClearTouch();
			}
		});
	Attach();
}

void Board::Attach() {
	if (!m_core)
		return;
	m_core->spu.onSample = [this](int16_t left, int16_t right) { m_sound->Push(left, right); };
}

void Board::Reset() {
	SetClockDivider(1);
	if (m_fresh) { // cœur tout juste construit par la fabrique de la machine
		m_fresh = false;
		return;
	}
	// Remise sous tension : un cœur neuf (le précédent enregistre sa sauvegarde en partant).
	m_core.reset();
	auto core = m_factory ? m_factory() : Result<std::unique_ptr<Core>, std::string>(Err(std::string("pas de fabrique")));
	if (core.IsError()) {
		m_error = core.Error();
		return;
	}
	m_error.clear();
	m_core = std::move(core.Value());
	Attach();
}

void Board::State(emu::StateArchive &archive) {
	// L'état du cœur est sérialisé par son propre format, rangé dans un bloc.
	if (archive.Saving()) {
		StateArchive inner(true);
		if (m_core)
			m_core->IoState(inner);
		archive.IoVector(inner.buffer);
		return;
	}
	std::vector<uint8_t> bytes;
	archive.IoVector(bytes);
	if (archive.Failed() || !m_core || bytes.empty()) {
		archive.Fail();
		return;
	}
	StateArchive inner(std::move(bytes));
	m_core->IoState(inner);
	if (inner.failed)
		archive.Fail();
}

void Board::Execute() {
	while (mIcount > 0) {
		if (!m_core) {
			mIcount = 0;
			return;
		}
		m_core->RunFrame();
		mIcount -= int64_t(m_frameTicks);
		PublishFrame(); // fin d'image : la tranche est écourtée, Execute rend la main
	}
}

void Board::PublishFrame() {
	// Le cœur garde jusqu'à deux images prêtes ; s'il n'en a pas, l'image précédente reste.
	(void)m_core->gpu.GetFrame(m_screen->Pixels().data(), m_mode == Mode::Gba);
	m_screen->EndFrame();
}

CPU *Board::MainCpu() const noexcept {
	if (!m_core)
		return nullptr;
	return &m_core->interpreter[m_mode == Mode::Gba ? 1 : 0];
}

uint8_t Board::DebugRead(uint32_t address) const {
	if (!m_core || (address >> 24) == 0x04) // registres d'E/S : leur lecture a des effets
		return 0;
	return m_core->memory.Read<uint8_t>(m_mode == Mode::Gba, address);
}

uint32_t Board::ProgramCounter() const {
	const CPU *cpu = MainCpu();
	if (!cpu)
		return 0;
	// r15 est en avance de deux instructions (pipeline) : la prochaine exécutée est à r15 − 1 instr.
	const bool thumb = cpu->GetCpsr() & (1u << 5);
	return cpu->GetRegister(15) - (thumb ? 2u : 4u);
}

std::vector<emu::RegisterValue> Board::DebugRegisters() const {
	std::vector<emu::RegisterValue> out;
	const CPU *cpu = MainCpu();
	if (!cpu)
		return out;
	for (int i = 0; i < 13; ++i)
		out.push_back({std::format("R{}", i), cpu->GetRegister(i), 32});
	out.push_back({"SP", cpu->GetRegister(13), 32});
	out.push_back({"LR", cpu->GetRegister(14), 32});
	out.push_back({"PC", ProgramCounter(), 32});
	out.push_back({"CPSR", cpu->GetCpsr(), 32});
	out.push_back({"HALT", cpu->IsHalted() ? 1u : 0u, 1});
	return out;
}

std::string Board::FlagsText() const {
	const CPU *cpu = MainCpu();
	if (!cpu)
		return {};
	const uint32_t cpsr = cpu->GetCpsr();
	const char *mode = "?";
	switch (cpsr & 0x1F) {
	case 0x10: mode = "USR"; break;
	case 0x11: mode = "FIQ"; break;
	case 0x12: mode = "IRQ"; break;
	case 0x13: mode = "SVC"; break;
	case 0x17: mode = "ABT"; break;
	case 0x1B: mode = "UND"; break;
	case 0x1F: mode = "SYS"; break;
	default: break;
	}
	return std::format("{}{}{}{} {}{}{} {}", (cpsr & (1u << 31)) ? 'N' : '-', (cpsr & (1u << 30)) ? 'Z' : '-', (cpsr & (1u << 29)) ? 'C' : '-',
					   (cpsr & (1u << 28)) ? 'V' : '-', (cpsr & (1u << 7)) ? 'I' : '-', (cpsr & (1u << 6)) ? 'F' : '-',
					   (cpsr & (1u << 5)) ? 'T' : '-', mode);
}

emu::DisassembledLine Board::Disassemble(uint32_t address) const {
	const CPU *cpu = MainCpu();
	const bool thumb = cpu && (cpu->GetCpsr() & (1u << 5));
	if (thumb)
		return DisassembleThumb(address & ~1u, m_debugSpace.Read16(address & ~1u), m_debugSpace.Read16((address & ~1u) + 2));
	return DisassembleArm(address & ~3u, m_debugSpace.Read32(address & ~3u));
}

std::vector<std::pair<std::string, uint32_t>> Board::MemoryRegions() const {
	if (m_mode == Mode::Gba)
		return {{"BIOS", 0x00000000}, {"EWRAM", 0x02000000}, {"IWRAM", 0x03000000}, {"Palette", 0x05000000},
				{"VRAM", 0x06000000}, {"OAM", 0x07000000},	 {"ROM", 0x08000000}};
	return {{"ITCM", 0x01000000}, {"RAM", 0x02000000}, {"WRAM", 0x03000000}, {"Palette", 0x05000000}, {"VRAM", 0x06000000}, {"OAM", 0x07000000}};
}

} // namespace consoles::arm
