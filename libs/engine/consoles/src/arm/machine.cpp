// Construction des machines Game Boy Advance et Nintendo DS (carte ARM et cœur du module).
#include <filesystem>

#include <emu/machine.hpp>

#include "arm/machine.hpp"
#include "consoles/arm/core.hpp"
#include "consoles/gba/gba.hpp"
#include "consoles/nds/nds.hpp"

namespace consoles::arm {

std::string FindFirmware(const emu::Media &media, std::string_view subdirectory, std::string_view name) {
	std::vector<std::filesystem::path> roots;
	if (!media.biosDirectory.empty())
		roots.push_back(media.biosDirectory);
	roots.emplace_back("assets/bios-firmware");
	std::error_code ec;
	for (const std::filesystem::path &root : roots)
		for (const std::filesystem::path &candidate : {root / name, root / subdirectory / name})
			if (std::filesystem::is_regular_file(candidate, ec))
				return candidate.string();
	return {};
}

std::string HeaderText(std::span<const uint8_t> rom, size_t offset, size_t length) {
	std::string out;
	for (size_t i = 0; i < length && offset + i < rom.size(); ++i) {
		const char c = char(rom[offset + i]);
		if (c == 0)
			break;
		out.push_back(c >= 0x20 && c < 0x7F ? c : '?');
	}
	return out;
}

namespace {
std::string CoreErrorText(CoreError error) { return Core::ErrorText(error); }
} // namespace

emu::MachineResult BuildMachine(const emu::Media &media, Mode mode, Settings settings) {
	const bool gba = mode == Mode::Gba;
	const RomType type = gba ? ROM_GBA : ROM_NDS;
	// Image de départ : celle fournie, sinon relue depuis le chemin (archive ZIP comprise).
	static const std::vector<std::string> kGbaExt = {".gba", ".agb", ".bin"}, kNdsExt = {".nds", ".srl"};
	const std::vector<std::string> &extensions = gba ? kGbaExt : kNdsExt;
	std::vector<uint8_t> image = media.rom.empty() ? emu::ReadRomImage(media.romPath, extensions) : media.rom;
	if (image.empty() && !media.romPath.empty())
		return Err(std::string("ROM introuvable, vide ou archive sans ROM ") + (gba ? "GBA" : "NDS") + " : " + media.romPath.filename().string());
	if (image.empty())
		return Err(std::string("ROM introuvable ou vide"));
	// Sans chemin, la réinitialisation repart d'une copie gardée en mémoire.
	std::shared_ptr<const std::vector<uint8_t>> copy;
	if (media.romPath.empty())
		copy = std::make_shared<const std::vector<uint8_t>>(image);
	auto first = Core::Create(type, std::move(image), settings);
	if (first.IsError())
		return Err(CoreErrorText(first.Error()));
	CoreFactory factory = [type, settings, path = media.romPath, copy, extensions]() -> Result<std::unique_ptr<Core>, std::string> {
		std::vector<uint8_t> rom = copy ? *copy : emu::ReadRomImage(path, extensions);
		if (rom.empty())
			return Err(std::string("ROM introuvable ou vide"));
		auto core = Core::Create(type, std::move(rom), settings);
		if (core.IsError())
			return Err(CoreErrorText(core.Error()));
		return Ok(std::move(core.Value()));
	};
	auto machine = std::make_unique<emu::Machine>(gba ? "gba" : "nds", gba ? gba::kMasterClock : nds::kMasterClock);
	const emu::Time frame = gba ? gba::kFrameTicks : nds::kFrameTicks;
	machine->SetFrameTicks(frame);
	machine->Root().Add<Board>("board", mode, std::move(first.Value()), std::move(factory), frame);
	machine->Start();
	return Ok(std::move(machine));
}

Settings BaseSettings(const emu::Media &media) {
	Settings settings;
	settings.directBoot = media.Option("direct-boot", "1") != "0";
	if (!media.romPath.empty()) {
		std::filesystem::path base = media.SavePath(".sav");
		base.replace_extension();
		settings.saveBasePath = base.string();
		std::error_code ec;
		if (base.has_parent_path())
			std::filesystem::create_directories(base.parent_path(), ec);
	}
	return settings;
}

} // namespace consoles::arm
