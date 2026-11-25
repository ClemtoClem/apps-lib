// Voxelia — point d'entrée : boucle SDL, routage des événements (jeu / Studio),
// monde animé derrière les menus, musique.
//
//   voxelia [--world DOSSIER] [--new] [--seed N] [--mods DIR] [--data DIR] [--frames N]
//           [--screenshot FICHIER] [--screen ÉCRAN] [--exec "commande"] [--studio] [--open FICHIER] [--studio-tab ID] [--no-vsync]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <vector>

#include <core/clock.hpp>
#include <core/hash.hpp>
#include <core/log.hpp>
#include <diagnostics/trace.hpp>
#include <sdl3/core/events_manager.hpp>
#include <sdl3/io/filesystem.hpp>
#include <sdl3/input/input.hpp>

#include "app.hpp"
#include "world_editor.hpp"

namespace voxelia {

namespace {

Options ParseArgs(int argc, char **argv) {
	Options o;
	o.data = sdl3::filesystem::AppDataDir("voxelia").c_str(); // ~/.local/share/apps-lib/voxelia
	for (int i = 1; i < argc; ++i) {
		const std::string_view a = argv[i];
		auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
		if (a == "--world") o.world = next();
		else if (a == "--new") o.fresh = true;
		else if (a == "--seed") { o.seed = std::strtoull(next().c_str(), nullptr, 10); o.seedSet = true; }
		else if (a == "--mods") o.mods = next();
		else if (a == "--data") o.data = next();
		else if (a == "--frames") o.frames = std::atoi(next().c_str());
		else if (a == "--screenshot") o.screenshot = next();
		else if (a == "--screen") o.screen = next();
		else if (a == "--exec") o.exec.push_back(next());
		else if (a == "--no-input") o.noInput = true;
		else if (a == "--studio") o.studio = true;
		else if (a == "--open") { o.studioFiles.emplace_back(next()); o.studio = true; }
		else if (a == "--studio-tab") { o.studioTabs.push_back(next()); o.studio = true; }
		else if (a == "--editor") o.editor = true;
		else if (a == "--no-vsync") { o.vsyncOverride = true; o.vsync = false; }
		else if (a == "--help" || a == "-h") {
			std::puts("voxelia [--world DOSSIER] [--new] [--seed N] [--mods DIR] [--data DIR] [--frames N] [--screenshot F.png]\n"
					  "        [--screen main|new|worlds|world|mods|mod|modsettings|settings|pause] [--exec CMD] [--studio] [--open F] [--studio-tab ID|3d] [--editor] [--no-input] [--no-vsync]");
			std::exit(0);
		}
	}
	return o;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// App
// ══════════════════════════════════════════════════════════════════════════
std::vector<std::pair<std::string, WorldSettings>> App::ListWorlds() const {
	std::vector<std::pair<std::string, WorldSettings>> out;
	std::error_code ec;
	for (const auto &e : fs::directory_iterator(WorldsDir(), ec)) {
		if (!e.is_directory())
			continue;
		WorldSettings ws = WorldSettings::Load(e.path()).value_or(WorldSettings{});
		if (!fs::exists(e.path() / "world.json"))
			ws.name = e.path().filename().string(); // monde antérieur aux réglages de monde
		out.emplace_back(e.path().filename().string(), std::move(ws));
	}
	std::ranges::sort(out, [](const auto &a, const auto &b) { return a.second.lastPlayed > b.second.lastPlayed; });
	return out;
}

void App::CaptureMouse(bool on) {
	if (on && editor && editor->active)
		on = false; // mode éditeur : souris libre (sélection, gizmo), regard au clic droit
	mouseCaptured = on;
	Window().SetRelativeMouseMode(on);
	if (on)
		Window().StopTextInput();
	else
		Window().StartTextInput();
}

void App::ReloadContentIfNeeded() {
	if (!contentDirty)
		return;
	background.reset();
	content.Load(opt.mods, settings.disabledMods, pool);
	audio.IndexSounds(content.mods);
	renderer->SetBlockTextures(content.textureLayers, content.layerCount);
	contentDirty = false;
}

bool App::StartGame(const std::string &folder, const std::optional<WorldSettings> &fresh) {
	ReloadContentIfNeeded();
	background.reset(); // libère le monde de fond (et ses maillages GPU)
	ResetGame();
	const fs::path dir = WorldsDir() / folder;
	WorldSettings ws;
	if (fresh) {
		std::error_code ec;
		fs::remove_all(dir, ec);
		ws = *fresh;
		if (ws.created.empty())
			ws.created = NowText();
	} else {
		ws = WorldSettings::Load(dir).value_or(WorldSettings{});
		if (!fs::exists(dir / "world.json")) {
			ws.name = folder;
			ws.created = NowText();
			ws.seed = opt.seedSet ? opt.seed : core::Mix64(sdl3::GetTicksMS()) % 1000000000;
		}
	}
	game = std::make_unique<Game>(content, settings, modSettings, pool, *renderer, Ui());
	if (!game->Open(dir, ws)) {
		Ui().Notify("Impossible d'ouvrir le monde " + folder, Ui().Style().danger);
		ResetGame();
		return false;
	}
	settings.lastWorld = folder;
	settings.Save(settingsFile);
	screen = Screen::Playing;
	consoleOpen = false;
	SetSceneVisible(true);
	CaptureMouse(true);
	audio.PlayGame();
	return true;
}

void App::ResetGame() {
	if (studio)
		studio->ReleaseGame();
	if (editor)
		editor->active = false;
	game.reset();
}

void App::ApplyHotReload() {
	FileChanges ch = hotReload.Take();
	if (ch.Empty())
		return;
	DIAG_ZONE("App::ApplyHotReload");
	std::vector<std::string> parts;
	if (!ch.definitions.empty()) {
		// Blocs redéfinis dans le même registre (identifiants stables), textures et tables
		// de maillage refaites, chunks remaillés : le monde reste ouvert.
		content.ReloadDefinitions(pool);
		renderer->SetBlockTextures(content.textureLayers, content.layerCount);
		for (Game *g : {game.get(), background.get()})
			if (g) {
				g->World().SetMeshContext(content.meshContext);
				g->World().RemeshAll();
			}
		parts.push_back(std::format("{} définition(s)", ch.definitions.size()));
	} else if (!ch.textures.empty()) {
		const size_t layers = content.ReloadTextureFiles(ch.textures);
		if (layers)
			renderer->SetBlockTextures(content.textureLayers, content.layerCount);
		parts.push_back(std::format("{} texture(s), {} couche(s)", ch.textures.size(), layers));
	}
	if (!ch.scripts.empty()) {
		if (game && game->Scripts())
			game->ReloadScripts(); // hooks, systèmes et commandes ; entités et blocs conservés
		parts.push_back(std::format("{} script(s)", ch.scripts.size()));
	}
	if (!ch.models.empty() || !ch.definitions.empty()) {
		// Rigs des créatures et modèles articulés rechargés (édition dans le Studio) :
		// les entités sont remaillées à la prochaine image.
		content.LoadEntityRigs();
		for (Game *g : {game.get(), background.get()})
			if (g)
				g->ReloadEntityModels();
	}
	if (!ch.models.empty())
		parts.push_back(std::format("{} modèle(s)", ch.models.size()));
	if (studio)
		studio->FilesChanged(ch);
	std::string files;
	size_t listed = 0;
	for (const auto *list : {&ch.scripts, &ch.definitions, &ch.textures, &ch.models})
		for (const fs::path &p : *list)
			if (listed++ < 3)
				files += (files.empty() ? "" : ", ") + p.filename().string();
	if (listed > 3)
		files += std::format(" (+{})", listed - 3);
	std::string summary;
	for (const std::string &p : parts)
		summary += (summary.empty() ? "" : ", ") + p;
	const std::string msg = std::format("[rechargement à chaud] {} — {}", summary, files);
	core::log::Info("{}", msg);
	if (game)
		game->Log(msg);
	Ui().Notify(msg, Ui().Style().info);
	reloadLog.push_back(msg);
	if (reloadLog.size() > 50)
		reloadLog.erase(reloadLog.begin());
}

void App::LeaveGame() {
	ResetGame(); // sauvegarde dans le destructeur
	screen = Screen::Main;
	CaptureMouse(false);
	if (settings.menuMusic)
		audio.PlayMenu();
	else
		audio.Stop();
	EnsureBackground();
}

void App::EnsureBackground() {
	if (game || !settings.menuBackground) {
		if (!game)
			background.reset();
		SetSceneVisible(game != nullptr || background != nullptr);
		return;
	}
	if (background)
		return;
	// Aperçu du menu : monde partiel (surface seule), sans entités ni scripts,
	// régénéré à chaque lancement (jamais sauvegardé).
	WorldSettings ws;
	ws.name = "Fond du menu";
	ws.seed = 20261110;
	ws.relief = 1.25f;
	ws.creatures = false;
	ws.startTime = 0.36f;
	ws.dayCycle = false;
	const fs::path dir = opt.data / "menu_background";
	std::error_code ec;
	fs::remove_all(dir, ec);
	background = std::make_unique<Game>(content, settings, modSettings, pool, *renderer, Ui());
	GameOptions o;
	o.scripts = false;
	o.persistent = false;
	o.cinematic = true;
	o.preview = true; // seulement une partie du monde (surface), sans entités
	if (!background->Open(dir, ws, o)) {
		background.reset();
		return;
	}
	background->Teleport({40.5f, float(background->World().Terrain().SurfaceHeight(40, 40)) + 26.f, 40.5f});
	SetSceneVisible(true);
}

void App::ApplySettings() {
	SetVSync(settings.vsync);
	audio.SetVolumes(settings.masterVolume, settings.musicVolume, settings.effectsVolume);
	audio.SetShuffle(settings.shuffleMusic);
	Canvas().SetScale(displayScale * std::clamp(settings.uiScale, 0.5f, 2.f));
	if (game)
		game->ApplySettings();
	if (background)
		background->ApplySettings();
	if (!game) {
		if (settings.menuMusic)
			audio.PlayMenu();
		else
			audio.Stop();
		EnsureBackground();
	}
}

void App::OpenStudio() {
	if (!studio)
		studio = std::make_unique<Studio>(*this);
	if (studio->Open() && mouseCaptured) {
		if (game && screen == Screen::Playing)
			screen = Screen::Paused;
		CaptureMouse(false);
	}
}

namespace {

void HandleEvents(App &app) {
	DIAG_ZONE("HandleEvents");
	if (app.Events().IsQuitRequested())
		app.RequestQuit();

	app.player.lookDX = app.player.lookDY = 0;
	app.player.hotbarDelta = 0;
	app.player.hotbarSelect = -1;
	app.player.gesture = 0;
	app.player.cycleSwimStyle = app.player.cycleCamera = app.player.swapHands = false;

	auto justPressed = [&](SDL_Keycode k) { return app.Events()[k].IsJustPressed(); };
	const bool playing = app.screen == Screen::Playing && !app.consoleOpen;

	// Caméra libre (éditeur) : regard au clic droit, vitesse à la molette.
	if (playing && app.editor && app.editor->active) {
		if (app.Events()[sdl3::MouseButton::Right].IsPressed()) {
			app.player.lookDX += app.Events().MouseRelX();
			app.player.lookDY += app.Events().MouseRelY();
		}
		if (app.Events().WheelY() != 0.f && !app.Ui().WantsMouse())
			app.editorWheel += app.Events().WheelY();
	}

	// Souris capturée (jeu) : regard aux déplacements, molette = barre d'outils.
	if (playing && app.mouseCaptured) {
		app.player.lookDX += app.Events().MouseRelX();
		app.player.lookDY += app.Events().MouseRelY();
		const float wy = app.Events().WheelY();
		if (wy != 0.f)
			app.player.hotbarDelta -= wy > 0.f ? 1 : (wy < 0.f ? -1 : 0);
	}

	// Raccourcis clavier (appui simple, sans répétition) — « return » sort du bloc.
	auto shortcuts = [&]() {
		if (justPressed(SDLK_F2)) {
			std::error_code ec;
			fs::create_directories("screenshots", ec);
			app.opt.screenshot = std::format("screenshots/voxelia_{}.png", std::chrono::system_clock::now().time_since_epoch().count());
		}
		if (justPressed(SDLK_F10)) {
			app.OpenStudio();
			return;
		}
		if (justPressed(SDLK_F11))
			app.Window().SetFullscreen(!app.Window().IsFullscreen());
		if (app.screen == Screen::Playing && app.game) {
			if (app.consoleOpen) {
				if (justPressed(SDLK_ESCAPE)) {
					app.consoleOpen = false;
					app.CaptureMouse(true);
					return;
				}
			} else {
				if (justPressed(SDLK_ESCAPE)) {
					if (app.game->InventoryOpen()) {
						app.game->ToggleInventory();
						app.CaptureMouse(true);
						return;
					}
					app.screen = Screen::Paused;
					app.CaptureMouse(false);
					return;
				}
				if (justPressed(SDLK_TAB)) {
					app.game->ToggleInventory();
					app.CaptureMouse(!app.game->InventoryOpen());
					return;
				}
				if (justPressed(SDLK_T) || justPressed(SDLK_RETURN) || justPressed(SDLK_SLASH)) {
					app.consoleOpen = true;
					app.consoleLine = justPressed(SDLK_SLASH) ? "/" : "";
					app.CaptureMouse(false);
					return;
				}
				if (justPressed(SDLK_F3))
					app.settings.showDebug = !app.settings.showDebug;
				if (justPressed(SDLK_F4)) { // mode éditeur du monde : caméra libre, gizmo, superpositions
					if (!app.editor)
						app.editor = std::make_unique<InGameEditor>();
					app.editor->Toggle(*app.game);
					app.CaptureMouse(!app.editor->active);
					app.Ui().Notify(app.editor->active ? "Éditeur du monde : caméra libre (F4 pour revenir au joueur)" : "Retour au joueur");
					return;
				}
				if (app.editor && app.editor->active && !app.Ui().WantsKeyboard()) {
					if (justPressed(SDLK_G))
						app.editor->gizmo.mode = TransformGizmo::Mode::Translate;
					if (justPressed(SDLK_R))
						app.editor->gizmo.mode = TransformGizmo::Mode::Rotate;
					if (justPressed(SDLK_DELETE) && app.editor->selected && app.editor->selected != app.game->Player()) {
						app.game->World().DespawnEntity(app.editor->selected);
						app.editor->selected = {};
					}
				}
				if (justPressed(SDLK_F5)) {
					app.game->Save();
					app.Ui().Notify("Monde sauvegardé");
				}
				if (justPressed(SDLK_F) && app.game->CanFly())
					app.game->SetFlying(!app.game->Flying());
				// Avatar : coucous, style de nage, caméra, main gauche.
				if (!(app.editor && app.editor->active)) {
					if (justPressed(SDLK_V))
						app.player.gesture = int(voxel::AvatarGesture::Wave);
					if (justPressed(SDLK_B))
						app.player.gesture = int(voxel::AvatarGesture::WaveBoth);
					if (justPressed(SDLK_N))
						app.player.gesture = int(voxel::AvatarGesture::WaveSmall);
					if (justPressed(SDLK_K))
						app.player.cycleSwimStyle = true;
					if (justPressed(SDLK_X))
						app.player.swapHands = true;
				}
				if (justPressed(SDLK_F6))
					app.player.cycleCamera = true;
				for (int i = 0; i < 9; ++i)
					if (justPressed(SDL_Keycode(SDLK_1 + Uint32(i))))
						app.player.hotbarSelect = i;
			}
		} else if (app.screen == Screen::Paused && justPressed(SDLK_ESCAPE) && !app.Ui().WantsKeyboard()) {
			app.screen = Screen::Playing;
			app.CaptureMouse(true);
		} else if (justPressed(SDLK_ESCAPE) && app.screen != Screen::Main && !app.Ui().WantsKeyboard()) {
			switch (app.screen) {
			case Screen::Settings:
				app.audio.SetVolumes(app.settings.masterVolume, app.settings.musicVolume, app.settings.effectsVolume);
				app.screen = app.settingsReturn;
				break;
			case Screen::ModDetail: app.screen = Screen::Mods; break;
			case Screen::ModSettings: app.screen = app.modReturn; break;
			case Screen::WorldSettings: app.screen = app.game ? Screen::Paused : Screen::Worlds; break;
			case Screen::Skins: app.screen = app.skinReturn; break;
			default: app.screen = Screen::Main; break;
			}
		}
	};
	shortcuts();

	// États continus lus directement auprès de SDL, seulement si la fenêtre du jeu a le focus.
	PlayerInput &p = app.player;
	const bool focused = app.Window().HasInputFocus();
	const bool active = focused && app.screen == Screen::Playing && !app.consoleOpen && (!app.game || !app.game->InventoryOpen());
	auto key = [&](SDL_Scancode s) { return active && sdl3::keyboard::IsPressed(s); };
	p.moveZ = float(key(SDL_SCANCODE_W)) - float(key(SDL_SCANCODE_S));
	p.moveX = float(key(SDL_SCANCODE_D)) - float(key(SDL_SCANCODE_A));
	p.jump = key(SDL_SCANCODE_SPACE);
	p.descend = key(SDL_SCANCODE_LSHIFT) && app.game && app.game->Flying();
	// Maj : s'accroupir au sol (descendre en vol) ; Ctrl : courir.
	p.crouch = key(SDL_SCANCODE_LSHIFT) && app.game && !app.game->Flying();
	p.sprint = key(SDL_SCANCODE_LCTRL);
	const sdl3::MouseState mouse = sdl3::mouse::State();
	p.primary = active && app.mouseCaptured && mouse.Left();
	p.secondary = active && app.mouseCaptured && mouse.Right();
	p.use = key(SDL_SCANCODE_E);
	if (app.opt.noInput) { // captures automatisées : le joueur n'obéit qu'aux commandes (--exec)
		const PlayerInput oneShots = p;
		p = PlayerInput{};
		p.gesture = oneShots.gesture;
	}
}

/// Écran demandé en ligne de commande (captures automatisées de chaque menu).
void SelectScreen(App &app, const std::string &name) {
	app.draft = app.settings;
	app.modDraft = app.modSettings;
	for (const auto &m : app.content.mods.Mods())
		if (!m.settings.empty())
			app.selectedMod = m.id;
	app.newWorld = WorldSettings{};
	app.newSeedText = "257417211";
	if (const auto worlds = app.ListWorlds(); !worlds.empty()) {
		app.editWorldFolder = worlds.front().first;
		app.editWorld = worlds.front().second;
	}
	const std::pair<std::string_view, Screen> map[] = {{"main", Screen::Main},		 {"new", Screen::NewWorld},		   {"worlds", Screen::Worlds},
													   {"world", Screen::WorldSettings}, {"mods", Screen::Mods},			   {"mod", Screen::ModDetail},
													   {"modsettings", Screen::ModSettings}, {"settings", Screen::Settings}, {"skins", Screen::Skins},
													   {"pause", Screen::Paused}};
	for (const auto &[key, s] : map)
		if (name == key && (s != Screen::Paused || app.game)) {
			app.screen = s;
			if (s == Screen::Paused)
				app.CaptureMouse(false);
		}
}

} // namespace

bool App::OnInit() {
	displayScale = Window().DisplayScale();
	if (displayScale <= 0)
		displayScale = 1.f;
	Canvas().SetScale(displayScale * settings.uiScale);
	BuildInterface(*this); // interface conservée (factory builder), rendue par Begin/Render/End
	renderer = voxel::Renderer::Create(Device());
	if (!renderer)
		return false;

	SetSceneVisible(false);
	content.Load(opt.mods, settings.disabledMods, pool);
	renderer->SetBlockTextures(content.textureLayers, content.layerCount);
	const fs::path coreMod = opt.mods / "core";
	audio.Init(coreMod / "sounds" / "musics", coreMod / "sounds" / "UI");
	audio.IndexSounds(content.mods);
	audio.SetVolumes(settings.masterVolume, settings.musicVolume, settings.effectsVolume);
	audio.SetShuffle(settings.shuffleMusic);
	Window().StartTextInput();

	if (!opt.world.empty() || opt.fresh || !opt.exec.empty()) {
		const std::string folder = opt.world.empty() ? settings.lastWorld : opt.world;
		std::optional<WorldSettings> fresh;
		if (opt.fresh) {
			WorldSettings ws;
			ws.name = folder;
			ws.seed = opt.seedSet ? opt.seed : core::Mix64(sdl3::GetTicksMS()) % 1000000000;
			fresh = ws;
		}
		StartGame(folder, fresh);
	} else {
		if (settings.menuMusic)
			audio.PlayMenu();
		EnsureBackground();
	}
	if (!opt.screen.empty())
		SelectScreen(*this, opt.screen);
	if (opt.studio) {
		OpenStudio();
		for (const fs::path &f : opt.studioFiles)
			studio->QueueOpen(f);
		for (const std::string &t : opt.studioTabs)
			studio->ActivateTab(t);
	}
	return true;
}

void App::OnUpdate(double frameSeconds) {
	const float dt = float(frameSeconds);
	HandleEvents(*this);
	if (game) {
		if (!execDone && game->WorldLoaded()) {
			for (const auto &cmd : opt.exec)
				game->ExecuteConsole(cmd);
			execDone = true;
			if (opt.editor && !(editor && editor->active)) {
				editor = std::make_unique<InGameEditor>();
				editor->Toggle(*game);
				CaptureMouse(false);
			}
		}
		if (editor && editor->active) { // les commandes pilotent la caméra libre, le joueur attend
			PlayerInput cam = player;
			const bool keys = screen == Screen::Playing && !consoleOpen && !Ui().WantsKeyboard() && Window().HasInputFocus();
			if (keys) {
				cam.jump = sdl3::keyboard::IsPressed(SDL_SCANCODE_SPACE) || sdl3::keyboard::IsPressed(SDL_SCANCODE_E);
				cam.descend = sdl3::keyboard::IsPressed(SDL_SCANCODE_LSHIFT) || sdl3::keyboard::IsPressed(SDL_SCANCODE_Q);
			}
			editor->Update(*game, cam, dt, keys && sdl3::keyboard::IsPressed(SDL_SCANCODE_LCTRL), editorWheel);
			editorWheel = 0;
			game->Update(PlayerInput{}, dt);
		} else {
			game->Update(screen == Screen::Playing ? player : PlayerInput{}, dt);
		}
	} else if (background) {
		background->Update(PlayerInput{}, dt);
	}
	hotReload.Update(content.mods, pool);
	ApplyHotReload();
	audio.Update(dt);
	{ // sons des entités (émetteurs), spatialisés autour de la caméra du joueur
		static std::vector<SoundSource> sources;
		sources.clear();
		if (game)
			game->CollectSounds(game->Camera().position, sources);
		audio.UpdateEmitters(sources, game ? game->Camera().position : math::FVector3{}, game ? game->Camera().yaw : 0.f);
	}
	if (studio && studio->IsOpen())
		studio->Frame(dt);
}

void App::OnOffscreen() {
	if (game)
		game->Render(*this);
	else if (background)
		background->Render(*this);
}

void App::OnRender(const sdl3::FRect &, double) {
	const float dt = FrameSeconds();
	interface->Begin(dt);
	if (game && (screen == Screen::Playing || screen == Screen::Paused)) {
		game->DrawHud(Canvas(), dt);
		if (editor && editor->active && screen == Screen::Playing)
			editor->Draw(Ui(), *game);
	}
	interface->Render(); // arborescence conservée : menus ou console, + callbacks
	interface->End();
	if (Ui().Clicked())
		audio.Click();
}

void App::OnShutdown() {
	if (studio)
		studio->Close();
	ResetGame();
	background.reset();
	settings.Save(settingsFile);
}

int Run(int argc, char **argv) {
	DIAG_THREAD_NAME("main");
	App app;
	app.opt = ParseArgs(argc, argv);
	if (std::getenv("VOXELIA_DEBUG"))
		core::log::SetMinLevel(core::log::Level::Debug);
	app.settingsFile = app.opt.data / "settings.json";
	app.modSettingsFile = app.opt.data / "mod_settings.json";
	app.settings = Settings::Load(app.settingsFile);
	app.modSettings.Load(app.modSettingsFile);
	if (app.opt.vsyncOverride)
		app.settings.vsync = app.opt.vsync;

	app::AppConfig config;
	config.title = "Voxelia";
	config.width = 1440;
	config.height = 860;
	config.vsync = app.settings.vsync;
	config.scene = true; // scène 3D hors écran (couleur + profondeur + post)
	config.maxFrames = app.opt.frames >= 0 ? uint64_t(app.opt.frames) : 0;
	config.screenshot = app.opt.screenshot;
	return app.Run(config);
}

} // namespace voxelia

int main(int argc, char **argv) { return voxelia::Run(argc, argv); }
