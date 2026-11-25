#include "retro/session.hpp"

#include <algorithm>
#include <format>

#include <sdl3/render/image.hpp>
#include <sdl3/render/render.hpp>

namespace retro {

Result<std::unique_ptr<Session>, std::string> Session::Open(const emu::SystemRegistry &registry, const SessionOptions &options) {
	auto identified = registry.Identify(options.rom, options.system);
	if (identified.IsError())
		return Err(identified.Error());
	const emu::SystemInfo *system = identified.Value();
	emu::Media media;
	if (!system->readsRomFromPath) { // sinon la fabrique lit elle-même le fichier (ou l'archive)
		auto rom = registry.LoadRom(options.rom, system->name);
		if (rom.IsError())
			return Err(rom.Error());
		media.rom = std::move(rom.Value().data);
	}
	media.romPath = options.rom;
	media.saveDirectory = options.saveDirectory;
	media.biosDirectory = options.biosDirectory;
	if (!options.model.empty())
		media.options["model"] = options.model;
	auto machine = system->create(media);
	if (machine.IsError())
		return Err(machine.Error());
	if (machine.Value()->Screens().empty())
		return Err(std::string("la machine n'a pas d'écran"));
	std::unique_ptr<Session> session(new Session());
	session->m_options = options;
	session->m_system = system;
	session->m_machine = std::move(machine.Value());
	return Ok(std::move(session));
}

bool Session::SetButton(std::string_view id, bool pressed) {
	for (emu::InputPort *port : m_machine->Inputs())
		if (port->SetPressed(id, pressed))
			return true;
	return false;
}

bool Session::SaveScreenshot(const std::filesystem::path &path) {
	emu::Screen &screen = Screen();
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);
	// La surface référence les pixels de l'écran : elle ne vit que le temps de l'écriture.
	auto surface = sdl3::Surface::CreateFromPixels(screen.Width(), screen.Height(), sdl3::PixelFormat::RGBA32, screen.Frame().data(),
												   screen.Width() * 4);
	if (surface.IsError())
		return false;
	const auto saved = sdl3::ImgSavePng(surface.Value(), String(path.string()));
	return saved.IsOk() && saved.Value();
}

std::filesystem::path Session::StatePath(int slot) const {
	return m_options.stateDirectory / std::format("{}.s{}", m_options.rom.stem().string(), slot);
}

bool Session::SaveState(int slot) {
	const std::filesystem::path file = StatePath(slot);
	// La vignette (image de l'instant enregistré) accompagne l'état : `<état>.png`.
	return emu::WriteFile(file, m_machine->SaveState()) && SaveScreenshot(file.string() + ".png");
}

bool Session::LoadState(int slot) {
	const std::vector<uint8_t> data = emu::ReadFile(StatePath(slot));
	return !data.empty() && m_machine->LoadState(data);
}

bool Session::ScreenHasContent() {
	const auto frame = Screen().Frame();
	return std::ranges::any_of(frame, [&](uint32_t pixel) { return pixel != frame.front(); });
}

} // namespace retro
