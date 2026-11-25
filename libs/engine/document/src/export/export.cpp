// Exports PDF, HTML et CSV.
#include "document/export.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <map>

#include <dataio/base64.hpp>
#include <dataio/documents/csv.hpp>
#include <pdf/writer.hpp>
#include <sdl3/io/iostream.hpp>
#include <sdl3/render/image.hpp>
#include <sdl3/render/render.hpp>

#include "document/render.hpp"

namespace doc {

namespace {

math::Color ToPdf(const Color &c) { return math::Color(c.r, c.g, c.b, c.a); }

/// Écriture atomique d'un fichier (temporaire puis renommage).
Status WriteFile(const std::filesystem::path &path, std::string_view bytes) {
	std::filesystem::path tmp = path;
	tmp += ".tmp";
	std::FILE *f = std::fopen(tmp.string().c_str(), "wb");
	if (!f)
		return Failure(ErrorCode::Io, std::format("impossible de créer {}", tmp.string()));
	const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
	const bool closed = std::fclose(f) == 0;
	std::error_code ec;
	if (written != bytes.size() || !closed) {
		std::filesystem::remove(tmp, ec);
		return Failure(ErrorCode::Io, std::format("écriture incomplète de {}", tmp.string()));
	}
	std::filesystem::rename(tmp, path, ec);
	if (ec) {
		std::error_code ignored;
		std::filesystem::remove(tmp, ignored);
		return Failure(ErrorCode::Io, std::format("impossible de renommer vers {}", path.string()));
	}
	return Success();
}

std::string DocumentName(const DocumentView &view) {
	const auto *v = view.Explicit(view.Root(), PropertyId::Name);
	return v ? std::get<std::string>(*v) : std::string();
}

} // namespace

// ── PDF ──

/// Zones cliquables de la page : segments portant un lien (URL, ou « #id » vers un nœud), un
/// rectangle par suite de segments contigus d'un même lien sur une ligne.
void AddLinks(pdf::Canvas &c, const LayoutResult &layout, size_t pageIndex) {
	for (const PlacedLine &line : layout.pages[pageIndex].lines) {
		for (size_t i = 0; i < line.runs.size();) {
			const std::string &link = line.runs[i].style.link;
			if (link.empty()) {
				++i;
				continue;
			}
			double x0 = line.runs[i].x, x1 = line.runs[i].x + line.runs[i].width;
			size_t j = i + 1;
			for (; j < line.runs.size() && line.runs[j].style.link == link; ++j)
				x1 = std::max(x1, line.runs[j].x + line.runs[j].width);
			if (const NodeId target = LinkTarget(link); target != kNoNode) {
				size_t page = 0;
				double y = 0;
				if (layout.Locate(target, page, y))
					c.LinkToPage(x0, line.rect.y, x1 - x0, line.rect.h, page, std::max(0.0, y - 12));
			} else {
				c.Link(x0, line.rect.y, x1 - x0, line.rect.h, link);
			}
			i = j;
		}
	}
}

DocResult<std::vector<uint8_t>> ExportPdf(const DocumentView &view, const LayoutResult &layout, const PdfExportOptions &options) {
	if (layout.stamp != view.Stamp())
		return Failure(ErrorCode::InvalidValue, "la mise en page ne correspond pas à ce document");
	RenderOptions render;
	render.paper = false; // le papier d'un PDF est blanc par défaut
	const std::vector<DisplayList> pages = RenderDocument(view, layout, render);

	pdf::Writer w;
	pdf::DocumentInfo info;
	info.title = !options.title.empty() ? options.title : DocumentName(view);
	info.author = options.author;
	info.subject = options.subject;
	info.keywords = options.keywords;
	info.creator = options.creator;
	if (options.creationTime != 0)
		info.creationDate = pdf::DateString(options.creationTime);
	w.SetInfo(std::move(info));

	std::map<ResourceId, pdf::ImageId> images;
	for (size_t pageIndex = 0; pageIndex < pages.size(); ++pageIndex) {
		const DisplayList &list = pages[pageIndex];
		pdf::Canvas &c = w.AddPage(list.width, list.height);
		AddLinks(c, layout, pageIndex);
		for (const DrawCommand &cmd : list.commands) {
			const Rect &r = cmd.rect;
			const bool rotated = cmd.rotation != 0;
			if (rotated) { // rotation de la commande seule (formes et images)
				c.Save();
				c.Rotate(cmd.rotation, cmd.pivotX, cmd.pivotY);
			}
			switch (cmd.kind) {
			case DrawKind::FillRect: c.FillRect(r.x, r.y, r.w, r.h, ToPdf(cmd.color)); break;
			case DrawKind::StrokeRect: c.StrokeRect(r.x, r.y, r.w, r.h, ToPdf(cmd.color), cmd.lineWidth); break;
			case DrawKind::FillEllipse:
				c.Save();
				c.SetFillColor(ToPdf(cmd.color));
				c.Ellipse(r.x, r.y, r.w, r.h);
				c.Fill();
				c.Restore();
				break;
			case DrawKind::StrokeEllipse:
				c.Save();
				c.SetStrokeColor(ToPdf(cmd.color));
				c.SetLineWidth(cmd.lineWidth);
				c.Ellipse(r.x, r.y, r.w, r.h);
				c.Stroke();
				c.Restore();
				break;
			case DrawKind::Line: c.Line(r.x, r.y, r.Right(), r.Bottom(), ToPdf(cmd.color), cmd.lineWidth); break;
			case DrawKind::FillPolygon:
			case DrawKind::StrokePolygon:
				if (cmd.points.size() < 6)
					break;
				c.Save();
				c.MoveTo(cmd.points[0], cmd.points[1]);
				for (size_t i = 2; i + 1 < cmd.points.size(); i += 2)
					c.LineTo(cmd.points[i], cmd.points[i + 1]);
				c.ClosePath();
				if (cmd.kind == DrawKind::FillPolygon) {
					c.SetFillColor(ToPdf(cmd.color));
					c.Fill();
				} else {
					c.SetStrokeColor(ToPdf(cmd.color));
					c.SetLineWidth(cmd.lineWidth);
					c.SetLineJoin(pdf::LineJoin::Round);
					c.Stroke();
				}
				c.Restore();
				break;
			case DrawKind::Text: c.Text(r.x, cmd.baseline, cmd.font, cmd.size, cmd.text, ToPdf(cmd.color)); break;
			case DrawKind::Image: {
				auto it = images.find(cmd.image);
				if (it == images.end()) {
					const ImageResource *res = view.FindResource(cmd.image);
					if (!res)
						break;
					auto added = w.AddImage(res->width, res->height, res->rgba);
					if (!added)
						return Failure(ErrorCode::InvalidValue, std::format("image {} : {}", cmd.image, added.Error().message));
					it = images.emplace(cmd.image, added.Value()).first;
				}
				c.Save();
				if (cmd.opacity < 1) // l'opacité de remplissage s'applique aux images
					c.SetFillColor(math::Color(0, 0, 0, uint8_t(std::clamp(cmd.opacity, 0.0, 1.0) * 255 + 0.5)));
				c.DrawImage(it->second, r.x, r.y, r.w, r.h);
				c.Restore();
				break;
			}
			}
			if (rotated)
				c.Restore();
		}
	}

	if (options.bookmarks) {
		// Titres de la structure (niveau → profondeur du signet) ; sans titre : blocs nommés.
		const Outline outline = BuildOutline(view);
		bool headings = false;
		for (const OutlineEntry &e : outline.entries) {
			size_t page = 0;
			double y = 0;
			if (e.kind != OutlineKind::Heading || !layout.Locate(e.node, page, y))
				continue;
			headings = true;
			w.AddBookmark(e.label.empty() ? e.text : e.label + " " + e.text, page, y, e.level - 1);
		}
		std::vector<NodeId> named;
		if (!headings)
		for (NodeId section : view.Sections())
			view.Visit(view.Body(section), [&](const Node &n) {
				if (TypeInfo(n.Type()).flowBlock && n.Properties().Contains(PropertyId::Name))
					named.push_back(n.Id());
			});
		for (NodeId id : named)
			for (size_t p = 0; p < layout.pages.size(); ++p) {
				const auto &boxes = layout.pages[p].boxes;
				const auto it = std::ranges::find(boxes, id, &LayoutBox::node);
				if (it != boxes.end()) {
					w.AddBookmark(std::get<std::string>(*view.Explicit(id, PropertyId::Name)), p, it->rect.y);
					break;
				}
			}
	}
	pdf::SerializeOptions serialize;
	serialize.compress = options.compress;
	auto bytes = w.Serialize(serialize);
	if (!bytes)
		return Failure(ErrorCode::InvalidValue, bytes.Error().message);
	return Ok(std::move(bytes.Value()));
}

Status SavePdf(const DocumentView &view, const LayoutResult &layout, const std::filesystem::path &path, const PdfExportOptions &options) {
	auto bytes = ExportPdf(view, layout, options);
	if (!bytes)
		return Err(bytes.Error());
	return WriteFile(path, std::string_view(reinterpret_cast<const char *>(bytes.Value().data()), bytes.Value().size()));
}

// ── HTML ──

namespace {

void Escape(std::string &out, std::string_view text) {
	for (const char c : text) {
		switch (c) {
		case '&': out += "&amp;"; break;
		case '<': out += "&lt;"; break;
		case '>': out += "&gt;"; break;
		case '"': out += "&quot;"; break;
		case '\'': out += "&#39;"; break;
		case '\n': out += "<br>"; break;
		default: out.push_back(c);
		}
	}
}

std::string Pt(double v) {
	std::string s = std::format("{:.2f}", v);
	while (s.back() == '0')
		s.pop_back();
	if (s.back() == '.')
		s.pop_back();
	return s == "-0" ? "0pt" : s + "pt";
}

std::string Css(const Color &c) {
	if (c.a == 255)
		return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
	return std::format("rgba({},{},{},{:.3f})", c.r, c.g, c.b, c.a / 255.0);
}

std::string FontStack(const std::string &family, const pdf::FontRef &font) {
	std::string quoted;
	for (const char c : family)
		if (c != '"' && c != '\'' && c != ';' && c != '<' && c != '>' && c != '{' && c != '}')
			quoted.push_back(c);
	const size_t base = size_t(font.standard) / 4; // famille générique de repli
	const char *generic = base == 1 ? "serif" : base == 2 ? "monospace" : "sans-serif";
	return std::format("\"{}\", {}", quoted, generic);
}

/// Image RVBA → PNG (encodeur SDL_image du wrapper sdl3) → URI data:.
std::optional<std::string> PngDataUri(const ImageResource &img) {
	auto surface = sdl3::Surface::FromRgba8(img.rgba, int(img.width), int(img.height));
	if (!surface)
		return std::nullopt;
	auto stream = sdl3::IOStream::FromDynamicMemory();
	if (!stream)
		return std::nullopt;
	if (auto saved = sdl3::ImgSavePngIO(surface.Value(), stream.Value()); !saved)
		return std::nullopt;
	const std::vector<uint8_t> png = stream.Value().DynamicMemoryBytes();
	if (png.empty())
		return std::nullopt;
	return "data:image/png;base64," + dataio::Base64Encode(png);
}

class HtmlWriter {
public:
	HtmlWriter(const DocumentView &view, const HtmlExportOptions &options) : m_view(view), m_options(options) {}

