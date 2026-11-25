#pragma once
/**
 * @file trace.hpp
 * @brief Traceur de performances à très faible coût pour le profiler.
 *
 * Instrumentation :
 * ```
 * DIAG_THREAD_NAME("main");
 * DIAG_ZONE("World::Update");          // portée mesurée (début/fin)
 * DIAG_COUNTER("chunks.loaded", n);    // série de valeurs
 * DIAG_FRAME();                        // fin d'image
 * ```
 * - Désactivé : chaque macro coûte une lecture atomique relâchée.
 * - Activé : chaque thread écrit dans **son** tampon (aucun verrou partagé, aucune
 *   allocation tant que le bloc de 64 Ki événements n'est pas plein).
 * - Les noms sont interneés une seule fois par site d'appel (`static`).
 * - Activation : variable d'environnement `DIAG_TRACE_FILE=chemin.vcap` (le
 *   profiler la positionne), ou `trace::Start(chemin)`. Écriture à `Stop()`, à la
 *   sortie du processus, ou sur réception de `SIGUSR2` (capture pilotée).
 * - `Checkpoint()` réécrit la capture en cours sans l'interrompre (fichier remplacé d'un
 *   bloc) : un plantage ne perd que ce qui a suivi. `diagnostics::session` (session.hpp)
 *   l'appelle périodiquement et ajoute rapports de plantage et de blocage.
 */
#include <cstdint>
#include <string>
#include <string_view>

namespace diagnostics::trace {

enum class EventType : uint8_t { Begin = 0, End = 1, Counter = 2, Frame = 3 };

struct Event {
	int64_t timeNs;
	uint32_t name;
	EventType type;
	double value;
};

[[nodiscard]] bool Enabled() noexcept;
/// Démarre une capture qui sera écrite dans `path` au `Stop()`.
bool Start(const std::string &path);
/// Arrête et écrit la capture. Retourne vrai si un fichier a été écrit.
bool Stop();
/// Écrit la capture accumulée sans l'arrêter (remplacement atomique du fichier). Avec
/// `waitForLocks` faux, un fil dont le tampon est verrouillé est écrit vide au lieu
/// d'attendre (appel depuis un gestionnaire de plantage). Vrai si un fichier a été écrit.
bool Checkpoint(bool waitForLocks = true);
/// Chemin de la capture en cours (vide si inactive).
[[nodiscard]] std::string CurrentPath();
/// Horloge monotone du traceur (ns).
[[nodiscard]] int64_t NowNs() noexcept;
/// Instant de la dernière image (`DIAG_FRAME`, horloge `NowNs`), 0 avant la première.
[[nodiscard]] int64_t LastFrameNs() noexcept;
/// Nombre d'images marquées depuis le début du processus.
[[nodiscard]] uint64_t FrameCount() noexcept;
/// État lisible de chaque fil : zones ouvertes (de l'externe à l'interne), dernière valeur
/// de chaque compteur, nombre d'événements. `waitForLocks` faux : voir `Checkpoint`.
[[nodiscard]] std::string DescribeState(bool waitForLocks = true);
/// Lit `DIAG_TRACE_FILE` et installe le gestionnaire SIGUSR2 ; appelé au premier usage.
void InitFromEnvironment();
/// Appelé par DIAG_FRAME : gère aussi les demandes d'écriture reçues par signal.
void Frame();

[[nodiscard]] uint32_t Intern(std::string_view name);
void SetThreadName(std::string_view name);
void Record(EventType type, uint32_t name, double value = 0.0) noexcept;

class Zone {
	uint32_t m_name;
	bool m_active;

public:
	explicit Zone(uint32_t name) noexcept : m_name(name), m_active(Enabled()) {
		if (m_active)
			Record(EventType::Begin, m_name);
	}
	~Zone() {
		if (m_active)
			Record(EventType::End, m_name);
	}
	Zone(const Zone &) = delete;
	Zone &operator=(const Zone &) = delete;
};

} // namespace diagnostics::trace

#define DIAG_CONCAT_INNER(a, b) a##b
#define DIAG_CONCAT(a, b) DIAG_CONCAT_INNER(a, b)
#define DIAG_ZONE(name)                                                                                                 \
	static const uint32_t DIAG_CONCAT(diagZoneId_, __LINE__) = ::diagnostics::trace::Intern(name);                      \
	::diagnostics::trace::Zone DIAG_CONCAT(diagZone_, __LINE__)(DIAG_CONCAT(diagZoneId_, __LINE__))
#define DIAG_COUNTER(name, value)                                                                                       \
	do {                                                                                                                \
		if (::diagnostics::trace::Enabled()) {                                                                          \
			static const uint32_t diagCounterId = ::diagnostics::trace::Intern(name);                                   \
			::diagnostics::trace::Record(::diagnostics::trace::EventType::Counter, diagCounterId, static_cast<double>(value)); \
		}                                                                                                               \
	} while (0)
#define DIAG_FRAME() ::diagnostics::trace::Frame()
#define DIAG_THREAD_NAME(name) ::diagnostics::trace::SetThreadName(name)
