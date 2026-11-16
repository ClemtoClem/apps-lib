#pragma once
/**
 * Wrapper C++ de SDL3/SDL_log.h — namespace les constantes de
 * SDL_LogPriority/SDL_LogCategory (cf. PixelFormat::), et ajoute deux
 * briques absentes de la SDL brute :
 *
 *  - LogRouter : un unique SDL_LogOutputFunction process-wide qui fan-out
 *    vers N sink C++ (std::function), pour que plusieurs consommateurs
 *    (console UI, export fichier...) puissent coexister sans se marcher
 *    dessus en réinstallant chacun leur propre callback SDL.
 *  - FileLogSink : écrit les messages dans un fichier depuis un thread dédié
 *    (std::jthread + file d'attente), pour que l'appelant (SDL_Log* — donc
 *    potentiellement le thread audio/rendu/réseau) ne bloque jamais sur de
 *    l'I/O disque.
 *
 * Inspiré de core::Logger/FileSink (SDL3_ui/src/Logger), adapté pour
 * s'appuyer sur le système de logging de SDL3 lui-même (catégories/priorités/
 * SDL_Log*) plutôt que de le dupliquer.
 */
#include <SDL3/SDL.h>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <core/core.hpp>
#include "sdl3/core/error.hpp"

namespace sdl3 {

/// Priorité d'un message de log.
enum class LogPriority: int {
    Invalid = SDL_LOG_PRIORITY_INVALID, ///< invalide
    Trace   = SDL_LOG_PRIORITY_TRACE,	///< trace
    Verbose = SDL_LOG_PRIORITY_VERBOSE, ///< verbeux
    Debug   = SDL_LOG_PRIORITY_DEBUG,	///< débogage
    Info    = SDL_LOG_PRIORITY_INFO,	///< information
    Warn    = SDL_LOG_PRIORITY_WARN,	///< avertissement
    Error   = SDL_LOG_PRIORITY_ERROR,	///< erreur
    Critical = SDL_LOG_PRIORITY_CRITICAL, ///< critique
    Count   = SDL_LOG_PRIORITY_COUNT	///< nombre de priorités
};

/// Catégorie d'un message de log.
enum class LogCategory: int {
    App     = SDL_LOG_CATEGORY_APPLICATION, ///< application
    Error   = SDL_LOG_CATEGORY_ERROR,		///< erreur
    Assert  = SDL_LOG_CATEGORY_ASSERT,		///< assertion
    System  = SDL_LOG_CATEGORY_SYSTEM,		///< système
    Audio   = SDL_LOG_CATEGORY_AUDIO,		///< audio
    Video   = SDL_LOG_CATEGORY_VIDEO,		///< vidéo
    Render  = SDL_LOG_CATEGORY_RENDER,		///< rendu
    Input   = SDL_LOG_CATEGORY_INPUT,		///< entrée
    Test    = SDL_LOG_CATEGORY_TEST,		///< test
    Gpu     = SDL_LOG_CATEGORY_GPU,			///< GPU
    Custom  = SDL_LOG_CATEGORY_CUSTOM		///< personnalisée
};

/// Nom lisible d'une priorité de log.
[[nodiscard]] StringView LogPriorityToString(LogPriority p) noexcept;

/// Horodatage courant formaté pour un journal.
[[nodiscard]] String LogTimestamp();

// ── Priorités / catégories (fines enveloppes de SDL_Log*) ──────────────────

/// Fixe la priorité minimale de tous les logs.
inline void SetLogPriorities(LogPriority p) noexcept { SDL_SetLogPriorities(static_cast<SDL_LogPriority>(p)); }
/// Fixe la priorité d'une catégorie.
inline void SetLogPriority(LogCategory c, LogPriority p) noexcept { SDL_SetLogPriority(static_cast<int>(c), static_cast<SDL_LogPriority>(p)); }
/// Lit la priorité d'une catégorie.
[[nodiscard]] inline LogPriority GetLogPriority(LogCategory c) noexcept { return static_cast<LogPriority>(SDL_GetLogPriority(static_cast<int>(c))); }
/// Réinitialise les priorités.
inline void ResetLogPriorities() noexcept { SDL_ResetLogPriorities(); }

/// Préfixe (hors format) écrit devant chaque message de la priorité `p`.
inline void SetLogPriorityPrefix(LogPriority p, const char *prefix) noexcept {
	SDL_SetLogPriorityPrefix(static_cast<SDL_LogPriority>(p), prefix);
}

// ── Log* — émission directe d'un message (SDL_log.h) ─────────────────────────

// Le format est par nature non littéral ici.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wdouble-promotion"

/// Émet un message avec catégorie et priorité.
template <typename... Args> inline void Log(LogCategory category, LogPriority priority, const char *fmt, Args &&...args) noexcept {
	SDL_LogMessage(static_cast<int>(category), static_cast<SDL_LogPriority>(priority), fmt, std::forward<Args>(args)...);
}
/// Émet un message (catégorie application, priorité info).
template <typename... Args> inline void Log(const char *fmt, Args &&...args) noexcept { SDL_Log(fmt, std::forward<Args>(args)...); }
/// Émet un message de trace.
template <typename... Args> inline void LogTrace(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogTrace(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
/// Émet un message verbeux.
template <typename... Args> inline void LogVerbose(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogVerbose(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
/// Émet un message de débogage.
template <typename... Args> inline void LogDebug(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogDebug(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
/// Émet un message d'information.
template <typename... Args> inline void LogInfo(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogInfo(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
/// Émet un avertissement.
template <typename... Args> inline void LogWarn(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogWarn(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
/// Émet une erreur.
template <typename... Args> inline void LogError(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogError(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
/// Émet une erreur critique.
template <typename... Args> inline void LogCritical(LogCategory c, const char *fmt, Args &&...args) noexcept {
	SDL_LogCritical(static_cast<int>(c), fmt, std::forward<Args>(args)...);
}
#pragma GCC diagnostic pop

/// Handler de sortie par défaut de SDL (écrit sur la console).
[[nodiscard]] inline SDL_LogOutputFunction GetDefaultLogOutputFunction() noexcept {
	return SDL_GetDefaultLogOutputFunction();
}

/// Un message tel que délivré aux sinks de LogRouter. `text` n'est valide que
/// pour la durée de l'appel du sink (vue sur le buffer interne de SDL) — un
/// sink qui doit le conserver au-delà (ex: FileLogSink, qui le transmet à un
/// thread) doit le copier en String immédiatement.
struct LogMessage {
    LogCategory category;
    LogPriority priority;
    StringView text;
};

/// Un consommateur de messages de log, enregistré auprès de LogRouter.
using LogSink = std::function<void(const LogMessage &)>;

// ============================================================================
// LogRouter — un seul SDL_LogOutputFunction process-wide, fan-out vers N sinks
// ============================================================================

class LogRouter {
public:
    /// Instance unique (singleton) du routeur.
    [[nodiscard]] static LogRouter &Instance();

    /// Enregistre un sink ; retourne un identifiant à repasser à removeSink().
    size_t AddSink(LogSink sink);

    /// Retire un sink.
    void RemoveSink(size_t id);

    /// Le SDL_LogOutputFunction installé avant que LogRouter ne prenne la
    /// main (généralement le handler par défaut de SDL, qui écrit sur la
    /// console) — exposé pour les sinks qui veulent y chaîner explicitement
    /// (cf. Application::buildLogging, activé seulement si l'utilisateur
    /// active l'écho console dans les réglages : l'écriture console
    /// synchrone est justement ce qui ralentit l'exécution par défaut).
    [[nodiscard]] SDL_LogOutputFunction PreviousHandler() const noexcept { return m_prevFn; }
    [[nodiscard]] void *PreviousHandlerData() const noexcept { return m_prevData; }

private:
    LogRouter();
    // Volontairement jamais désinstallé : LogRouter vit pour la durée du
    // process (singleton), donc restaurer le handler précédent au dtor
    // n'aurait jamais l'occasion de s'exécuter avant la sortie du process.
    ~LogRouter() = default;

    LogRouter(const LogRouter &) = delete;
    LogRouter &operator=(const LogRouter &) = delete;

    static void SDLCALL Trampoline(void *userdata, int category, SDL_LogPriority priority, const char *message);

    std::mutex m_mutex;
    std::unordered_map<size_t, LogSink> m_sinks;
    size_t m_nextId = 1;
    SDL_LogOutputFunction m_prevFn = nullptr;
    void *m_prevData = nullptr;
};

// ============================================================================
// FileLogSink — écrit les messages dans un fichier depuis un thread dédié
// ============================================================================

/**
 * @brief Sink de log fichier asynchrone : write() copie/enfile le message et
 * revient immédiatement (jamais d'I/O sur le thread appelant) ; un unique
 * thread dédié dépile et écrit sur disque.
 *
 * Non copiable ni déplaçable : le thread de fond capture `this` par
 * référence. Détruire l'objet arrête le thread (std::jthread::request_stop),
 * réveille la file d'attente et joint — les messages déjà enfilés sont
 * garantis écrits avant le retour du destructeur.
 */
class FileLogSink {
public:
    [[nodiscard]] static Result<std::unique_ptr<FileLogSink>, Error> Create(const String &path);

    ~FileLogSink();

    FileLogSink(const FileLogSink &) = delete;
    FileLogSink &operator=(const FileLogSink &) = delete;
    FileLogSink(FileLogSink &&) = delete;
    FileLogSink &operator=(FileLogSink &&) = delete;

    /// Non-bloquant : copie le message formaté dans la file et revient.
    void Write(const LogMessage &msg);

    [[nodiscard]] const String &Path() const noexcept { return m_path; }

private:
    FileLogSink(String path, std::ofstream file)
        : m_path(std::move(path)), m_file(std::move(file)), m_thread([this](std::stop_token st) { ThreadLoop(st); }) {}

    void ThreadLoop(std::stop_token st);

    String m_path;
    std::ofstream m_file;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<String> m_queue;
    std::jthread m_thread; // déclaré en dernier : démarre après le reste, s'arrête/joint avant
};

} // namespace sdl3
