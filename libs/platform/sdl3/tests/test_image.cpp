#include "test.hpp"
#include <sdl3/sdl3.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace {

// 1x1 transparent RGBA PNG (71-byte file).
constexpr std::array<uint8_t, 71> kTinyPng = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
	0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0x00, 0x02, 0x00,
	0x00, 0x0a, 0x00, 0x02, 0xff, 0xe5, 0xc4, 0xa5, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

} // namespace

TEST(image_version) {
	CHECK(sdl3::ImgVersion() > 0);
}

TEST(image_format_detection) {
	auto io = sdl3::IOStream::FromConstMemory(kTinyPng.data(), kTinyPng.size());
	REQUIRE(io.IsOk());
	CHECK(sdl3::ImgIsPng(io.Value()));
	CHECK(!sdl3::ImgIsJpg(io.Value()));
	CHECK(!sdl3::ImgIsGif(io.Value()));
	CHECK(!sdl3::ImgIsBmp(io.Value()));
}

TEST(image_load_memory) {
	auto surface = sdl3::ImgLoadMemory(kTinyPng.data(), kTinyPng.size());
	REQUIRE(surface.IsOk());
	CHECK_EQ(surface.Value().Get()->w, 1);
	CHECK_EQ(surface.Value().Get()->h, 1);

	// std::span<const std::byte> overload via std::as_bytes.
	auto spanSurface = sdl3::ImgLoadMemory(std::as_bytes(std::span<const uint8_t>(kTinyPng)));
	REQUIRE(spanSurface.IsOk());
	CHECK_EQ(spanSurface.Value().Get()->w, 1);
}

TEST(image_save_reload_roundtrip) {
	auto surface = sdl3::Surface::Create(4, 3);
	REQUIRE(surface.IsOk());

	// Save PNG into a growable in-memory stream, then reload from it.
	auto stream = sdl3::IOStream::FromDynamicMemory();
	REQUIRE(stream.IsOk());
	auto saved = sdl3::ImgSavePngIO(surface.Value(), stream.Value());
	REQUIRE(saved.IsOk());

	auto bytes = stream.Value().DynamicMemoryBytes();
	CHECK(!bytes.empty());

	auto reloaded = sdl3::ImgLoadMemory(bytes.data(), bytes.size());
	REQUIRE(reloaded.IsOk());
	CHECK_EQ(reloaded.Value().Get()->w, 4);
	CHECK_EQ(reloaded.Value().Get()->h, 3);
}

TEST(image_save_jpg_roundtrip) {
	auto surface = sdl3::Surface::Create(2, 2);
	REQUIRE(surface.IsOk());

	auto stream = sdl3::IOStream::FromDynamicMemory();
	REQUIRE(stream.IsOk());
	auto saved = sdl3::ImgSaveJpgIO(surface.Value(), stream.Value(), false, 90);
	REQUIRE(saved.IsOk());

	auto bytes = stream.Value().DynamicMemoryBytes();
	CHECK(!bytes.empty());

	auto reloaded = sdl3::ImgLoadMemory(bytes.data(), bytes.size());
	REQUIRE(reloaded.IsOk());
	CHECK_EQ(reloaded.Value().Get()->w, 2);
	CHECK_EQ(reloaded.Value().Get()->h, 2);
}
