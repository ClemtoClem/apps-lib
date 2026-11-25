// Définitions de sdl3/assert.hpp
#include <sdl3/sdl3.hpp>
#include "sdl3/core/assert.hpp"

namespace sdl3 {

SDL_AssertState SDLCALL AssertionHandler::Trampoline(const SDL_AssertData *data, void *ud) noexcept {
	auto *ctx = static_cast<Ctx *>(ud);
	if (ctx && ctx->fn)
		return static_cast<SDL_AssertState>(ctx->fn(*data));
	return SDL_ASSERTION_ABORT;
}

AssertionHandler::~AssertionHandler() {
	if (m_ctx) {
		SDL_SetAssertionHandler(nullptr, nullptr);
		delete m_ctx;
	}
}

} // namespace sdl3
