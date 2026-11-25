// retro — logique de l'application fenêtrée : fenêtre, bibliothèque, états, émulation.
#include "retro/app.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <future>

#include <consoles/consoles.hpp>
#include <core/log.hpp>
#include <sdl3/core/events_manager.hpp>
#include <ui/ui.hpp>

namespace fs = std::filesystem;

namespace retro {

namespace {

/// Touches par défaut : un scancode par bouton de console (modifiable dans
/// l'application via le modal de configuration).
[[nodiscard]] std::vector<KeyBinding> DefaultKeys() {
	return {
		{SDL_SCANCODE_UP, "up"},		 {SDL_SCANCODE_DOWN, "down"},		{SDL_SCANCODE_LEFT, "left"},	{SDL_SCANCODE_RIGHT, "right"},
		{SDL_SCANCODE_X, "a"},			 {SDL_SCANCODE_Z, "b"},				{SDL_SCANCODE_RETURN, "start"}, {SDL_SCANCODE_RSHIFT, "select"},
		{SDL_SCANCODE_A, "l"},			 {SDL_SCANCODE_S, "r"},				{SDL_SCANCODE_Q, "x"},			{SDL_SCANCODE_D, "y"}, // Nintendo DS
	};
}

/// Libellé lisible d'un bouton de console.
[[nodiscard]] const char *ButtonLabel(std::string_view button) {
	if (button == "up") return "Haut";
	if (button == "down") return "Bas";
	if (button == "left") return "Gauche";
	if (button == "right") return "Droite";
	if (button == "a") return "A";
	if (button == "b") return "B";
	if (button == "start") return "Start";
	if (button == "select") return "Select";
	if (button == "l") return "L";
	if (button == "r") return "R";
	if (button == "x") return "X";
	if (button == "y") return "Y";
	return "?";
}

struct PadBinding {
	sdl3::GamepadButton pad;
	const char *button;
};
constexpr PadBinding kPad[] = {
	{sdl3::GamepadButton::D_PAD_UP, "up"},		 {sdl3::GamepadButton::D_PAD_DOWN, "down"},
	{sdl3::GamepadButton::D_PAD_LEFT, "left"},	 {sdl3::GamepadButton::D_PAD_RIGHT, "right"},
	{sdl3::GamepadButton::EAST, "a"},			 {sdl3::GamepadButton::SOUTH, "b"},
	{sdl3::GamepadButton::START, "start"},		 {sdl3::GamepadButton::BACK, "select"},
	{sdl3::GamepadButton::LEFT_SHOULDER, "l"},	 {sdl3::GamepadButton::RIGHT_SHOULDER, "r"},
	{sdl3::GamepadButton::NORTH, "x"},			 {sdl3::GamepadButton::WEST, "y"},
};

std::string FormatTime(fs::file_time_type time) {
	const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
	return std::format("{:%d/%m/%Y %H:%M}", std::chrono::floor<std::chrono::minutes>(system));
}

} // namespace

App::App(AppOptions options) : m_options(std::move(options)) {
	consoles::RegisterAll(m_registry);
	m_view = m_options.debugView ? View::Debug : View::Game;
	m_paused = m_options.paused;
	m_keys = DefaultKeys();
	m_options.session.stateDirectory = m_options.dataDirectory / "states";
	if (m_options.session.saveDirectory.empty())
		m_options.session.saveDirectory = m_options.dataDirectory / "saves";
}

App::~App() {
	SaveLibraryMarks();
	m_session.reset(); // la machine enregistre sa RAM sur pile
	m_thumbnails.clear();
	m_tileTexture.reset();
	m_screenTexture.reset();
	m_gamepad.reset();
}

void App::Log(LogLevel level, std::string text) {
	core::log::Info("{}", text);
	m_log.emplace_back(level, std::move(text));
	while (m_log.size() > 300)
		m_log.pop_front();
}

bool App::InitWindow() {
	auto context = sdl3::SdlContext::Create(sdl3::initFlags::VIDEO | sdl3::initFlags::AUDIO | sdl3::initFlags::GAMEPAD |
											sdl3::initFlags::EVENTS);
	if (context.IsError())
		return false;
	m_sdl.emplace(std::move(context.Value()));
	// Fenêtre sans décoration : la barre de titre est dessinée par l'application.
	auto window = sdl3::Window::Create(String("retro"), 1360, 800,
									   sdl3::windowFlags::BORDERLESS | sdl3::windowFlags::RESIZABLE | sdl3::windowFlags::HIGH_PIXEL_DENSITY);
	if (window.IsError())
		return false;
	m_window.emplace(std::move(window.Value()));
	m_window->SetMinimumSize(900, 560);
	auto renderer = sdl3::Renderer::Create(*m_window);
	if (renderer.IsError())
		return false;
	m_renderer.emplace(std::move(renderer.Value()));
	m_renderer->SetVSync(sdl3::Renderer::VSync(1));
	m_scale = m_window->DisplayScale();
	if (m_scale <= 0.f)
		m_scale = 1.f;
	ui::icons::RegisterAll();
	if (!m_canvas.Init(*m_renderer,
					   sdl3::CanvasFontSet{"assets/fonts/DejaVuSans.ttf", "assets/fonts/DejaVuSans-Bold.ttf", "assets/fonts/DejaVuSansMono.ttf",
									"assets/fonts/MaterialIcons-Regular.otf"},
					   m_scale))
		return false;
	m_ui = std::make_unique<ui::Context>(m_canvas, m_events);
	ui::Theme &theme = m_ui->Style();
	theme.background = sdl3::Rgba(15, 19, 30);
	theme.panel = sdl3::Rgba(24, 30, 46, 245);
	theme.panelBorder = sdl3::Rgba(48, 58, 84);
	theme.title = sdl3::Rgba(20, 25, 38);
	theme.accent = sdl3::Rgba(74, 144, 255);
	theme.accentHover = sdl3::Rgba(110, 168, 255);
	theme.accentText = sdl3::Rgba(255, 255, 255);
	theme.widget = sdl3::Rgba(36, 44, 66);
	theme.widgetHover = sdl3::Rgba(48, 58, 86);
	theme.widgetActive = sdl3::Rgba(62, 74, 108);
	// Interface retenue (builder + CSS + affichage automatique) : barre d'état.
	m_retained = std::make_unique<ui::Ui>(m_canvas, m_events);
	m_retained->SetTheme(theme); // mêmes couleurs que l'interface immédiate
	if (auto sheet = ui::StyleSheet::Load("assets/ui.css"); sheet.IsOk())
		m_retained->Sheet(std::move(sheet.Value()));
	m_retained->Root(ui::Row().Class("status").Children({
		ui::Text("").Id("status-left").Class("left"),
		ui::Box().Grow(),
		ui::Text("").Id("status-right").Class("right"),
	}));
	// Commandes du lecteur (curseur de volume lié, boutons de vitesse) : même
	// feuille CSS, affichage automatique dans la zone des commandes.
	m_controls = std::make_unique<ui::Ui>(m_canvas, m_events);
	m_controls->SetTheme(theme); // mêmes couleurs que l'interface immédiate
	if (auto sheet = ui::StyleSheet::Load("assets/ui.css"); sheet.IsOk())
		m_controls->Sheet(std::move(sheet.Value()));
	m_controls->Root(ui::Row().Gap(6).Children({
		ui::Button("Lecture").Id("play").OnClick([this] { SetPaused(false); }),
		ui::Button("Pause").Id("pause").OnClick([this] { SetPaused(true); }),
		ui::Slider(0.f, 1.f).Bind(m_volume).Width(104).Height(30).Class("slider")
			.OnChange([this](float v) { if (m_audio) m_audio->SetGain(v); }),
		ui::Button("×0,5").OnClick([this] { m_speed = 0.5f; }),
		ui::Button("×1").OnClick([this] { m_speed = 1.f; }),
		ui::Button("×2").OnClick([this] { m_speed = 2.f; }),
	}));
	// Modal de configuration des touches (overlay au premier plan, l'arrière-plan
	// est désactivé tant qu'il est ouvert).
	m_modal = std::make_unique<ui::Ui>(m_canvas, m_events);
	m_modal->SetTheme(theme); // mêmes couleurs que l'interface immédiate
	if (auto sheet = ui::StyleSheet::Load("assets/ui.css"); sheet.IsOk())
		m_modal->Sheet(std::move(sheet.Value()));
	BuildKeyConfigModal();
	m_chrome = std::make_unique<ui::WindowChrome>(m_window->Get());
	m_window->StartTextInput();
	// Manettes : ouvrir la première à la connexion, la fermer à la déconnexion.
	(void)m_events.Bind(sdl3::EventType::GAMEPAD_ADDED, [this](const sdl3::Event &ev) {
		if (m_gamepad)
			return;
		if (auto pad = sdl3::Gamepad::Open(ev.GamepadDevice().which); pad.IsOk()) {
			m_gamepad.emplace(std::move(pad.Value()));
			m_gamepadName = m_gamepad->Name();
			Log(LogLevel::Info, std::format("[INFO] manette connectée : {}", m_gamepadName));
		}
	});
	(void)m_events.Bind(sdl3::EventType::GAMEPAD_REMOVED, [this](const sdl3::Event &ev) {
		if (m_gamepad && m_gamepad->GetId() == ev.GamepadDevice().which) {
			m_gamepad.reset();
			m_gamepadName.clear();
		}
	});
	// Déplacement vers un autre écran (échelle d'affichage) : re-résoudre le canvas.
	(void)m_events.Bind(sdl3::EventType::WINDOW_DISPLAY_SCALE_CHANGED, [this](const sdl3::Event &) {
		m_scale = std::max(m_window->DisplayScale(), 0.5f);
		m_canvas.SetScale(m_scale);
	});
	return true;
}

// ── Bibliothèque ────────────────────────────────────────────────────────────

void App::LoadLibraryMarks() {
	std::ifstream in(m_options.dataDirectory / "library.txt");
	std::string line;
	while (std::getline(in, line)) {
		if (line.starts_with("fav\t"))
			m_favorites.insert(line.substr(4));
		else if (line.starts_with("recent\t"))
			m_recent.push_back(line.substr(7));
		else if (line == "layout\tside-by-side")
			m_dualLayout = DualLayout::SideBySide;
	}
}

void App::SaveLibraryMarks() const {
	std::error_code ec;
	fs::create_directories(m_options.dataDirectory, ec);
	std::ofstream out(m_options.dataDirectory / "library.txt", std::ios::trunc);
	for (const std::string &path : m_favorites)
		out << "fav\t" << path << '\n';
	for (const std::string &path : m_recent)
		out << "recent\t" << path << '\n';
	if (m_dualLayout == DualLayout::SideBySide)
		out << "layout\tside-by-side\n";
}

std::vector<RomEntry> App::FindRoms(const emu::SystemRegistry &registry, const std::vector<fs::path> &directories) {
	std::vector<RomEntry> library;
	std::set<std::string> seen;
	for (const fs::path &directory : directories) {
		std::error_code ec;
		for (fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
			if (ec || !it->is_regular_file(ec) || !registry.IsRomFile(it->path()))
				continue;
			const std::string key = fs::absolute(it->path(), ec).lexically_normal().string();
			if (!seen.insert(key).second)
				continue;
			// Archive : le système est celui de la ROM qu'elle contient (les correctifs seuls sont écartés).
			auto identified = registry.Identify(it->path());
			if (identified.IsError())
				continue;
			RomEntry entry;
			entry.path = key;
			entry.title = it->path().stem().string();
			entry.system = identified.Value()->description;
			library.push_back(std::move(entry));
		}
	}
	return library;
}

void App::ScanLibrary() {
	// Ouvrir une centaine d'archives prend quelques secondes : l'analyse se fait sur un autre
	// fil (le registre est en lecture seule), la liste est reprise par PollLibrary().
	m_scan = std::async(std::launch::async, &App::FindRoms, std::cref(m_registry), m_options.libraries);
}

void App::PollLibrary() {
	if (!m_scan.valid() || m_scan.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;
	m_library = m_scan.get();
	SortLibrary();
}

void App::SortLibrary() {
	for (RomEntry &entry : m_library) {
		entry.favorite = m_favorites.contains(entry.path.string());
		const auto it = std::ranges::find(m_recent, entry.path.string());
		entry.recent = it == m_recent.end() ? -1 : int(it - m_recent.begin());
	}
	RefreshThumbnails();
	// Récents d'abord (du plus récent au plus ancien), puis favoris, puis ordre alphabétique.
	std::ranges::sort(m_library, [](const RomEntry &x, const RomEntry &y) {
		const int rx = x.recent < 0 ? 1 << 20 : x.recent, ry = y.recent < 0 ? 1 << 20 : y.recent;
		if (rx != ry)
			return rx < ry;
		if (x.favorite != y.favorite)
			return x.favorite;
		return x.title < y.title;
	});
}

bool App::LoadRom(const fs::path &path) {
	SessionOptions options = m_options.session;
	options.rom = path;
	auto session = Session::Open(m_registry, options);
	if (session.IsError()) {
		Log(LogLevel::Error, std::format("[ERREUR] {}", session.Error()));
		if (m_ui)
			m_ui->Notify(session.Error(), m_ui->Style().danger, 5.f);
		return false;
	}
	m_session = std::move(session.Value());
	emu::Screen &screen = m_session->Screen();
	m_screenW = screen.Width();
	m_screenH = screen.Height();
	m_screenTexture.reset();
	if (m_renderer) {
		if (auto texture = sdl3::Texture::Create(*m_renderer, m_screenW, m_screenH, sdl3::PixelFormat::RGBA32); texture.IsOk()) {
			m_screenTexture.emplace(std::move(texture.Value()));
			m_screenTexture->SetScaleMode(sdl3::ScaleMode::NEAREST);
		}
		// Son : flux SDL à la fréquence de la sortie de la machine.
		m_audio.reset();
		const auto sounds = m_session->Machine().Sounds();
		if (!sounds.empty() && !m_options.mute) {
			auto stream = sdl3::AudioStream::OpenPlayback({sdl3::AudioFormat::S16, 2, sounds.front()->SampleRate()});
			if (stream.IsOk()) {
				m_audio.emplace(std::move(stream.Value()));
				m_audio->SetGain(m_volume);
				m_audio->Resume();
			} else {
				Log(LogLevel::Warn, "[AVERT] son : périphérique indisponible");
			}
		}
	}
	m_breakpoints.clear();
	m_memoryPrevious.clear();
	m_unmappedReads = 0;
	m_frameDebt = 0;
	if (emu::Debuggable *debug = m_session->Machine().Debugger())
		m_disasmAnchor = debug->ProgramCounter();
	// Marque « joué récemment ».
	std::error_code ec;
	const std::string key = fs::absolute(path, ec).lexically_normal().string();
	std::erase(m_recent, key);
	m_recent.insert(m_recent.begin(), key);
	if (m_recent.size() > 30)
		m_recent.resize(30);
	SortLibrary();
	RefreshStates();
	Log(LogLevel::Info, std::format("[INFO] {} — {}", m_session->System().description, path.filename().string()));
	return true;
}

// ── États sauvegardés ───────────────────────────────────────────────────────

void App::RefreshThumbnails() {
	std::error_code ec;
	for (RomEntry &rom : m_library) {
		rom.thumbnail.clear();
		for (int slot = 1; slot <= 3 && rom.thumbnail.empty(); ++slot) {
			const fs::path png = m_options.session.stateDirectory / std::format("{}.s{}.png", rom.title, slot);
			if (fs::is_regular_file(png, ec))
				rom.thumbnail = png;
		}
	}
}

void App::RefreshStates() {
	m_states.clear();
	m_renaming = -1;
	if (!m_session)
		return;
	const std::string prefix = m_session->RomPath().stem().string() + ".s";
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(m_options.session.stateDirectory, ec)) {
		const std::string name = entry.path().filename().string();
		if (!name.starts_with(prefix) || entry.path().extension() == ".png" || entry.path().extension() == ".txt")
			continue;
		const std::string number = name.substr(prefix.size());
		if (number.empty() || !std::ranges::all_of(number, [](char c) { return c >= '0' && c <= '9'; }))
			continue;
		StateEntry state;
		state.file = entry.path();
		state.slot = std::atoi(number.c_str());
		state.time = entry.last_write_time(ec);
		state.date = FormatTime(state.time);
		std::ifstream label(entry.path().string() + ".txt");
		std::getline(label, state.label);
		m_states.push_back(std::move(state));
	}
	std::ranges::sort(m_states, [](const StateEntry &a, const StateEntry &b) { return a.time > b.time; });
	RefreshThumbnails();
}

void App::SaveNewState() {
	if (!m_session)
		return;
	int slot = 1;
	for (const StateEntry &state : m_states)
		slot = std::max(slot, state.slot + 1);
	const fs::path file = m_session->StatePath(slot);
	if (m_session->SaveState(slot)) {
		m_thumbnails.erase(file.string() + ".png");
		Log(LogLevel::Info, std::format("[INFO] état {} enregistré", slot));
		m_ui->Notify(std::format("État {} enregistré", slot), m_ui->Style().success);
	} else {
		Log(LogLevel::Error, "[ERREUR] état non enregistré");
		m_ui->Notify("État non enregistré", m_ui->Style().danger);
	}
	RefreshStates();
}

void App::LoadStateFile(const StateEntry &state) {
	if (!m_session)
		return;
	const std::vector<uint8_t> data = emu::ReadFile(state.file);
	if (!data.empty() && m_session->Machine().LoadState(data)) {
		Log(LogLevel::Info, std::format("[INFO] état {} chargé", state.slot));
		m_ui->Notify(std::format("État {} chargé", state.slot), m_ui->Style().info);
	} else {
		Log(LogLevel::Error, std::format("[ERREUR] état {} illisible ou incompatible", state.slot));
		m_ui->Notify("État illisible ou incompatible", m_ui->Style().danger);
	}
}

void App::QuickLoad() {
	if (!m_states.empty())
		LoadStateFile(m_states.front()); // le plus récent
	else if (m_ui)
		m_ui->Notify("Aucun état enregistré pour cette ROM");
}

void App::DeleteState(size_t index) {
	if (index >= m_states.size())
		return;
	std::error_code ec;
	const std::string file = m_states[index].file.string();
	fs::remove(file, ec);
	fs::remove(file + ".png", ec);
	fs::remove(file + ".txt", ec);
	m_thumbnails.erase(file + ".png");
	RefreshStates();
}

sdl3::Texture *App::Thumbnail(const fs::path &png) {
	std::error_code ec;
	const auto time = fs::last_write_time(png, ec);
	if (ec)
		return nullptr;
	auto &slot = m_thumbnails[png.string()];
	if (!slot.second || slot.first != time) {
		slot.first = time;
		slot.second.reset();
		if (auto texture = sdl3::Texture::CreateFromFile(*m_renderer, String(png.string())); texture.IsOk()) {
			slot.second.emplace(std::move(texture.Value()));
			slot.second->SetScaleMode(sdl3::ScaleMode::NEAREST);
		}
	}
	return slot.second ? &*slot.second : nullptr;
}

// ── Émulation ───────────────────────────────────────────────────────────────

void App::SetPaused(bool paused) {
	m_paused = paused;
	m_frameDebt = 0;
	if (m_audio && paused)
		m_audio->Clear();
}

void App::StepInto() {
	if (!m_session)
		return;
	SetPaused(true);
	m_session->Machine().StepInstruction();
}

void App::StepOver() {
	if (!m_session)
		return;
	SetPaused(true);
	emu::Machine &machine = m_session->Machine();
	emu::Debuggable *debug = machine.Debugger();
	if (!debug) {
		machine.StepInstruction();
		return;
	}
	// Un appel est exécuté jusqu'à son retour (l'instruction suivante), borné à ~4 images.
	const emu::DisassembledLine line = debug->Disassemble(debug->ProgramCounter());
	if (line.mnemonic != "CALL" && line.mnemonic != "RST") {
		machine.StepInstruction();
		return;
	}
	const uint32_t target = line.address + line.size;
	for (int i = 0; i < 400000 && (i == 0 || debug->ProgramCounter() != target); ++i)
		machine.StepInstruction();
}

bool App::IsDualScreen() const noexcept { return m_session && m_session->System().name == "nds"; }

void App::ToggleDualLayout() {
	m_dualLayout = m_dualLayout == DualLayout::Stacked ? DualLayout::SideBySide : DualLayout::Stacked;
	SaveLibraryMarks();
}

void App::ToggleFullscreen() {
	m_fullscreen = !m_fullscreen;
	m_window->SetFullscreen(m_fullscreen);
}

void App::Emulate(float dt) {
	if (!m_session || m_paused)
		return;
	emu::Machine &machine = m_session->Machine();
	const float speed = m_fast ? 4.f : m_speed;
	m_frameDebt += double(dt) * machine.FrameRate() * double(speed);
	m_frameDebt = std::min(m_frameDebt, 8.0); // pas de rattrapage après un gel
	emu::Debuggable *debug = machine.Debugger();
	const auto start = std::chrono::steady_clock::now();
	while (m_frameDebt >= 1.0 && !m_paused) {
		m_frameDebt -= 1.0;
		if (m_breakpoints.empty() || !debug) {
			machine.RunFrame();
		} else {
			// Avec des points d'arrêt, l'image avance instruction par instruction.
			const uint64_t frame = machine.FrameCount();
			while (machine.FrameCount() == frame) {
				machine.StepInstruction();
				if (m_breakpoints.contains(debug->ProgramCounter())) {
					SetPaused(true);
					m_view = View::Debug;
					Log(LogLevel::Info, std::format("[INFO] point d'arrêt atteint en ${:X}", debug->ProgramCounter()));
					break;
				}
			}
		}
		// Son : à vitesse normale seulement ; la file SDL est bornée à ~0,2 s.
		const auto sounds = machine.Sounds();
		if (!sounds.empty()) {
			m_samples.resize(sounds.front()->Available() * 2);
			(void)sounds.front()->Pull(m_samples);
			if (m_audio && speed == 1.f && m_audio->Queued() < sounds.front()->SampleRate() * 4 / 5)
				m_audio->PutData(std::span<const int16_t>(m_samples));
		}
	}
	m_lastEmulationMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
	if (debug) { // accès au bus hors de toute projection : signe d'un programme égaré
		const uint64_t unmapped = debug->ProgramSpace().UnmappedReads();
		if (unmapped > m_unmappedReads + 1000) {
			Log(LogLevel::Warn, std::format("[AVERT] {} lectures hors projection du bus", unmapped - m_unmappedReads));
			m_unmappedReads = unmapped;
		}
	}
}

void App::HandleEvents() {
	m_events.Poll();
	if (m_events.IsQuitRequested())
		m_running = false;

	// Modal de configuration des touches : il capture tous les évènements
	// (l'arrière-plan ignore la souris et le clavier).
	if (m_keysOpen) {
		if (!m_rebinding.empty()) {
			// Réassignation : la prochaine touche pressée remplace le binding
			// (Échap annule sans rien changer).
			if (m_events.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed()) {
				m_rebinding.clear();
				m_modalDirty = true;
			} else {
				for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc) {
					if (m_events.KeyScan(static_cast<SDL_Scancode>(sc)).IsJustPressed()) {
						for (KeyBinding &binding : m_keys)
							if (binding.button == m_rebinding) {
								binding.key = static_cast<SDL_Scancode>(sc);
								break;
							}
						m_rebinding.clear();
						m_modalDirty = true;
						break;
					}
				}
			}
		} else if (m_events.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed()) {
			m_keysOpen = false;
		}
		return;
	}

