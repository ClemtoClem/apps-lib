// retro — émulateur de consoles bâti sur le cadre `emu`.
//
//   retro [ROM] [--roms DOSSIER]… [--system NOM] [--model dmg|cgb] [--save-dir DOSSIER]
//             [--data-dir DOSSIER] [--mute] [--debug] [--paused]
//   retro ROM --headless [--frames N] [--screenshot F.png] [--press BOUTON@IMAGE[+DURÉE]]…
//             [--save-state N] [--load-state N] [--require-video]
//   retro --list-systems
//
// Code de sortie : 0 si tout s'est bien passé ; 1 si la ROM n'a pas démarré ou si une
// vérification demandée a échoué ; 2 si la ligne de commande est invalide.
#include <charconv>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <consoles/consoles.hpp>
#include <core/log.hpp>
#include <sdl3/io/filesystem.hpp>

#include "retro/app.hpp"
#include "retro/session.hpp"

namespace {

/// Appui programmé (mode sans écran) : `a@120+10` presse A de l'image 120 à l'image 130.
struct Press {
	std::string button;
	int frame = 0, duration = 5;
};

/// Appui programmé sur l'écran tactile : `128,96@300+10` (coordonnées dans la zone tactile).
struct Touch {
	int x = 0, y = 0, frame = 0, duration = 5;
};

struct Options {
	retro::AppOptions app;
	retro::SessionOptions &session = app.session;
	bool headless = false, listSystems = false, requireVideo = false, help = false;
	int saveState = -1, loadState = -1;
	std::vector<Press> presses;
	std::vector<Touch> touches;
};

void Usage() {
	std::puts("retro — émulateur de consoles (Game Boy, Game Boy Color, Game Boy Advance, Nintendo DS)\n\n"
			  "  retro [ROM] [--roms DOSSIER]… [--system NOM] [--model dmg|cgb] [--save-dir D] [--data-dir D]\n"
			  "        [--mute] [--debug] [--paused]\n"
			  "  retro ROM --headless [--frames N] [--screenshot F.png] [--press BOUTON@IMAGE[+DURÉE]]…\n"
			  "            [--touch X,Y@IMAGE[+DURÉE]]… [--save-state N] [--load-state N] [--require-video]\n"
			  "  retro --list-systems\n\n"
			  "Clavier : flèches, X (A), W/Z (B), Entrée (Start), Maj droite (Select), A/S (L/R), Q/D (X/Y DS) ;\n"
			  "souris : stylet de l'écran tactile ; F5 : nouvel état,\n"
			  "F9 : chargement rapide, F2 : capture, P : pause, Tab : avance rapide, F12 : débogage,\n"
			  "F11 / F10 : pas à pas.");
}

bool ParseInt(std::string_view text, int &out) {
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out);
	return error == std::errc() && end == text.data() + text.size();
}

bool ParseTouch(std::string_view text, Touch &out) {
	const size_t comma = text.find(','), at = text.find('@');
	if (comma == std::string_view::npos || at == std::string_view::npos || at < comma)
		return false;
	std::string_view rest = text.substr(at + 1);
	const size_t plus = rest.find('+');
	if (plus != std::string_view::npos && !ParseInt(rest.substr(plus + 1), out.duration))
		return false;
	return ParseInt(text.substr(0, comma), out.x) && ParseInt(text.substr(comma + 1, at - comma - 1), out.y) &&
		   ParseInt(rest.substr(0, plus), out.frame) && out.frame >= 0 && out.duration > 0;
}

bool ParsePress(std::string_view text, Press &out) {
	const size_t at = text.find('@');
	if (at == std::string_view::npos || at == 0)
		return false;
	out.button = std::string(text.substr(0, at));
	std::string_view rest = text.substr(at + 1);
	const size_t plus = rest.find('+');
	if (plus != std::string_view::npos && !ParseInt(rest.substr(plus + 1), out.duration))
		return false;
	return ParseInt(rest.substr(0, plus), out.frame) && out.frame >= 0 && out.duration > 0;
}

