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

TEST(svg_parse) {
	auto svg = sdl3::Svg::Parse(R"(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="80" viewBox="0 0 120 80"><rect x="1" y="2" width="10" height="20" fill="red"/><circle cx="5" cy="5" r="3"/></svg>)");
	REQUIRE(svg.IsOk());
	CHECK_EQ(svg.Value().Width(), 120.f);
	CHECK_EQ(svg.Value().Height(), 80.f);
	CHECK(svg.Value().HasViewBox());
	CHECK_EQ(svg.Value().ViewBox().w, 120.f);
	CHECK_EQ(svg.Value().ViewBox().h, 80.f);
	CHECK_EQ(svg.Value().ShapeCount(), 2u);
}

TEST(svg_parse_doctype_and_declaration) {
	auto svg = sdl3::Svg::Parse(R"(<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.1//EN" "http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd"><svg width="64" height="64"><path d="M0 0 L10 10"/></svg>)");
	REQUIRE(svg.IsOk());
	CHECK_EQ(svg.Value().Width(), 64.f);
	CHECK_EQ(svg.Value().Height(), 64.f);
	CHECK(!svg.Value().HasViewBox());
	CHECK_EQ(svg.Value().ShapeCount(), 1u);
}

TEST(svg_rasterize_fill_color) {
	// `fill="#ffffff"` rastérise en blanc opaque ; `currentColor` (non géré par
	// nanosvg) retombe sur le noir par défaut — d'où l'usage du blanc pour teinter.
	auto white = sdl3::Svg::Parse(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 8 8"><rect x="1" y="1" width="6" height="6" fill="#ffffff"/></svg>)");
	REQUIRE(white.IsOk());
	auto surf = white.Value().Rasterize(8, 8);
	REQUIRE(surf.IsOk());
	auto rgba = surf.Value().ToRgba8();
	REQUIRE(rgba.size() == 8u * 8u * 4u);
	CHECK_EQ(rgba[(3 * 8 + 3) * 4 + 0], 255); // R du centre : blanc
	CHECK_EQ(rgba[(3 * 8 + 3) * 4 + 3], 255); // A du centre : opaque
	CHECK_EQ(rgba[3], 0);					   // A du coin : transparent
}

TEST(svg_icon_files_rasterize_white) {
	// Les icônes de la barre d'outils (assets/icons/*.svg) rastérisent en blanc
	// opaque (teintables), pas en noir.
	auto svg = sdl3::Svg::Load("assets/icons/select.svg");
	REQUIRE(svg.IsOk());
	auto surf = svg.Value().Rasterize(24, 24);
	REQUIRE(surf.IsOk());
	auto rgba = surf.Value().ToRgba8();
	REQUIRE(rgba.size() == 24u * 24u * 4u);
	size_t opaque = 0, white = 0;
	for (size_t i = 0; i < rgba.size(); i += 4)
		if (rgba[i + 3] > 0) {
			++opaque;
			white += rgba[i] == 255 && rgba[i + 1] == 255 && rgba[i + 2] == 255 ? 1u : 0u;
		}
	CHECK(opaque > 0);
	CHECK_EQ(white, opaque);
}
