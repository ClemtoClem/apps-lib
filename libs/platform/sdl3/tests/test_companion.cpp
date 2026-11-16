#include "test.hpp"
#include <sdl3/sdl3.hpp>

// Version numbers are compile-time constants and safe to read without
// initializing any companion library (decoder/FreeType queries need Init).

TEST(companion_versions) {
	CHECK(sdl3::MixerVersion() > 0);
	CHECK(sdl3::TtfVersion() > 0);
	CHECK(sdl3::NetVersion() > 0);
	CHECK(sdl3::ImgVersion() > 0);
}