	const bool typing = m_ui->WantsKeyboard();

	// Clavier physique (scancodes) → boutons de la machine.
	if (m_session) {
		for (const KeyBinding &binding : m_keys) {
			const sdl3::Trigger t = m_events.KeyScan(binding.key);
			if (t.IsJustPressed() && !typing)
				(void)m_session->SetButton(binding.button, true);
			if (t.IsJustReleased())
				(void)m_session->SetButton(binding.button, false);
		}
	}

	// Raccourcis clavier (appui simple, sans champ de saisie).
	if (!typing) {
		if (m_events.KeyScan(SDL_SCANCODE_ESCAPE).IsJustPressed() && m_fullscreen)
			ToggleFullscreen();
		if (m_events.KeyScan(SDL_SCANCODE_P).IsJustPressed())
			SetPaused(!m_paused);
		if (m_events.KeyScan(SDL_SCANCODE_F5).IsJustPressed())
			SaveNewState();
		if (m_events.KeyScan(SDL_SCANCODE_F9).IsJustPressed())
			QuickLoad();
		if (m_events.KeyScan(SDL_SCANCODE_F10).IsJustPressed())
			StepOver();
		if (m_events.KeyScan(SDL_SCANCODE_F11).IsJustPressed())
			StepInto();
		if (m_events.KeyScan(SDL_SCANCODE_F12).IsJustPressed())
			m_view = m_view == View::Game ? View::Debug : View::Game;
		if (m_events.KeyScan(SDL_SCANCODE_F3).IsJustPressed() && IsDualScreen())
			ToggleDualLayout();
		if (m_events.KeyScan(SDL_SCANCODE_F2).IsJustPressed() && m_session) {
			const fs::path path = m_options.dataDirectory / "screenshots" /
								  std::format("{}_{}.png", m_session->RomPath().stem().string(), m_session->Machine().FrameCount());
			m_ui->Notify(m_session->SaveScreenshot(path) ? "Capture : " + path.string() : std::string("Capture non écrite"));
		}
	}