	DocResult<std::string> Run() {
		const std::vector<NodeId> sections = m_view.Sections();
		const PageGeometry g = ResolvePageGeometry(m_view, sections.front(), kNoNode);
		m_contentWidth = std::max(1.0, g.width - g.margins.left - g.margins.right);
		m_outline = BuildOutline(m_view, m_styles);
		std::string title = !m_options.title.empty() ? m_options.title : DocumentName(m_view);
		if (title.empty())
			title = "Document";
		const TextStyle base = ResolveTextStyle(m_view, m_styles, m_view.Root());
		m_out += "<!DOCTYPE html>\n<html lang=\"";
		Escape(m_out, m_options.language);
		m_out += "\">\n<head>\n<meta charset=\"utf-8\">\n<title>";
		Escape(m_out, title);
		m_out += "</title>\n<style>\n";
		m_out += std::format("@page {{ size: {} {}; margin: {} {} {} {}; }}\n", Pt(g.width), Pt(g.height), Pt(g.margins.top),
							 Pt(g.margins.right), Pt(g.margins.bottom), Pt(g.margins.left));
		m_out += std::format("body {{ font-family: {}; font-size: {}; color: {}; max-width: {}; margin: 0 auto; }}\n",
							 FontStack(m_styles.Get(m_view, m_view.Root(), props::FontFamily), base.font), Pt(base.size), Css(base.color),
							 Pt(m_contentWidth));
		m_out += "p { margin: 0; }\ntable { border-collapse: collapse; }\ntd { vertical-align: top; }\n"
				 ".page-break { break-after: page; height: 0; }\n</style>\n</head>\n<body>\n";
		if (const NodeId header = DefaultOf(sections.front(), NodeType::Header); header != kNoNode)
			Container("header", header);
		m_out += "<main>\n";
		for (NodeId s : sections)
			Container("section", m_view.Body(s));
		m_out += "</main>\n";
		if (const NodeId footer = DefaultOf(sections.front(), NodeType::Footer); footer != kNoNode)
			Container("footer", footer);
		m_out += "</body>\n</html>\n";
		return Ok(std::move(m_out));
	}

private:
	NodeId DefaultOf(NodeId section, NodeType type) {
		if (const Node *s = m_view.Find(section))
			for (NodeId c : s->Children())
				if (const Node *n = m_view.Find(c); n && n->Type() == type && m_styles.Get(m_view, c, props::Visible) &&
													m_styles.Get(m_view, c, props::Variant) == HeaderFooterVariant::Default)
					return c;
		return kNoNode;
	}

