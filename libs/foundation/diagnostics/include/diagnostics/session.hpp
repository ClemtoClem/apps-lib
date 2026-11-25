#pragma once
/**
 * @file session.hpp
 * @brief Session de diagnostic d'une application : de quoi comprendre après coup un
 *        plantage ou un blocage, sans débogueur.
 *
 * `Start` ouvre, dans `DefaultDirectory()` (celui qu'explore le profiler), trois fichiers
 * partageant un nom de base `<app>_<date>_<pid>` :
 * - `.vcap` : la trace (`DIAG_ZONE`, `DIAG_COUNTER`, `DIAG_FRAME`), réécrite toutes les
 *   `checkpointSeconds` : un plantage ne perd que les dernières secondes ;
 * - `.log` : copie du journal (`core::log`), vidée ligne par ligne ;
 * - `.crash.txt`, seulement en cas d'incident :
 *   - plantage (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS) : signal, adresse fautive, état des
 *     fils (zones ouvertes, derniers compteurs), pile d'appels du fil fautif ;
 *   - blocage (aucune image pendant `hangSeconds`, détecté par un fil de garde) : même état,
 *     et pile d'appels du fil principal là où il est arrêté. Le rapport est complété si les
 *     images reprennent.
 *
 * Les piles d'appels donnent `binaire(+décalage)` : `addr2line -f -C -e binaire décalage`
 * retrouve la fonction et la ligne (binaire compilé avec `-g`). Le gestionnaire de plantage
 * fait de son mieux : l'état d'un processus qui plante n'est pas garanti.
 */
#include <string>

namespace diagnostics::session {

/// `$XDG_DATA_HOME/apps-lib/diagnostics`, sinon `~/.local/share/apps-lib/diagnostics`.
[[nodiscard]] std::string DefaultDirectory();

struct Options {
	std::string app;				 ///< nom de l'application (préfixe des fichiers)
	std::string directory;			 ///< vide : `DefaultDirectory()`
	double checkpointSeconds = 10.0; ///< intervalle de réécriture de la trace (0 : jamais)
	double hangSeconds = 5.0;		 ///< délai sans image avant un rapport de blocage (0 : aucun)
	bool logFile = true;			 ///< recopie le journal dans `.log`
};

/// Démarre la session (une seule à la fois ; appelée depuis le fil principal, dont la pile
/// sera relevée en cas de blocage). Si une trace est déjà active (`profiler record`), elle
/// est conservée telle quelle. Rend le chemin de base des fichiers, vide en cas d'échec.
std::string Start(const Options &options);
/// Termine la session : trace finale, fil de garde arrêté, journal rendu à son puits.
void Stop();
/// Chemin de base de la session en cours (sans extension), vide sinon.
[[nodiscard]] std::string BasePath();
/// Mémoire résidente du processus (Mo), 0 si inconnue.
[[nodiscard]] double ResidentMemoryMb() noexcept;

} // namespace diagnostics::session
