#include "consoles/gbc/cartridge.hpp"

#include <algorithm>
#include <bit>

#include <emu/machine.hpp>
#include <emu/state.hpp>

namespace gbc {

const char *MbcName(Mbc mbc) noexcept {
	switch (mbc) {
	case Mbc::None: return "ROM seule";
	case Mbc::Mbc1: return "MBC1";
	case Mbc::Mbc2: return "MBC2";
	case Mbc::Mbc3: return "MBC3";
	case Mbc::Mbc5: return "MBC5";
	}
	return "?";
}

CartridgeHeader ParseHeader(std::span<const uint8_t> rom) {
	CartridgeHeader h;
	if (rom.size() < 0x150) {
		h.error = "image trop petite pour contenir un en-tête de cartouche";
		return h;
	}
	const uint8_t cgbFlag = rom[0x143];
	h.cgb = (cgbFlag & 0x80) != 0;
	h.cgbOnly = cgbFlag == 0xC0;
	// Titre : 16 octets (11 si la cartouche est en couleur : le reste est le code du fabricant).
	const size_t titleLength = h.cgb ? 11 : 16;
	for (size_t i = 0; i < titleLength; ++i) {
		const char c = char(rom[0x134 + i]);
		if (c == 0)
			break;
		h.title.push_back(c >= 0x20 && c < 0x7F ? c : '?');
	}
	uint8_t sum = 0;
	for (size_t i = 0x134; i <= 0x14C; ++i)
		sum = uint8_t(sum - rom[i] - 1);
	h.checksumOk = sum == rom[0x14D];

	h.type = rom[0x147];
	switch (h.type) {
	case 0x00: h.mbc = Mbc::None; break;
	case 0x01:
	case 0x02: h.mbc = Mbc::Mbc1; break;
	case 0x03:
		h.mbc = Mbc::Mbc1;
		h.battery = true;
		break;
	case 0x05: h.mbc = Mbc::Mbc2; break;
	case 0x06:
		h.mbc = Mbc::Mbc2;
		h.battery = true;
		break;
	case 0x08: h.mbc = Mbc::None; break;
	case 0x09:
		h.mbc = Mbc::None;
		h.battery = true;
		break;
	case 0x0F:
	case 0x10:
		h.mbc = Mbc::Mbc3;
		h.battery = h.rtc = true;
		break;
	case 0x11:
	case 0x12: h.mbc = Mbc::Mbc3; break;
	case 0x13:
		h.mbc = Mbc::Mbc3;
		h.battery = true;
		break;
	case 0x19:
	case 0x1A: h.mbc = Mbc::Mbc5; break;
	case 0x1B:
		h.mbc = Mbc::Mbc5;
		h.battery = true;
		break;
	case 0x1C:
	case 0x1D:
		h.mbc = Mbc::Mbc5;
		h.rumble = true;
		break;
	case 0x1E:
		h.mbc = Mbc::Mbc5;
		h.rumble = h.battery = true;
		break;
	default:
		h.error = "type de cartouche non pris en charge";
		return h;
	}
	if (rom[0x148] > 8) {
		h.error = "taille de ROM invalide dans l'en-tête";
		return h;
	}
	h.romSize = size_t(0x8000) << rom[0x148];
	static constexpr size_t kRamSizes[] = {0, 0x800, 0x2000, 0x8000, 0x20000, 0x10000};
	h.ramSize = rom[0x149] < 6 ? kRamSizes[rom[0x149]] : 0;
	if (h.mbc == Mbc::Mbc2)
		h.ramSize = 512; // 512 quartets intégrés au contrôleur
	h.valid = true;
	return h;
}

Cartridge::Cartridge(emu::Machine &machine, std::string tag, emu::Device *owner, std::vector<uint8_t> rom, const CartridgeHeader &header,
					 emu::AddressSpace &bus)
	: Device(machine, std::move(tag), owner), m_bus(bus), m_rom(std::move(rom)), m_header(header) {
	// La ROM est complétée à une puissance de deux (≥ 32 Ko) : le numéro de banque se masque.
	const size_t size = std::bit_ceil(std::max<size_t>(m_rom.size(), 0x8000));
	m_rom.resize(size, 0xFF);
	m_romBanks = unsigned(size / 0x4000);
	if (m_header.ramSize > 0)
		m_ram = &Add<emu::Nvram>("ram", m_header.ramSize, uint8_t(0x00));
}

void Cartridge::Reset() {
	m_ramEnabled = false;
	m_bankLow = 1;
	m_bankHigh = 0;
	m_mode = 0;
	m_ramSelect = 0;
	m_rtcBaseSeconds = 0;
	m_rtcBaseTime = 0;
	m_rtcLatchState = 0xFF;
	m_rtcHalted = false;
	std::ranges::fill(m_rtcLatch, uint8_t(0));
	m_bus.MapWrite(0x0000, 0x7FFF, [this](uint32_t address, uint8_t value) { WriteControl(address, value); });
	Remap();
}

void Cartridge::State(emu::StateArchive &archive) {
	archive.Io(m_ramEnabled);
	archive.Io(m_bankLow);
	archive.Io(m_bankHigh);
	archive.Io(m_mode);
	archive.Io(m_ramSelect);
	archive.Io(m_rtcBaseSeconds);
	archive.Io(m_rtcBaseTime);
	archive.Io(m_rtcLatch);
	archive.Io(m_rtcLatchState);
	archive.Io(m_rtcHalted);
	if (!archive.Saving())
		Remap();
}

void Cartridge::WriteControl(uint32_t address, uint8_t value) {
	switch (m_header.mbc) {
	case Mbc::None: return;
	case Mbc::Mbc1:
		if (address < 0x2000)
			m_ramEnabled = (value & 0x0F) == 0x0A;
		else if (address < 0x4000)
			m_bankLow = (value & 0x1F) ? (value & 0x1F) : 1;
		else if (address < 0x6000)
			m_bankHigh = value & 3;
		else
			m_mode = value & 1;
		break;
	case Mbc::Mbc2:
		if (address >= 0x4000)
			return;
		if (address & 0x100)
			m_bankLow = (value & 0x0F) ? (value & 0x0F) : 1;
		else
			m_ramEnabled = (value & 0x0F) == 0x0A;
		break;
	case Mbc::Mbc3:
		if (address < 0x2000)
			m_ramEnabled = (value & 0x0F) == 0x0A;
		else if (address < 0x4000)
			m_bankLow = (value & 0x7F) ? (value & 0x7F) : 1;
		else if (address < 0x6000)
			m_ramSelect = value & 0x0F;
		else {
			if (m_rtcLatchState == 0 && value == 1)
				LatchRtc();
			m_rtcLatchState = value;
		}
		break;
	case Mbc::Mbc5:
		if (address < 0x2000)
			m_ramEnabled = (value & 0x0F) == 0x0A;
		else if (address < 0x3000)
			m_bankLow = value;
		else if (address < 0x4000)
			m_bankHigh = value & 1;
		else if (address < 0x6000)
			m_ramSelect = value & 0x0F;
		break;
	}
	Remap();
}

void Cartridge::Remap() {
	m_romBank0 = 0;
	m_ramBank = 0;
	switch (m_header.mbc) {
	case Mbc::None: m_romBank = 1; break;
	case Mbc::Mbc1:
		m_romBank = unsigned(m_bankHigh << 5) | m_bankLow;
		if (m_mode) {
			m_romBank0 = unsigned(m_bankHigh << 5);
			m_ramBank = m_bankHigh;
		}
		break;
	case Mbc::Mbc2: m_romBank = m_bankLow; break;
	case Mbc::Mbc3:
		m_romBank = m_bankLow;
		m_ramBank = m_ramSelect & 3;
		break;
	case Mbc::Mbc5:
		m_romBank = unsigned(m_bankHigh << 8) | m_bankLow;
		m_ramBank = m_ramSelect & (m_header.rumble ? 0x07 : 0x0F);
		break;
	}
	m_romBank0 &= m_romBanks - 1;
	m_romBank &= m_romBanks - 1;
	const std::span<const uint8_t> rom(m_rom);
	m_bus.MapRom(0x0000, 0x3FFF, rom.subspan(size_t(m_romBank0) * 0x4000, 0x4000));
	m_bus.MapRom(0x4000, 0x7FFF, rom.subspan(size_t(m_romBank) * 0x4000, 0x4000));

	// RAM externe : retirée du bus quand elle est désactivée ou absente.
	m_bus.Unmap(0xA000, 0xBFFF);
	if (!m_ramEnabled)
		return;
	if (m_header.mbc == Mbc::Mbc3 && m_ramSelect >= 0x08) { // registres de l'horloge
		if (!m_header.rtc)
			return;
		m_bus.MapRead(0xA000, 0xBFFF, [this](uint32_t) { return ReadRtc(); });
		m_bus.MapWrite(0xA000, 0xBFFF, [this](uint32_t, uint8_t value) {
			const unsigned index = m_ramSelect - 0x08u;
			if (index < 5) {
				m_rtcLatch[index] = value;
				// L'écriture règle l'horloge : on repart des registres verrouillés.
				const uint64_t days = m_rtcLatch[3] | (uint64_t(m_rtcLatch[4] & 1) << 8);
				m_rtcBaseSeconds = m_rtcLatch[0] + m_rtcLatch[1] * 60u + m_rtcLatch[2] * 3600u + days * 86400u;
				m_rtcBaseTime = GetMachine().GetScheduler().Now();
				m_rtcHalted = (m_rtcLatch[4] & 0x40) != 0;
			}
		});
		return;
	}
	if (!m_ram)
		return;
	const std::span<uint8_t> ram = m_ram->Data();
	if (m_header.mbc == Mbc::Mbc2) { // 512 quartets répétés, quartet haut lu à 1
		m_bus.MapRead(0xA000, 0xBFFF, [this](uint32_t address) { return uint8_t(m_ram->Data()[address & 0x1FF] | 0xF0); });
		m_bus.MapWrite(0xA000, 0xBFFF, [this](uint32_t address, uint8_t value) {
			m_ram->Data()[address & 0x1FF] = value & 0x0F;
			m_ram->MarkDirty();
		});
		return;
	}
	const size_t bankSize = std::min<size_t>(ram.size(), 0x2000);
	const size_t offset = (size_t(m_ramBank) * 0x2000) % ram.size();
	const std::span<uint8_t> bank = ram.subspan(offset, std::min(bankSize, ram.size() - offset));
	m_bus.MapRom(0xA000, 0xBFFF, bank); // lecture directe ; l'écriture passe par le suivi « modifié »
	m_bus.MapWrite(0xA000, 0xBFFF, [this, bank](uint32_t address, uint8_t value) {
		bank[(address - 0xA000) % bank.size()] = value;
		m_ram->MarkDirty();
	});
}

void Cartridge::LatchRtc() {
	uint64_t seconds = m_rtcBaseSeconds;
	if (!m_rtcHalted) {
		const emu::Machine &machine = GetMachine();
		seconds += (machine.GetScheduler().Now() - m_rtcBaseTime) / machine.MasterClock();
	}
	const uint64_t days = seconds / 86400;
	m_rtcLatch[0] = uint8_t(seconds % 60);
	m_rtcLatch[1] = uint8_t((seconds / 60) % 60);
	m_rtcLatch[2] = uint8_t((seconds / 3600) % 24);
	m_rtcLatch[3] = uint8_t(days);
	m_rtcLatch[4] = uint8_t(((days >> 8) & 1) | (m_rtcHalted ? 0x40 : 0) | (days > 511 ? 0x80 : 0));
}

uint8_t Cartridge::ReadRtc() const {
	const unsigned index = m_ramSelect - 0x08u;
	return index < 5 ? m_rtcLatch[index] : 0xFF;
}

} // namespace gbc
