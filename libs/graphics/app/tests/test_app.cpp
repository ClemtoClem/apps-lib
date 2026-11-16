// Tests de graphics.app : horloge à pas fixe (durées synthétiques) et options communes
// de la ligne de commande. La boucle elle-même est vérifiée par les exemples
// (`--frames=N --screenshot=F.png`), qui demandent un affichage.
#include <cpp-maker/test.hpp>

#include <cmath>

#include <app/app.hpp>

TEST(Clock_StepsAndAlpha) {
	app::FixedStepClock clock({.stepSeconds = 0.01, .maxFrameTime = 1.0, .maxStepsPerFrame = 100});
	CHECK_EQ(clock.Advance(0.005), 0);
	CHECK(std::abs(clock.Alpha() - 0.5) < 1e-9);
	CHECK_EQ(clock.Advance(0.007), 1); // 0,012 s cumulées : un pas, reste 0,002
	CHECK(std::abs(clock.Alpha() - 0.2) < 1e-6);
	CHECK_EQ(clock.Advance(0.05), 5);
	CHECK_EQ(clock.TotalSteps(), 6u);
	CHECK(std::abs(clock.SimulatedSeconds() - 0.06) < 1e-9);
	CHECK(clock.Advance(-1.0) == 0); // durée négative ignorée
	CHECK_EQ(clock.DroppedSeconds(), 0.0);
}

TEST(Clock_SpiralOfDeathIsBounded) {
	// Durées exactes en binaire (1/16 s) : aucune erreur d'arrondi dans les attentes.
	app::FixedStepClock clock({.stepSeconds = 0.0625, .maxFrameTime = 0.5, .maxStepsPerFrame = 4});
	// Image aberrante (débogueur) : bornée à 0,5 s, puis à 4 pas ; le reste est abandonné.
	CHECK_EQ(clock.Advance(3.0), 4);
	CHECK_EQ(clock.Alpha(), 0.0);
	CHECK_EQ(clock.DroppedSeconds(), 3.0 - 4 * 0.0625);
	CHECK_EQ(clock.Advance(0.125), 2); // la cadence normale reprend aussitôt
	clock.Reset();
	CHECK_EQ(clock.TotalSteps(), 0u);
	CHECK_EQ(clock.DroppedSeconds(), 0.0);
	CHECK_EQ(clock.Alpha(), 0.0);
}

TEST(Config_ParseArgs) {
	const char *argv[] = {"demo", "--frames=120", "fichier.txt", "--screenshot", "out/capture.png", "--verbose", "--frames", "7"};
	app::AppConfig config;
	const std::vector<std::string> rest = config.ParseArgs(8, argv);
	CHECK_EQ(config.maxFrames, 7u); // la dernière occurrence l'emporte
	CHECK_EQ(config.screenshot.string(), std::string("out/capture.png"));
	REQUIRE(rest.size() == 2u);
	CHECK_EQ(rest[0], std::string("fichier.txt"));
	CHECK_EQ(rest[1], std::string("--verbose"));

	const char *none[] = {"demo"};
	app::AppConfig defaults;
	CHECK(defaults.ParseArgs(1, none).empty());
	CHECK_EQ(defaults.maxFrames, 0u);
	CHECK(defaults.screenshot.empty());
}

TEST(Theme_Midnight) {
	const ui::Theme theme = app::MidnightTheme();
	CHECK_EQ(theme.accent.b, 255);
	CHECK(theme.background.r < 40 && theme.background.b < 40);
	CHECK_EQ(theme.accentText.r, 255); // texte blanc sur l'accent bleu
}
