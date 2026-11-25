#pragma once
/**
 * @file cpu.hpp
 * @brief Sharp LR35902 (SM83) : le processeur 8 bits de la Game Boy et de la Game Boy Color.
 *
 * Le processeur ne connaît que son bus (`emu::AddressSpace`) : chaque accès mémoire coûte
 * 4 cycles, les cycles internes sont comptés aux mêmes endroits que sur le matériel
 * (durées d'instruction exactes). Les registres d'interruption IF (0xFF0F) et IE (0xFFFF)
 * lui appartiennent ; la carte les projette sur le bus.
 */
#include <cstdint>
#include <functional>

#include <emu/debug.hpp>
#include <emu/device.hpp>
#include <emu/memory.hpp>
#include <emu/scheduler.hpp>

namespace gbc {

/// Bits des interruptions (IF/IE) ; vecteur = 0x40 + 8 × indice.
enum Interrupt : uint8_t { kIntVBlank = 0x01, kIntStat = 0x02, kIntTimer = 0x04, kIntSerial = 0x08, kIntJoypad = 0x10 };

/// Drapeaux du registre F.
enum Flag : uint8_t { kFlagZ = 0x80, kFlagN = 0x40, kFlagH = 0x20, kFlagC = 0x10 };

/// Désassemble l'instruction LR35902 à `address`.
[[nodiscard]] emu::DisassembledLine Disassemble(const emu::AddressSpace &bus, uint16_t address);

class Lr35902 : public emu::Device, public emu::Executable, public emu::Debuggable {
public:
	struct Registers {
		uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
		uint16_t sp = 0, pc = 0;
		[[nodiscard]] uint16_t Bc() const noexcept { return uint16_t((b << 8) | c); }
		[[nodiscard]] uint16_t De() const noexcept { return uint16_t((d << 8) | e); }
		[[nodiscard]] uint16_t Hl() const noexcept { return uint16_t((h << 8) | l); }
	};

	Lr35902(emu::Machine &machine, std::string tag, emu::Device *owner, emu::AddressSpace &bus);

	void Start() override;
	void Reset() override;
	void State(emu::StateArchive &archive) override;
	void Execute() override;

	/// Exécute une instruction (ou la prise en compte d'une interruption) ; retourne sa durée en cycles.
	int Step();

	/// Registres chargés à chaque `Reset()` (état laissé par le programme de démarrage).
	void SetResetState(const Registers &registers) noexcept { m_resetState = registers; }
	[[nodiscard]] Registers &Regs() noexcept { return m_r; }
	[[nodiscard]] const Registers &Regs() const noexcept { return m_r; }
	[[nodiscard]] bool Halted() const noexcept { return m_halted; }
	[[nodiscard]] bool InterruptsEnabled() const noexcept { return m_ime; }

	void RequestInterrupt(uint8_t bits) noexcept { m_if = uint8_t(m_if | (bits & 0x1F)); }
	[[nodiscard]] uint8_t ReadIf() const noexcept { return uint8_t(m_if | 0xE0); }
	void WriteIf(uint8_t value) noexcept { m_if = value & 0x1F; }
	[[nodiscard]] uint8_t ReadIe() const noexcept { return m_ie; }
	void WriteIe(uint8_t value) noexcept { m_ie = value; }

	// emu::Debuggable
	[[nodiscard]] std::vector<emu::RegisterValue> DebugRegisters() const override;
	[[nodiscard]] uint32_t ProgramCounter() const override { return m_r.pc; }
	[[nodiscard]] emu::DisassembledLine Disassemble(uint32_t address) const override { return gbc::Disassemble(m_bus, uint16_t(address)); }
	[[nodiscard]] const emu::AddressSpace &ProgramSpace() const override { return m_bus; }
	[[nodiscard]] std::string FlagsText() const override;
	[[nodiscard]] std::vector<std::pair<std::string, uint32_t>> MemoryRegions() const override {
		return {{"ROM", 0x0000}, {"VRAM", 0x8000}, {"RAM", 0xC000}, {"E/S", 0xFF00}};
	}

	/// Appelé par l'instruction STOP (changement de vitesse de la Game Boy Color).
	void OnStop(std::function<void()> callback) { m_onStop = std::move(callback); }

private:
	[[nodiscard]] uint8_t Read(uint16_t address) {
		m_cycles += 4;
		return m_bus.Read8(address);
	}
	void Write(uint16_t address, uint8_t value) {
		m_cycles += 4;
		m_bus.Write8(address, value);
	}
	void Idle() noexcept { m_cycles += 4; }
	[[nodiscard]] uint8_t Fetch() { return Read(m_r.pc++); }
	[[nodiscard]] uint16_t Fetch16() {
		const uint8_t lo = Fetch();
		return uint16_t(lo | (Fetch() << 8));
	}
	void Push(uint16_t value);
	[[nodiscard]] uint16_t Pop();
	[[nodiscard]] uint8_t GetR(int index);
	void SetR(int index, uint8_t value);
	[[nodiscard]] uint16_t GetRp(int index) const noexcept;
	void SetRp(int index, uint16_t value) noexcept;
	[[nodiscard]] bool Condition(int index) const noexcept;
	void Alu(int op, uint8_t value);
	[[nodiscard]] uint8_t Inc8(uint8_t value) noexcept;
	[[nodiscard]] uint8_t Dec8(uint8_t value) noexcept;
	void AddHl(uint16_t value) noexcept;
	[[nodiscard]] uint16_t AddSpOffset(int8_t offset) noexcept;
	void Daa() noexcept;
	void ExecuteCb();
	void ExecuteOpcode(uint8_t opcode);
	void SetFlags(bool z, bool n, bool h, bool c) noexcept {
		m_r.f = uint8_t((z ? kFlagZ : 0) | (n ? kFlagN : 0) | (h ? kFlagH : 0) | (c ? kFlagC : 0));
	}

	emu::AddressSpace &m_bus;
	Registers m_r, m_resetState;
	uint8_t m_if = 0, m_ie = 0;
	bool m_ime = false, m_halted = false;
	uint8_t m_eiDelay = 0; ///< EI prend effet après l'instruction suivante
	int m_cycles = 0;
	std::function<void()> m_onStop;
};

} // namespace gbc
