// Tests de graphics.app : horloge à pas fixe (durées synthétiques) et options communes
// de la ligne de commande. La boucle elle-même est vérifiée par les exemples
// (`--frames=N --screenshot=F.png`), qui demandent un affichage.
#include <vector>
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
	CHECK(defaults.recordDirectory.empty());
	CHECK_EQ(defaults.fixedFrameSeconds, 0.0);

	// Enregistrement image par image et pas de temps fixe (captures reproductibles).
	const char *rec[] = {"demo", "--record=out/frames", "--dt", "0.04", "--dt=-1x"};
	app::AppConfig recording;
	CHECK(recording.ParseArgs(4, rec).empty());
	CHECK_EQ(recording.recordDirectory.string(), std::string("out/frames"));
	CHECK_NEAR(recording.fixedFrameSeconds, 0.04, 1e-9);
	CHECK(recording.ParseArgs(5, rec).empty());
	CHECK_EQ(recording.fixedFrameSeconds, 0.0); // valeur négative : temps réel

	// Attente minimale avant capture et test de redimensionnement.
	const char *wait[] = {"demo", "--wait=10", "--resize-test", "out/resize"};
	app::AppConfig waiting;
	CHECK(waiting.ParseArgs(4, wait).empty());
	CHECK_NEAR(waiting.minSeconds, 10.0, 1e-9);
	CHECK_EQ(waiting.resizeTestDirectory.string(), std::string("out/resize"));
	CHECK_EQ(defaults.minSeconds, 0.0);
	CHECK(defaults.resizeTestDirectory.empty());
}

TEST(Theme_Midnight) {
	const ui::Theme theme = app::MidnightTheme();
	CHECK_EQ(theme.accent.b, 255);
	CHECK(theme.background.r < 40 && theme.background.b < 40);
	CHECK_EQ(theme.accentText.r, 255); // texte blanc sur l'accent bleu
}

TEST(ImageNoise_DetectsCorruptedFrames) {
	// Dégradé lisse (image rendue) contre bruit aléatoire (texture non initialisée).
	const int w = 64, h = 32;
	std::vector<uint8_t> smooth(size_t(w) * h * 4), noisy(smooth.size());
	uint32_t s = 12345;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			uint8_t *p = &smooth[(size_t(y) * w + size_t(x)) * 4];
			p[0] = uint8_t(x * 2), p[1] = uint8_t(y * 4), p[2] = 120, p[3] = 255;
			uint8_t *q = &noisy[(size_t(y) * w + size_t(x)) * 4];
			for (int c = 0; c < 4; ++c) {
				s = s * 1664525u + 1013904223u;
				q[c] = uint8_t(s >> 24);
			}
		}
	CHECK(app::ImageNoise(smooth, w, h) < 5.f);
	CHECK(app::ImageNoise(noisy, w, h) > app::kCorruptionThreshold);
	CHECK_EQ(app::ImageNoise({}, w, h), 0.f); // tampon trop court : pas d'accès hors limites
}