	// Manette → boutons de la machine.
	if (m_session) {
		for (const PadBinding &binding : kPad) {
			const sdl3::Trigger t = m_events[binding.pad];
			if (t.IsJustPressed())
				(void)m_session->SetButton(binding.button, true);
			if (t.IsJustReleased())
				(void)m_session->SetButton(binding.button, false);
		}
	}

	// Avance rapide : Tab maintenu (hors champ de saisie).
	m_fast = !typing && sdl3::keyboard::IsPressed(SDL_SCANCODE_TAB);
}

void App::OpenKeyConfig() {
	m_keysOpen = true;
	m_rebinding.clear();
	m_modalDirty = true;
}

void App::BuildKeyConfigModal() {
	ui::WidgetBuilder column = ui::Column().Gap(6);
	for (const KeyBinding &binding : m_keys) {
		const std::string button = binding.button;
		const std::string keyName = sdl3::keyboard::ScancodeName(binding.key);
		const bool rebinding = m_rebinding == button;
		column.Child(ui::Row().Gap(8).AlignItems(ui::Align::Center).Children({
			ui::Text(ButtonLabel(button)).Width(64),
			ui::Text(rebinding ? "Appuyez sur une touche…" : keyName).Grow(),
			ui::Button(rebinding ? "Annuler" : "Modifier").OnClick([this, button] {
				m_rebinding = m_rebinding == button ? "" : button;
				m_modalDirty = true;
			}),
		}).Height(30));
	}
	m_modal->Root(ui::Modal().Title("Configurer les touches")
					  .OnClose([this] {
						  m_keysOpen = false;
						  m_rebinding.clear();
					  })
					  .Child(column));
}

