#include "math/color.hpp"

#include <algorithm>
#include <cctype>

namespace math {

namespace {

struct NamedColor {
	std::string_view name;
	Color color;
};

// Sous-ensemble des couleurs CSS nommées (les plus usuelles pour scripts et réglages).
constexpr NamedColor kPalette[] = {
	{"aliceblue", {240, 248, 255}}, {"aqua", {0, 255, 255}},		 {"beige", {245, 245, 220}},
	{"black", {0, 0, 0}},			 {"blue", {0, 0, 255}},			 {"brown", {165, 42, 42}},
	{"chocolate", {210, 105, 30}},	 {"coral", {255, 127, 80}},		 {"crimson", {220, 20, 60}},
	{"cyan", {0, 255, 255}},		 {"darkblue", {0, 0, 139}},		 {"darkgray", {169, 169, 169}},
	{"darkgreen", {0, 100, 0}},		 {"darkgrey", {169, 169, 169}},	 {"darkorange", {255, 140, 0}},
	{"darkred", {139, 0, 0}},		 {"deeppink", {255, 20, 147}},	 {"deepskyblue", {0, 191, 255}},
	{"dimgray", {105, 105, 105}},	 {"dimgrey", {105, 105, 105}},	 {"dodgerblue", {30, 144, 255}},
	{"firebrick", {178, 34, 34}},	 {"forestgreen", {34, 139, 34}},	 {"fuchsia", {255, 0, 255}},
	{"gold", {255, 215, 0}},		 {"gray", {128, 128, 128}},		 {"green", {0, 128, 0}},
	{"grey", {128, 128, 128}},		 {"indigo", {75, 0, 130}},		 {"ivory", {255, 255, 240}},
	{"khaki", {240, 230, 140}},		 {"lavender", {230, 230, 250}},	 {"lightblue", {173, 216, 230}},
	{"lightgray", {211, 211, 211}},	 {"lightgreen", {144, 238, 144}}, {"lightgrey", {211, 211, 211}},
	{"lime", {0, 255, 0}},			 {"magenta", {255, 0, 255}},		 {"maroon", {128, 0, 0}},
	{"midnightblue", {25, 25, 112}}, {"navy", {0, 0, 128}},			 {"olive", {128, 128, 0}},
	{"orange", {255, 165, 0}},		 {"orangered", {255, 69, 0}},	 {"pink", {255, 192, 203}},
	{"purple", {128, 0, 128}},		 {"red", {255, 0, 0}},			 {"royalblue", {65, 105, 225}},
	{"salmon", {250, 128, 114}},	 {"seagreen", {46, 139, 87}},	 {"silver", {192, 192, 192}},
	{"skyblue", {135, 206, 235}},	 {"slategray", {112, 128, 144}},	 {"snow", {255, 250, 250}},
	{"springgreen", {0, 255, 127}},	 {"steelblue", {70, 130, 180}},	 {"teal", {0, 128, 128}},
	{"tomato", {255, 99, 71}},		 {"turquoise", {64, 224, 208}},	 {"violet", {238, 130, 238}},
	{"wheat", {245, 222, 179}},		 {"white", {255, 255, 255}},		 {"yellow", {255, 255, 0}},
	{"yellowgreen", {154, 205, 50}},
};

uint8_t ToByte(float v) noexcept {
	const int i = int(v + 0.5f);
	return uint8_t(i < 0 ? 0 : (i > 255 ? 255 : i));
}

float NormalizeHue(float h) noexcept {
	h = std::fmod(h, 360.f);
	return h < 0.f ? h + 360.f : h;
}

float HueOf(float r, float g, float b, float mx, float d) noexcept {
	float h = 0.f;
	if (d > 0.f) {
		if (mx == r)
			h = 60.f * std::fmod((g - b) / d, 6.f);
		else if (mx == g)
			h = 60.f * ((b - r) / d + 2.f);
		else
			h = 60.f * ((r - g) / d + 4.f);
	}
	return h < 0.f ? h + 360.f : h;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			return false;
	return true;
}

int HexDigit(char c) noexcept {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

} // namespace

std::optional<Color> Color::FromName(std::string_view name) {
	for (const NamedColor &e : kPalette)
		if (EqualsIgnoreCase(e.name, name))
			return e.color;
	return std::nullopt;
}

std::optional<Color> Color::FromHex(std::string_view hex) {
	if (!hex.empty() && hex.front() == '#')
		hex.remove_prefix(1);
	if (hex.size() != 6 && hex.size() != 8)
		return std::nullopt;
	int d[8];
	for (size_t i = 0; i < hex.size(); ++i) {
		d[i] = HexDigit(hex[i]);
		if (d[i] < 0)
			return std::nullopt;
	}
	const auto pair = [&](size_t i) { return uint8_t(d[i] * 16 + d[i + 1]); };
	if (hex.size() == 6)
		return Color(pair(0), pair(2), pair(4));
	return Color(pair(0), pair(2), pair(4), pair(6));
}

Hsv ColorToHsv(Color c) noexcept {
	const float r = float(c.r) / 255.f, g = float(c.g) / 255.f, b = float(c.b) / 255.f;
	const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
	const float d = mx - mn;
	const float s = mx > 0.f ? d / mx : 0.f;
	return {HueOf(r, g, b, mx, d), s * 100.f, mx * 100.f};
}

Color HsvToColor(Hsv hsv) noexcept {
	const float h = NormalizeHue(hsv.hue);
	const float s = Clamp(hsv.saturation, 0.f, 100.f) / 100.f;
	const float v = Clamp(hsv.brightness, 0.f, 100.f) / 100.f;
	const float c = v * s;
	const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
	const float m = v - c;
	float r = 0.f, g = 0.f, b = 0.f;
	if (h < 60.f) { r = c; g = x; }
	else if (h < 120.f) { r = x; g = c; }
	else if (h < 180.f) { g = c; b = x; }
	else if (h < 240.f) { g = x; b = c; }
	else if (h < 300.f) { r = x; b = c; }
	else { r = c; b = x; }
	return {ToByte((r + m) * 255.f), ToByte((g + m) * 255.f), ToByte((b + m) * 255.f)};
}

Hsl ColorToHsl(Color c) noexcept {
	const float r = float(c.r) / 255.f, g = float(c.g) / 255.f, b = float(c.b) / 255.f;
	const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
	const float l = (mx + mn) * 0.5f;
	const float d = mx - mn;
	const float s = d > 0.f ? d / (1.f - std::fabs(2.f * l - 1.f)) : 0.f;
	return {HueOf(r, g, b, mx, d), s, l};
}

Color HslToColor(Hsl hsl) noexcept {
	const float h = NormalizeHue(hsl.hue);
	const float s = Clamp(hsl.saturation, 0.f, 1.f);
	const float l = Clamp(hsl.lightness, 0.f, 1.f);
	const float c = (1.f - std::fabs(2.f * l - 1.f)) * s;
	const float x = c * (1.f - std::fabs(std::fmod(h / 60.f, 2.f) - 1.f));
	const float m = l - c * 0.5f;
	float r = 0.f, g = 0.f, b = 0.f;
	if (h < 60.f) { r = c; g = x; }
	else if (h < 120.f) { r = x; g = c; }
	else if (h < 180.f) { g = c; b = x; }
	else if (h < 240.f) { g = x; b = c; }
	else if (h < 300.f) { r = x; b = c; }
	else { r = c; b = x; }
	return {ToByte((r + m) * 255.f), ToByte((g + m) * 255.f), ToByte((b + m) * 255.f)};
}

Color ColorGradient::Sample(float location) const noexcept {
	if (points.empty())
		return Color{};
	if (points.size() == 1)
		return points.front().color;
	if (location <= points.front().location)
		return points.front().color;
	if (location >= points.back().location)
		return points.back().color;
	const auto it = std::ranges::lower_bound(points, location, {}, &GradientPoint::location);
	const GradientPoint &a = *(it - 1), &b = *it;
	const float span = b.location - a.location;
	const float t = span > 0.f ? (location - a.location) / span : 0.f;
	return a.color.Lerp(b.color, t);
}

} // namespace math
