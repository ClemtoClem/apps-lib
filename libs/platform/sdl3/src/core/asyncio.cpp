// Définitions de sdl3/asyncio.hpp
#include <sdl3/sdl3.hpp>
#include "sdl3/core/asyncio.hpp"

namespace sdl3 {

// ── AsyncIOQueue ─────────────────────────────────────────────────────────────

Result<AsyncIOQueue, Error> AsyncIOQueue::Create() {
	auto *q = SDL_CreateAsyncIOQueue();
	if (!q)
		return Err(GetError());
	return Ok(AsyncIOQueue(q));
}

bool AsyncIOQueue::GetResult(AsyncIOOutcome &outcome) noexcept {
	SDL_AsyncIOOutcome raw{};
	if (!SDL_GetAsyncIOResult(mHandle, &raw))
		return false;
	outcome = AsyncIOOutcome(raw);
	return true;
}

bool AsyncIOQueue::WaitResult(AsyncIOOutcome &outcome, Sint32 timeoutMS) noexcept {
	SDL_AsyncIOOutcome raw{};
	if (!SDL_WaitAsyncIOResult(mHandle, &raw, timeoutMS))
		return false;
	outcome = AsyncIOOutcome(raw);
	return true;
}

// ── AsyncIO ──────────────────────────────────────────────────────────────────

AsyncIO::~AsyncIO() {
	CloseBlocking();
}

Result<AsyncIO, Error> AsyncIO::FromFile(const char *file, const char *mode) {
	auto *a = SDL_AsyncIOFromFile(file, mode);
	if (!a)
		return Err(GetError());
	return Ok(AsyncIO(a));
}

bool AsyncIO::Read(void *ptr, Uint64 offset, Uint64 size, AsyncIOQueue &queue, void *userdata) noexcept {
	return SDL_ReadAsyncIO(m_handle, ptr, offset, size, queue.Get(), userdata);
}

bool AsyncIO::Write(const void *ptr, Uint64 offset, Uint64 size, AsyncIOQueue &queue, void *userdata) noexcept {
	return SDL_WriteAsyncIO(m_handle, const_cast<void *>(ptr), offset, size, queue.Get(), userdata);
}

bool AsyncIO::Close(bool flush, AsyncIOQueue &queue, void *userdata) noexcept {
	bool ok = SDL_CloseAsyncIO(m_handle, flush, queue.Get(), userdata);
	if (ok)
		m_handle = nullptr;
	return ok;
}

void AsyncIO::CloseBlocking() noexcept {
	if (!m_handle)
		return;

	// Fermeture bloquante : on crée une file temporaire, on lance la tâche de
	// fermeture puis on laisse SDL_DestroyAsyncIOQueue attendre sa complétion.
	SDL_AsyncIOQueue *q = SDL_CreateAsyncIOQueue();
	if (!q)
		return; // OOM extrême : impossible de fermer proprement, on abandonne le handle.

	SDL_CloseAsyncIO(m_handle, false, q, nullptr);
	SDL_DestroyAsyncIOQueue(q);
	m_handle = nullptr;
}

} // namespace sdl3
