#pragma once
/**
 * @file truetype.hpp
 * @brief Polices TrueType / OpenType (.ttf, .otf, première face d'un .ttc) : noms, style,
 *        métriques, table des caractères, largeurs WinAnsi, programme de police à incorporer
 *        dans un PDF ; inventaire rapide des polices d'un ensemble de dossiers.
 *
 * Aucune dépendance externe : les tables `head`, `hhea`, `maxp`, `hmtx`, `cmap` (formats 4 et
 * 12), `name`, `OS/2` et `post` sont lues directement, avec contrôle de chaque décalage.
 *
 *   auto fonts = pdf::ScanFonts(dirs);                       // inventaire (tables de noms seulement)
 *   auto face = pdf::TrueTypeFont::Load(fonts[0].path);      // police complète
 *   double w = face.Value()->TextWidth("Été", 12.0);
 */
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "pdf/writer.hpp"

namespace pdf {

/// Description d'un fichier de police (inventaire : sans lire les contours ni les métriques).
struct FontDescription {
	std::filesystem::path path;
	std::string family;		///< famille typographique (« DejaVu Sans »)
	std::string style;		///< style (« Bold Oblique »)
	std::string postscript; ///< nom PostScript
	bool bold = false, italic = false;
	int weight = 400;		///< 100 à 900
	bool cff = false;		///< contours CFF (OpenType « OTTO »)
};

/// Lit l'en-tête et les tables de noms d'un fichier ; std::nullopt si ce n'est pas une police
/// lisible (ou sans table des caractères Unicode).
[[nodiscard]] std::optional<FontDescription> DescribeFontFile(const std::filesystem::path &path);
/// Polices .ttf / .otf / .ttc des dossiers (récursivement), triées par famille puis style ;
/// un même nom PostScript n'apparaît qu'une fois.
[[nodiscard]] std::vector<FontDescription> ScanFonts(std::span<const std::filesystem::path> directories);

class TrueTypeFont {
public:
	[[nodiscard]] static PdfResult<std::shared_ptr<const TrueTypeFont>> Load(const std::filesystem::path &path);
	[[nodiscard]] static PdfResult<std::shared_ptr<const TrueTypeFont>> Parse(Bytes data, std::filesystem::path path = {});

	[[nodiscard]] const std::string &Family() const noexcept { return m_desc.family; }
	[[nodiscard]] const std::string &Style() const noexcept { return m_desc.style; }
	[[nodiscard]] const std::string &PostScriptName() const noexcept { return m_desc.postscript; }
	[[nodiscard]] const FontDescription &Description() const noexcept { return m_desc; }
	[[nodiscard]] bool Bold() const noexcept { return m_desc.bold; }
	[[nodiscard]] bool Italic() const noexcept { return m_desc.italic; }
	[[nodiscard]] bool IsCff() const noexcept { return m_desc.cff; }
	/// Incorporation autorisée par la licence (OS/2 fsType : pas « restreinte »).
	[[nodiscard]] bool Embeddable() const noexcept { return (m_fsType & 0x000F) != 0x0002; }

	[[nodiscard]] uint16_t UnitsPerEm() const noexcept { return m_unitsPerEm; }
	/// Métriques en 1/1000 du corps (comme les AFM des polices standard).
	[[nodiscard]] int Ascent() const noexcept { return m_ascent; }
	[[nodiscard]] int Descent() const noexcept { return m_descent; } ///< négative
	[[nodiscard]] int CapHeight() const noexcept { return m_capHeight; }
	[[nodiscard]] double ItalicAngle() const noexcept { return m_italicAngle; }
	[[nodiscard]] const std::array<int, 4> &BoundingBox() const noexcept { return m_bbox; }
	[[nodiscard]] int UnderlinePosition() const noexcept { return m_underlinePosition; }
	[[nodiscard]] int UnderlineThickness() const noexcept { return m_underlineThickness; }

	/// Glyphe d'un point de code (0 : .notdef).
	[[nodiscard]] uint16_t GlyphIndex(char32_t codepoint) const noexcept;
	[[nodiscard]] bool HasGlyph(char32_t codepoint) const noexcept { return GlyphIndex(codepoint) != 0; }
	/// Avance d'un glyphe en 1/1000 du corps.
	[[nodiscard]] double GlyphAdvance(uint16_t glyph) const noexcept;
	/// Largeurs des octets WinAnsi 32 à 255 (1/1000 du corps), écrites dans /Widths du PDF :
	/// la mesure et le positionnement dans le PDF coïncident.
	[[nodiscard]] const std::array<uint16_t, 224> &WinAnsiWidths() const noexcept { return m_widths; }
	/// Largeur d'un texte UTF-8 encodé en WinAnsi (caractères absents remplacés par « ? »).
	[[nodiscard]] double TextWidth(std::string_view utf8, double size) const noexcept;

	/// Programme de police à incorporer : le fichier (face extraite pour un .ttc), ou la table
	/// `CFF ` d'une police OpenType CFF.
	[[nodiscard]] const Bytes &FontProgram() const noexcept { return m_program; }
	[[nodiscard]] const std::filesystem::path &Path() const noexcept { return m_desc.path; }

private:
	TrueTypeFont() = default;

	FontDescription m_desc;
	uint16_t m_unitsPerEm = 1000, m_fsType = 0;
	int m_ascent = 800, m_descent = -200, m_capHeight = 700;
	int m_underlinePosition = -100, m_underlineThickness = 50;
	double m_italicAngle = 0;
	std::array<int, 4> m_bbox{0, -200, 1000, 800};
	std::vector<uint16_t> m_advances; ///< unités de la police, par glyphe
	std::vector<uint16_t> m_bmp;	  ///< glyphe des points de code 0 à 0xFFFF
	struct Group {
		uint32_t first, last, glyph;
	};
	std::vector<Group> m_supplementary; ///< format 12 au-delà du plan multilingue de base
	std::array<uint16_t, 224> m_widths{};
	Bytes m_program;
};

} // namespace pdf
