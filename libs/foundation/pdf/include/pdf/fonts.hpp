#pragma once
/**
 * @file fonts.hpp
 * @brief Polices standard PDF (Helvetica, Times, Courier et leurs variantes) : métriques AFM,
 *        encodage WinAnsi, mesure de texte UTF-8.
 *
 * Ces polices n'ont pas besoin d'être incorporées : tout lecteur PDF les fournit. Les largeurs
 * ASCII (32–126) sont celles des fichiers AFM d'Adobe. Pour les caractères WinAnsi 128–255, les
 * lettres accentuées ont la largeur de leur lettre de base (comme dans les AFM) ; les autres
 * signes reçoivent une largeur approchée (voir README). Le `Writer` écrit ces mêmes largeurs
 * dans le tableau /Widths de chaque police : le positionnement dans le PDF est donc identique à
 * la mesure faite ici.
 *
 *   const double w = pdf::TextWidth(pdf::StandardFont::Helvetica, "Été", 12.0);
 */
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace pdf {

enum class StandardFont : uint8_t {
	Helvetica,
	HelveticaBold,
	HelveticaOblique,
	HelveticaBoldOblique,
	TimesRoman,
	TimesBold,
	TimesItalic,
	TimesBoldItalic,
	Courier,
	CourierBold,
	CourierOblique,
	CourierBoldOblique,
	Count
};
inline constexpr size_t kStandardFontCount = size_t(StandardFont::Count);

/// Métriques d'une police (unités : 1/1000 du corps).
struct FontMetrics {
	std::string_view baseName; ///< nom PostScript (/BaseFont)
	int ascent = 0;			   ///< hauteur des ascendantes (positive)
	int descent = 0;		   ///< profondeur des descendantes (négative)
	int capHeight = 0;
	int underlinePosition = 0; ///< position du soulignement sous la ligne de base (négative)
	int underlineThickness = 0;
	std::array<uint16_t, 224> widths{}; ///< octets WinAnsi 32 à 255
};

[[nodiscard]] const FontMetrics &Metrics(StandardFont font) noexcept;
/// Police standard la plus proche d'une famille : « Times », « Times New Roman », « Georgia »,
/// « serif »… → Times ; « Courier », « Consolas », « monospace »… → Courier ; sinon Helvetica.
[[nodiscard]] StandardFont MatchFont(std::string_view family, bool bold, bool italic) noexcept;
[[nodiscard]] bool IsBold(StandardFont font) noexcept;
[[nodiscard]] bool IsItalic(StandardFont font) noexcept;

/// Octet WinAnsi d'un point de code (0 s'il n'est pas représentable).
[[nodiscard]] uint8_t ToWinAnsi(char32_t codepoint) noexcept;
/// Point de code Unicode d'un octet WinAnsi (U+FFFD pour les octets non attribués).
[[nodiscard]] char32_t FromWinAnsi(uint8_t byte) noexcept;
/// UTF-8 → WinAnsi ; les caractères non représentables deviennent `replacement`.
[[nodiscard]] std::string EncodeWinAnsi(std::string_view utf8, char replacement = '?');
/// WinAnsi → UTF-8.
[[nodiscard]] std::string DecodeWinAnsi(std::string_view bytes);

/// Avance d'un octet WinAnsi en points pour un corps `size` (octets < 32 : largeur nulle).
[[nodiscard]] double ByteWidth(StandardFont font, uint8_t byte, double size) noexcept;
/// Largeur d'un texte UTF-8 (mesuré comme il sera encodé : remplacements compris).
[[nodiscard]] double TextWidth(StandardFont font, std::string_view utf8, double size) noexcept;
/// Largeur d'un texte déjà encodé en WinAnsi.
[[nodiscard]] double EncodedWidth(StandardFont font, std::string_view winAnsi, double size) noexcept;
[[nodiscard]] double Ascent(StandardFont font, double size) noexcept;
/// Profondeur des descendantes, positive.
[[nodiscard]] double Descent(StandardFont font, double size) noexcept;

class TrueTypeFont;

/// Police d'un texte : police standard, ou police TrueType / OpenType chargée (`face`, qui sera
/// incorporée au PDF). Les mesures sont celles qui seront écrites dans le PDF.
struct FontRef {
	StandardFont standard = StandardFont::Helvetica; ///< police standard (ou repli si `face` est nul)
	std::shared_ptr<const TrueTypeFont> face;

	[[nodiscard]] double TextWidth(std::string_view utf8, double size) const noexcept;
	[[nodiscard]] double Ascent(double size) const noexcept;
	[[nodiscard]] double Descent(double size) const noexcept; ///< positive
	/// Distance de la ligne de base au trait de soulignement (positive : sous la ligne).
	[[nodiscard]] double UnderlineOffset(double size) const noexcept;
	[[nodiscard]] double UnderlineThickness(double size) const noexcept;
	[[nodiscard]] bool Bold() const noexcept;
	[[nodiscard]] bool Italic() const noexcept;
	[[nodiscard]] std::string_view Family() const noexcept;
	friend bool operator==(const FontRef &a, const FontRef &b) noexcept { return a.standard == b.standard && a.face == b.face; }
};

} // namespace pdf
