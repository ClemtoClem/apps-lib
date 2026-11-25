// Machine Nintendo DS : en-tête de cartouche et fabrique (carte ARM en mode NDS).
#include "consoles/nds/nds.hpp"

#include "arm/machine.hpp"

namespace nds {

namespace {
uint16_t Crc16(std::span<const uint8_t> data) {
	static constexpr uint16_t kTable[8] = {0xC0C1, 0xC181, 0xC301, 0xC601, 0xCC01, 0xD801, 0xF001, 0xA001};
	uint32_t crc = 0xFFFF;
	for (const uint8_t byte : data) {
		crc ^= byte;
		for (int j = 0; j < 8; ++j)
			crc = (crc >> 1) ^ ((crc & 1) ? uint32_t(kTable[j]) << (7 - j) : 0u);
	}
	return uint16_t(crc);
}
} // namespace

Header ParseHeader(std::span<const uint8_t> rom) {
	Header h;
	if (rom.size() < 0x200) {
		h.error = "image trop petite pour contenir un en-tête de cartouche Nintendo DS";
		return h;
	}
	h.title = consoles::arm::HeaderText(rom, 0x00, 12);
	h.gameCode = consoles::arm::HeaderText(rom, 0x0C, 4);
	h.checksumOk = Crc16(rom.subspan(0, 0x15E)) == uint16_t(rom[0x15E] | (rom[0x15F] << 8));
	h.valid = true;
	return h;
}

emu::MachineResult CreateMachine(const emu::Media &media) {
	consoles::arm::Settings settings = consoles::arm::BaseSettings(media);
	settings.ndsBios9Path = consoles::arm::FindFirmware(media, "nintendo-ds", "ndsBios9.bin");
	settings.ndsBios7Path = consoles::arm::FindFirmware(media, "nintendo-ds", "ndsBios7.bin");
	settings.firmwarePath = consoles::arm::FindFirmware(media, "nintendo-ds", "firmware.bin");
	settings.threaded3D = media.Option("threads-3d", "0") == "0" ? 0 : 2;
	if (!media.rom.empty()) {
		const Header header = ParseHeader(media.rom);
		if (!header.valid)
			return Err(header.error);
	}
	return consoles::arm::BuildMachine(media, consoles::arm::Mode::Nds, std::move(settings));
}

emu::SystemInfo System() {
	emu::SystemInfo info;
	info.name = "nds";
	info.description = "Nintendo DS";
	info.manufacturer = "Nintendo";
	info.year = 2004;
	info.extensions = {".nds", ".srl"};
	info.create = &CreateMachine;
	info.readsRomFromPath = true;
	return info;
}

} // namespace nds
