#pragma once
/**
 * @file style.hpp
 * @brief Style déclaratif d'un widget : boîte (margin/padding/border), position,
 *        taille, police — avec des unités auto-adaptatives (px, %, em, rem,
 *        vw, vh, auto, fr) et les propriétés de mise en page flex / grid.
 *
 * Deux représentations, comme en CSS :
 *  - `StyleProps`  : valeurs DÉCLARÉES (chaque champ est un Option ; NONE =
 *                    non définie) — posées par un builder ou une règle CSS ;
 *  - `ComputedStyle` : valeurs CALCULÉES (tous les champs résolus) — lues par
 *                    la mise en page et le rendu.
 *
 * Les couleurs sont des `sdl3::Color` (8 bits/composante) — identiques à
 * `sdl3::Color` et `ui::Color` déjà en usage, donc directement dessinables.
 * Ce sous-ensemble reprend les concepts du module `ui` de SDL3_Wrapper.
 */
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <math/math.hpp>
#include <sdl3/core/structs.hpp>
#include <sdl3/input/input.hpp> // SystemCursor (curseur système)

#include "layout.hpp" // Anchor, Align (partagés avec la mise en page immédiate)

namespace ui {

// ============================================================================
// Length — longueur CSS résoluble
// ============================================================================

enum class Unit : uint8_t {
	Px,   ///< pixels
	Pct,  ///< % de la boîte de contenu du parent (axe concerné)
	Em,   ///< multiple de la taille de police du widget
	Rem,  ///< multiple de la taille de police de la racine
	Vw,   ///< % de la largeur de la fenêtre
	Vh,   ///< % de la hauteur de la fenêtre
	Auto, ///< taille intrinsèque (contenu)
	Fr,   ///< part de l'espace restant sur l'axe principal (poids)
};

/// Bases de résolution d'une longueur.
struct LengthBasis {
	float parent = 0.f;
	float fontSize = 14.f;
	float rootFontSize = 14.f;
	float viewportW = 0.f;
	float viewportH = 0.f;
	float scale = 1.f;
};

struct Length {
	float value = 0.f;
	Unit unit = Unit::Auto;
	float offset = 0.f; ///< pixels ajoutés après résolution (`calc(50% - 8px)`)

	constexpr Length() = default;
	/// Conversion implicite : `Length(140.f)` = 140 pixels.
	constexpr Length(float px) noexcept : value(px), unit(Unit::Px), offset(0.f) {}
	constexpr Length(float v, Unit u, float o) noexcept : value(v), unit(u), offset(o) {}

	[[nodiscard]] static constexpr Length Px(float v) noexcept { return Length(v, Unit::Px, 0.f); }
	[[nodiscard]] static constexpr Length Pct(float v) noexcept { return Length(v, Unit::Pct, 0.f); }
	[[nodiscard]] static constexpr Length Em(float v) noexcept { return Length(v, Unit::Em, 0.f); }
	[[nodiscard]] static constexpr Length Rem(float v) noexcept { return Length(v, Unit::Rem, 0.f); }
	[[nodiscard]] static constexpr Length Vw(float v) noexcept { return Length(v, Unit::Vw, 0.f); }
	[[nodiscard]] static constexpr Length Vh(float v) noexcept { return Length(v, Unit::Vh, 0.f); }
	[[nodiscard]] static constexpr Length Auto() noexcept { return Length(0.f, Unit::Auto, 0.f); }
	[[nodiscard]] static constexpr Length Fr(float weight = 1.f) noexcept { return Length(weight, Unit::Fr, 0.f); }

	/// Ajoute un décalage fixe en pixels.
	[[nodiscard]] constexpr Length Plus(float px) const noexcept { return Length(value, unit, offset + px); }

	[[nodiscard]] constexpr bool IsAuto() const noexcept { return unit == Unit::Auto; }
	[[nodiscard]] constexpr bool IsFr() const noexcept { return unit == Unit::Fr; }
	[[nodiscard]] constexpr bool IsRelative() const noexcept { return unit == Unit::Pct; }

	/// Résout en pixels (Auto et Fr résolvent à `offset` seul).
	[[nodiscard]] constexpr float Resolve(const LengthBasis &b) const noexcept {
		float base = 0.f;
		switch (unit) {
		case Unit::Px: base = value * b.scale; break;
		case Unit::Pct: base = value / 100.f * b.parent; break;
		case Unit::Em: base = value * b.fontSize; break;
		case Unit::Rem: base = value * b.rootFontSize * b.scale; break;
		case Unit::Vw: base = value / 100.f * b.viewportW; break;
		case Unit::Vh: base = value / 100.f * b.viewportH; break;
		case Unit::Auto:
		case Unit::Fr: break;
		}
		return base + offset * b.scale;
	}

