// Définitions de sdl3/loadso.hpp
#include <sdl3/sdl3.hpp>
#include "sdl3/io/loadso.hpp"

namespace sdl3 {

Result<SharedObject, Error> SharedObject::Load(const char *path) {
	auto *so = SDL_LoadObject(path);
	if (!so)
		return Err(GetError());
	return Ok(SharedObject(so));
}

} // namespace sdl3
