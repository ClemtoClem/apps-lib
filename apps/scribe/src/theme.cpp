#include "scribe/theme.hpp"

#include <format>

#include <app/application.hpp>

namespace scribe {

namespace {

using sdl3::Rgba;

Palette Night() {
	Palette p;
	p.ui = app::MidnightTheme();
	p.workspace = Rgba(38, 42, 52);
	p.panel = Rgba(24, 27, 34);
	p.sidebar = Rgba(30, 36, 52);
	p.card = Rgba(34, 39, 50);
	p.text = Rgba(214, 220, 232);
	p.strong = Rgba(228, 232, 240);
	p.muted = Rgba(150, 160, 176);
	p.faint = Rgba(120, 128, 144);
	p.border = Rgba(80, 86, 100);
	p.hover = Rgba(255, 255, 255, 14);
	p.selection = Rgba(79, 157, 255);
	p.accentSoft = Rgba(120, 180, 255);
	p.popup = Rgba(30, 34, 44);
	p.popupBorder = Rgba(70, 78, 96);
	p.guide = Rgba(230, 120, 0);
	p.rulerFrame = Rgba(48, 53, 66);
	p.caption = Rgba(110, 170, 120);
	p.status = Rgba(27, 31, 40);
	p.statusText = Rgba(140, 150, 168);
	return p;
}

Palette Light() {
	Palette p;
	p.ui = app::LightTheme();
	p.workspace = Rgba(206, 211, 220);
	p.panel = Rgba(247, 248, 251);
	p.sidebar = Rgba(43, 87, 154);
	p.card = Rgba(255, 255, 255);
	p.text = Rgba(40, 46, 58);
	p.strong = Rgba(18, 22, 30);
	p.muted = Rgba(92, 102, 120);
	p.faint = Rgba(128, 136, 150);
	p.border = Rgba(196, 203, 214);
	p.hover = Rgba(0, 0, 0, 16);
	p.selection = Rgba(47, 111, 208);
	p.accentSoft = Rgba(47, 111, 208);
	p.popup = Rgba(255, 255, 255);
	p.popupBorder = Rgba(196, 203, 214);
	p.guide = Rgba(214, 102, 0);
	p.rulerFrame = Rgba(220, 224, 231);
	p.caption = Rgba(38, 140, 70);
	p.status = Rgba(230, 234, 241);
	p.statusText = Rgba(86, 96, 112);
	return p;
}

Palette Contrast() {
	Palette p;
	p.ui = app::ContrastTheme();
	p.workspace = Rgba(36, 36, 36);
	p.panel = Rgba(0, 0, 0);
	p.sidebar = Rgba(0, 0, 0);
	p.card = Rgba(16, 16, 16);
	p.text = Rgba(255, 255, 255);
	p.strong = Rgba(255, 255, 255);
	p.muted = Rgba(235, 235, 235);
	p.faint = Rgba(210, 210, 210);
	p.border = Rgba(255, 255, 255);
	p.hover = Rgba(255, 255, 255, 48);
	p.selection = Rgba(255, 255, 0);
	p.accentSoft = Rgba(255, 255, 0);
	p.popup = Rgba(0, 0, 0);
	p.popupBorder = Rgba(255, 255, 255);
	p.guide = Rgba(255, 150, 0);
	p.rulerFrame = Rgba(0, 0, 0);
	p.caption = Rgba(0, 255, 0);
	p.status = Rgba(0, 0, 0);
	p.statusText = Rgba(255, 255, 255);
	return p;
}

constexpr ThemeInfo kThemes[] = {{"night", "Nuit", &Night}, {"light", "Clair", &Light}, {"contrast", "Contraste élevé", &Contrast}};

Palette &Current() {
	static Palette palette = Night();
	return palette;
}

std::string Hex(ui::Color c) { return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b); }

} // namespace

std::span<const ThemeInfo> Themes() noexcept { return kThemes; }

size_t ThemeIndex(std::string_view key) noexcept {
	for (size_t i = 0; i < std::size(kThemes); ++i)
		if (kThemes[i].key == key)
			return i;
	return 0;
}

const Palette &P() noexcept { return Current(); }

void SetPalette(const Palette &palette) { Current() = palette; }

std::string StyleSheetFor(const Palette &p) {
	return std::format(":root {{ color: {}; font-size: 13px; accent-color: {}; widget-color: {}; widget-hover-color: {}; widget-active-color: {}; }}\n"
					   ".status {{ background-color: {}; color: {}; font-size: 12px; padding: 3px 8px; }}\n",
					   Hex(p.text), Hex(p.ui.accent), Hex(p.ui.widget), Hex(p.ui.widgetHover), Hex(p.ui.widgetActive), Hex(p.status), Hex(p.statusText));
}

} // namespace scribe
