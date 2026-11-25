#include "pdf/fonts.hpp"

#include <algorithm>
#include <cctype>

#include <core/string.hpp>

namespace pdf {

namespace {

using Ascii = std::array<uint16_t, 95>; // octets 32 à 126

// Largeurs AFM d'Adobe (espace … tilde).
constexpr Ascii kHelvetica{
	278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,		   // ␠ à /
	556, 556, 556, 556, 556, 556, 556, 556, 556, 556,									   // 0-9
	278, 278, 584, 584, 584, 556, 1015,													   // : à @
	667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833,					   // A-M
	722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611,					   // N-Z
	278, 278, 278, 469, 556, 333,														   // [ à `
	556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833,					   // a-m
	556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500,					   // n-z
	334, 260, 334, 584};																   // { à ~
constexpr Ascii kHelveticaBold{
	278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278,
	556, 556, 556, 556, 556, 556, 556, 556, 556, 556,
	333, 333, 584, 584, 584, 611, 975,
	722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833,
	722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611,
	333, 278, 333, 584, 556, 333,
	556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889,
	611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500,
	389, 280, 389, 584};
constexpr Ascii kTimesRoman{
	250, 333, 408, 500, 500, 833, 778, 180, 333, 333, 500, 564, 250, 333, 250, 278,
	500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
	278, 278, 564, 564, 564, 444, 921,
	722, 667, 667, 722, 611, 556, 722, 722, 333, 389, 722, 611, 889,
	722, 722, 556, 722, 667, 556, 611, 722, 722, 944, 722, 722, 611,
	333, 278, 333, 469, 500, 333,
	444, 500, 444, 500, 444, 333, 500, 500, 278, 278, 500, 278, 778,
	500, 500, 500, 500, 333, 389, 278, 500, 500, 722, 500, 500, 444,
	480, 200, 480, 541};
constexpr Ascii kTimesBold{
	250, 333, 555, 500, 500, 1000, 833, 278, 333, 333, 500, 570, 250, 333, 250, 278,
	500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
	333, 333, 570, 570, 570, 500, 930,
	722, 667, 722, 722, 667, 611, 778, 778, 389, 500, 778, 667, 944,
	722, 778, 611, 778, 722, 556, 667, 722, 722, 1000, 722, 722, 667,
	333, 278, 333, 581, 500, 333,
	500, 556, 444, 556, 444, 333, 500, 556, 278, 333, 556, 278, 833,
	556, 500, 556, 556, 444, 389, 333, 556, 500, 722, 500, 500, 444,
	394, 220, 394, 520};
constexpr Ascii kTimesItalic{
	250, 333, 420, 500, 500, 833, 778, 214, 333, 333, 500, 675, 250, 333, 250, 278,
	500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
	333, 333, 675, 675, 675, 500, 920,
	611, 611, 667, 722, 611, 611, 722, 722, 333, 444, 667, 556, 833,
	667, 722, 611, 722, 611, 500, 556, 722, 611, 833, 611, 556, 556,
	389, 278, 389, 422, 500, 333,
	500, 500, 444, 500, 444, 278, 500, 500, 278, 278, 444, 278, 722,
	500, 500, 500, 500, 389, 389, 278, 500, 444, 667, 444, 444, 389,
	400, 275, 400, 541};
constexpr Ascii kTimesBoldItalic{
	250, 389, 555, 500, 500, 833, 778, 278, 333, 333, 500, 570, 250, 333, 250, 278,
	500, 500, 500, 500, 500, 500, 500, 500, 500, 500,
	333, 333, 570, 570, 570, 500, 832,
	667, 667, 667, 722, 667, 667, 722, 778, 389, 500, 667, 611, 889,
	722, 722, 611, 722, 667, 556, 611, 722, 667, 889, 667, 611, 611,
	333, 278, 333, 570, 500, 333,
	500, 500, 444, 500, 444, 333, 500, 556, 278, 278, 500, 278, 778,
	556, 500, 500, 500, 389, 389, 278, 556, 444, 667, 500, 444, 389,
	348, 220, 348, 570};

/// Points de code des octets WinAnsi 0x80 à 0x9F (0 : non attribué).
constexpr std::array<char32_t, 32> kWinAnsiHigh{
	0x20AC, 0,		0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
	0x2039, 0x0152, 0,		0x017D, 0,		0,		0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
	0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,		0x017E, 0x0178};

/// Largeurs des octets WinAnsi 0x80 à 0xFF (0 : octet non attribué), relevées dans les AFM des
/// polices URW aux métriques identiques (Nimbus Sans / Nimbus Roman : leurs largeurs ASCII sont
/// celles des AFM d'Adobe ci-dessus, à l'unité près).
using High = std::array<uint16_t, 128>;
constexpr High kHelveticaHigh{
	556, 0, 222, 556, 333, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
	0, 222, 222, 333, 333, 350, 556, 1000, 333, 1000, 500, 333, 944, 0, 500, 667,
	278, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 333, 737, 333,
	400, 584, 333, 333, 333, 556, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611,
	667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
	722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
	556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278,
	556, 556, 556, 556, 556, 556, 556, 584, 611, 556, 556, 556, 556, 500, 556, 500};
constexpr High kHelveticaBoldHigh{
	556, 0, 278, 556, 500, 1000, 556, 556, 333, 1000, 667, 333, 1000, 0, 611, 0,
	0, 278, 278, 500, 500, 350, 556, 1000, 333, 1000, 556, 333, 944, 0, 500, 667,
	278, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 333, 737, 333,
	400, 584, 333, 333, 333, 611, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611,
	722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278,
	722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611,
	556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278,
	611, 611, 611, 611, 611, 611, 611, 584, 611, 611, 611, 611, 611, 556, 611, 556};
constexpr High kTimesRomanHigh{
	500, 0, 333, 500, 444, 1000, 500, 500, 333, 1000, 556, 333, 889, 0, 611, 0,
	0, 333, 333, 444, 444, 350, 500, 1000, 333, 980, 389, 333, 722, 0, 444, 722,
	250, 333, 500, 500, 500, 500, 200, 500, 333, 760, 276, 500, 564, 333, 760, 333,
	400, 564, 300, 300, 333, 500, 453, 250, 333, 300, 310, 500, 750, 750, 750, 444,
	722, 722, 722, 722, 722, 722, 889, 667, 611, 611, 611, 611, 333, 333, 333, 333,
	722, 722, 722, 722, 722, 722, 722, 564, 722, 722, 722, 722, 722, 722, 556, 500,
	444, 444, 444, 444, 444, 444, 667, 444, 444, 444, 444, 444, 278, 278, 278, 278,
	500, 500, 500, 500, 500, 500, 500, 564, 500, 500, 500, 500, 500, 500, 500, 500};
constexpr High kTimesBoldHigh{
	500, 0, 333, 500, 500, 1000, 500, 500, 333, 1000, 556, 333, 1000, 0, 667, 0,
	0, 333, 333, 500, 500, 350, 500, 1000, 333, 1000, 389, 333, 722, 0, 444, 722,
	250, 333, 500, 500, 500, 500, 220, 500, 333, 747, 300, 500, 570, 333, 747, 333,
	400, 570, 300, 300, 333, 556, 540, 250, 333, 300, 330, 500, 750, 750, 750, 500,
	722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 389, 389, 389, 389,
	722, 722, 778, 778, 778, 778, 778, 570, 778, 722, 722, 722, 722, 722, 611, 556,
	500, 500, 500, 500, 500, 500, 722, 444, 444, 444, 444, 444, 278, 278, 278, 278,
	500, 556, 500, 500, 500, 500, 500, 570, 500, 556, 556, 556, 556, 500, 556, 500};
constexpr High kTimesItalicHigh{
	500, 0, 333, 500, 556, 889, 500, 500, 333, 1000, 500, 333, 944, 0, 556, 0,
	0, 333, 333, 556, 556, 350, 500, 889, 333, 980, 389, 333, 667, 0, 389, 556,
	250, 389, 500, 500, 500, 500, 275, 500, 333, 760, 276, 500, 675, 333, 760, 333,
	400, 675, 300, 300, 333, 500, 523, 250, 333, 300, 310, 500, 750, 750, 750, 500,
	611, 611, 611, 611, 611, 611, 889, 667, 611, 611, 611, 611, 333, 333, 333, 333,
	722, 667, 722, 722, 722, 722, 722, 675, 722, 722, 722, 722, 722, 556, 611, 500,
	500, 500, 500, 500, 500, 500, 667, 444, 444, 444, 444, 444, 278, 278, 278, 278,
	500, 500, 500, 500, 500, 500, 500, 675, 500, 500, 500, 500, 500, 444, 500, 444};
constexpr High kTimesBoldItalicHigh{
	500, 0, 333, 500, 500, 1000, 500, 500, 333, 1000, 556, 333, 944, 0, 611, 0,
	0, 333, 333, 500, 500, 350, 500, 1000, 333, 1000, 389, 333, 722, 0, 389, 611,
	250, 389, 500, 500, 500, 500, 220, 500, 333, 747, 266, 500, 606, 333, 747, 333,
	400, 570, 300, 300, 333, 576, 500, 250, 333, 300, 300, 500, 750, 750, 750, 500,
	667, 667, 667, 667, 667, 667, 944, 667, 667, 667, 667, 667, 389, 389, 389, 389,
	722, 722, 722, 722, 722, 722, 722, 570, 722, 722, 722, 722, 722, 611, 611, 500,
	500, 500, 500, 500, 500, 500, 722, 444, 444, 444, 444, 444, 278, 278, 278, 278,
	500, 556, 500, 500, 500, 500, 500, 570, 500, 556, 556, 556, 556, 444, 500, 444};

/// Octets WinAnsi non attribués (0x81, 0x8D, 0x8F, 0x90, 0x9D).
constexpr bool Unassigned(size_t b) noexcept { return b >= 0x80 && b <= 0x9F && kWinAnsiHigh[b - 0x80] == 0; }

FontMetrics Build(std::string_view name, const Ascii *ascii, const High *high, int ascent, int descent, int cap) {
	FontMetrics m;
	m.baseName = name;
	m.ascent = ascent;
	m.descent = descent;
	m.capHeight = cap;
	m.underlinePosition = -100;
	m.underlineThickness = 50;
	const auto widthOf = [&](char c) -> uint16_t { return ascii ? (*ascii)[size_t(uint8_t(c) - 32)] : 600; };
	for (size_t b = 32; b < 256; ++b) {
		uint16_t w = 0;
		if (b < 127)
			w = widthOf(char(b));
		else if (b == 127)
			w = widthOf(' ');
		else if (!Unassigned(b))
			w = high ? (*high)[b - 128] : 600; // Courier : chasse fixe
		m.widths[b - 32] = w;
	}
	return m;
}

const std::array<FontMetrics, kStandardFontCount> &Table() {
	static const std::array<FontMetrics, kStandardFontCount> table{
		Build("Helvetica", &kHelvetica, &kHelveticaHigh, 718, -207, 718),
		Build("Helvetica-Bold", &kHelveticaBold, &kHelveticaBoldHigh, 718, -207, 718),
		Build("Helvetica-Oblique", &kHelvetica, &kHelveticaHigh, 718, -207, 718),
		Build("Helvetica-BoldOblique", &kHelveticaBold, &kHelveticaBoldHigh, 718, -207, 718),
		Build("Times-Roman", &kTimesRoman, &kTimesRomanHigh, 683, -217, 662),
		Build("Times-Bold", &kTimesBold, &kTimesBoldHigh, 683, -217, 676),
		Build("Times-Italic", &kTimesItalic, &kTimesItalicHigh, 683, -217, 653),
		Build("Times-BoldItalic", &kTimesBoldItalic, &kTimesBoldItalicHigh, 683, -217, 669),
		Build("Courier", nullptr, nullptr, 629, -157, 562),
		Build("Courier-Bold", nullptr, nullptr, 629, -157, 562),
		Build("Courier-Oblique", nullptr, nullptr, 629, -157, 562),
		Build("Courier-BoldOblique", nullptr, nullptr, 629, -157, 562),
	};
	return table;
}

bool Has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

} // namespace

const FontMetrics &Metrics(StandardFont font) noexcept { return Table()[std::min(size_t(font), kStandardFontCount - 1)]; }

StandardFont MatchFont(std::string_view family, bool bold, bool italic) noexcept {
	std::string f(family);
	for (char &c : f)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	int base = 0; // 0 Helvetica, 4 Times, 8 Courier
	if (Has(f, "courier") || Has(f, "mono") || Has(f, "consolas") || Has(f, "menlo") || Has(f, "code"))
		base = 8;
	else if (!Has(f, "sans") &&
			 (Has(f, "times") || Has(f, "serif") || Has(f, "georgia") || Has(f, "garamond") || Has(f, "cambria") || Has(f, "roman")))
		base = 4;
	return StandardFont(base + (bold ? 1 : 0) + (italic ? 2 : 0));
}

bool IsBold(StandardFont font) noexcept { return (size_t(font) & 1) != 0; }
bool IsItalic(StandardFont font) noexcept { return (size_t(font) & 2) != 0; }

uint8_t ToWinAnsi(char32_t cp) noexcept {
	if ((cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0xFF))
		return uint8_t(cp);
	for (size_t i = 0; i < kWinAnsiHigh.size(); ++i)
		if (kWinAnsiHigh[i] == cp && cp != 0)
			return uint8_t(0x80 + i);
	return 0;
}

char32_t FromWinAnsi(uint8_t byte) noexcept {
	if (byte >= 0x80 && byte <= 0x9F) {
		const char32_t cp = kWinAnsiHigh[byte - 0x80];
		return cp ? cp : U'�';
	}
	return byte;
}

std::string EncodeWinAnsi(std::string_view utf8, char replacement) {
	std::string out;
	out.reserve(utf8.size());
	for (size_t i = 0; i < utf8.size();) {
		const char32_t cp = unicode::DecodeUtf8(utf8, i);
		const uint8_t b = ToWinAnsi(cp);
		out.push_back(b ? char(b) : replacement);
	}
	return out;
}

std::string DecodeWinAnsi(std::string_view bytes) {
	std::string out;
	out.reserve(bytes.size());
	for (const char c : bytes)
		unicode::AppendUtf8(out, FromWinAnsi(uint8_t(c)));
	return out;
}

double ByteWidth(StandardFont font, uint8_t byte, double size) noexcept {
	return byte < 32 ? 0.0 : Metrics(font).widths[byte - 32] * size / 1000.0;
}

double EncodedWidth(StandardFont font, std::string_view winAnsi, double size) noexcept {
	const FontMetrics &m = Metrics(font);
	uint32_t units = 0;
	for (const char c : winAnsi)
		if (const uint8_t b = uint8_t(c); b >= 32)
			units += m.widths[b - 32];
	return units * size / 1000.0;
}

double TextWidth(StandardFont font, std::string_view utf8, double size) noexcept {
	const FontMetrics &m = Metrics(font);
	uint32_t units = 0;
	for (size_t i = 0; i < utf8.size();) {
		const uint8_t b = ToWinAnsi(unicode::DecodeUtf8(utf8, i));
		units += m.widths[(b ? b : uint8_t('?')) - 32];
	}
	return units * size / 1000.0;
}

double Ascent(StandardFont font, double size) noexcept { return Metrics(font).ascent * size / 1000.0; }
double Descent(StandardFont font, double size) noexcept { return -Metrics(font).descent * size / 1000.0; }

} // namespace pdf
