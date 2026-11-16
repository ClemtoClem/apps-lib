// Définitions de sdl3/ui/style.hpp
#include "ui/style.hpp"

#include <charconv>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>

namespace ui {

// ============================================================================
// Easing
// ============================================================================

float Ease(float t, Easing e) noexcept {
	t = std::clamp(t, 0.f, 1.f);
	switch (e) {
	case Easing::Linear: return t;
	case Easing::InQuad: return t * t;
	case Easing::OutQuad: return t * (2.f - t);
	case Easing::InOutQuad: return t < 0.5f ? 2.f * t * t : -1.f + (4.f - 2.f * t) * t;
	case Easing::InCubic: return t * t * t;
	case Easing::OutCubic: {
		const float u = t - 1.f;
		return u * u * u + 1.f;
	}
	case Easing::InOutCubic: return t < 0.5f ? 4.f * t * t * t : (t - 1.f) * (2.f * t - 2.f) * (2.f * t - 2.f) + 1.f;
	}
	return t;
}

// ============================================================================
// StyleProps
// ============================================================================

// Champs héritables (typographie) — repris dans MergeInherited / ApplyInherited.
#define UI_FOR_EACH_INHERITED(F)                                                                                       \
	F(color) F(fontSize) F(fontWeight) F(fontStyle) F(underline) F(textAlign)

#define UI_FOR_EACH_PROP(F)                                                                                            \
	F(display) F(position) F(anchor)                                                                                    \
	F(top) F(right) F(bottom) F(left)                                                                                   \
	F(width) F(height) F(minWidth) F(minHeight) F(maxWidth) F(maxHeight)                                                \
	F(marginTop) F(marginRight) F(marginBottom) F(marginLeft)                                                           \
	F(paddingTop) F(paddingRight) F(paddingBottom) F(paddingLeft)                                                        \
	F(borderColor)                                                                                                      \
	F(borderTopWidth) F(borderRightWidth) F(borderBottomWidth) F(borderLeftWidth)                                       \
	F(borderTopLeftRadius) F(borderTopRightRadius) F(borderBottomRightRadius) F(borderBottomLeftRadius)                 \
	F(backgroundColor) F(opacity) F(zIndex)                                                                             \
	F(flexDirection) F(flexWrap) F(justifyContent) F(alignItems) F(alignSelf)                                           \
	F(flexGrow) F(flexShrink) F(gap) F(gridColumns)                                                                     \
	F(overflowX) F(overflowY)                                                                                           \
	F(color) F(fontSize) F(fontWeight) F(fontStyle) F(underline) F(textAlign)

void StyleProps::Merge(const StyleProps &over) {
#define UI_M(f) if (over.f) f = over.f;
	UI_FOR_EACH_PROP(UI_M)
#undef UI_M
}

void StyleProps::MergeInherited(const StyleProps &over) {
#define UI_M(f) if (over.f) f = over.f;
	UI_FOR_EACH_INHERITED(UI_M)
#undef UI_M
}

void StyleProps::SetMargin(const math::Sides &s) {
	marginTop = s.top;
	marginRight = s.right;
	marginBottom = s.bottom;
	marginLeft = s.left;
}
void StyleProps::SetPadding(const math::Sides &s) {
	paddingTop = s.top;
	paddingRight = s.right;
	paddingBottom = s.bottom;
	paddingLeft = s.left;
}
void StyleProps::SetBorderWidth(const math::Sides &s) {
	borderTopWidth = s.top;
	borderRightWidth = s.right;
	borderBottomWidth = s.bottom;
	borderLeftWidth = s.left;
}
void StyleProps::SetBorderRadius(const math::Corners &c) {
	borderTopLeftRadius = c.tl;
	borderTopRightRadius = c.tr;
	borderBottomRightRadius = c.br;
	borderBottomLeftRadius = c.bl;
}

bool StyleProps::IsEmpty() const noexcept {
	bool any = false;
#define UI_M(f) any = any || f.has_value();
	UI_FOR_EACH_PROP(UI_M)
#undef UI_M
	return !any;
}

// ============================================================================
// ComputedStyle
// ============================================================================

void ComputedStyle::Apply(const StyleProps &props) {
#define UI_M(f) if (props.f) f = *props.f;
	UI_FOR_EACH_PROP(UI_M)
#undef UI_M
}

void ComputedStyle::ApplyInherited(const StyleProps &props) {
#define UI_M(f) if (props.f) f = *props.f;
	UI_FOR_EACH_INHERITED(UI_M)
#undef UI_M
}

// ============================================================================
// Analyse des couleurs
// ============================================================================

namespace {

const std::map<std::string_view, uint32_t> &NamedColors() {
	static const std::map<std::string_view, uint32_t> m = {
		{"transparent", 0x00000000}, {"black", 0x000000ff}, {"white", 0xffffffff},
		{"red", 0xff0000ff}, {"green", 0x008000ff}, {"blue", 0x0000ffff},
		{"gray", 0x808080ff}, {"grey", 0x808080ff}, {"silver", 0xc0c0c0ff},
		{"yellow", 0xffff00ff}, {"orange", 0xffa500ff}, {"purple", 0x800080ff},
		{"cyan", 0x00ffffff}, {"magenta", 0xff00ffff}, {"lime", 0x00ff00ff},
		{"maroon", 0x800000ff}, {"navy", 0x000080ff}, {"olive", 0x808000ff},
		{"teal", 0x008080ff}, {"darkgray", 0xa9a9a9ff}, {"lightgray", 0xd3d3d3ff},
	};
	return m;
}

} // namespace

std::optional<sdl3::Color> ParseColor(std::string_view text) noexcept {
	while (!text.empty() && std::isspace(uint8_t(text.front())))
		text.remove_prefix(1);
	while (!text.empty() && std::isspace(uint8_t(text.back())))
		text.remove_suffix(1);
	if (text.empty())
		return std::nullopt;

	// Nommé.
	if (auto it = NamedColors().find(text); it != NamedColors().end()) {
		const uint32_t v = it->second;
		return sdl3::Color{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
	}
	// Hexadécimal.
	if (text[0] == '#') {
		std::string_view h = text.substr(1);
		if (h.size() != 3 && h.size() != 4 && h.size() != 6 && h.size() != 8)
			return std::nullopt;
		auto nib = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		};
		auto expand = [&](std::string_view x) -> uint32_t {
			uint32_t v = 0;
			for (char c : x) v = v * 16u + uint32_t(nib(c));
			return v;
		};
		if (h.size() == 3 || h.size() == 4) {
			int r = nib(h[0]), g = nib(h[1]), b = nib(h[2]);
			int a = h.size() == 4 ? nib(h[3]) : 15;
			if (r < 0 || g < 0 || b < 0 || a < 0)
				return std::nullopt;
			return sdl3::Color{uint8_t(r * 17), uint8_t(g * 17), uint8_t(b * 17), uint8_t(a * 17)};
		}
		uint32_t v = expand(h);
		if (h.size() == 6)
			return sdl3::Color{uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v), 255};
		return sdl3::Color{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
	}
	// rgb(r, g, b) / rgba(r, g, b, a)
	if (text.starts_with("rgb")) {
		const size_t open = text.find('(');
		const size_t close = text.rfind(')');
		if (open == std::string_view::npos || close == std::string_view::npos || close <= open)
			return std::nullopt;
		int c[4] = {0, 0, 0, 255};
		std::string_view inner = text.substr(open + 1, close - open - 1);
		size_t n = 0;
		size_t start = 0;
		while (n < 4 && start <= inner.size()) {
			const size_t comma = inner.find(',', start);
			const std::string_view tok = inner.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
			std::string_view t = tok;
			while (!t.empty() && std::isspace(uint8_t(t.front()))) t.remove_prefix(1);
			while (!t.empty() && std::isspace(uint8_t(t.back()))) t.remove_suffix(1);
			int val = 0;
			if (std::from_chars(t.data(), t.data() + t.size(), val).ec != std::errc())
				return std::nullopt;
			c[n++] = std::clamp(val, 0, 255);
			if (comma == std::string_view::npos)
				break;
			start = comma + 1;
		}
		return sdl3::Color{uint8_t(c[0]), uint8_t(c[1]), uint8_t(c[2]), uint8_t(c[3])};
	}
	return std::nullopt;
}

std::optional<Length> ParseLength(std::string_view text) noexcept {
	while (!text.empty() && std::isspace(uint8_t(text.front())))
		text.remove_prefix(1);
	while (!text.empty() && std::isspace(uint8_t(text.back())))
		text.remove_suffix(1);
	if (text.empty())
		return std::nullopt;
	if (text == "auto")
		return Length::Auto();
	if (text.ends_with("fr")) {
		float v = 1.f;
		std::string_view num = text.substr(0, text.size() - 2);
		if (!num.empty())
			std::from_chars(num.data(), num.data() + num.size(), v);
		return Length::Fr(v);
	}
	// calc(...)
	if (text.starts_with("calc(") && text.ends_with(")")) {
		const std::string_view inner = text.substr(5, text.size() - 6);
		const size_t minus = inner.find(" - ");
		if (minus != std::string_view::npos) {
			auto a = ParseLength(inner.substr(0, minus));
			float px = 0.f;
			std::string_view rest = inner.substr(minus + 3);
			rest.remove_suffix(2); // "px"
			std::from_chars(rest.data(), rest.data() + rest.size(), px);
			if (a)
				return a->Plus(-px);
		}
		return std::nullopt;
	}
	// Numérique + unité.
	size_t i = 0;
	if (i < text.size() && (text[i] == '+' || text[i] == '-' || text[i] == '.'))
		++i;
	while (i < text.size() && (std::isdigit(uint8_t(text[i])) || text[i] == '.'))
		++i;
	if (i == 0)
		return std::nullopt;
	float v = 0.f;
	std::from_chars(text.data(), text.data() + i, v);
	std::string_view unit = text.substr(i);
	while (!unit.empty() && std::isspace(uint8_t(unit.front())))
		unit.remove_prefix(1);
	if (unit.empty() || unit == "px")
		return Length::Px(v);
	if (unit == "%")
		return Length::Pct(v);
	if (unit == "em")
		return Length::Em(v);
	if (unit == "rem")
		return Length::Rem(v);
	if (unit == "vw")
		return Length::Vw(v);
	if (unit == "vh")
		return Length::Vh(v);
	return std::nullopt;
}

std::optional<float> ParsePixels(std::string_view text) noexcept {
	auto l = ParseLength(text);
	if (!l || l->unit != Unit::Px)
		return std::nullopt;
	return l->value;
}

// ============================================================================
// SetDeclaration — pose une déclaration `nom: valeur`
// ============================================================================

namespace {

std::vector<std::string_view> Split(std::string_view s, char sep) {
	std::vector<std::string_view> out;
	size_t start = 0;
	while (start <= s.size()) {
		const size_t end = s.find(sep, start);
		const std::string_view tok = s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
		std::string_view t = tok;
		while (!t.empty() && std::isspace(uint8_t(t.front()))) t.remove_prefix(1);
		while (!t.empty() && std::isspace(uint8_t(t.back()))) t.remove_suffix(1);
		out.push_back(t);
		if (end == std::string_view::npos)
			break;
		start = end + 1;
	}
	return out;
}

std::optional<float> ParseFloat(std::string_view s) {
	auto v = ParsePixels(s);
	if (v)
		return v;
	// nombre nu
	float f = 0.f;
	std::string_view t = s;
	while (!t.empty() && std::isspace(uint8_t(t.front()))) t.remove_prefix(1);
	const auto r = std::from_chars(t.data(), t.data() + t.size(), f);
	if (r.ec == std::errc() && r.ptr == t.data() + t.size())
		return f;
	return std::nullopt;
}

std::optional<FlexDirection> ParseFlexDirection(std::string_view s) {	if (s == "row") return FlexDirection::Row;
	if (s == "column") return FlexDirection::Column;
	if (s == "row-reverse") return FlexDirection::RowReverse;
	if (s == "column-reverse") return FlexDirection::ColumnReverse;
	return std::nullopt;
}

bool ParseSides(std::string_view value, math::Sides &out) {
	const auto parts = Split(value, ' ');
	if (parts.empty() || parts.size() > 4)
		return false;
	std::optional<float> v[4];
	for (size_t i = 0; i < parts.size(); ++i) {
		v[i] = ParseFloat(parts[i]);
		if (!v[i])
			return false;
	}
	float l, t, r, b;
	switch (parts.size()) {
	case 1: l = t = r = b = *v[0]; break;
	case 2: t = b = *v[0]; l = r = *v[1]; break;
	case 3: t = *v[0]; l = r = *v[1]; b = *v[2]; break;
	default: t = *v[0]; r = *v[1]; b = *v[2]; l = *v[3]; break;
	}
	out = {l, t, r, b};
	return true;
}

} // namespace

bool SetDeclaration(StyleProps &props, std::string_view name, std::string_view value) {
	while (!name.empty() && std::isspace(uint8_t(name.front()))) name.remove_prefix(1);
	while (!name.empty() && std::isspace(uint8_t(name.back()))) name.remove_suffix(1);
	while (!value.empty() && std::isspace(uint8_t(value.front()))) value.remove_prefix(1);
	while (!value.empty() && std::isspace(uint8_t(value.back()))) value.remove_suffix(1);

	// Raccourcis.
	if (name == "margin") {
		math::Sides s;
		if (!ParseSides(value, s))
			return false;
		props.SetMargin(s);
		return true;
	}
	if (name == "padding") {
		math::Sides s;
		if (!ParseSides(value, s))
			return false;
		props.SetPadding(s);
		return true;
	}
	if (name == "border") {
		// "border: <largeur> <color>" (ou "solid" ignoré).
		float width = 1.f;
		sdl3::Color color{0, 0, 0, 255};
		for (std::string_view tok : Split(value, ' ')) {
			if (tok == "solid" || tok == "none")
				continue;
			if (auto w = ParseFloat(tok))
				width = *w;
			else if (auto c = ParseColor(tok))
				color = *c;
			else
				return false;
		}
		props.borderTopWidth = props.borderRightWidth = props.borderBottomWidth = props.borderLeftWidth = width;
		props.borderColor = color;
		return true;
	}
	if (name == "border-radius") {
		const auto parts = Split(value, ' ');
		if (parts.size() == 1) {
			if (auto r = ParseFloat(parts[0])) {
				props.SetBorderRadius(math::Corners(*r));
				return true;
			}
		}
		return false;
	}
	if (name == "background" || name == "background-color") {
		if (auto c = ParseColor(value)) {
			props.backgroundColor = *c;
			return true;
		}
		return false;
	}
	if (name == "color") {
		if (auto c = ParseColor(value)) {
			props.color = *c;
			return true;
		}
		return false;
	}
	if (name == "flex") {
		// "flex: <grow> [shrink]" — simplifié.
		const auto parts = Split(value, ' ');
		if (!parts.empty()) {
			if (auto g = ParseFloat(parts[0]))
				props.flexGrow = *g;
			if (parts.size() > 1)
				if (auto s = ParseFloat(parts[1]))
					props.flexShrink = *s;
			return true;
		}
		return false;
	}
	if (name == "overflow") {
		Overflow o = Overflow::Hidden;
		if (value == "visible") o = Overflow::Visible;
		else if (value == "hidden") o = Overflow::Hidden;
		else if (value == "scroll") o = Overflow::Scroll;
		else if (value == "auto") o = Overflow::Auto;
		else return false;
		props.overflowX = props.overflowY = o;
		return true;
	}
	if (name == "inset") {
		const auto parts = Split(value, ' ');
		if (parts.size() == 1) {
			if (auto l = ParseLength(parts[0])) {
				props.top = props.right = props.bottom = props.left = *l;
				return true;
			}
		}
		return false;
	}
	if (name == "font") {
		// "font: <weight> <size>px" simplifié.
		for (std::string_view tok : Split(value, ' ')) {
			if (auto l = ParseLength(tok))
				props.fontSize = l->value;
			else if (tok == "bold")
				props.fontWeight = 700;
		}
		return true;
	}

	// Propriétés simples (mappées par nom).
	// On traite les enums par comparaison directe pour la lisibilité.
	if (name == "display") {
		if (value == "flex") props.display = Display::Flex;
		else if (value == "grid") props.display = Display::Grid;
		else if (value == "stack") props.display = Display::Stack;
		else if (value == "none") props.display = Display::None;
		else return false;
		return true;
	}
	if (name == "position") {
		if (value == "relative") props.position = Position::Relative;
		else if (value == "absolute") props.position = Position::Absolute;
		else if (value == "fixed") props.position = Position::Fixed;
		else return false;
		return true;
	}
	if (name == "anchor") {
		if (value == "top-left") props.anchor = Anchor::TopLeft;
		else if (value == "top") props.anchor = Anchor::Top;
		else if (value == "top-right") props.anchor = Anchor::TopRight;
		else if (value == "left") props.anchor = Anchor::Left;
		else if (value == "center") props.anchor = Anchor::Center;
		else if (value == "right") props.anchor = Anchor::Right;
		else if (value == "bottom-left") props.anchor = Anchor::BottomLeft;
		else if (value == "bottom") props.anchor = Anchor::Bottom;
		else if (value == "bottom-right") props.anchor = Anchor::BottomRight;
		else return false;
		return true;
	}
	if (name == "flex-direction") {
		if (auto d = ParseFlexDirection(value)) { props.flexDirection = *d; return true; }
		return false;
	}
	if (name == "flex-wrap") {
		if (value == "nowrap") props.flexWrap = FlexWrap::NoWrap;
		else if (value == "wrap") props.flexWrap = FlexWrap::Wrap;
		else return false;
		return true;
	}
	if (name == "justify-content") {
		if (value == "start" || value == "flex-start") props.justifyContent = Justify::Start;
		else if (value == "center") props.justifyContent = Justify::Center;
		else if (value == "end" || value == "flex-end") props.justifyContent = Justify::End;
		else if (value == "space-between") props.justifyContent = Justify::SpaceBetween;
		else if (value == "space-around") props.justifyContent = Justify::SpaceAround;
		else if (value == "space-evenly") props.justifyContent = Justify::SpaceEvenly;
		else return false;
		return true;
	}
	auto parseAlign = [](std::string_view v) -> std::optional<Align> {
		if (v == "start" || v == "flex-start") return Align::Start;
		if (v == "center") return Align::Center;
		if (v == "end" || v == "flex-end") return Align::End;
		if (v == "stretch") return Align::Stretch;
		return std::nullopt;
	};
	if (name == "align-items") {
		if (auto a = parseAlign(value)) { props.alignItems = *a; return true; }
		return false;
	}
	if (name == "align-self") {
		if (auto a = parseAlign(value)) { props.alignSelf = *a; return true; }
		return false;
	}
	if (name == "text-align") {
		if (value == "left") props.textAlign = TextAlign::Left;
		else if (value == "center") props.textAlign = TextAlign::Center;
		else if (value == "right") props.textAlign = TextAlign::Right;
		else return false;
		return true;
	}
	if (name == "font-style") {
		if (value == "normal") props.fontStyle = FontStyle::Normal;
		else if (value == "italic") props.fontStyle = FontStyle::Italic;
		else return false;
		return true;
	}
	if (name == "text-decoration" || name == "underline") {
		if (value == "underline") props.underline = true;
		else if (value == "none" || value == "false") props.underline = false;
		else return false;
		return true;
	}
	if (name == "overflow-x" || name == "overflow-y") {
		Overflow o = Overflow::Hidden;
		if (value == "visible") o = Overflow::Visible;
		else if (value == "hidden") o = Overflow::Hidden;
		else if (value == "scroll") o = Overflow::Scroll;
		else if (value == "auto") o = Overflow::Auto;
		else return false;
		if (name == "overflow-x") props.overflowX = o; else props.overflowY = o;
		return true;
	}

	// Longueurs.
	auto setLength = [&](std::string_view n, std::optional<Length> StyleProps::*field) {
		if (name == n) {
			if (auto l = ParseLength(value)) { props.*field = *l; return true; }
			return false;
		}
		return false;
	};
	if (setLength("width", &StyleProps::width)) return true;
	if (setLength("height", &StyleProps::height)) return true;
	if (setLength("min-width", &StyleProps::minWidth)) return true;
	if (setLength("min-height", &StyleProps::minHeight)) return true;
	if (setLength("max-width", &StyleProps::maxWidth)) return true;
	if (setLength("max-height", &StyleProps::maxHeight)) return true;
	if (setLength("top", &StyleProps::top)) return true;
	if (setLength("right", &StyleProps::right)) return true;
	if (setLength("bottom", &StyleProps::bottom)) return true;
	if (setLength("left", &StyleProps::left)) return true;

	// Nombres flottants (marges, padding, bordures, tailles de police…).
	auto setFloat = [&](std::string_view n, std::optional<float> StyleProps::*field) {
		if (name == n) {
			if (auto f = ParseFloat(value)) { props.*field = *f; return true; }
			return false;
		}
		return false;
	};
	if (setFloat("margin-top", &StyleProps::marginTop)) return true;
	if (setFloat("margin-right", &StyleProps::marginRight)) return true;
	if (setFloat("margin-bottom", &StyleProps::marginBottom)) return true;
	if (setFloat("margin-left", &StyleProps::marginLeft)) return true;
	if (setFloat("padding-top", &StyleProps::paddingTop)) return true;
	if (setFloat("padding-right", &StyleProps::paddingRight)) return true;
	if (setFloat("padding-bottom", &StyleProps::paddingBottom)) return true;
	if (setFloat("padding-left", &StyleProps::paddingLeft)) return true;
	if (setFloat("border-top-width", &StyleProps::borderTopWidth)) return true;
	if (setFloat("border-right-width", &StyleProps::borderRightWidth)) return true;
	if (setFloat("border-bottom-width", &StyleProps::borderBottomWidth)) return true;
	if (setFloat("border-left-width", &StyleProps::borderLeftWidth)) return true;
	if (setFloat("border-top-left-radius", &StyleProps::borderTopLeftRadius)) return true;
	if (setFloat("border-top-right-radius", &StyleProps::borderTopRightRadius)) return true;
	if (setFloat("border-bottom-right-radius", &StyleProps::borderBottomRightRadius)) return true;
	if (setFloat("border-bottom-left-radius", &StyleProps::borderBottomLeftRadius)) return true;
	if (setFloat("font-size", &StyleProps::fontSize)) return true;
	if (setFloat("flex-grow", &StyleProps::flexGrow)) return true;
	if (setFloat("flex-shrink", &StyleProps::flexShrink)) return true;
	if (setFloat("gap", &StyleProps::gap)) return true;
	if (setFloat("opacity", &StyleProps::opacity)) return true;

	// Entiers.
	if (name == "font-weight") {
		int w = 0;
		if (std::from_chars(value.data(), value.data() + value.size(), w).ec == std::errc()) { props.fontWeight = w; return true; }
		if (value == "bold") { props.fontWeight = 700; return true; }
		return false;
	}
	if (name == "grid-columns") {
		int n = 0;
		if (std::from_chars(value.data(), value.data() + value.size(), n).ec == std::errc()) { props.gridColumns = n; return true; }
		return false;
	}
	if (name == "z-index") {
		int z = 0;
		if (std::from_chars(value.data(), value.data() + value.size(), z).ec == std::errc()) { props.zIndex = z; return true; }
		return false;
	}
	if (name == "border-color") {
		if (auto c = ParseColor(value)) { props.borderColor = *c; return true; }
		return false;
	}
	if (name == "visibility") {
		if (value == "hidden") { props.display = Display::None; return true; }
		if (value == "visible") { props.display = Display::Flex; return true; }
		return false;
	}
	// Propriété inconnue.
	return false;
}

} // namespace ui
