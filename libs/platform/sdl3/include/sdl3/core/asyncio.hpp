#pragma once
/**
 * @file asyncio.hpp
 * @brief Entrées/sorties asynchrones (SDL_asyncio.h) : lecture/écriture/fermeture différées.
 */
#include <SDL3/SDL.h>

#include <core/core.hpp>

namespace sdl3 {

// ============================================================================
// AsyncIO — E/S asynchrone (SDL_asyncio.h)
// ============================================================================

/// Type d'une tâche d'E/S asynchrone.
enum class AsyncIOTaskType {
	READ  = SDL_ASYNCIO_TASK_READ,	///< lecture
	WRITE = SDL_ASYNCIO_TASK_WRITE, ///< écriture
	CLOSE = SDL_ASYNCIO_TASK_CLOSE, ///< fermeture
};

/// Résultat d'une tâche d'E/S asynchrone.
enum class AsyncIOResult {
	COMPLETE = SDL_ASYNCIO_COMPLETE, ///< terminée avec succès
	FAILURE  = SDL_ASYNCIO_FAILURE,	 ///< échec
	CANCELED = SDL_ASYNCIO_CANCELED, ///< annulée
};

/// Informations sur une tâche d'E/S asynchrone terminée.
struct AsyncIOOutcome {
	SDL_AsyncIO *asyncio = nullptr; ///< Source de la tâche (pointeur invalide si elle a été fermée).
	AsyncIOTaskType type = AsyncIOTaskType::READ; ///< type de la tâche
	AsyncIOResult result = AsyncIOResult::COMPLETE; ///< résultat
	void *buffer = nullptr; ///< tampon cible de la lecture/écriture
	Uint64 offset = 0;		///< décalage dans le fichier
	Uint64 bytesRequested = 0;	 ///< octets demandés
	Uint64 bytesTransferred = 0; ///< octets réellement transférés
	void *userdata = nullptr;	 ///< données utilisateur fournies au lancement

	/// Construit depuis un `SDL_AsyncIOOutcome` brut.
	constexpr explicit AsyncIOOutcome(const SDL_AsyncIOOutcome &o)
		: asyncio(o.asyncio)
		, type(static_cast<AsyncIOTaskType>(o.type))
		, result(static_cast<AsyncIOResult>(o.result))
		, buffer(o.buffer)
		, offset(o.offset)
		, bytesRequested(o.bytes_requested)
		, bytesTransferred(o.bytes_transferred)
		, userdata(o.userdata) {}
};

class AsyncIO;

// ============================================================================
// AsyncIOQueue — file de tâches d'E/S terminées (RAII SDL_AsyncIOQueue)
// ============================================================================

/// File de tâches d'E/S asynchrone terminées (RAII).
class AsyncIOQueue : public Wrapper<SDL_AsyncIOQueue, SDL_DestroyAsyncIOQueue> {
public:
	using Wrapper::Wrapper;

	/// Crée une nouvelle file (retourne une erreur si l'allocation échoue).
	[[nodiscard]] static Result<AsyncIOQueue, Error> Create();

	/// Défile une tâche terminée sans bloquer. Retourne false si aucune tâche n'est prête.
	bool GetResult(AsyncIOOutcome &outcome) noexcept;

	/// Attend (au plus `timeoutMS` ms, -1 = indéfiniment) une tâche terminée.
	bool WaitResult(AsyncIOOutcome &outcome, Sint32 timeoutMS = -1) noexcept;

	/// Réveille tous les threads bloqués dans WaitResult().
	void Signal() noexcept { SDL_SignalAsyncIOQueue(mHandle); }
};

// ============================================================================
// AsyncIO — fichier ouvert en lecture/écriture asynchrone (RAII SDL_AsyncIO)
//
// Le destructeur ferme le fichier de façon bloquante (flush=false) : il crée
// une file temporaire, lance la tâche de fermeture puis attend sa complétion.
// Pour un contrôle fin (flush, ordre des tâches), appeler Close() explicitement.
// ============================================================================

/// Fichier ouvert en lecture/écriture asynchrone (RAII sur SDL_AsyncIO).
class AsyncIO {
	SDL_AsyncIO *m_handle = nullptr;

public:
	AsyncIO() noexcept = default;
	constexpr explicit AsyncIO(SDL_AsyncIO *handle) noexcept : m_handle(handle) {}

	~AsyncIO();

	AsyncIO(const AsyncIO &) = delete;
	AsyncIO &operator=(const AsyncIO &) = delete;

	AsyncIO(AsyncIO &&o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }
	AsyncIO &operator=(AsyncIO &&o) noexcept {
		if (this != &o) {
			CloseBlocking();
			m_handle = o.m_handle;
			o.m_handle = nullptr;
		}
		return *this;
	}

	/// Ouvre un fichier (mode "r", "w", "r+", "w+"). N'est PAS asynchrone.
	[[nodiscard]] static Result<AsyncIO, Error> FromFile(const char *file, const char *mode);

	/// Taille du flux (appel synchrone, non bloquant).
	[[nodiscard]] Sint64 GetSize() noexcept { return SDL_GetAsyncIOSize(m_handle); }

	/// Lance une lecture asynchrone vers `ptr` (qui doit rester valide jusqu'à complétion).
	bool Read(void *ptr, Uint64 offset, Uint64 size, AsyncIOQueue &queue, void *userdata = nullptr) noexcept;

	/// Lance une écriture asynchrone depuis `ptr`.
	bool Write(const void *ptr, Uint64 offset, Uint64 size, AsyncIOQueue &queue, void *userdata = nullptr) noexcept;

	/// Lance la fermeture asynchrone (le handle devient invalide immédiatement).
	bool Close(bool flush, AsyncIOQueue &queue, void *userdata = nullptr) noexcept;

	/// Fermeture bloquante (utilisée par le destructeur / l'opérateur de déplacement).
	void CloseBlocking() noexcept;

	[[nodiscard]] SDL_AsyncIO *Get() const noexcept { return m_handle; }
	[[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }
};

// ============================================================================
// Fonction libre : chargement complet d'un fichier, asynchrone (SDL_LoadFileAsync)
// ============================================================================

/// Charge un fichier entier de façon asynchrone ; le résultat arrive dans `queue`.
inline bool LoadFileAsync(const char *file, AsyncIOQueue &queue, void *userdata = nullptr) noexcept {
	return SDL_LoadFileAsync(file, queue.Get(), userdata);
}

} // namespace sdl3