/// Faux (avec un message) si la ligne de commande est invalide.
bool Parse(int argc, char **argv, Options &o) {
	for (int i = 1; i < argc; ++i) {
		const std::string_view arg = argv[i];
		auto value = [&](std::string_view &out) {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "retro : valeur manquante après %s\n", argv[i]);
				return false;
			}
			out = argv[++i];
			return true;
		};
		auto number = [&](int &out) {
			std::string_view text;
			if (!value(text))
				return false;
			if (!ParseInt(text, out)) {
				std::fprintf(stderr, "retro : nombre attendu après %s\n", argv[i - 1]);
				return false;
			}
			return true;
		};
		std::string_view text;
		if (arg == "--help" || arg == "-h")
			o.help = true;
		else if (arg == "--list-systems")
			o.listSystems = true;
		else if (arg == "--headless")
			o.headless = true;
		else if (arg == "--mute")
			o.app.mute = true;
		else if (arg == "--debug")
			o.app.debugView = true;
		else if (arg == "--paused")
			o.app.paused = true;
		else if (arg == "--roms") {
			if (!value(text))
				return false;
			o.app.libraries.emplace_back(text);
		} else if (arg == "--data-dir") {
			if (!value(text))
				return false;
			o.app.dataDirectory = text;
		}
		else if (arg == "--require-video")
			o.requireVideo = true;
		else if (arg == "--system") {
			if (!value(text))
				return false;
			o.session.system = std::string(text);
		} else if (arg == "--model") {
			if (!value(text))
				return false;
			o.session.model = std::string(text);
		} else if (arg == "--save-dir") {
			if (!value(text))
				return false;
			o.session.saveDirectory = text;
		} else if (arg == "--state-dir") {
			if (!value(text))
				return false;
			o.session.stateDirectory = text;
		} else if (arg == "--bios-dir") {
			if (!value(text))
				return false;
			o.session.biosDirectory = text;
		} else if (arg == "--screenshot") {
			if (!value(text))
				return false;
			o.app.screenshot = text;
		} else if (arg == "--frames") {
			if (!number(o.app.frames))
				return false;
		} else if (arg == "--save-state") {
			if (!number(o.saveState))
				return false;
		} else if (arg == "--load-state") {
			if (!number(o.loadState))
				return false;
		} else if (arg == "--touch") {
			Touch touch;
			if (!value(text) || !ParseTouch(text, touch)) {
				std::fprintf(stderr, "retro : --touch attend X,Y@IMAGE[+DURÉE]\n");
				return false;
			}
			o.touches.push_back(touch);
		} else if (arg == "--press") {
			Press press;
			if (!value(text) || !ParsePress(text, press)) {
				std::fprintf(stderr, "retro : --press attend BOUTON@IMAGE[+DURÉE]\n");
				return false;
			}
			o.presses.push_back(std::move(press));
		} else if (!arg.empty() && arg.front() == '-') {
			std::fprintf(stderr, "retro : option inconnue %s\n", argv[i]);
			return false;
		} else if (o.session.rom.empty()) {
			o.session.rom = arg;
		} else {
			std::fprintf(stderr, "retro : une seule ROM attendue\n");
			return false;
		}
	}
	return true;
}

int RunHeadless(retro::Session &session, const Options &o) {
	const int frames = o.app.frames >= 0 ? o.app.frames : 60;
	if (o.loadState >= 0 && !session.LoadState(o.loadState)) {
		std::fprintf(stderr, "retro : état %d introuvable ou incompatible\n", o.loadState);
		return 1;
	}
	for (int frame = 0; frame < frames; ++frame) {
		for (const Press &press : o.presses) {
			if (frame == press.frame && !session.SetButton(press.button, true)) {
				std::fprintf(stderr, "retro : bouton inconnu « %s »\n", press.button.c_str());
				return 1;
			}
			if (frame == press.frame + press.duration)
				(void)session.SetButton(press.button, false);
		}
		for (const Touch &touch : o.touches) {
			const auto panels = session.Machine().TouchPanels();
			if (frame == touch.frame && panels.empty()) {
				std::fprintf(stderr, "retro : ce système n'a pas d'écran tactile\n");
				return 1;
			}
			if (frame == touch.frame)
				panels.front()->Set(touch.x, touch.y, true);
			if (frame == touch.frame + touch.duration)
				panels.front()->Set(touch.x, touch.y, false);
		}
		session.Machine().RunFrame();
	}
	int code = 0;
	if (!o.app.screenshot.empty() && !session.SaveScreenshot(o.app.screenshot)) {
		std::fprintf(stderr, "retro : capture non écrite : %s\n", o.app.screenshot.string().c_str());
		code = 1;
	}
	if (o.saveState >= 0 && !session.SaveState(o.saveState)) {
		std::fprintf(stderr, "retro : état %d non enregistré\n", o.saveState);
		code = 1;
	}
	if (o.requireVideo && !session.ScreenHasContent()) {
		std::fprintf(stderr, "retro : l'écran est uniforme après %d images\n", frames);
		code = 1;
	}
	std::printf("%s : %d image(s), %llu au total\n", session.System().name.c_str(), frames,
				static_cast<unsigned long long>(session.Machine().FrameCount()));
	return code;
}

} // namespace

int main(int argc, char **argv) {
	Options options;
	// Données utilisateur : ~/.local/share/apps-lib/retro/ (surchargées par --data-dir / --state-dir).
	const std::filesystem::path data = sdl3::filesystem::AppDataDir("retro").c_str();
	options.app.dataDirectory = data;
	options.session.stateDirectory = data / "states";
	if (!Parse(argc, argv, options)) {
		Usage();
		return 2;
	}
	emu::SystemRegistry registry;
	consoles::RegisterAll(registry);
	if (options.help) {
		Usage();
		return 0;
	}
	if (options.listSystems) {
		for (const emu::SystemInfo &system : registry.All()) {
			std::string extensions;
			for (const std::string &e : system.extensions)
				extensions += (extensions.empty() ? "" : " ") + e;
			std::printf("%-6s %s (%s, %d) : %s\n", system.name.c_str(), system.description.c_str(), system.manufacturer.c_str(), system.year,
						extensions.c_str());
		}
		return 0;
	}
	if (!options.headless) {
		retro::App app(std::move(options.app));
		return app.Run();
	}
	if (options.session.rom.empty()) {
		Usage();
		return 2;
	}
	auto session = retro::Session::Open(registry, options.session);
	if (session.IsError()) {
		std::fprintf(stderr, "retro : %s\n", session.Error().c_str());
		return 1;
	}
	return RunHeadless(*session.Value(), options);
}