	bool Visible(NodeId id) { return m_styles.Get(m_view, id, props::Visible); }

	void Container(std::string_view tag, NodeId container) {
		m_out += std::format("<{}>\n", tag);
		if (const Node *n = m_view.Find(container))
			for (NodeId c : n->Children())
				Block(c, m_contentWidth);
		m_out += std::format("</{}>\n", tag);
	}

	std::string BoxStyle(NodeId id) {
		std::string s;
		if (const Color bg = m_styles.Get(m_view, id, props::BackgroundColor); bg.a != 0)
			s += "background-color: " + Css(bg) + "; ";
		if (const double w = m_styles.Get(m_view, id, props::BorderWidth); w > 0)
			s += std::format("border: {} solid {}; ", Pt(w), Css(m_styles.Get(m_view, id, props::BorderColor)));
		const double pt = m_styles.Get(m_view, id, props::PaddingTop), pr = m_styles.Get(m_view, id, props::PaddingRight),
					 pb = m_styles.Get(m_view, id, props::PaddingBottom), pl = m_styles.Get(m_view, id, props::PaddingLeft);
		if (pt > 0 || pr > 0 || pb > 0 || pl > 0)
			s += std::format("padding: {} {} {} {}; ", Pt(pt), Pt(pr), Pt(pb), Pt(pl));
		return s;
	}

