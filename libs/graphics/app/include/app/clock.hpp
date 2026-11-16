#pragma once
/**
 * @file clock.hpp
 * @brief Horloge à pas fixe (« Fix Your Timestep ») : la simulation avance par pas
 *        constants, découplée de la cadence d'affichage.
 *
 * Chaque image, `Advance(durée réelle)` rend le nombre de pas à simuler ; `Alpha()` donne
 * la fraction du pas suivant déjà écoulée, pour interpoler le rendu entre les deux
 * derniers états. Une durée aberrante (débogueur, fenêtre déplacée) est bornée par
 * `maxFrameTime`, et le nombre de pas par image par `maxStepsPerFrame`, ce qui évite la
 * « spirale de la mort » ; le temps ainsi abandonné est compté (`DroppedSeconds`).
 *
 * Logique pure, sans SDL : testable avec des durées synthétiques.
 */
#include <cstdint>

namespace app {

class FixedStepClock {
public:
	struct Config {
		double stepSeconds = 1.0 / 60.0;
		double maxFrameTime = 0.25;
		int maxStepsPerFrame = 8;
	};

	FixedStepClock() = default;
	explicit FixedStepClock(Config config) noexcept : m_config(config) {}

	/// Ajoute `frameSeconds` de temps réel ; rend le nombre de pas fixes à exécuter.
	int Advance(double frameSeconds) noexcept;

	/// Fraction [0, 1) du pas suivant déjà écoulée (interpolation du rendu).
	[[nodiscard]] double Alpha() const noexcept { return m_accumulator / m_config.stepSeconds; }
	[[nodiscard]] double StepSeconds() const noexcept { return m_config.stepSeconds; }
	/// Temps simulé total (pas exécutés × durée d'un pas).
	[[nodiscard]] double SimulatedSeconds() const noexcept { return double(m_steps) * m_config.stepSeconds; }
	[[nodiscard]] uint64_t TotalSteps() const noexcept { return m_steps; }
	/// Temps réel abandonné parce qu'au-delà des bornes (retard non rattrapable).
	[[nodiscard]] double DroppedSeconds() const noexcept { return m_dropped; }

	void Reset() noexcept;

private:
	Config m_config;
	double m_accumulator = 0.0;
	double m_dropped = 0.0;
	uint64_t m_steps = 0;
};

} // namespace app
