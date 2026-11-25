#include "pdf/truetype.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <set>

#include <core/string.hpp>

#include "pdf/fonts.hpp"

namespace pdf {

namespace {

constexpr uint32_t Tag(std::string_view t) {
	return uint32_t(uint8_t(t[0])) << 24 | uint32_t(uint8_t(t[1])) << 16 | uint32_t(uint8_t(t[2])) << 8 | uint8_t(t[3]);
}
constexpr uint32_t kTrueType = 0x00010000, kOtto = 0x4F54544F /* OTTO */, kTrue = 0x74727565 /* true */, kTtcf = 0x74746366 /* ttcf */;

/// Lecture gros-boutiste bornée d'un tampon.
class Reader {
public:
	explicit Reader(std::span<const uint8_t> data) noexcept : m_data(data) {}
	[[nodiscard]] bool Has(size_t offset, size_t size) const noexcept { return offset <= m_data.size() && size <= m_data.size() - offset; }
	[[nodiscard]] uint8_t U8(size_t o) const noexcept { return Has(o, 1) ? m_data[o] : 0; }
	[[nodiscard]] uint16_t U16(size_t o) const noexcept { return Has(o, 2) ? uint16_t(m_data[o] << 8 | m_data[o + 1]) : 0; }
	[[nodiscard]] int16_t S16(size_t o) const noexcept { return int16_t(U16(o)); }
	[[nodiscard]] uint32_t U32(size_t o) const noexcept {
		return Has(o, 4) ? uint32_t(m_data[o]) << 24 | uint32_t(m_data[o + 1]) << 16 | uint32_t(m_data[o + 2]) << 8 | m_data[o + 3] : 0;
	}
	[[nodiscard]] std::span<const uint8_t> Span(size_t o, size_t n) const noexcept { return Has(o, n) ? m_data.subspan(o, n) : std::span<const uint8_t>{}; }
	[[nodiscard]] size_t Size() const noexcept { return m_data.size(); }

private:
	std::span<const uint8_t> m_data;
};

struct TableEntry {
	uint32_t tag, offset, length;
};

/// Répertoire des tables d'une face commençant à `faceOffset`.
std::vector<TableEntry> Tables(const Reader &r, size_t faceOffset) {
	std::vector<TableEntry> out;
	const uint16_t count = r.U16(faceOffset + 4);
	if (!r.Has(faceOffset + 12, size_t(count) * 16))
		return out;
	for (uint16_t i = 0; i < count; ++i) {
		const size_t e = faceOffset + 12 + size_t(i) * 16;
		const TableEntry t{r.U32(e), r.U32(e + 8), r.U32(e + 12)};
		if (r.Has(t.offset, t.length))
			out.push_back(t);
	}
	return out;
}

const TableEntry *Find(const std::vector<TableEntry> &tables, uint32_t tag) {
	const auto it = std::ranges::find(tables, tag, &TableEntry::tag);
	return it == tables.end() ? nullptr : &*it;
}

std::string Utf16BeToUtf8(std::span<const uint8_t> s) {
	std::u16string u;
	for (size_t i = 0; i + 1 < s.size(); i += 2)
		u.push_back(char16_t(s[i] << 8 | s[i + 1]));
	return unicode::FromUtf16(u);
}

/// Table `name` : (identifiant → texte), Windows Unicode anglais d'abord, puis tout Windows
/// Unicode, puis Macintosh Roman (ASCII).
std::map<uint16_t, std::string> Names(const Reader &r, const TableEntry &t) {
	std::map<uint16_t, std::pair<int, std::string>> best; // id → (rang de préférence, texte)
	const size_t base = t.offset;
	const uint16_t count = r.U16(base + 2), storage = r.U16(base + 4);
	for (uint16_t i = 0; i < count; ++i) {
		const size_t rec = base + 6 + size_t(i) * 12;
		if (rec + 12 > base + t.length)
			break;
		const uint16_t platform = r.U16(rec), encoding = r.U16(rec + 2), language = r.U16(rec + 4), id = r.U16(rec + 6);
		const uint16_t length = r.U16(rec + 8), offset = r.U16(rec + 10);
		const auto bytes = r.Span(base + storage + offset, length);
		if (bytes.empty() && length != 0)
			continue;
		int rank = 99;
		std::string text;
		if (platform == 3 && (encoding == 1 || encoding == 10)) {
			rank = language == 0x409 ? 0 : 1;
			text = Utf16BeToUtf8(bytes);
		} else if (platform == 0) {
			rank = 2;
			text = Utf16BeToUtf8(bytes);
		} else if (platform == 1 && encoding == 0) {
			rank = 3;
			text.assign(bytes.begin(), bytes.end());
		} else {
			continue;
		}
		auto it = best.find(id);
		if (it == best.end() || rank < it->second.first)
			best[id] = {rank, std::move(text)};
	}
	std::map<uint16_t, std::string> out;
	for (auto &[id, v] : best)
		out[id] = std::move(v.second);
	return out;
}

/// Description depuis les tables `name`, `OS/2`, `head` (inventaire et chargement complet).
bool Describe(const Reader &r, const std::vector<TableEntry> &tables, uint32_t sfnt, FontDescription &d) {
	const TableEntry *name = Find(tables, Tag("name"));
	const TableEntry *head = Find(tables, Tag("head"));
	if (!name || !head || !Find(tables, Tag("cmap")))
		return false;
	const auto names = Names(r, *name);
	const auto get = [&](uint16_t id) {
		const auto it = names.find(id);
		return it == names.end() ? std::string() : it->second;
	};
	d.family = !get(16).empty() ? get(16) : get(1);
	d.style = !get(17).empty() ? get(17) : get(2);
	d.postscript = get(6);
	if (d.family.empty())
		return false;
	if (d.style.empty())
		d.style = "Regular";
	const uint16_t macStyle = r.U16(head->offset + 44);
	d.bold = (macStyle & 1) != 0;
	d.italic = (macStyle & 2) != 0;
	if (const TableEntry *os2 = Find(tables, Tag("OS/2")); os2 && os2->length >= 64) {
		d.weight = r.U16(os2->offset + 4);
		const uint16_t selection = r.U16(os2->offset + 62);
		d.bold = d.bold || (selection & 0x20) != 0 || d.weight >= 600;
		d.italic = d.italic || (selection & 0x201) != 0; // italique ou oblique
	} else {
		d.weight = d.bold ? 700 : 400;
	}
	d.cff = sfnt == kOtto;
	if (d.postscript.empty()) {
		for (char c : d.family + "-" + d.style)
			if (c != ' ')
				d.postscript.push_back(c);
	}
	return true;
}

bool LowerExtension(const std::filesystem::path &p, std::string_view ext) {
	std::string e = p.extension().string();
	for (char &c : e)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return e == ext;
}

/// Face autonome extraite d'une collection .ttc (répertoire des tables reconstruit).
Bytes ExtractFace(const Reader &r, const std::vector<TableEntry> &tables, uint32_t sfnt) {
	Bytes out;
	const auto put16 = [&](uint16_t v) { out.insert(out.end(), {uint8_t(v >> 8), uint8_t(v)}); };
	const auto put32 = [&](uint32_t v) { out.insert(out.end(), {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}); };
	const uint16_t n = uint16_t(tables.size());
	uint16_t power = 1, log2 = 0;
	while (power * 2 <= n) {
		power *= 2;
		++log2;
	}
	put32(sfnt);
	put16(n);
	put16(uint16_t(power * 16));
	put16(log2);
	put16(uint16_t(n * 16 - power * 16));
	size_t offset = 12 + size_t(n) * 16;
	std::vector<size_t> offsets;
	for (const TableEntry &t : tables) {
		offsets.push_back(offset);
		offset += (size_t(t.length) + 3) & ~size_t(3);
	}
	for (size_t i = 0; i < tables.size(); ++i) {
		put32(tables[i].tag);
		put32(0); // somme de contrôle : non vérifiée par les lecteurs PDF
		put32(uint32_t(offsets[i]));
		put32(tables[i].length);
	}
	for (const TableEntry &t : tables) {
		const auto bytes = r.Span(t.offset, t.length);
		out.insert(out.end(), bytes.begin(), bytes.end());
		while (out.size() % 4)
			out.push_back(0);
	}
	return out;
}

} // namespace

std::optional<FontDescription> DescribeFontFile(const std::filesystem::path &path) {
	if (!LowerExtension(path, ".ttf") && !LowerExtension(path, ".otf") && !LowerExtension(path, ".ttc"))
		return std::nullopt;
	std::FILE *f = std::fopen(path.string().c_str(), "rb");
	if (!f)
		return std::nullopt;
	// Lecture partielle : en-tête, répertoire, puis seulement les tables utiles (une police CJK
	// peut peser des dizaines de mégaoctets).
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec || size < 12) {
		std::fclose(f);
		return std::nullopt;
	}
	Bytes data(size_t(size), 0);
	bool readOk = true; // une lecture incomplète rejette le fichier (zéros pris pour des données)
	const auto readAt = [&](size_t offset, size_t length) {
		if (offset >= data.size())
			return;
		length = std::min(length, data.size() - offset);
		if (std::fseek(f, long(offset), SEEK_SET) != 0 || std::fread(data.data() + offset, 1, length, f) != length)
			readOk = false;
	};
	readAt(0, 12);
	if (!readOk) {
		std::fclose(f);
		return std::nullopt;
	}
	Reader r(data);
	size_t face = 0;
	if (r.U32(0) == kTtcf) {
		readAt(12, 4);
		face = r.U32(12);
		readAt(face, 12);
	}
	const uint32_t sfnt = r.U32(face);
	if (sfnt != kTrueType && sfnt != kOtto && sfnt != kTrue) {
		std::fclose(f);
		return std::nullopt;
	}
	readAt(face + 12, size_t(r.U16(face + 4)) * 16);
	const auto tables = Tables(r, face);
	for (const char *tag : {"name", "head", "OS/2"})
		if (const TableEntry *t = Find(tables, Tag(tag)))
			readAt(t->offset, t->length);
	std::fclose(f);
	if (!readOk)
		return std::nullopt;
	FontDescription d;
	d.path = path;
	if (!Describe(r, tables, sfnt, d))
		return std::nullopt;
	return d;
}