	std::string FlowStyle(NodeId id) {
		std::string s = std::format("margin: {} 0 {} 0; ", Pt(m_styles.Get(m_view, id, props::SpaceBefore)), Pt(m_styles.Get(m_view, id, props::SpaceAfter)));
		if (m_styles.Get(m_view, id, props::PageBreakBefore))
			s += "break-before: page; ";
		if (m_styles.Get(m_view, id, props::KeepTogether))
			s += "break-inside: avoid; ";
		if (m_styles.Get(m_view, id, props::KeepWithNext))
			s += "break-after: avoid; ";
		return s;
	}

	std::string TextStyleCss(const TextStyle &t, const std::string &family, const TextStyle *parent, const std::string *parentFamily) {
		std::string s;
		if (!parent || family != *parentFamily || t.font != parent->font)
			s += "font-family: " + FontStack(family, t.font) + "; ";
		if (!parent || t.size != parent->size)
			s += "font-size: " + Pt(t.size) + "; ";
		if (!parent || t.font.Bold() != parent->font.Bold())
			s += t.font.Bold() ? "font-weight: bold; " : "font-weight: normal; ";
		if (!parent || t.font.Italic() != parent->font.Italic())
			s += t.font.Italic() ? "font-style: italic; " : "font-style: normal; ";
		if (!parent || !(t.color == parent->color))
			s += "color: " + Css(t.color) + "; ";
		if ((t.underline && (!parent || !parent->underline)) || (t.strike && (!parent || !parent->strike)))
			s += std::format("text-decoration: {}{}; ", t.underline ? "underline" : "", t.strike ? (t.underline ? " line-through" : "line-through") : "");
		if (t.rise != 0)
			s += t.rise > 0 ? "vertical-align: super; " : "vertical-align: sub; ";
		if (t.background.a != 0)
			s += "background-color: " + Css(t.background) + "; ";
		return s;
	}

	static void Attr(std::string &out, std::string_view style) {
		if (style.empty())
			return;
		out += " style=\"";
		Escape(out, style.substr(0, style.size() - 1)); // dernier espace retiré
		out += "\"";
	}

	void Block(NodeId id, double width) {
		const Node *n = m_view.Find(id);
		if (!n || !TypeInfo(n->Type()).flowBlock || !Visible(id))
			return;
		switch (n->Type()) {
		case NodeType::Paragraph: Paragraph(*n); break;
		case NodeType::Table: Table(*n, width); break;
		case NodeType::Image: Image(*n, width); break;
		case NodeType::Shape: Shape(*n, width); break;
		case NodeType::Drawing: Drawing(*n, width); break;
		case NodeType::PageBreak: m_out += "<div class=\"page-break\"></div>\n"; break;
		case NodeType::TableOfContents: Toc(*n); break;
		default: break;
		}
	}

