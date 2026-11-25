// Profiler — ligne de commande.
//
//   profiler                                   visualiseur (capture la plus récente)
//
// Captures cherchées dans ~/.local/share/apps-lib/profiler/captures (`record`) et
// ~/.local/share/apps-lib/diagnostics (sessions de diagnostic des applications : trace,
// journal, rapport de plantage ou de blocage).
//   profiler record [--out F] [--duration S] -- programme args…
//   profiler list
//   profiler report [F] [--from MS --to MS] [--thread NOM] [--target-fps N] [--top N]
//   profiler compare AVANT.vcap APRES.vcap [--top N]
//   profiler export [F] --chrome SORTIE.json
//   profiler view [F] [--frames N --screenshot P.png]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <core/log.hpp>
#include <core/string.hpp>
#include <diagnostics/capture.hpp>
#include <sdl3/io/filesystem.hpp>
#include <sdl3/io/process.hpp>
#include <sdl3/core/stdinc.hpp>

#include "analysis.hpp"
#include "catalog.hpp"
#include "viewer.hpp"

namespace fs = std::filesystem;
using namespace profiler;

namespace {

/// Dossier des captures de `record` (~/.local/share/apps-lib/profiler/captures).
fs::path CaptureDir() { return CaptureDirectories().front(); }

void Usage() {
	std::puts("profiler — analyse des programmes instrumentés avec diagnostics::trace\n\n"
			  "  profiler                                    visualiseur (capture la plus récente)\n"
			  "  profiler record [--out F] [--duration S] -- programme args…\n"
			  "  profiler list\n"
			  "  profiler report [F] [--from MS] [--to MS] [--thread NOM] [--target-fps N] [--top N]\n"
			  "  profiler compare AVANT.vcap APRES.vcap [--top N]\n"
			  "  profiler export [F] --chrome SORTIE.json\n"
			  "  profiler view [F] [options des applications : --frames N, --screenshot P.png, --wait S…]\n\n"
			  "Captures cherchées dans ~/.local/share/apps-lib/profiler/captures et ~/.local/share/apps-lib/diagnostics.");
}

std::string LatestOr(const std::string &given) {
	if (!given.empty())
		return given;
	const auto all = FindCaptures();
	return all.empty() ? std::string() : all.front().string();
}

struct Args {
	std::vector<std::string> positional;
	std::string out, chrome, thread;
	double from = 0, to = 0, fps = 60, duration = 0;
	size_t top = 25;
	std::vector<std::string> program;
};

Args Parse(int argc, char **argv, int start) {
	Args a;
	for (int i = start; i < argc; ++i) {
		const std::string_view s = argv[i];
		auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (s == "--") {
			for (++i; i < argc; ++i)
				a.program.emplace_back(argv[i]);
			break;
		}
		if (s == "--out") a.out = next();
		else if (s == "--chrome") a.chrome = next();
		else if (s == "--thread") a.thread = next();
		else if (s == "--from") a.from = std::atof(next().c_str());
		else if (s == "--to") a.to = std::atof(next().c_str());
		else if (s == "--target-fps") a.fps = std::atof(next().c_str());
		else if (s == "--top") a.top = size_t(std::atoi(next().c_str()));
		else if (s == "--duration") a.duration = std::atof(next().c_str());
		else a.positional.emplace_back(s);
	}
	return a;
}

int Record(const Args &a) {
	if (a.program.empty()) {
		std::fputs("record : programme manquant (profiler record -- ./build/host/release/bin/voxelia …)\n", stderr);
		return 2;
	}
	std::error_code ec;
	fs::create_directories(CaptureDir(), ec);
	const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
	const std::string out = a.out.empty() ? (CaptureDir() / std::format("{}_{}.vcap", fs::path(a.program[0]).filename().string(), stamp)).string() : a.out;
	fs::remove(out, ec);
	// Le traceur du programme lit ces variables au démarrage (environnement hérité).
	sdl3::env::Set("DIAG_TRACE_FILE", fs::absolute(out).string().c_str());
	if (a.duration > 0) {
		const std::string d = std::format("{}", a.duration);
		sdl3::env::Set("DIAG_TRACE_DURATION", d.c_str());
	}
	std::vector<String> args(a.program.begin(), a.program.end());
	auto proc = sdl3::Process::Create(args, false);
	if (!proc) {
		std::fprintf(stderr, "record : lancement impossible : %s\n", proc.Error().CStr());
		return 1;
	}
	std::printf("capture de %s → %s\n", a.program[0].c_str(), out.c_str());
	// Avec --duration, le programme écrit sa capture au bout de S secondes puis continue :
	// on attend le fichier et on arrête le programme. Sinon : jusqu'à sa sortie.
	for (;;) {
		if (auto code = proc.Value().Wait(false)) {
			std::printf("programme terminé (code %d)\n", *code);
			break;
		}
		if (a.duration > 0 && fs::exists(out, ec)) {
			const auto size = fs::file_size(out, ec);
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
			if (fs::file_size(out, ec) == size) {
				(void)proc.Value().Kill(false);
				(void)proc.Value().Wait(true);
				break;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (!fs::exists(out, ec)) {
		std::fputs("record : aucune capture produite (le programme est-il instrumenté avec DIAG_FRAME ?)\n", stderr);
		return 1;
	}
	std::printf("capture écrite : %s (%.1f Ko)\n", out.c_str(), double(fs::file_size(out, ec)) / 1024.0);
	return 0;
}

std::optional<diagnostics::capture::Capture> LoadOrReport(const std::string &path) {
	if (path.empty()) {
		std::fputs("aucune capture (voir « profiler record »)\n", stderr);
		return std::nullopt;
	}
	auto c = diagnostics::capture::Load(path);
	if (!c) {
		std::fprintf(stderr, "%s : %s\n", path.c_str(), c.Error().c_str());
		return std::nullopt;
	}
	return std::move(c.Value());
}

} // namespace

int main(int argc, char **argv) {
	const std::string cmd = argc > 1 ? argv[1] : "view";
	if (cmd == "--help" || cmd == "-h" || cmd == "help") {
		Usage();
		return 0;
	}
	const bool known = cmd == "record" || cmd == "list" || cmd == "report" || cmd == "compare" || cmd == "export" || cmd == "view";
	const Args a = Parse(argc, argv, known ? 2 : 1);
	if (cmd == "record")
		return Record(a);
	if (cmd == "list") {
		for (const fs::path &dir : CaptureDirectories())
			std::printf("dossier : %s\n", dir.string().c_str());
		for (const auto &p : FindCaptures()) {
			std::error_code ec;
			const Companions c = CompanionsOf(p);
			std::printf("%s  (%.1f Ko)%s\n", p.string().c_str(), double(fs::file_size(p, ec)) / 1024.0, c.incident.empty() ? "" : "  [incident]");
		}
		return 0;
	}
	if (cmd == "report") {
		const std::string path = LatestOr(a.positional.empty() ? "" : a.positional[0]);
		auto c = LoadOrReport(path);
		if (!c)
			return 1;
		AnalyzeOptions o{a.from, a.to, a.fps, a.thread};
		std::fputs(FormatReport(Analyze(*c, o), a.top).c_str(), stdout);
		if (const Companions comp = CompanionsOf(path); !comp.incident.empty())
			std::printf("\n── Incident (%s) ──\n%s", comp.incident.string().c_str(), ReadText(comp.incident).c_str());
		return 0;
	}
	if (cmd == "compare") {
		if (a.positional.size() < 2) {
			Usage();
			return 2;
		}
		auto before = LoadOrReport(a.positional[0]), after = LoadOrReport(a.positional[1]);
		if (!before || !after)
			return 1;
		AnalyzeOptions o;
		o.targetFps = a.fps;
		std::fputs(CompareReports(Analyze(*before, o), Analyze(*after, o), a.top).c_str(), stdout);
		return 0;
	}
	if (cmd == "export") {
		auto c = LoadOrReport(LatestOr(a.positional.empty() ? "" : a.positional[0]));
		if (!c || a.chrome.empty()) {
			if (a.chrome.empty())
				std::fputs("export : --chrome SORTIE.json requis\n", stderr);
			return 1;
		}
		std::ofstream f(a.chrome, std::ios::binary);
		const std::string json = ExportChromeTrace(*c);
		f.write(json.data(), std::streamsize(json.size()));
		std::printf("Chrome Trace écrit : %s (ouvrir dans ui.perfetto.dev)\n", a.chrome.c_str());
		return f ? 0 : 1;
	}
	// view (par défaut) : `profiler`, `profiler F.vcap`, `profiler view F` ; les options communes
	// des applications (--frames, --screenshot, --wait, --resize-test…) sont lues par AppConfig.
	app::AppConfig config;
	std::string path;
	for (const std::string &arg : config.ParseArgs(argc, argv))
		if (arg != "view" && !arg.starts_with("--"))
			path = arg;
	return RunViewer(config, LatestOr(path));
}