	constexpr bool operator==(const Length &) const = default;
};

// ============================================================================
// Énumérations de mise en page et de typographie
// ============================================================================

enum class Display : uint8_t { Flex, Grid, Stack, None };
enum class FlexDirection : uint8_t { Row, Column, RowReverse, ColumnReverse };
enum class FlexWrap : uint8_t { NoWrap, Wrap };
enum class Justify : uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class Position : uint8_t { Relative, Absolute, Fixed };
enum class Overflow : uint8_t { Visible, Hidden, Scroll, Auto };
enum class TextAlign : uint8_t { Left, Center, Right };
enum class FontStyle : uint8_t { Normal, Italic };

/// Fonction d'interpolation (easing) d'une animation.
enum class Easing : uint8_t { Linear, InQuad, OutQuad, InOutQuad, InCubic, OutCubic, InOutCubic };
/// Applique l'easing à `t` (0..1) et rend une valeur 0..1.
[[nodiscard]] float Ease(float t, Easing e) noexcept;

/// Pseudo-classes d'état, combinables.
enum class State : uint16_t {
	None = 0,
	Hover = 1 << 0,
	Active = 1 << 1,
	Focus = 1 << 2,
	Checked = 1 << 3,
	Disabled = 1 << 4,
	Selected = 1 << 5,
	Open = 1 << 6,
};

[[nodiscard]] constexpr State operator|(State a, State b) noexcept { return State(uint16_t(a) | uint16_t(b)); }
[[nodiscard]] constexpr State operator&(State a, State b) noexcept { return State(uint16_t(a) & uint16_t(b)); }
[[nodiscard]] constexpr State operator~(State a) noexcept { return State(uint16_t(~uint16_t(a))); }
[[nodiscard]] constexpr bool HasState(State set, State flags) noexcept {
	return (uint16_t(set) & uint16_t(flags)) == uint16_t(flags);
}

// ============================================================================
// StyleProps — valeurs déclarées
// ============================================================================

struct StyleProps {
	// Position / taille
	std::optional<Display> display;
	std::optional<Position> position;
	std::optional<Anchor> anchor;
	std::optional<Length> top, right, bottom, left;
	std::optional<Length> width, height, minWidth, minHeight, maxWidth, maxHeight;
	// Boîte : marges, padding, bordure
	std::optional<float> marginTop, marginRight, marginBottom, marginLeft;
	std::optional<float> paddingTop, paddingRight, paddingBottom, paddingLeft;
	std::optional<sdl3::Color> borderColor;
	std::optional<float> borderTopWidth, borderRightWidth, borderBottomWidth, borderLeftWidth;
	std::optional<float> borderTopLeftRadius, borderTopRightRadius, borderBottomRightRadius, borderBottomLeftRadius;
	std::optional<sdl3::Color> backgroundColor;
	std::optional<float> opacity;
	std::optional<int> zIndex;
	// Flex / grid
	std::optional<FlexDirection> flexDirection;
	std::optional<FlexWrap> flexWrap;
	std::optional<Justify> justifyContent;
	std::optional<Align> alignItems, alignSelf;
	std::optional<float> flexGrow, flexShrink, gap;
	std::optional<int> gridColumns;
	std::optional<Overflow> overflowX, overflowY;
	// Typographie
	std::optional<sdl3::Color> color;
	std::optional<float> fontSize;
	std::optional<int> fontWeight;
	std::optional<FontStyle> fontStyle;
	std::optional<bool> underline;
	std::optional<TextAlign> textAlign;
	// Curseur de la souris au survol (et selon l'état : `:hover`, `:active`…).
	std::optional<sdl3::SystemCursor> cursor;
	// Couleurs sémantiques (héritables) : remplacent celles du thème `ui::Theme`
	// pour un widget ou tout un sous-arbre. Transparent = hérité du thème.
	std::optional<sdl3::Color> accentColor;       ///< accent (sélection, cases cochées, liens)
	std::optional<sdl3::Color> widgetColor;       ///< pistes, cases, cadrans, boutons de SpinBox
	std::optional<sdl3::Color> widgetHoverColor;  ///< survol d'une ligne / d'un onglet
	std::optional<sdl3::Color> widgetActiveColor; ///< ligne / onglet sélectionné, piste active
	std::optional<sdl3::Color> titleColor;        ///< en-tête de tableau, barre de titre

	/// Les champs définis dans `over` remplacent ceux de *this.
	void Merge(const StyleProps &over);
	/// Ne garde que les propriétés héritables (couleur, police).
	void MergeInherited(const StyleProps &over);