	void Paragraph(const Node &n) {
		const TextStyle ps = ResolveTextStyle(m_view, m_styles, n.Id());
		const std::string family = m_styles.Get(m_view, n.Id(), props::FontFamily);
		std::string style = FlowStyle(n.Id()) + BoxStyle(n.Id()) + TextStyleCss(ps, family, nullptr, nullptr);
		static constexpr const char *kAlign[] = {"left", "center", "right", "justify", "left"}; // None : comme Left
		style += std::format("text-align: {}; ", kAlign[size_t(m_styles.Get(m_view, n.Id(), props::HAlign))]);
		style += std::format("line-height: {:.3f}; ", 1.2 * m_styles.Get(m_view, n.Id(), props::LineSpacing));
		if (const double v = m_styles.Get(m_view, n.Id(), props::IndentLeft); v != 0)
			style += "margin-left: " + Pt(v) + "; ";
		if (const double v = m_styles.Get(m_view, n.Id(), props::IndentRight); v != 0)
			style += "margin-right: " + Pt(v) + "; ";
		if (const double v = m_styles.Get(m_view, n.Id(), props::FirstLineIndent); v != 0)
			style += "text-indent: " + Pt(v) + "; ";
		const int64_t lines = m_styles.Get(m_view, n.Id(), props::MinimumLines);
		style += std::format("orphans: {}; widows: {}; ", lines, lines);
		if (const double h = m_styles.Get(m_view, n.Id(), props::MinHeight); h > 0)
			style += "min-height: " + Pt(h) + "; ";
		const ParagraphPrefix *prefix = m_outline.Prefix(n.Id());
		if (prefix && prefix->marker) // élément de liste : marqueur dans le retrait
			style += std::format("padding-left: {}; text-indent: -18pt; ", Pt(18.0 * (prefix->listLevel + 1)));
		const int64_t level = m_styles.Get(m_view, n.Id(), props::OutlineLevel);
		const std::string tag = level >= 1 && level <= 6 ? std::format("h{}", level) : std::string("p");
		m_out += std::format("<{} id=\"n{}\"", tag, n.Id());
		Attr(m_out, style);
		m_out += ">";
		bool any = false;
		if (prefix) {
			m_out += std::format("<span class=\"{}\">", prefix->marker ? "marker" : "number");
			Escape(m_out, prefix->text);
			m_out += prefix->marker ? "</span>\u00A0\u00A0" : "</span> ";
		}
		for (NodeId c : n.Children()) {
			const Node *t = m_view.Find(c);
			if (!t || t->Type() != NodeType::Text || !Visible(c))
				continue;
			const TextStyle ts = ResolveTextStyle(m_view, m_styles, c);
			const std::string tf = m_styles.Get(m_view, c, props::FontFamily);
			const FieldKind field = m_styles.Get(m_view, c, props::Field);
			const std::string css = TextStyleCss(ts, tf, &ps, &family);
			if (field == FieldKind::Reference) { // renvoi : numéro de la cible, lien d'ancre
				const NodeId target = LinkTarget(ts.link);
				m_out += std::format("<a href=\"#n{}\"", target);
				Attr(m_out, css);
				m_out += ">";
				Escape(m_out, m_outline.Reference(target));
				m_out += "</a>";
				any = true;
				continue;
			}
			if (field != FieldKind::None) { // flux continu : pas de numéro de page, champ conservé
				static constexpr const char *kFields[] = {"", "page-number", "page-count", "reference", "page-reference", "chapter"};
				m_out += std::format("<span class=\"field\" data-field=\"{}\"", kFields[size_t(field)]);
				Attr(m_out, css);
				m_out += "></span>";
				any = true;
				continue;
			}
			const std::string text = m_styles.Get(m_view, c, props::Text);
			if (text.empty())
				continue;
			if (!ts.link.empty()) {
				const NodeId target = LinkTarget(ts.link);
				m_out += "<a href=\"";
				Escape(m_out, target != kNoNode ? std::format("#n{}", target) : ts.link);
				m_out += "\"";
				Attr(m_out, css);
				m_out += ">";
				Escape(m_out, text);
				m_out += "</a>";
			} else if (css.empty()) {
				Escape(m_out, text);
			} else {
				m_out += "<span";
				Attr(m_out, css);
				m_out += ">";
				Escape(m_out, text);
				m_out += "</span>";
			}
			any = true;
		}
		if (!any && !prefix)
			m_out += "<br>"; // paragraphe vide : garde sa hauteur de ligne
		m_out += std::format("</{}>\n", tag);
	}

	/// Table des matières : entrées en liens d'ancre (flux continu : pas de numéros de page).
	void Toc(const Node &n) {
		const TocContent content = m_styles.Get(m_view, n.Id(), props::TocContent);
		const bool links = m_styles.Get(m_view, n.Id(), props::Hyperlinks);
		const TextStyle ts = ResolveTextStyle(m_view, m_styles, n.Id());
		const std::string family = m_styles.Get(m_view, n.Id(), props::FontFamily);
		std::string style = FlowStyle(n.Id()) + BoxStyle(n.Id()) + TextStyleCss(ts, family, nullptr, nullptr);
		m_out += std::format("<nav class=\"toc\" id=\"n{}\"", n.Id());
		Attr(m_out, style);
		m_out += ">\n";
		for (const OutlineEntry *e : m_outline.Entries(content, int(m_styles.Get(m_view, n.Id(), props::TocDepth)))) {
			const double indent = content == TocContent::Headings ? 16.0 * (e->level - 1) : 0;
			m_out += std::format("<p style=\"margin: 0 0 0 {};\">", Pt(indent));
			if (links)
				m_out += std::format("<a href=\"#n{}\">", e->node);
			if (!e->label.empty()) {
				Escape(m_out, e->label);
				m_out += e->kind == OutlineKind::Heading ? " " : " : ";
			}
			Escape(m_out, e->text);
			if (links)
				m_out += "</a>";
			m_out += "</p>\n";
		}
		m_out += "</nav>\n";
	}