std::vector<FontDescription> ScanFonts(std::span<const std::filesystem::path> directories) {
	std::vector<FontDescription> out;
	std::set<std::string> seen;
	for (const auto &dir : directories) {
		std::error_code ec;
		if (!std::filesystem::is_directory(dir, ec))
			continue;
		for (auto it = std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec);
			 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
			if (!it->is_regular_file(ec))
				continue;
			auto d = DescribeFontFile(it->path());
			if (d && seen.insert(d->postscript).second)
				out.push_back(std::move(*d));
		}
	}
	std::ranges::sort(out, [](const FontDescription &a, const FontDescription &b) {
		return std::tie(a.family, a.weight, a.italic, a.style) < std::tie(b.family, b.weight, b.italic, b.style);
	});
	return out;
}

PdfResult<std::shared_ptr<const TrueTypeFont>> TrueTypeFont::Load(const std::filesystem::path &path) {
	std::FILE *f = std::fopen(path.string().c_str(), "rb");
	if (!f)
		return Err(Error{std::format("police introuvable : {}", path.string())});
	Bytes data;
	uint8_t buffer[65536];
	for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, f)) > 0;)
		data.insert(data.end(), buffer, buffer + n);
	std::fclose(f);
	return Parse(std::move(data), path);
}

PdfResult<std::shared_ptr<const TrueTypeFont>> TrueTypeFont::Parse(Bytes data, std::filesystem::path path) {
	const auto fail = [&](std::string_view what) { return Err(Error{std::format("police {} : {}", path.string(), what)}); };
	Reader r(data);
	size_t face = 0;
	const bool collection = r.U32(0) == kTtcf;
	if (collection)
		face = r.U32(12);
	const uint32_t sfnt = r.U32(face);
	if (sfnt != kTrueType && sfnt != kOtto && sfnt != kTrue)
		return fail("format non reconnu");
	const auto tables = Tables(r, face);
	auto font = std::shared_ptr<TrueTypeFont>(new TrueTypeFont());
	font->m_desc.path = path;
	if (!Describe(r, tables, sfnt, font->m_desc))
		return fail("tables name/head/cmap absentes");

	const TableEntry *head = Find(tables, Tag("head")), *hhea = Find(tables, Tag("hhea")), *maxp = Find(tables, Tag("maxp"));
	const TableEntry *hmtx = Find(tables, Tag("hmtx")), *cmap = Find(tables, Tag("cmap"));
	if (!hhea || !maxp || !hmtx || head->length < 54)
		return fail("tables hhea/maxp/hmtx absentes");
	font->m_unitsPerEm = r.U16(head->offset + 18);
	if (font->m_unitsPerEm < 16 || font->m_unitsPerEm > 16384)
		return fail("unitsPerEm invalide");
	const double scale = 1000.0 / font->m_unitsPerEm;
	const auto em = [&](int v) { return int(std::lround(v * scale)); };
	font->m_bbox = {em(r.S16(head->offset + 36)), em(r.S16(head->offset + 38)), em(r.S16(head->offset + 40)), em(r.S16(head->offset + 42))};
	font->m_ascent = em(r.S16(hhea->offset + 4));
	font->m_descent = em(r.S16(hhea->offset + 6));
	font->m_capHeight = font->m_ascent;
	if (const TableEntry *os2 = Find(tables, Tag("OS/2")); os2 && os2->length >= 78) {
		font->m_fsType = r.U16(os2->offset + 8);
		if (r.U16(os2->offset) >= 2 && os2->length >= 90)
			font->m_capHeight = em(r.S16(os2->offset + 88));
	}
	if (const TableEntry *post = Find(tables, Tag("post")); post && post->length >= 12) {
		font->m_italicAngle = double(int32_t(r.U32(post->offset + 4))) / 65536.0;
		font->m_underlinePosition = em(r.S16(post->offset + 8));
		font->m_underlineThickness = std::max(1, em(r.S16(post->offset + 10)));
	}
	// Avances horizontales.
	const uint16_t glyphs = r.U16(maxp->offset + 4);
	const uint16_t metrics = std::min<uint16_t>(r.U16(hhea->offset + 34), glyphs);
	if (glyphs == 0 || metrics == 0 || !r.Has(hmtx->offset, size_t(metrics) * 4))
		return fail("table hmtx invalide");
	font->m_advances.resize(glyphs);
	for (uint16_t g = 0; g < glyphs; ++g)
		font->m_advances[g] = r.U16(hmtx->offset + size_t(std::min<uint16_t>(g, uint16_t(metrics - 1))) * 4);
	// Table des caractères : (3,10) format 12, sinon (3,1) / (0,x) format 4.
	size_t format4 = 0, format12 = 0;
	const uint16_t subtables = r.U16(cmap->offset + 2);
	for (uint16_t i = 0; i < subtables; ++i) {
		const size_t rec = cmap->offset + 4 + size_t(i) * 8;
		const uint16_t platform = r.U16(rec), encoding = r.U16(rec + 2);
		const size_t offset = cmap->offset + r.U32(rec + 4);
		const uint16_t format = r.U16(offset);
		const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
		if (!unicode)
			continue;
		if (format == 12 && !format12)
			format12 = offset;
		else if (format == 4 && !format4)
			format4 = offset;
	}
	font->m_bmp.assign(0x10000, 0);
	if (format12) {
		const uint32_t groups = r.U32(format12 + 12);
		if (!r.Has(format12 + 16, size_t(groups) * 12))
			return fail("cmap format 12 invalide");
		for (uint32_t i = 0; i < groups; ++i) {
			const size_t g = format12 + 16 + size_t(i) * 12;
			const uint32_t first = r.U32(g), last = std::min<uint32_t>(r.U32(g + 4), 0x10FFFF), glyph = r.U32(g + 8);
			if (last < first)
				continue;
			for (uint32_t cp = first; cp <= std::min<uint32_t>(last, 0xFFFF); ++cp)
				font->m_bmp[cp] = uint16_t(glyph + (cp - first) < glyphs ? glyph + (cp - first) : 0);
			if (last > 0xFFFF)
				font->m_supplementary.push_back({std::max<uint32_t>(first, 0x10000), last, glyph + (std::max<uint32_t>(first, 0x10000) - first)});
		}
	} else if (format4) {
		const uint16_t segX2 = r.U16(format4 + 6);
		const size_t ends = format4 + 14, starts = ends + segX2 + 2, deltas = starts + segX2, ranges = deltas + segX2;
		if (!r.Has(ends, size_t(segX2) * 4 + 2))
			return fail("cmap format 4 invalide");
		for (uint16_t s = 0; s < segX2 / 2; ++s) {
			const uint16_t end = r.U16(ends + s * 2u), start = r.U16(starts + s * 2u);
			const uint16_t delta = r.U16(deltas + s * 2u), rangeOffset = r.U16(ranges + s * 2u);
			for (uint32_t cp = start; cp <= end && cp != 0xFFFF; ++cp) {
				uint16_t glyph = 0;
				if (rangeOffset == 0) {
					glyph = uint16_t(cp + delta);
				} else {
					const size_t at = ranges + s * 2u + rangeOffset + (cp - start) * 2u;
					if (const uint16_t g = r.U16(at); g != 0)
						glyph = uint16_t(g + delta);
				}
				font->m_bmp[cp] = glyph < glyphs ? glyph : 0;
			}
		}
	} else {
		return fail("aucune table cmap Unicode");
	}
	// Largeurs WinAnsi (écrites dans /Widths).
	const double missing = font->GlyphAdvance(font->GlyphIndex(U'?'));
	for (size_t b = 32; b < 256; ++b) {
		const char32_t cp = FromWinAnsi(uint8_t(b));
		const uint16_t g = font->GlyphIndex(cp);
		font->m_widths[b - 32] = uint16_t(std::lround(g ? font->GlyphAdvance(g) : missing));
	}
	// Programme à incorporer.
	if (font->m_desc.cff) {
		const TableEntry *cff = Find(tables, Tag("CFF "));
		if (!cff)
			return fail("table CFF absente");
		const auto bytes = r.Span(cff->offset, cff->length);
		font->m_program.assign(bytes.begin(), bytes.end());
	} else if (collection) {
		font->m_program = ExtractFace(r, tables, sfnt);
	} else {
		font->m_program = std::move(data);
	}
	return Ok(std::shared_ptr<const TrueTypeFont>(std::move(font)));
}