bool App::SaveWindowScreenshot(const fs::path &path) {
	std::error_code ec;
	if (path.has_parent_path())
		fs::create_directories(path.parent_path(), ec);
	auto surface = m_renderer->ReadPixels();
	if (surface.IsError())
		return false;
	return surface.Value().SavePng(String(path.string()));
}

int App::Run() {
	if (!InitWindow()) {
		core::log::Error("retro : fenêtre ou rendu indisponible ({})", sdl3::GetError());
		return 1;
	}
	LoadLibraryMarks();
	if (!m_options.session.rom.empty()) {
		m_options.libraries.push_back(m_options.session.rom.parent_path().empty() ? fs::path(".") : m_options.session.rom.parent_path());
		if (!LoadRom(m_options.session.rom))
			return 1;
	}
	ScanLibrary();

	auto last = std::chrono::steady_clock::now();
	int code = 0;
	while (m_running) {
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.0005f, 0.1f);
		last = now;
		HandleEvents();
		PollLibrary();
		Emulate(dt);

		// Statistiques.
		m_frameMs.push_back(dt * 1000.f);
		m_emulationMs.push_back(m_lastEmulationMs);
		if (m_frameMs.size() > 240) {
			m_frameMs.erase(m_frameMs.begin());
			m_emulationMs.erase(m_emulationMs.begin());
		}
		m_fpsTimer += dt;
		++m_fpsFrames;
		if (m_fpsTimer >= 0.5f) {
			m_fps = float(m_fpsFrames) / m_fpsTimer;
			m_fpsTimer = 0;
			m_fpsFrames = 0;
		}

		if (m_session && m_screenTexture)
			m_screenTexture->Update(m_session->Screen().Frame().data(), m_screenW * 4);
		m_renderer->SetDrawColor(sdl3::Color{15, 19, 30, 255});
		m_renderer->Clear();
		m_canvas.Begin();
		m_ui->Begin(dt);
		Draw(dt);
		m_ui->DrawNotifications(dt, 52.f);
		m_ui->End();
		m_canvas.End();
		++m_uiFrames;
		if (m_options.frames >= 0 && m_uiFrames >= m_options.frames) {
			if (!m_options.screenshot.empty() && !SaveWindowScreenshot(m_options.screenshot)) {
				core::log::Error("retro : capture non écrite : {}", m_options.screenshot.string());
				code = 1;
			}
			m_running = false;
		}
		m_renderer->Present();
	}
	return code;
}

} // namespace retro