	void Table(const Node &n, double width) {
		BlockPtr measured = MeasureBlock(m_view, m_styles, n.Id(), width);
		std::string style = FlowStyle(n.Id()) + BoxStyle(n.Id()) + std::format("width: {}; ", Pt(measured->width));
		if (measured->x > 0)
			style += "margin-left: auto; margin-right: auto; ";
		m_out += "<table";
		Attr(m_out, style);
		m_out += ">\n";
		bool inHead = false, bodyOpen = false;
		size_t rowIndex = 0;
		for (NodeId r : n.Children()) {
			const Node *row = m_view.Find(r);
			if (!row || row->Type() != NodeType::TableRow || !Visible(r))
				continue;
			const RowLayout *rl = rowIndex < measured->rows.size() ? &measured->rows[rowIndex] : nullptr;
			++rowIndex;
			const bool header = rl && rl->header;
			if (header && !inHead && !bodyOpen) {
				m_out += "<thead>\n"; // répété à l'impression par les navigateurs
				inHead = true;
			} else if (!header && !bodyOpen) {
				if (inHead)
					m_out += "</thead>\n";
				m_out += "<tbody>\n";
				inHead = false;
				bodyOpen = true;
			}
			std::string rowStyle;
			if (const Color bg = m_styles.Get(m_view, r, props::BackgroundColor); bg.a != 0)
				rowStyle += "background-color: " + Css(bg) + "; ";
			if (rl)
				rowStyle += "height: " + Pt(rl->height) + "; ";
			m_out += "<tr";
			Attr(m_out, rowStyle);
			m_out += ">";
			const auto cells = row->Children();
			for (size_t c = 0; c < cells.size(); ++c) {
				// Mise en page de la cellule ; absente : case couverte par une fusion (pas de <td>).
				const CellLayout *cl = nullptr;
				if (rl) {
					for (const CellLayout &x : rl->cells)
						if (x.node == cells[c])
							cl = &x;
					if (!cl)
						continue;
				}
				std::string cellStyle = BoxStyle(cells[c]);
				if (cl)
					cellStyle += "width: " + Pt(cl->width) + "; ";
				static constexpr const char *kVAlign[] = {"top", "middle", "bottom"};
				cellStyle += std::format("vertical-align: {}; ", kVAlign[size_t(m_styles.Get(m_view, cells[c], props::VAlign))]);
				const char *tag = header ? "th" : "td";
				m_out += std::format("<{}", tag);
				if (cl && cl->columns > 1)
					m_out += std::format(" colspan=\"{}\"", cl->columns);
				if (cl && cl->rows > 1)
					m_out += std::format(" rowspan=\"{}\"", cl->rows);
				Attr(m_out, cellStyle);
				m_out += ">";
				if (const Node *cell = m_view.Find(cells[c]); cell && Visible(cells[c]))
					for (NodeId child : cell->Children())
						Block(child, cl ? cl->width : width);
				m_out += std::format("</{}>", tag);
			}
			m_out += "</tr>\n";
		}
		if (inHead)
			m_out += "</thead>\n";
		if (bodyOpen)
			m_out += "</tbody>\n";
		m_out += "</table>\n";
	}

	/// `item` : élément d'une zone de dessin (position absolue dans sa zone intérieure).
	std::string PlacedStyle(const Node &n, const BlockLayout &b, const Insets *item = nullptr) {
		std::string s = item ? std::format("position: absolute; left: {}; top: {}; box-sizing: border-box; width: {}; height: {}; ",
										   Pt(item->left + m_styles.Get(m_view, n.Id(), props::PositionX)),
										   Pt(item->top + m_styles.Get(m_view, n.Id(), props::PositionY)), Pt(b.width), Pt(b.height))
							 : FlowStyle(n.Id()) + std::format("display: block; box-sizing: border-box; width: {}; height: {}; ", Pt(b.width), Pt(b.height));
		if (!item && b.x > 0)
			s += "margin-left: " + Pt(b.x) + "; ";
		if (const double o = m_styles.Get(m_view, n.Id(), props::Opacity); o < 1)
			s += std::format("opacity: {:.3f}; ", o);
		if (const int64_t z = m_styles.Get(m_view, n.Id(), props::ZIndex); z != 0)
			s += item ? std::format("z-index: {}; ", z) : std::format("position: relative; z-index: {}; ", z);
		if (const double r = m_styles.Get(m_view, n.Id(), props::Rotation); r != 0) {
			s += std::format("transform: rotate({:g}deg); ", r);
			if (m_styles.Get(m_view, n.Id(), props::RotationOriginReference) == OriginBase::Node) // repères page / parent : centre
				s += std::format("transform-origin: calc(50% + {}) calc(50% + {}); ", Pt(m_styles.Get(m_view, n.Id(), props::RotationOriginX)),
								 Pt(m_styles.Get(m_view, n.Id(), props::RotationOriginY)));
		}
		return s;
	}

