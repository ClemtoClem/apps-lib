// Machine Game Boy Advance : en-tête de cartouche et fabrique (carte ARM en mode GBA).
#include "consoles/gba/gba.hpp"

#include "arm/machine.hpp"

namespace gba {

Header ParseHeader(std::span<const uint8_t> rom) {
	Header h;
	if (rom.size() < 0xC0) {
		h.error = "image trop petite pour contenir un en-tête de cartouche GBA";
		return h;
	}
	h.title = consoles::arm::HeaderText(rom, 0xA0, 12);
	h.gameCode = consoles::arm::HeaderText(rom, 0xAC, 4);
	uint8_t sum = 0;
	for (size_t i = 0xA0; i <= 0xBC; ++i)
		sum = uint8_t(sum - rom[i]);
	h.checksumOk = uint8_t(sum - 0x19) == rom[0xBD];
	h.valid = true;
	return h;
}

emu::MachineResult CreateMachine(const emu::Media &media) {
	consoles::arm::Settings settings = consoles::arm::BaseSettings(media);
	settings.directBoot = media.Option("direct-boot", "0") != "0"; // introduction du BIOS par défaut
	settings.gbaBiosPath = consoles::arm::FindFirmware(media, "nintendo-game-boy-advance", "gbaBios.bin");
	if (settings.gbaBiosPath.empty())
		return Err(std::string("BIOS de la Game Boy Advance (gbaBios.bin) introuvable : placez-le dans assets/bios-firmware"));
	if (!media.rom.empty()) {
		const Header header = ParseHeader(media.rom);
		if (!header.valid)
			return Err(header.error);
	}
	return consoles::arm::BuildMachine(media, consoles::arm::Mode::Gba, std::move(settings));
}

emu::SystemInfo System() {
	emu::SystemInfo info;
	info.name = "gba";
	info.description = "Game Boy Advance";
	info.manufacturer = "Nintendo";
	info.year = 2001;
	info.extensions = {".gba", ".agb"};
	info.create = &CreateMachine;
	info.readsRomFromPath = true;
	return info;
}

} // namespace gba
