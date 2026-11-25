#include <cpp-maker/test.hpp>

#include <filesystem>
#include <thread>

#include <diagnostics/assert.hpp>
#include <diagnostics/capture.hpp>
#include <diagnostics/trace.hpp>

TEST(Trace_RecordAndParse) {
	const auto path = (std::filesystem::temp_directory_path() / "apps_lib_trace_test.vcap").string();
	REQUIRE(diagnostics::trace::Start(path));
	DIAG_THREAD_NAME("main");
	for (int f = 0; f < 3; ++f) {
		DIAG_ZONE("Frame");
		{
			DIAG_ZONE("Update");
			DIAG_COUNTER("entities", 100 + f);
		}
		std::jthread worker([] {
			DIAG_THREAD_NAME("worker");
			DIAG_ZONE("Job");
		});
		worker.join();
		DIAG_FRAME();
	}
	REQUIRE(diagnostics::trace::Stop());
	auto cap = diagnostics::capture::Load(path);
	REQUIRE(cap.IsOk());
	const auto &c = cap.Value();
	CHECK_EQ(c.frames.size(), 3u);
	size_t zones = 0, updates = 0;
	for (const auto &t : c.threads)
		for (const auto &z : t.zones) {
			++zones;
			if (c.Name(z.name) == "Update") {
				++updates;
				CHECK_EQ(z.depth, 1u);
			}
		}
	CHECK_EQ(updates, 3u);
	CHECK_GE(zones, 9u);
	std::filesystem::remove(path);
}

TEST(Assert_Verify) {
	CHECK_FALSE(DIAG_VERIFY(1 == 2, "attendu {}", 3));
	CHECK(DIAG_VERIFY(true, "ok"));
}

// ── Session : trace réécrite en cours de route, journal, rapports de blocage et de plantage ──

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include <core/log.hpp>
#include <diagnostics/capture.hpp>
#include <diagnostics/session.hpp>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path TempDir(const char *name) {
	const auto dir = std::filesystem::temp_directory_path() / std::format("diag_test_{}_{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
	std::filesystem::create_directories(dir);
	return dir;
}

std::string ReadText(const std::filesystem::path &p) {
	std::ifstream f(p);
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

} // namespace

TEST(Session_CheckpointsTraceAndLog) {
	const auto dir = TempDir("session");
	diagnostics::session::Options o;
	o.app = "essai";
	o.directory = dir.string();
	o.checkpointSeconds = 0.2;
	o.hangSeconds = 0.0;
	const std::string base = diagnostics::session::Start(o);
	REQUIRE(!base.empty());
	CHECK_EQ(diagnostics::session::BasePath(), base);
	core::log::Info("message de la session");
	for (int i = 0; i < 5; ++i) {
		DIAG_ZONE("Session::Image");
		DIAG_COUNTER("valeur", i);
		DIAG_FRAME();
	}
	// La trace est écrite pendant que la session continue (point de sauvegarde).
	std::this_thread::sleep_for(std::chrono::milliseconds(700));
	auto partial = diagnostics::capture::Load(base + ".vcap");
	REQUIRE(partial.IsOk());
	CHECK(partial.Value().frames.size() >= 5u);
	diagnostics::session::Stop();
	CHECK(diagnostics::session::BasePath().empty());
	CHECK(ReadText(base + ".log").find("message de la session") != std::string::npos);
	CHECK(!std::filesystem::exists(base + ".crash.txt")); // aucun incident
	CHECK(diagnostics::session::ResidentMemoryMb() > 0.0);
	std::filesystem::remove_all(dir);
}

#if defined(__linux__)
TEST(Session_ReportsHangWithOpenZonesAndStack) {
	const auto dir = TempDir("hang");
	diagnostics::session::Options o;
	o.app = "gel";
	o.directory = dir.string();
	o.checkpointSeconds = 0.0;
	o.hangSeconds = 0.5;
	o.logFile = false;
	const std::string base = diagnostics::session::Start(o);
	REQUIRE(!base.empty());
	DIAG_FRAME();
	{
		DIAG_ZONE("Boucle::Bloquee");
		std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // aucune image : blocage
	}
	DIAG_FRAME(); // reprise
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	diagnostics::session::Stop();
	const std::string report = ReadText(base + ".crash.txt");
	CHECK(report.find("blocage") != std::string::npos);
	CHECK(report.find("Boucle::Bloquee") != std::string::npos); // zone ouverte au moment du gel
	CHECK(report.find("Pile d'appels du fil principal") != std::string::npos);
	CHECK(report.find("(+0x") != std::string::npos || report.find("[0x") != std::string::npos); // pile relevée
	CHECK(report.find("reprise") != std::string::npos);
	std::filesystem::remove_all(dir);
}

TEST(Session_ReportsCrashFromChildProcess) {
	const auto dir = TempDir("crash");
	const pid_t child = ::fork();
	REQUIRE(child >= 0);
	if (child == 0) { // processus enfant : session, zone ouverte, accès invalide
		diagnostics::session::Options o;
		o.app = "plantage";
		o.directory = dir.string();
		o.hangSeconds = 0.0;
		o.logFile = false;
		(void)diagnostics::session::Start(o);
		DIAG_FRAME();
		DIAG_ZONE("Jeu::Accident");
		volatile int *null = nullptr;
		*null = 42;
		::_exit(0);
	}
	int status = 0;
	::waitpid(child, &status, 0);
	CHECK(WIFSIGNALED(status));
	std::string report;
	for (const auto &e : std::filesystem::directory_iterator(dir))
		if (e.path().string().ends_with(".crash.txt"))
			report = ReadText(e.path());
	CHECK(report.find("plantage") != std::string::npos);
	CHECK(report.find("SIGSEGV") != std::string::npos);
	CHECK(report.find("Jeu::Accident") != std::string::npos);
	bool trace = false; // dernière trace écrite par le gestionnaire
	for (const auto &e : std::filesystem::directory_iterator(dir))
		trace = trace || e.path().extension() == ".vcap";
	CHECK(trace);
	std::filesystem::remove_all(dir);
}
#endif
