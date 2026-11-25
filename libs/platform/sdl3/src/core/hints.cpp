// Définitions de sdl3/hints.hpp
#include <sdl3/sdl3.hpp>
#include "sdl3/core/hints.hpp"

namespace sdl3 {

void SDLCALL HintCallback::Trampoline(void *ud, const char *name, const char *oldValue, const char *newValue) noexcept {
	auto *ctx = static_cast<Ctx *>(ud);
	if (ctx && ctx->fn)
		ctx->fn(StringView(name), StringView(oldValue), StringView(newValue));
}

void HintCallback::Reset() noexcept {
	if (m_ctx) {
		SDL_RemoveHintCallback(m_ctx->name.c_str(), Trampoline, m_ctx.get());
		m_ctx.reset();
	}
}

} // namespace sdl3
