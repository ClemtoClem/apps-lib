// Tests du profiler : arbre des appels, catalogue des captures (dossier des diagnostics).
#include <cpp-maker/test.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "analysis.hpp"
#include "catalog.hpp"

using namespace profiler;

namespace {

/// Capture synthétique : un fil, zones (nom, début ns, fin ns, profondeur) déjà ordonnées.
diagnostics::capture::Capture Synthetic() {
	diagnostics::capture::Capture c;
	c.names = {"Frame", "Update", "Render", "Physique"};
	c.durationNs = 40'000'000;
	diagnostics::capture::ThreadTrace t;
	t.id = 1;
	t.name = "main";
	// Deux images : Frame { Update { Physique }, Render }.
	t.zones = {{0, 0, 10'000'000, 0},		   {1, 0, 6'000'000, 1},		  {3, 1'000'000, 4'000'000, 2}, {2, 6'000'000, 10'000'000, 1},
			   {0, 20'000'000, 32'000'000, 0}, {1, 20'000'000, 28'000'000, 1}, {3, 21'000'000, 26'000'000, 2}, {2, 28'000'000, 32'000'000, 1}};
	c.threads.push_back(t);
	return c;
}

} // namespace

TEST(CallTree_AggregatesByCallPath) {
	const auto trees = BuildCallTrees(Synthetic());
	REQUIRE(trees.size() == 1u);
	const CallTree &tree = trees[0];
	CHECK_EQ(tree.thread, std::string("main"));
	REQUIRE(tree.roots.size() == 1u);
	const CallNode &frame = tree.roots[0];
	CHECK_EQ(frame.name, std::string("Frame"));
	CHECK_EQ(frame.calls, 2u);
	CHECK_NEAR(frame.totalMs, 22.0, 1e-9);
	CHECK_NEAR(frame.selfMs, 0.0, 1e-9); // entièrement couvert par Update et Render
	CHECK_NEAR(frame.minMs, 10.0, 1e-9);
	CHECK_NEAR(frame.maxMs, 12.0, 1e-9);
	REQUIRE(frame.children.size() == 2u);
	const CallNode &update = frame.children[0];
	CHECK_EQ(update.name, std::string("Update"));
	CHECK_NEAR(update.totalMs, 14.0, 1e-9);
	CHECK_NEAR(update.selfMs, 6.0, 1e-9); // 14 − (3 + 5) de physique
	REQUIRE(update.children.size() == 1u);
	CHECK_NEAR(update.children[0].AvgMs(), 4.0, 1e-9);
	CHECK_NEAR(tree.busyMs, 22.0, 1e-9);
	// Plage : seule la seconde image.
	const auto second = BuildCallTrees(Synthetic(), 15.0, 40.0);
	REQUIRE(second.size() == 1u);
	CHECK_EQ(second[0].roots[0].calls, 1u);
	CHECK_NEAR(second[0].roots[0].totalMs, 12.0, 1e-9);
}

TEST(Catalog_FindsSessionCompanions) {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / std::format("profiler_test_{}", std::chrono::steady_clock::now().time_since_epoch().count());
	fs::create_directories(dir);
	const fs::path capture = dir / "jeu_20261121-120000_42.vcap";
	std::ofstream(capture) << "x";
	std::ofstream(dir / "jeu_20261121-120000_42.crash.txt") << "blocage";
	const Companions c = CompanionsOf(capture);
	CHECK(!c.incident.empty());
	CHECK(c.log.empty()); // pas de journal pour celle-ci
	CHECK_EQ(ReadText(c.incident), std::string("blocage"));
	CHECK_EQ(ReadText(c.incident, 3), std::string("age")); // fin seulement
	// Le dossier des diagnostics fait partie des dossiers explorés.
	bool diagnostics = false;
	for (const fs::path &d : CaptureDirectories())
		diagnostics = diagnostics || d.string().find("diagnostics") != std::string::npos;
	CHECK(diagnostics);
	fs::remove_all(dir);
}
