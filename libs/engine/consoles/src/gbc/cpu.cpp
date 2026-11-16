#include "consoles/gbc/cpu.hpp"

#include <emu/machine.hpp>
#include <emu/state.hpp>

namespace gbc {

Lr35902::Lr35902(emu::Machine &machine, std::string tag, emu::Device *owner, emu::AddressSpace &bus)
	: Device(machine, std::move(tag), owner), m_bus(bus) {}

void Lr35902::Start() { GetMachine().GetScheduler().AddExecutable(*this); }

void Lr35902::Reset() {
	m_r = m_resetState;
	m_if = m_ie = 0;
	m_ime = m_halted = false;
	m_eiDelay = 0;
	m_cycles = 0;
}

void Lr35902::State(emu::StateArchive &archive) {
	archive.Io(m_r);
	archive.Io(m_if);
	archive.Io(m_ie);
	archive.Io(m_ime);
	archive.Io(m_halted);
	archive.Io(m_eiDelay);
}

void Lr35902::Execute() {
	while (mIcount > 0) {
		if (m_halted && !(m_if & m_ie & 0x1F)) {
			mIcount = 0; // arrêté jusqu'à la prochaine interruption : la tranche est consommée
			return;
		}
		mIcount -= Step();
	}
}

void Lr35902::Push(uint16_t value) {
	Write(--m_r.sp, uint8_t(value >> 8));
	Write(--m_r.sp, uint8_t(value));
}

uint16_t Lr35902::Pop() {
	const uint8_t lo = Read(m_r.sp++);
	return uint16_t(lo | (Read(m_r.sp++) << 8));
}

// Registres 8 bits dans l'ordre de l'encodage : B, C, D, E, H, L, (HL), A.
uint8_t Lr35902::GetR(int index) {
	switch (index) {
	case 0: return m_r.b;
	case 1: return m_r.c;
	case 2: return m_r.d;
	case 3: return m_r.e;
	case 4: return m_r.h;
	case 5: return m_r.l;
	case 6: return Read(m_r.Hl());
	default: return m_r.a;
	}
}

void Lr35902::SetR(int index, uint8_t value) {
	switch (index) {
	case 0: m_r.b = value; break;
	case 1: m_r.c = value; break;
	case 2: m_r.d = value; break;
	case 3: m_r.e = value; break;
	case 4: m_r.h = value; break;
	case 5: m_r.l = value; break;
	case 6: Write(m_r.Hl(), value); break;
	default: m_r.a = value; break;
	}
}

// Paires 16 bits : BC, DE, HL, SP.
uint16_t Lr35902::GetRp(int index) const noexcept {
	switch (index) {
	case 0: return m_r.Bc();
	case 1: return m_r.De();
	case 2: return m_r.Hl();
	default: return m_r.sp;
	}
}

void Lr35902::SetRp(int index, uint16_t value) noexcept {
	switch (index) {
	case 0:
		m_r.b = uint8_t(value >> 8);
		m_r.c = uint8_t(value);
		break;
	case 1:
		m_r.d = uint8_t(value >> 8);
		m_r.e = uint8_t(value);
		break;
	case 2:
		m_r.h = uint8_t(value >> 8);
		m_r.l = uint8_t(value);
		break;
	default: m_r.sp = value; break;
	}
}

bool Lr35902::Condition(int index) const noexcept {
	switch (index) {
	case 0: return !(m_r.f & kFlagZ);
	case 1: return (m_r.f & kFlagZ) != 0;
	case 2: return !(m_r.f & kFlagC);
	default: return (m_r.f & kFlagC) != 0;
	}
}

// ADD, ADC, SUB, SBC, AND, XOR, OR, CP sur A.
void Lr35902::Alu(int op, uint8_t value) {
	const uint8_t a = m_r.a;
	const int carry = (m_r.f & kFlagC) ? 1 : 0;
	switch (op) {
	case 0:
	case 1: {
		const int c = op == 1 ? carry : 0;
		const int result = a + value + c;
		SetFlags((result & 0xFF) == 0, false, ((a & 0xF) + (value & 0xF) + c) > 0xF, result > 0xFF);
		m_r.a = uint8_t(result);
		break;
	}
	case 2:
	case 3:
	case 7: {
		const int c = op == 3 ? carry : 0;
		const int result = a - value - c;
		SetFlags((result & 0xFF) == 0, true, ((a & 0xF) - (value & 0xF) - c) < 0, result < 0);
		if (op != 7)
			m_r.a = uint8_t(result);
		break;
	}
	case 4:
		m_r.a = a & value;
		SetFlags(m_r.a == 0, false, true, false);
		break;
	case 5:
		m_r.a = a ^ value;
		SetFlags(m_r.a == 0, false, false, false);
		break;
	default:
		m_r.a = a | value;
		SetFlags(m_r.a == 0, false, false, false);
		break;
	}
}

uint8_t Lr35902::Inc8(uint8_t value) noexcept {
	const uint8_t result = uint8_t(value + 1);
	m_r.f = uint8_t((m_r.f & kFlagC) | (result == 0 ? kFlagZ : 0) | ((value & 0xF) == 0xF ? kFlagH : 0));
	return result;
}

uint8_t Lr35902::Dec8(uint8_t value) noexcept {
	const uint8_t result = uint8_t(value - 1);
	m_r.f = uint8_t((m_r.f & kFlagC) | kFlagN | (result == 0 ? kFlagZ : 0) | ((value & 0xF) == 0 ? kFlagH : 0));
	return result;
}

void Lr35902::AddHl(uint16_t value) noexcept {
	const uint32_t hl = m_r.Hl(), result = hl + value;
	m_r.f = uint8_t((m_r.f & kFlagZ) | (((hl & 0xFFF) + (value & 0xFFF)) > 0xFFF ? kFlagH : 0) | (result > 0xFFFF ? kFlagC : 0));
	SetRp(2, uint16_t(result));
}

uint16_t Lr35902::AddSpOffset(int8_t offset) noexcept {
	const uint16_t sp = m_r.sp;
	const uint8_t value = uint8_t(offset);
	SetFlags(false, false, ((sp & 0xF) + (value & 0xF)) > 0xF, ((sp & 0xFF) + value) > 0xFF);
	return uint16_t(sp + offset);
}

void Lr35902::Daa() noexcept {
	uint8_t a = m_r.a, adjust = 0;
	bool carry = (m_r.f & kFlagC) != 0;
	if (!(m_r.f & kFlagN)) {
		if ((m_r.f & kFlagH) || (a & 0x0F) > 9)
			adjust |= 0x06;
		if (carry || a > 0x99) {
			adjust |= 0x60;
			carry = true;
		}
		a = uint8_t(a + adjust);
	} else {
		if (m_r.f & kFlagH)
			adjust |= 0x06;
		if (carry)
			adjust |= 0x60;
		a = uint8_t(a - adjust);
	}
	m_r.a = a;
	m_r.f = uint8_t((a == 0 ? kFlagZ : 0) | (m_r.f & kFlagN) | (carry ? kFlagC : 0));
}

void Lr35902::ExecuteCb() {
	const uint8_t opcode = Fetch();
	const int reg = opcode & 7, y = (opcode >> 3) & 7;
	uint8_t value = GetR(reg);
	switch (opcode >> 6) {
	case 0: { // rotations et décalages
		const bool carryIn = (m_r.f & kFlagC) != 0;
		bool carry = false;
		switch (y) {
		case 0: // RLC
			carry = (value & 0x80) != 0;
			value = uint8_t((value << 1) | (carry ? 1 : 0));
			break;
		case 1: // RRC
			carry = (value & 1) != 0;
			value = uint8_t((value >> 1) | (carry ? 0x80 : 0));
			break;
		case 2: // RL
			carry = (value & 0x80) != 0;
			value = uint8_t((value << 1) | (carryIn ? 1 : 0));
			break;
		case 3: // RR
			carry = (value & 1) != 0;
			value = uint8_t((value >> 1) | (carryIn ? 0x80 : 0));
			break;
		case 4: // SLA
			carry = (value & 0x80) != 0;
			value = uint8_t(value << 1);
			break;
		case 5: // SRA
			carry = (value & 1) != 0;
			value = uint8_t((value >> 1) | (value & 0x80));
			break;
		case 6: // SWAP
			value = uint8_t((value << 4) | (value >> 4));
			break;
		default: // SRL
			carry = (value & 1) != 0;
			value = uint8_t(value >> 1);
			break;
		}
		SetFlags(value == 0, false, false, carry);
		SetR(reg, value);
		break;
	}
	case 1: // BIT : pas d'écriture
		m_r.f = uint8_t((m_r.f & kFlagC) | kFlagH | ((value & (1 << y)) ? 0 : kFlagZ));
		break;
	case 2: SetR(reg, uint8_t(value & ~(1 << y))); break; // RES
	default: SetR(reg, uint8_t(value | (1 << y))); break; // SET
	}
}

int Lr35902::Step() {
	m_cycles = 0;
	const uint8_t pending = m_if & m_ie & 0x1F;
	if (pending) {
		m_halted = false;
		if (m_ime) { // prise en compte : 5 cycles machine
			int index = 0;
			while (!(pending & (1 << index)))
				++index;
			m_if = uint8_t(m_if & ~(1 << index));
			m_ime = false;
			Idle();
			Idle();
			Push(m_r.pc);
			m_r.pc = uint16_t(0x40 + index * 8);
			Idle();
			return m_cycles;
		}
	}
	if (m_halted) {
		Idle();
		return m_cycles;
	}
	ExecuteOpcode(Fetch());
	if (m_eiDelay && --m_eiDelay == 0)
		m_ime = true;
	return m_cycles;
}

void Lr35902::ExecuteOpcode(uint8_t opcode) {
	const int x = opcode >> 6, y = (opcode >> 3) & 7, z = opcode & 7, p = y >> 1, q = y & 1;
	if (x == 1) {
		if (opcode == 0x76)
			m_halted = true; // HALT
		else
			SetR(y, GetR(z));
		return;
	}
	if (x == 2) {
		Alu(y, GetR(z));
		return;
	}
	if (x == 0) {
		switch (z) {
		case 0:
			if (y == 0) { // NOP
			} else if (y == 1) { // LD (nn),SP
				const uint16_t address = Fetch16();
				Write(address, uint8_t(m_r.sp));
				Write(uint16_t(address + 1), uint8_t(m_r.sp >> 8));
			} else if (y == 2) { // STOP
				(void)Fetch();
				if (m_onStop)
					m_onStop();
			} else { // JR d / JR cc,d
				const int8_t offset = int8_t(Fetch());
				if (y == 3 || Condition(y - 4)) {
					m_r.pc = uint16_t(m_r.pc + offset);
					Idle();
				}
			}
			break;
		case 1:
			if (q == 0)
				SetRp(p, Fetch16()); // LD rp,nn
			else {
				AddHl(GetRp(p)); // ADD HL,rp
				Idle();
			}
			break;
		case 2: {
			const uint16_t address = p == 0 ? m_r.Bc() : p == 1 ? m_r.De() : m_r.Hl();
			if (q == 0)
				Write(address, m_r.a);
			else
				m_r.a = Read(address);
			if (p == 2)
				SetRp(2, uint16_t(address + 1)); // (HL+)
			else if (p == 3)
				SetRp(2, uint16_t(address - 1)); // (HL-)
			break;
		}
		case 3:
			SetRp(p, uint16_t(GetRp(p) + (q == 0 ? 1 : -1))); // INC/DEC rp
			Idle();
			break;
		case 4: SetR(y, Inc8(GetR(y))); break;
		case 5: SetR(y, Dec8(GetR(y))); break;
		case 6: SetR(y, Fetch()); break; // LD r,n
		default:
			switch (y) {
			case 0: { // RLCA
				const bool carry = (m_r.a & 0x80) != 0;
				m_r.a = uint8_t((m_r.a << 1) | (carry ? 1 : 0));
				SetFlags(false, false, false, carry);
				break;
			}
			case 1: { // RRCA
				const bool carry = (m_r.a & 1) != 0;
				m_r.a = uint8_t((m_r.a >> 1) | (carry ? 0x80 : 0));
				SetFlags(false, false, false, carry);
				break;
			}
			case 2: { // RLA
				const bool carry = (m_r.a & 0x80) != 0;
				m_r.a = uint8_t((m_r.a << 1) | ((m_r.f & kFlagC) ? 1 : 0));
				SetFlags(false, false, false, carry);
				break;
			}
			case 3: { // RRA
				const bool carry = (m_r.a & 1) != 0;
				m_r.a = uint8_t((m_r.a >> 1) | ((m_r.f & kFlagC) ? 0x80 : 0));
				SetFlags(false, false, false, carry);
				break;
			}
			case 4: Daa(); break;
			case 5: // CPL
				m_r.a = uint8_t(~m_r.a);
				m_r.f = uint8_t(m_r.f | kFlagN | kFlagH);
				break;
			case 6: m_r.f = uint8_t((m_r.f & kFlagZ) | kFlagC); break; // SCF
			default: m_r.f = uint8_t((m_r.f & kFlagZ) | ((m_r.f & kFlagC) ? 0 : kFlagC)); break; // CCF
			}
			break;
		}
		return;
	}
	// x == 3
	switch (z) {
	case 0:
		if (y < 4) { // RET cc
			Idle();
			if (Condition(y)) {
				m_r.pc = Pop();
				Idle();
			}
		} else if (y == 4) { // LDH (n),A
			Write(uint16_t(0xFF00 | Fetch()), m_r.a);
		} else if (y == 5) { // ADD SP,d
			m_r.sp = AddSpOffset(int8_t(Fetch()));
			Idle();
			Idle();
		} else if (y == 6) { // LDH A,(n)
			m_r.a = Read(uint16_t(0xFF00 | Fetch()));
		} else { // LD HL,SP+d
			SetRp(2, AddSpOffset(int8_t(Fetch())));
			Idle();
		}
		break;
	case 1:
		if (q == 0) { // POP : BC, DE, HL, AF
			const uint16_t value = Pop();
			if (p == 3) {
				m_r.a = uint8_t(value >> 8);
				m_r.f = uint8_t(value & 0xF0);
			} else {
				SetRp(p, value);
			}
		} else if (p == 0) { // RET
			m_r.pc = Pop();
			Idle();
		} else if (p == 1) { // RETI
			m_r.pc = Pop();
			Idle();
			m_ime = true;
		} else if (p == 2) { // JP HL
			m_r.pc = m_r.Hl();
		} else { // LD SP,HL
			m_r.sp = m_r.Hl();
			Idle();
		}
		break;
	case 2:
		if (y < 4) { // JP cc,nn
			const uint16_t address = Fetch16();
			if (Condition(y)) {
				m_r.pc = address;
				Idle();
			}
		} else if (y == 4) {
			Write(uint16_t(0xFF00 | m_r.c), m_r.a); // LD (FF00+C),A
		} else if (y == 5) {
			Write(Fetch16(), m_r.a); // LD (nn),A
		} else if (y == 6) {
			m_r.a = Read(uint16_t(0xFF00 | m_r.c)); // LD A,(FF00+C)
		} else {
			m_r.a = Read(Fetch16()); // LD A,(nn)
		}
		break;
	case 3:
		if (y == 0) { // JP nn
			m_r.pc = Fetch16();
			Idle();
		} else if (y == 1) {
			ExecuteCb();
		} else if (y == 6) { // DI
			m_ime = false;
			m_eiDelay = 0;
		} else if (y == 7) { // EI : effectif après l'instruction suivante
			m_eiDelay = 2;
		}
		break; // codes non définis : sans effet
	case 4:
		if (y < 4) { // CALL cc,nn
			const uint16_t address = Fetch16();
			if (Condition(y)) {
				Idle();
				Push(m_r.pc);
				m_r.pc = address;
			}
		}
		break;
	case 5:
		if (q == 0) { // PUSH : BC, DE, HL, AF
			Idle();
			Push(p == 3 ? uint16_t((m_r.a << 8) | m_r.f) : GetRp(p));
		} else if (p == 0) { // CALL nn
			const uint16_t address = Fetch16();
			Idle();
			Push(m_r.pc);
			m_r.pc = address;
		}
		break;
	case 6: Alu(y, Fetch()); break;
	default: // RST
		Idle();
		Push(m_r.pc);
		m_r.pc = uint16_t(y * 8);
		break;
	}
}

} // namespace gbc
