#include "pdf/writer.hpp"

#include "pdf/truetype.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <functional>

#include <archive/archive_crc.hpp>
#include <archive/archive_deflate.hpp>
#include <core/string.hpp>

namespace pdf {

namespace {

constexpr double kKappa = 0.5522847498307936; // approximation d'un quart de cercle par une Bézier

void AppendNumber(std::string &out, double v) {
	if (!std::isfinite(v))
		v = 0;
	std::string s = std::format("{:.4f}", v);
	while (!s.empty() && s.back() == '0')
		s.pop_back();
	if (!s.empty() && s.back() == '.')
		s.pop_back();
	if (s == "-0" || s.empty())
		s = "0";
	out += s;
}

/// Chaîne littérale PDF « (…) » : parenthèses et barres obliques échappées, octets non
/// imprimables en octal.
void AppendLiteral(std::string &out, std::string_view bytes) {
	out.push_back('(');
	for (const char c : bytes) {
		const auto b = uint8_t(c);
		if (c == '(' || c == ')' || c == '\\') {
			out.push_back('\\');
			out.push_back(c);
		} else if (b < 0x20 || b >= 0x7F) {
			out += std::format("\\{:03o}", b);
		} else {
			out.push_back(c);
		}
	}
	out.push_back(')');
}

std::string Hex2(uint8_t v) { return std::format("{:02X}", v); }

template <typename T> void AddUnique(std::vector<T> &v, T value) {
	if (std::ranges::find(v, value) == v.end())
		v.push_back(value);
}

} // namespace

std::string DateString(int64_t unixSeconds) {
	using namespace std::chrono;
	const sys_seconds tp{seconds(unixSeconds)};
	const auto day = floor<days>(tp);
	const year_month_day ymd{day};
	const hh_mm_ss hms{tp - day};
	return std::format("D:{:04}{:02}{:02}{:02}{:02}{:02}Z", int(ymd.year()), unsigned(ymd.month()), unsigned(ymd.day()),
					   hms.hours().count(), hms.minutes().count(), hms.seconds().count());
}

std::string TextString(std::string_view utf8) {
	const bool ascii = std::ranges::all_of(utf8, [](char c) { return uint8_t(c) >= 0x20 && uint8_t(c) < 0x7F; });
	std::string out;
	if (ascii) {
		AppendLiteral(out, utf8);
		return out;
	}
	out = "<FEFF";
	for (const char16_t u : unicode::ToUtf16(utf8))
		out += std::format("{:04X}", unsigned(u));
	out.push_back('>');
	return out;
}

// ── Canvas ──

void Canvas::Number(double v) {
	AppendNumber(m_content, v);
	m_content.push_back(' ');
}

void Canvas::Point(double x, double y) {
	Number(x);
	Number(m_height - y);
}

void Canvas::Op(std::string_view op) {
	m_content += op;
	m_content.push_back('\n');
}

void Canvas::FillAlpha(uint8_t alpha) {
	if (alpha == m_fillAlpha)
		return;
	m_fillAlpha = alpha;
	AddUnique(m_fillAlphas, alpha);
	m_content += "/Ga" + Hex2(alpha) + " gs\n";
}

void Canvas::StrokeAlpha(uint8_t alpha) {
	if (alpha == m_strokeAlpha)
		return;
	m_strokeAlpha = alpha;
	AddUnique(m_strokeAlphas, alpha);
	m_content += "/GA" + Hex2(alpha) + " gs\n";
}

void Canvas::Save() {
	m_alphaStack.emplace_back(m_fillAlpha, m_strokeAlpha);
	Op("q");
}

void Canvas::Restore() {
	if (m_alphaStack.empty())
		return; // Q sans q : flux invalide, refusé
	m_fillAlpha = m_alphaStack.back().first;
	m_strokeAlpha = m_alphaStack.back().second;
	m_alphaStack.pop_back();
	Op("Q");
}

void Canvas::SetFillColor(math::Color c) {
	Number(c.r / 255.0);
	Number(c.g / 255.0);
	Number(c.b / 255.0);
	Op("rg");
	FillAlpha(c.a);
}

void Canvas::SetStrokeColor(math::Color c) {
	Number(c.r / 255.0);
	Number(c.g / 255.0);
	Number(c.b / 255.0);
	Op("RG");
	StrokeAlpha(c.a);
}

void Canvas::SetLineWidth(double width) {
	Number(std::max(0.0, width));
	Op("w");
}

void Canvas::SetLineCap(LineCap cap) {
	Number(double(int(cap)));
	Op("J");
}

void Canvas::SetLineJoin(LineJoin join) {
	Number(double(int(join)));
	Op("j");
}

void Canvas::SetDash(std::span<const double> pattern, double phase) {
	m_content.push_back('[');
	for (size_t i = 0; i < pattern.size(); ++i) {
		if (i)
			m_content.push_back(' ');
		AppendNumber(m_content, std::max(0.0, pattern[i]));
	}
	m_content += "] ";
	Number(phase);
	Op("d");
}

void Canvas::MoveTo(double x, double y) {
	Point(x, y);
	Op("m");
}

void Canvas::LineTo(double x, double y) {
	Point(x, y);
	Op("l");
}

void Canvas::CurveTo(double x1, double y1, double x2, double y2, double x3, double y3) {
	Point(x1, y1);
	Point(x2, y2);
	Point(x3, y3);
	Op("c");
}

void Canvas::ClosePath() { Op("h"); }

void Canvas::Rectangle(double x, double y, double w, double h) {
	Number(x);
	Number(m_height - y - h);
	Number(w);
	Number(h);
	Op("re");
}

void Canvas::Ellipse(double x, double y, double w, double h) {
	const double rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry;
	const double kx = rx * kKappa, ky = ry * kKappa;
	MoveTo(cx + rx, cy);
	CurveTo(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
	CurveTo(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
	CurveTo(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
	CurveTo(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
	ClosePath();
}

void Canvas::Fill(bool evenOdd) { Op(evenOdd ? "f*" : "f"); }
void Canvas::Stroke() { Op("S"); }
void Canvas::FillAndStroke(bool evenOdd) { Op(evenOdd ? "B*" : "B"); }

void Canvas::Clip(bool evenOdd) {
	Op(evenOdd ? "W*" : "W");
	Op("n");
}

void Canvas::FillRect(double x, double y, double w, double h, math::Color color) {
	if (color.a == 0)
		return;
	Save();
	SetFillColor(color);
	Rectangle(x, y, w, h);
	Fill();
	Restore();
}

void Canvas::StrokeRect(double x, double y, double w, double h, math::Color color, double lineWidth) {
	if (color.a == 0 || lineWidth <= 0)
		return;
	Save();
	SetStrokeColor(color);
	SetLineWidth(lineWidth);
	Rectangle(x, y, w, h);
	Stroke();
	Restore();
}

void Canvas::Line(double x1, double y1, double x2, double y2, math::Color color, double lineWidth) {
	if (color.a == 0 || lineWidth <= 0)
		return;
	Save();
	SetStrokeColor(color);
	SetLineWidth(lineWidth);
	MoveTo(x1, y1);
	LineTo(x2, y2);
	Stroke();
	Restore();
}

void Canvas::Text(double x, double baseline, StandardFont font, double size, std::string_view utf8, math::Color color) {
	if (utf8.empty() || color.a == 0 || size <= 0)
		return;
	const size_t index = std::min(size_t(font), kStandardFontCount - 1);
	m_fontsUsed |= 1u << index;
	Save();
	SetFillColor(color);
	m_content += std::format("BT /F{} ", index);
	Number(size);
	m_content += "Tf ";
	Point(x, baseline);
	m_content += "Td ";
	AppendLiteral(m_content, EncodeWinAnsi(utf8));
	m_content += " Tj ET\n";
	Restore();
}

void Canvas::Text(double x, double baseline, const FontRef &font, double size, std::string_view utf8, math::Color color) {
	if (!font.face || !m_embedded || !font.face->Embeddable()) {
		Text(x, baseline, font.standard, size, utf8, color);
		return;
	}
	if (utf8.empty() || color.a == 0 || size <= 0)
		return;
	auto it = std::ranges::find(*m_embedded, font.face);
	if (it == m_embedded->end()) {
		m_embedded->push_back(font.face);
		it = m_embedded->end() - 1;
	}
	const auto index = uint32_t(it - m_embedded->begin());
	AddUnique(m_embeddedUsed, index);
	Save();
	SetFillColor(color);
	m_content += std::format("BT /T{} ", index);
	Number(size);
	m_content += "Tf ";
	Point(x, baseline);
	m_content += "Td ";
	AppendLiteral(m_content, EncodeWinAnsi(utf8));
	m_content += " Tj ET\n";
	Restore();
}

void Canvas::Rotate(double degrees, double cx, double cy) {
	// Repère PDF (y vers le haut) : une rotation horaire à l'écran est antihoraire ici.
	const double phi = -degrees * 3.14159265358979323846 / 180.0;
	const double a = std::cos(phi), b = std::sin(phi), c = -std::sin(phi), d = std::cos(phi);
	const double px = cx, py = m_height - cy;
	Number(a);
	Number(b);
	Number(c);
	Number(d);
	Number(px - a * px - c * py);
	Number(py - b * px - d * py);
	Op("cm");
}

void Canvas::DrawImage(ImageId image, double x, double y, double w, double h) {
	if (!image.Valid() || w <= 0 || h <= 0)
		return;
	AddUnique(m_images, image.index);
	Save();
	Number(w);
	m_content += "0 0 ";
	Number(h);
	Number(x);
	Number(m_height - y - h);
	m_content += std::format("cm /Im{} Do\n", image.index);
	Restore();
}

void Canvas::Link(double x, double y, double w, double h, std::string uri) {
	if (w > 0 && h > 0 && !uri.empty())
		m_links.push_back({x, y, w, h, std::move(uri)});
}

void Canvas::LinkToPage(double x, double y, double w, double h, size_t page, double top) {
	if (w > 0 && h > 0)
		m_links.push_back({x, y, w, h, {}, page, top});
}

// ── Writer ──

Canvas &Writer::AddPage(double width, double height) {
	return m_pages.emplace_back(std::clamp(width, 3.0, 14400.0), std::clamp(height, 3.0, 14400.0), m_fonts);
}

PdfResult<ImageId> Writer::AddImage(uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
	if (width == 0 || height == 0)
		return Err(Error{"image vide"});
	if (rgba.size() != size_t(width) * height * 4)
		return Err(Error{std::format("image {}×{} : {} octets attendus, {} reçus", width, height, size_t(width) * height * 4, rgba.size())});
	Image img{width, height, {}, {}};
	img.rgb.reserve(size_t(width) * height * 3);
	bool opaque = true;
	for (size_t i = 0; i < rgba.size(); i += 4) {
		img.rgb.insert(img.rgb.end(), {rgba[i], rgba[i + 1], rgba[i + 2]});
		opaque = opaque && rgba[i + 3] == 255;
	}
	if (!opaque) {
		img.alpha.reserve(size_t(width) * height);
		for (size_t i = 3; i < rgba.size(); i += 4)
			img.alpha.push_back(rgba[i]);
	}
	m_images.push_back(std::move(img));
	return Ok(ImageId{uint32_t(m_images.size() - 1)});
}

void Writer::AddBookmark(std::string title, size_t page, double y, int level) {
	m_bookmarks.push_back({std::move(title), page, y, std::max(0, level)});
}

namespace {

/// Tampon de sortie avec table des positions d'objets.
class ObjectStream {
public:
	std::string data;
	std::vector<size_t> offsets{0}; ///< offsets[n] : position de l'objet n

	uint32_t Reserve() {
		offsets.push_back(0);
		return uint32_t(offsets.size() - 1);
	}
	void Begin(uint32_t number) {
		offsets[number] = data.size();
		data += std::format("{} 0 obj\n", number);
	}
	void End() { data += "\nendobj\n"; }
	void Object(uint32_t number, std::string_view body) {
		Begin(number);
		data += body;
		End();
	}
	void Stream(uint32_t number, std::string dict, std::span<const uint8_t> bytes) {
		Begin(number);
		data += "<<" + dict + std::format(" /Length {} >>\nstream\n", bytes.size());
		data.append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		data += "\nendstream";
		End();
	}
};

std::string Ref(uint32_t n) { return std::format("{} 0 R", n); }

} // namespace

PdfResult<Bytes> Writer::Serialize(const SerializeOptions &options) const {
	for (const Bookmark &b : m_bookmarks)
		if (b.page >= m_pages.size())
			return Err(Error{std::format("signet « {} » vers la page {} inexistante", b.title, b.page + 1)});
	const int level = std::clamp(options.level, 0, 9);

	ObjectStream out;
	out.data = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
	const uint32_t catalog = out.Reserve(), pages = out.Reserve(), info = out.Reserve();

	// Ressources partagées : polices, opacités, images.
	uint32_t fontsUsed = 0;
	std::vector<uint8_t> fillAlphas, strokeAlphas;
	for (const Canvas &c : m_pages) {
		fontsUsed |= c.m_fontsUsed;
		for (uint8_t a : c.m_fillAlphas)
			AddUnique(fillAlphas, a);
		for (uint8_t a : c.m_strokeAlphas)
			AddUnique(strokeAlphas, a);
	}
	std::array<uint32_t, kStandardFontCount> fontObj{};
	for (size_t f = 0; f < kStandardFontCount; ++f)
		if (fontsUsed & (1u << f)) {
			fontObj[f] = out.Reserve();
			const FontMetrics &m = Metrics(StandardFont(f));
			std::string dict = std::format("<< /Type /Font /Subtype /Type1 /BaseFont /{} /Encoding /WinAnsiEncoding "
										   "/FirstChar 32 /LastChar 255 /Widths [",
										   m.baseName);
			for (size_t i = 0; i < m.widths.size(); ++i)
				dict += std::format("{}{}", i ? " " : "", m.widths[i]);
			dict += "] >>";
			out.Object(fontObj[f], dict);
		}
	struct GState {
		bool fill;
		uint8_t alpha;
		uint32_t obj;
	};
	// Polices incorporées : dictionnaire, descripteur et programme (FontFile2 / FontFile3).
	std::vector<uint32_t> embeddedObj(m_fonts->size(), 0);
	for (size_t i = 0; i < m_fonts->size(); ++i) {
		const bool used = std::ranges::any_of(m_pages, [&](const Canvas &c) { return std::ranges::find(c.m_embeddedUsed, uint32_t(i)) != c.m_embeddedUsed.end(); });
		if (!used)
			continue;
		const TrueTypeFont &f = *(*m_fonts)[i];
		std::string name;
		for (const char ch : f.PostScriptName())
			if (ch > 32 && ch < 127 && ch != '/' && ch != '(' && ch != ')' && ch != '[' && ch != ']' && ch != '<' && ch != '>' && ch != '{' && ch != '}' && ch != '%')
				name.push_back(ch);
		if (name.empty())
			name = std::format("Font{}", i);
		const uint32_t program = out.Reserve();
		const Bytes packed = archive::ZlibCompress(f.FontProgram(), level);
		if (f.IsCff())
			out.Stream(program, " /Subtype /Type1C /Filter /FlateDecode", packed);
		else
			out.Stream(program, std::format(" /Length1 {} /Filter /FlateDecode", f.FontProgram().size()), packed);
		const uint32_t descriptor = out.Reserve();
		const auto &box = f.BoundingBox();
		int flags = 32; // non symbolique (codes WinAnsi)
		if (f.Italic())
			flags |= 64;
		std::string desc = std::format("<< /Type /FontDescriptor /FontName /{} /Flags {} /FontBBox [{} {} {} {}] /ItalicAngle ", name, flags, box[0],
									   box[1], box[2], box[3]);
		AppendNumber(desc, f.ItalicAngle());
		desc += std::format(" /Ascent {} /Descent {} /CapHeight {} /StemV {} {} {} >>", f.Ascent(), f.Descent(), f.CapHeight(), f.Bold() ? 140 : 80,
							f.IsCff() ? "/FontFile3" : "/FontFile2", Ref(program));
		out.Object(descriptor, desc);
		embeddedObj[i] = out.Reserve();
		std::string dict = std::format("<< /Type /Font /Subtype /{} /BaseFont /{} /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 255 /Widths [",
									   f.IsCff() ? "Type1" : "TrueType", name);
		const auto &widths = f.WinAnsiWidths();
		for (size_t k = 0; k < widths.size(); ++k)
			dict += std::format("{}{}", k ? " " : "", widths[k]);
		dict += "] /FontDescriptor " + Ref(descriptor) + " >>";
		out.Object(embeddedObj[i], dict);
	}

	std::vector<GState> gstates;
	for (uint8_t a : fillAlphas) {
		const uint32_t n = out.Reserve();
		std::string body = "<< /Type /ExtGState /ca ";
		AppendNumber(body, a / 255.0);
		out.Object(n, body + " >>");
		gstates.push_back({true, a, n});
	}
	for (uint8_t a : strokeAlphas) {
		const uint32_t n = out.Reserve();
		std::string body = "<< /Type /ExtGState /CA ";
		AppendNumber(body, a / 255.0);
		out.Object(n, body + " >>");
		gstates.push_back({false, a, n});
	}
	std::vector<uint32_t> imageObj(m_images.size(), 0);
	for (size_t i = 0; i < m_images.size(); ++i) {
		const Image &img = m_images[i];
		uint32_t mask = 0;
		if (!img.alpha.empty()) {
			mask = out.Reserve();
			out.Stream(mask,
					   std::format(" /Type /XObject /Subtype /Image /Width {} /Height {} /ColorSpace /DeviceGray "
								   "/BitsPerComponent 8 /Filter /FlateDecode",
								   img.width, img.height),
					   archive::ZlibCompress(img.alpha, level));
		}
		imageObj[i] = out.Reserve();
		out.Stream(imageObj[i],
				   std::format(" /Type /XObject /Subtype /Image /Width {} /Height {} /ColorSpace /DeviceRGB "
							   "/BitsPerComponent 8 /Filter /FlateDecode{}",
							   img.width, img.height, mask ? " /SMask " + Ref(mask) : ""),
				   archive::ZlibCompress(img.rgb, level));
	}

	// Pages.
	std::vector<uint32_t> pageObj(m_pages.size());
	for (uint32_t &p : pageObj)
		p = out.Reserve();
	for (size_t i = 0; i < m_pages.size(); ++i) {
		const Canvas &c = m_pages[i];
		const uint32_t content = out.Reserve();
		const std::span<const uint8_t> raw(reinterpret_cast<const uint8_t *>(c.m_content.data()), c.m_content.size());
		if (options.compress)
			out.Stream(content, " /Filter /FlateDecode", archive::ZlibCompress(raw, level));
		else
			out.Stream(content, "", raw);

		std::string res = "<< /ProcSet [/PDF /Text /ImageB /ImageC]";
		if (c.m_fontsUsed || !c.m_embeddedUsed.empty()) {
			res += " /Font <<";
			for (size_t f = 0; f < kStandardFontCount; ++f)
				if (c.m_fontsUsed & (1u << f))
					res += std::format(" /F{} {}", f, Ref(fontObj[f]));
			for (uint32_t e : c.m_embeddedUsed)
				res += std::format(" /T{} {}", e, Ref(embeddedObj[e]));
			res += " >>";
		}
		if (!c.m_fillAlphas.empty() || !c.m_strokeAlphas.empty()) {
			res += " /ExtGState <<";
			for (const GState &g : gstates) {
				const auto &usedByPage = g.fill ? c.m_fillAlphas : c.m_strokeAlphas;
				if (std::ranges::find(usedByPage, g.alpha) != usedByPage.end())
					res += std::format(" /{}{} {}", g.fill ? "Ga" : "GA", Hex2(g.alpha), Ref(g.obj));
			}
			res += " >>";
		}
		if (!c.m_images.empty()) {
			res += " /XObject <<";
			for (uint32_t img : c.m_images)
				res += std::format(" /Im{} {}", img, Ref(imageObj[img]));
			res += " >>";
		}
		res += " >>";

		std::string page = "<< /Type /Page /Parent " + Ref(pages) + " /MediaBox [0 0 ";
		AppendNumber(page, c.m_width);
		page.push_back(' ');
		AppendNumber(page, c.m_height);
		page += "] /Resources " + res + " /Contents " + Ref(content);
		if (!c.m_links.empty()) {
			page += " /Annots [";
			for (const auto &l : c.m_links) {
				if (l.uri.empty() && l.page >= m_pages.size())
					continue; // lien interne vers une page absente
				page += " << /Type /Annot /Subtype /Link /Border [0 0 0] /Rect [";
				AppendNumber(page, l.x);
				page.push_back(' ');
				AppendNumber(page, c.m_height - l.y - l.h);
				page.push_back(' ');
				AppendNumber(page, l.x + l.w);
				page.push_back(' ');
				AppendNumber(page, c.m_height - l.y);
				if (l.uri.empty()) {
					page += "] /Dest [" + Ref(pageObj[l.page]) + " /XYZ 0 ";
					AppendNumber(page, m_pages[l.page].m_height - l.top);
					page += " null] >>";
				} else {
					page += "] /A << /S /URI /URI ";
					AppendLiteral(page, l.uri);
					page += " >> >>";
				}
			}
			page += " ]";
		}
		page += " >>";
		out.Object(pageObj[i], page);
	}
	std::string kids;
	for (size_t i = 0; i < pageObj.size(); ++i)
		kids += (i ? " " : "") + Ref(pageObj[i]);
	out.Object(pages, std::format("<< /Type /Pages /Kids [{}] /Count {} >>", kids, pageObj.size()));

	// Signets : arbre reconstruit à partir des niveaux.
	uint32_t outlines = 0;
	if (!m_bookmarks.empty()) {
		outlines = out.Reserve();
		struct Item {
			uint32_t obj;
			int parent; // -1 : racine
			std::vector<size_t> children;
		};
		std::vector<Item> items(m_bookmarks.size());
		std::vector<size_t> roots;
		std::vector<size_t> stack; // dernier signet de chaque niveau
		for (size_t i = 0; i < m_bookmarks.size(); ++i) {
			items[i].obj = out.Reserve();
			const int lvl = std::min<int>(m_bookmarks[i].level, int(stack.size()));
			stack.resize(size_t(lvl));
			items[i].parent = lvl == 0 ? -1 : int(stack.back());
			(lvl == 0 ? roots : items[stack.back()].children).push_back(i);
			stack.push_back(i);
		}
		const auto siblings = [&](size_t i) -> const std::vector<size_t> & {
			return items[i].parent < 0 ? roots : items[size_t(items[i].parent)].children;
		};
		const std::function<size_t(size_t)> descendants = [&](size_t i) {
			size_t n = items[i].children.size();
			for (size_t c : items[i].children)
				n += descendants(c);
			return n;
		};
		for (size_t i = 0; i < items.size(); ++i) {
			const Bookmark &b = m_bookmarks[i];
			const auto &sib = siblings(i);
			const size_t pos = size_t(std::ranges::find(sib, i) - sib.begin());
			std::string body = "<< /Title " + TextString(b.title) +
							   " /Parent " + Ref(items[i].parent < 0 ? outlines : items[size_t(items[i].parent)].obj);
			if (pos > 0)
				body += " /Prev " + Ref(items[sib[pos - 1]].obj);
			if (pos + 1 < sib.size())
				body += " /Next " + Ref(items[sib[pos + 1]].obj);
			if (!items[i].children.empty())
				body += std::format(" /First {} /Last {} /Count {}", Ref(items[items[i].children.front()].obj),
									Ref(items[items[i].children.back()].obj), descendants(i));
			body += " /Dest [" + Ref(pageObj[b.page]) + " /XYZ 0 ";
			AppendNumber(body, m_pages[b.page].m_height - b.y);
			body += " null] >>";
			out.Object(items[i].obj, body);
		}
		out.Object(outlines, std::format("<< /Type /Outlines /First {} /Last {} /Count {} >>", Ref(items[roots.front()].obj),
										 Ref(items[roots.back()].obj), items.size()));
	}

	out.Object(catalog, "<< /Type /Catalog /Pages " + Ref(pages) +
							(outlines ? " /Outlines " + Ref(outlines) + " /PageMode /UseOutlines" : "") + " >>");
	std::string infoDict = "<< /Producer " + TextString(m_info.producer);
	const auto field = [&](std::string_view key, const std::string &value) {
		if (!value.empty())
			infoDict += std::format(" /{} ", key) + TextString(value);
	};
	field("Title", m_info.title);
	field("Author", m_info.author);
	field("Subject", m_info.subject);
	field("Keywords", m_info.keywords);
	field("Creator", m_info.creator);
	field("CreationDate", m_info.creationDate);
	out.Object(info, infoDict + " >>");

	// Table des références croisées et en-queue.
	const size_t xref = out.data.size();
	out.data += std::format("xref\n0 {}\n0000000000 65535 f \n", out.offsets.size());
	for (size_t n = 1; n < out.offsets.size(); ++n)
		out.data += std::format("{:010} 00000 n \n", out.offsets[n]);
	archive::Xxh64 h1(1), h2(2);
	const std::span<const uint8_t> body(reinterpret_cast<const uint8_t *>(out.data.data()), out.data.size());
	h1.Update(body);
	h2.Update(body);
	const std::string id = std::format("{:016X}{:016X}", h1.Digest(), h2.Digest());
	out.data += std::format("trailer\n<< /Size {} /Root {} /Info {} /ID [<{}> <{}>] >>\nstartxref\n{}\n%%EOF\n", out.offsets.size(),
							Ref(catalog), Ref(info), id, id, xref);
	return Ok(Bytes(out.data.begin(), out.data.end()));
}

PdfResult<size_t> Writer::Save(const std::filesystem::path &path, const SerializeOptions &options) const {
	auto bytes = Serialize(options);
	if (!bytes)
		return Err(bytes.Error());
	std::filesystem::path tmp = path;
	tmp += ".tmp";
	std::FILE *f = std::fopen(tmp.string().c_str(), "wb");
	if (!f)
		return Err(Error{std::format("impossible de créer {}", tmp.string())});
	const size_t written = std::fwrite(bytes.Value().data(), 1, bytes.Value().size(), f);
	const bool closed = std::fclose(f) == 0;
	if (written != bytes.Value().size() || !closed) {
		std::error_code ignored;
		std::filesystem::remove(tmp, ignored);
		return Err(Error{std::format("écriture incomplète de {}", tmp.string())});
	}
	std::error_code ec;
	std::filesystem::rename(tmp, path, ec);
	if (ec) {
		std::filesystem::remove(tmp, ec);
		return Err(Error{std::format("impossible de renommer vers {}", path.string())});
	}
	return Ok(written);
}

} // namespace pdf