	void Image(const Node &n, double width, const BlockLayout *placed = nullptr, const Insets *item = nullptr) {
		BlockPtr measured = placed ? nullptr : MeasureBlock(m_view, m_styles, n.Id(), width);
		const BlockLayout &b = placed ? *placed : *measured;
		std::string style = PlacedStyle(n, b, item) + BoxStyle(n.Id());
		const ImageResource *res = m_view.FindResource(ResourceId(m_styles.Get(m_view, n.Id(), props::Resource)));
		std::optional<std::string> uri;
		if (res && m_options.embedImages)
			uri = PngDataUri(*res);
		if (!uri) {
			style += "background-color: #ebebeb; ";
			m_out += "<div role=\"img\"";
			Attr(m_out, style);
			m_out += "></div>\n";
			return;
		}
		m_out += "<img src=\"" + *uri + "\" alt=\"";
		Escape(m_out, m_styles.Get(m_view, n.Id(), props::Name));
		m_out += "\"";
		Attr(m_out, style);
		m_out += ">\n";
	}

	void Shape(const Node &n, double width, const BlockLayout *placed = nullptr, const Insets *item = nullptr) {
		BlockPtr measured = placed ? nullptr : MeasureBlock(m_view, m_styles, n.Id(), width);
		const BlockLayout &b = placed ? *placed : *measured;
		std::string style = PlacedStyle(n, b, item);
		const ShapeKind kind = m_styles.Get(m_view, n.Id(), props::Shape);
		const double stroke = m_styles.Get(m_view, n.Id(), props::BorderWidth);
		const Color strokeColor = m_styles.Get(m_view, n.Id(), props::BorderColor), fill = m_styles.Get(m_view, n.Id(), props::BackgroundColor);
		std::string svg;
		if (kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse) {
			style += BoxStyle(n.Id());
			if (kind == ShapeKind::Ellipse)
				style += "border-radius: 50%; ";
		} else {
			// Contour (ou trait) dessiné en SVG dans la boîte ; le rembourrage reste celui du texte.
			const double pt = m_styles.Get(m_view, n.Id(), props::PaddingTop), pr = m_styles.Get(m_view, n.Id(), props::PaddingRight),
						 pb = m_styles.Get(m_view, n.Id(), props::PaddingBottom), pl = m_styles.Get(m_view, n.Id(), props::PaddingLeft);
			style += std::format("position: {}; padding: {} {} {} {}; ", item ? "absolute" : "relative", Pt(pt), Pt(pr), Pt(pb), Pt(pl));
			const Rect box(0, 0, b.width, b.height);
			const auto points = [](const std::vector<double> &p) {
				std::string out;
				for (size_t i = 0; i + 1 < p.size(); i += 2)
					out += std::format("{}{:.2f},{:.2f}", out.empty() ? "" : " ", p[i], p[i + 1]);
				return out;
			};
			svg = std::format("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {:.2f} {:.2f}\" preserveAspectRatio=\"none\" "
							  "style=\"position: absolute; left: 0; top: 0; width: 100%; height: 100%; overflow: visible\">",
							  std::max(b.width, 0.01), std::max(b.height, 0.01));
			const std::string strokeCss = std::format("stroke=\"{}\" stroke-width=\"{:.2f}\"", Css(strokeColor), stroke);
			if (IsLineShape(kind)) {
				const double w = stroke > 0 ? stroke : 1.0, y = b.height / 2;
				svg += std::format("<line x1=\"0\" y1=\"{:.2f}\" x2=\"{:.2f}\" y2=\"{:.2f}\" stroke=\"{}\" stroke-width=\"{:.2f}\"/>", y,
								   kind == ShapeKind::ArrowLine ? b.width - std::min(b.width, std::max(8.0, w * 4)) * 0.8 : b.width, y, Css(strokeColor), w);
				if (kind == ShapeKind::ArrowLine)
					svg += std::format("<polygon points=\"{}\" fill=\"{}\"/>", points(ArrowHead(box, w)), Css(strokeColor));
			} else
				svg += std::format("<polygon points=\"{}\" fill=\"{}\" {} stroke-linejoin=\"round\"/>", points(ShapeOutline(kind, box)),
								   fill.a ? Css(fill) : std::string("none"), stroke > 0 ? strokeCss : std::string());
			svg += "</svg>";
		}
		m_out += "<div";
		Attr(m_out, style);
		m_out += ">" + svg;
		if (!n.Children().empty()) { // texte de la forme
			const VerticalAlign v = m_styles.Get(m_view, n.Id(), props::VAlign);
			m_out += std::format("<div style=\"position: relative; display: flex; flex-direction: column; height: 100%; justify-content: {}\">\n",
								 v == VerticalAlign::Middle ? "center" : v == VerticalAlign::Bottom ? "flex-end" : "flex-start");
			for (NodeId c : n.Children())
				if (const Node *p = m_view.Find(c); p && p->Type() == NodeType::Paragraph && Visible(c))
					Paragraph(*p);
			m_out += "</div>";
		}
		m_out += "</div>\n";
	}

