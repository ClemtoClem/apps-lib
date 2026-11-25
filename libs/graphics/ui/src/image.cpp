// Définitions de ui/image.hpp — import d'images (SVG via dataio, raster via SDL_image).
#include "ui/image.hpp"

#include <sdl3/render/image.hpp>

namespace ui {

Result<Image, String> Image::Load(const String &path) {
	// SVG : analysé via dataio (XML) puis rastérisé à sa taille naturelle.
	const StringView view(path);
	const bool svg = view.size() >= 4 &&
					 (view.substr(view.size() - 4) == ".svg" || view.substr(view.size() - 4) == ".SVG");
	if (svg) {
		auto parsed = sdl3::Svg::Load(path);
		if (parsed.IsError())
			return Err(parsed.Error());
		auto surf = parsed->Rasterize();
		if (surf.IsError())
			return Err(String(surf.Error()));
		return Ok(Image(std::make_shared<sdl3::Surface>(std::move(surf.Value()))));
	}

	// Autres formats : SDL3_image (PNG, JPG, BMP, GIF, WEBP, …).
	auto surf = sdl3::ImgLoad(path);
	if (surf.IsError())
		return Err(String(surf.Error()));
	return Ok(Image(std::make_shared<sdl3::Surface>(std::move(surf.Value()))));
}

} // namespace ui