uint16_t TrueTypeFont::GlyphIndex(char32_t cp) const noexcept {
	if (cp < 0x10000)
		return cp < m_bmp.size() ? m_bmp[cp] : 0;
	for (const Group &g : m_supplementary)
		if (cp >= g.first && cp <= g.last) {
			const uint32_t glyph = g.glyph + (cp - g.first);
			return glyph < m_advances.size() ? uint16_t(glyph) : 0;
		}
	return 0;
}

double TrueTypeFont::GlyphAdvance(uint16_t glyph) const noexcept {
	if (glyph >= m_advances.size())
		return 0;
	return m_advances[glyph] * 1000.0 / m_unitsPerEm;
}

double TrueTypeFont::TextWidth(std::string_view utf8, double size) const noexcept {
	uint32_t units = 0;
	for (size_t i = 0; i < utf8.size();) {
		const uint8_t b = ToWinAnsi(unicode::DecodeUtf8(utf8, i));
		units += m_widths[(b >= 32 ? b : uint8_t('?')) - 32];
	}
	return units * size / 1000.0;
}

} // namespace pdf

namespace pdf {

double FontRef::TextWidth(std::string_view utf8, double size) const noexcept {
	return face ? face->TextWidth(utf8, size) : pdf::TextWidth(standard, utf8, size);
}
double FontRef::Ascent(double size) const noexcept { return face ? face->Ascent() * size / 1000.0 : pdf::Ascent(standard, size); }
double FontRef::Descent(double size) const noexcept { return face ? -face->Descent() * size / 1000.0 : pdf::Descent(standard, size); }
double FontRef::UnderlineOffset(double size) const noexcept {
	return -(face ? face->UnderlinePosition() : Metrics(standard).underlinePosition) * size / 1000.0;
}
double FontRef::UnderlineThickness(double size) const noexcept {
	return (face ? face->UnderlineThickness() : Metrics(standard).underlineThickness) * size / 1000.0;
}
bool FontRef::Bold() const noexcept { return face ? face->Bold() : IsBold(standard); }
bool FontRef::Italic() const noexcept { return face ? face->Italic() : IsItalic(standard); }
std::string_view FontRef::Family() const noexcept {
	if (face)
		return face->Family();
	static constexpr std::string_view kFamilies[] = {"Helvetica", "Times", "Courier"};
	return kFamilies[size_t(standard) / 4];
}

} // namespace pdf