	/// Zone de dessin : boîte relative, éléments en position absolue (rotation comprise).
	void Drawing(const Node &n, double width) {
		BlockPtr b = MeasureBlock(m_view, m_styles, n.Id(), width);
		std::string style = PlacedStyle(n, *b) + BoxStyle(n.Id()) + "position: relative; overflow: visible; ";
		m_out += "<div";
		Attr(m_out, style);
		m_out += ">\n";
		const Insets inner{b->padding.top, b->padding.right, b->padding.bottom, b->padding.left};
		for (const auto &[unused, item] : b->content)
			if (const Node *c = m_view.Find(item->node)) {
				if (c->Type() == NodeType::Image)
					Image(*c, b->width, item.get(), &inner);
				else if (c->Type() == NodeType::Shape)
					Shape(*c, b->width, item.get(), &inner);
			}
		m_out += "</div>\n";
	}

	const DocumentView &m_view;
	const HtmlExportOptions &m_options;
	PropertyCache m_styles;
	double m_contentWidth = 0;
	Outline m_outline;
	std::string m_out;
};

/// Texte brut d'une cellule (paragraphes séparés par « \n », tableaux imbriqués aplatis).
void CellText(const DocumentView &view, PropertyCache &styles, NodeId id, std::string &out) {
	const Node *n = view.Find(id);
	if (!n || !styles.Get(view, id, props::Visible))
		return;
	if (n->Type() == NodeType::Paragraph) {
		if (!out.empty())
			out.push_back('\n');
		for (NodeId c : n->Children())
			if (const Node *t = view.Find(c); t && t->Type() == NodeType::Text && styles.Get(view, c, props::Visible) &&
											  styles.Get(view, c, props::Field) == FieldKind::None)
				out += styles.Get(view, c, props::Text);
		return;
	}
	for (NodeId c : n->Children())
		CellText(view, styles, c, out);
}

} // namespace

DocResult<std::string> ExportHtml(const DocumentView &view, const HtmlExportOptions &options) {
	if (view.Sections().empty())
		return Failure(ErrorCode::InvalidValue, "document sans section");
	return HtmlWriter(view, options).Run();
}

Status SaveHtml(const DocumentView &view, const std::filesystem::path &path, const HtmlExportOptions &options) {
	auto html = ExportHtml(view, options);
	if (!html)
		return Err(html.Error());
	return WriteFile(path, html.Value());
}

// ── CSV ──

DocResult<std::string> ExportCsv(const DocumentView &view, NodeId table) {
	const Node *t = view.Find(table);
	if (!t || t->Type() != NodeType::Table)
		return Failure(ErrorCode::InvalidValue, std::format("{} n'est pas un tableau", table));
	PropertyCache styles;
	std::vector<std::vector<String>> rows;
	size_t columns = 0;
	for (NodeId r : t->Children()) {
		const Node *row = view.Find(r);
		if (!row || row->Type() != NodeType::TableRow || !styles.Get(view, r, props::Visible))
			continue;
		std::vector<String> cells;
		for (NodeId c : row->Children()) {
			std::string text;
			CellText(view, styles, c, text);
			cells.emplace_back(std::move(text));
		}
		columns = std::max(columns, cells.size());
		rows.push_back(std::move(cells));
	}
	for (auto &row : rows)
		row.resize(columns);
	return Ok(std::string(dataio::CsvDocument::EncodeRows(rows)));
}

Status SaveCsv(const DocumentView &view, NodeId table, const std::filesystem::path &path) {
	auto csv = ExportCsv(view, table);
	if (!csv)
		return Err(csv.Error());
	return WriteFile(path, csv.Value());
}

} // namespace doc
