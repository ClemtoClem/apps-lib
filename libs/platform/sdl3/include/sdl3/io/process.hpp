#pragma once
/**
 * @file process.hpp
 * @brief Sous-processus : lancement, entrées/sorties, attente, terminaison (SDL_process.h).
 */
#include <SDL3/SDL.h>
#include <utility>
#include <vector>

#include <core/core.hpp>
#include "sdl3/io/iostream.hpp"

namespace sdl3 {

// ============================================================================
// ProcessIO
// ============================================================================

/// Mode de gestion des flux standard (stdin/stdout/stderr) d'un sous-processus.
enum class ProcessIO: Uint8 {
	INHERITED = SDL_PROCESS_STDIO_INHERITED, ///< hérités du parent
	Null	   = SDL_PROCESS_STDIO_NULL,	   ///< déconnectés (/dev/null)
	APP		   = SDL_PROCESS_STDIO_APP,	   ///< reliés à des SDL_IOStream de l'application
	REDIRECT   = SDL_PROCESS_STDIO_REDIRECT, ///< redirigés vers un fichier
};

namespace detail {
	constexpr SDL_ProcessIO ToSDL(ProcessIO e) {
		return static_cast<SDL_ProcessIO>(e);
	}
}

// ============================================================================
// ProcessOutput — résultat de Process::read()
// ============================================================================

/// Sortie capturée d'un sous-processus terminé.
struct ProcessOutput {
	std::vector<uint8_t> data; ///< contenu complet de stdout
	int exitCode = 0;		   ///< code de sortie du processus
};

// ============================================================================
// Process — RAII SDL_Process (lancement de sous-processus)
// ============================================================================

/// Sous-processus lancé (RAII sur SDL_Process).
/// Paramètres d'un processus à lancer (`Process::Create(const ProcessOptions&)`).
struct ProcessOptions {
	std::vector<String> args;								 ///< args[0] cherché dans le PATH
	std::vector<std::pair<String, String>> environment;		 ///< variables ajoutées ou remplacées (environnement hérité)
	ProcessIO stdinMode = ProcessIO::Null;
	ProcessIO stdoutMode = ProcessIO::APP;					 ///< APP : lisible par Read()
	bool stderrToStdout = true;								 ///< sorties mêlées (journal d'une commande)
	bool background = false;								 ///< détaché : ni attente ni lecture
};

class Process : public Wrapper<SDL_Process, SDL_DestroyProcess> {
public:
	using Wrapper::Wrapper;

	/// Lance un sous-processus. `args[0]` est le chemin de l'exécutable.
	/// Si `pipeStdio` est vrai, stdin/stdout/stderr sont redirigés vers des
	/// SDL_IOStream accessibles via StdinStream()/StdoutStream().
	[[nodiscard]] static Result<Process, Error> Create(const std::vector<String> &args, bool pipeStdio = false);

	/// Lance un sous-processus avec un contrôle fin (répertoire de travail,
	/// redirections individuelles, environnement…) via un SDL_PropertiesID
	/// préparé par l'appelant (SDL_PROP_PROCESS_CREATE_*).
	[[nodiscard]] static Result<Process, StringView> CreateWithProperties(SDL_PropertiesID props);
	/// Processus d'après des options (environnement modifié, redirections).
	[[nodiscard]] static Result<Process, Error> Create(const ProcessOptions &options);
	/// Lance `options`, attend la fin et rend la sortie complète (bloquant) ; Err si le
	/// lancement échoue (programme absent…). Un code de sortie non nul n'est pas une erreur.
	[[nodiscard]] static Result<ProcessOutput, Error> Run(const ProcessOptions &options);

	/// Propriétés associées au processus.
	[[nodiscard]] SDL_PropertiesID Properties() const noexcept;

	/// Flux d'entrée du processus (prêté, valide tant que le Process existe).
	/// Non nul seulement si le process a été créé avec pipeStdio=true ou une redirection App.
	[[nodiscard]] Option<IOStreamView> StdinStream() const noexcept;
	/// Flux de sortie (stdout) du processus (prêté, mêmes conditions que StdinStream).
	[[nodiscard]] Option<IOStreamView> StdoutStream() const noexcept;

	/// Lit la sortie complète du process (bloquant jusqu'à la fin de l'exécution).
	/// Nécessite que le process ait été créé avec pipeStdio=true.
	[[nodiscard]] Result<ProcessOutput, StringView> Read();

	/// Termine le processus. `force` envoie un signal de terminaison immédiate.
	[[nodiscard]] bool Kill(bool force) noexcept { return mHandle && SDL_KillProcess(mHandle, force); }

	/// Attend la fin du processus.
	/// - block=true : bloque et retourne le code de sortie ;
	/// - block=false : sonde sans bloquer (NONE si le process tourne toujours).
	[[nodiscard]] Option<int> Wait(bool block) noexcept;
};

} // namespace sdl3