	void SetMargin(const math::Sides &s);
	void SetPadding(const math::Sides &s);
	void SetBorderWidth(const math::Sides &s);
	void SetBorderRadius(const math::Corners &c);
	[[nodiscard]] bool IsEmpty() const noexcept;
};

// ============================================================================
// ComputedStyle — valeurs calculées (toutes résolues)
// ============================================================================

struct ComputedStyle {
	Display display = Display::Flex;
	Position position = Position::Relative;
	Anchor anchor = Anchor::TopLeft;
	Length top, right, bottom, left;
	Length width, height, minWidth, minHeight, maxWidth, maxHeight;
	float marginTop = 0.f, marginRight = 0.f, marginBottom = 0.f, marginLeft = 0.f;
	float paddingTop = 0.f, paddingRight = 0.f, paddingBottom = 0.f, paddingLeft = 0.f;
	sdl3::Color borderColor{0, 0, 0, 0};
	float borderTopWidth = 0.f, borderRightWidth = 0.f, borderBottomWidth = 0.f, borderLeftWidth = 0.f;
	float borderTopLeftRadius = 0.f, borderTopRightRadius = 0.f, borderBottomRightRadius = 0.f, borderBottomLeftRadius = 0.f;
	sdl3::Color backgroundColor{0, 0, 0, 0};
	float opacity = 1.f;
	int zIndex = 0;
	FlexDirection flexDirection = FlexDirection::Column;
	FlexWrap flexWrap = FlexWrap::NoWrap;
	Justify justifyContent = Justify::Start;
	Align alignItems = Align::Stretch;
	std::optional<Align> alignSelf; ///< NONE = hérite de align-items
	float flexGrow = 0.f, flexShrink = 1.f, gap = 0.f;
	int gridColumns = 1;
	Overflow overflowX = Overflow::Visible, overflowY = Overflow::Visible;
	sdl3::Color color{230, 232, 240, 255};
	float fontSize = 14.f;
	int fontWeight = 400;
	FontStyle fontStyle = FontStyle::Normal;
	bool underline = false;
	TextAlign textAlign = TextAlign::Left;
	/// Curseur système résolu (NONE : aucun curseur posé par le style).
	std::optional<sdl3::SystemCursor> cursor;
	/// Couleurs sémantiques résolues (transparent : remplacées par le thème au rendu).
	sdl3::Color accentColor{0, 0, 0, 0};
	sdl3::Color widgetColor{0, 0, 0, 0};
	sdl3::Color widgetHoverColor{0, 0, 0, 0};
	sdl3::Color widgetActiveColor{0, 0, 0, 0};
	sdl3::Color titleColor{0, 0, 0, 0};

	/// Copie dans *this les valeurs définies dans `props`.
	void Apply(const StyleProps &props);
	/// Copie seulement les valeurs héritables définies dans `props`.
	void ApplyInherited(const StyleProps &props);

	[[nodiscard]] math::Sides Margin() const noexcept { return {marginLeft, marginTop, marginRight, marginBottom}; }
	[[nodiscard]] math::Sides Padding() const noexcept { return {paddingLeft, paddingTop, paddingRight, paddingBottom}; }
	[[nodiscard]] math::Sides BorderWidth() const noexcept {
		return {borderLeftWidth, borderTopWidth, borderRightWidth, borderBottomWidth};
	}
	[[nodiscard]] math::Corners BorderRadius() const noexcept {
		return {borderTopLeftRadius, borderTopRightRadius, borderBottomLeftRadius, borderBottomRightRadius};
	}
	[[nodiscard]] bool HasBorder() const noexcept {
		return borderColor.a > 0 &&
			   (borderTopWidth > 0.f || borderRightWidth > 0.f || borderBottomWidth > 0.f || borderLeftWidth > 0.f);
	}
	[[nodiscard]] math::Sides BorderBox() const noexcept { return HasBorder() ? BorderWidth() : math::Sides{}; }
	[[nodiscard]] bool Clips() const noexcept {
		return overflowX != Overflow::Visible || overflowY != Overflow::Visible;
	}
	[[nodiscard]] bool IsRow() const noexcept {
		return flexDirection == FlexDirection::Row || flexDirection == FlexDirection::RowReverse;
	}
	[[nodiscard]] bool IsBold() const noexcept { return fontWeight >= 600; }
};

// ============================================================================
// Analyse des valeurs CSS
// ============================================================================

[[nodiscard]] std::optional<sdl3::Color> ParseColor(std::string_view text) noexcept;
[[nodiscard]] std::optional<Length> ParseLength(std::string_view text) noexcept;
[[nodiscard]] std::optional<float> ParsePixels(std::string_view text) noexcept;

/// Pose une déclaration `nom: valeur` (raccourcis compris : margin, padding,
/// border, border-radius, background, font-size, flex…). Rend faux si la
/// propriété est inconnue ou la valeur invalide.
bool SetDeclaration(StyleProps &props, std::string_view name, std::string_view value);

} // namespace ui
