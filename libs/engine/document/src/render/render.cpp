// Rendu : conversion d'une page mise en page en liste d'affichage.
#include "document/render.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <jobs/jobs.hpp>

namespace doc {

namespace {

constexpr double kDefaultLineWidth = 1.0;
const Color kPaper = Color::WHITE();
const Color kPlaceholderFill = Color(235, 235, 235);
const Color kPlaceholderLine = Color(160, 160, 160);
const Color kOverflow = Color(220, 40, 40);

Color WithOpacity(Color c, double opacity) {
	return Color(c.r, c.g, c.b, uint8_t(std::clamp(double(c.a) * std::clamp(opacity, 0.0, 1.0) + 0.5, 0.0, 255.0)));
}

class Renderer {
public:
	Renderer(const DocumentView &view, const LayoutResult &layout, const RenderOptions &options)
		: m_view(view), m_layout(layout), m_options(options) {}

	DisplayList Page(size_t index) {
		DisplayList list;
		if (index >= m_layout.pages.size())
			return list;
		const PageLayout &page = m_layout.pages[index];
		list.width = page.geometry.width;
		list.height = page.geometry.height;
		list.page = page.page;
		list.number = page.number;
		m_out = &list.commands;

		const Rect paper(0, 0, page.geometry.width, page.geometry.height);
		if (m_options.paper)
			Fill(paper, kPaper, page.page);
		const auto pageProp = [&](PropertyId p) { return ResolvePageProperty(m_view, page.section, page.page, p).value; };
		Fill(paper, Prop<Color>::FromValue(pageProp(PropertyId::BackgroundColor)), page.page);
		if (const double border = Prop<double>::FromValue(pageProp(PropertyId::BorderWidth)); border > 0) // cadre autour de la zone de flux
			Stroke(Inflate(page.content, border / 2 + 4), Prop<Color>::FromValue(pageProp(PropertyId::BorderColor)), border, page.page);

		// Boîtes par couche (ZIndex des images et formes), ordre de la mise en page à couche égale ;
		// le texte par-dessus.
		std::vector<const LayoutBox *> order;
		order.reserve(page.boxes.size());
		for (const LayoutBox &box : page.boxes)
			order.push_back(&box);
		std::ranges::stable_sort(order, {}, [](const LayoutBox *b) { return b->Layer(); });
		for (const LayoutBox *box : order)
			Box(*box);
		for (const PlacedLine &line : page.lines)
			Line(line);
		if (m_options.blockBounds)
			for (const LayoutBox &box : page.boxes)
				Stroke(box.rect, m_options.boundsColor, 0.5, box.node);
		if (m_options.overflowMarkers)
			for (const LayoutBox &box : page.boxes)
				if (box.overflow)
					Stroke(box.rect, kOverflow, 1.5, box.node);
		return list;
	}

private:
	static Rect Inflate(const Rect &r, double d) { return Rect(r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d); }

	void Push(DrawCommand c) {
		c.rotation = m_rotation;
		c.pivotX = m_pivotX;
		c.pivotY = m_pivotY;
		m_out->push_back(std::move(c));
	}

	void Fill(const Rect &r, Color color, NodeId node, DrawKind kind = DrawKind::FillRect) {
		if (color.a == 0 || r.w <= 0 || r.h <= 0)
			return;
		DrawCommand c;
		c.kind = kind;
		c.rect = r;
		c.color = color;
		c.node = node;
		Push(std::move(c));
	}

	void Stroke(const Rect &r, Color color, double width, NodeId node, DrawKind kind = DrawKind::StrokeRect) {
		if (color.a == 0 || width <= 0)
			return;
		DrawCommand c;
		c.kind = kind;
		c.rect = r;
		c.color = color;
		c.lineWidth = width;
		c.node = node;
		Push(std::move(c));
	}

	void Segment(double x1, double y1, double x2, double y2, Color color, double width, NodeId node) {
		if (color.a == 0 || width <= 0)
			return;
		DrawCommand c;
		c.kind = DrawKind::Line;
		c.rect = Rect(x1, y1, x2 - x1, y2 - y1);
		c.color = color;
		c.lineWidth = width;
		c.node = node;
		Push(std::move(c));
	}

	/// Fond puis bordure (bordure centrée sur le bord de la boîte).
	void Decorate(const LayoutBox &box, double opacity = 1.0) {
		Fill(box.rect, WithOpacity(m_styles.Get(m_view, box.node, props::BackgroundColor), opacity), box.node);
		const Color color = WithOpacity(m_styles.Get(m_view, box.node, props::BorderColor), opacity);
		const double width = m_styles.Get(m_view, box.node, props::BorderWidth);
		const int64_t sides = m_styles.Get(m_view, box.node, props::BorderSides);
		if (sides == border::kAll) {
			Stroke(box.rect, color, width, box.node);
			return;
		}
		if (width <= 0 || color.a == 0)
			return;
		// Côtés choisis (filets d'un tableau, encadré à barre latérale) : un trait par côté.
		const Rect &r = box.rect;
		if (sides & border::kTop)
			Segment(r.x, r.y, r.Right(), r.y, color, width, box.node);
		if (sides & border::kBottom)
			Segment(r.x, r.Bottom(), r.Right(), r.Bottom(), color, width, box.node);
		if (sides & border::kLeft)
			Segment(r.x, r.y, r.x, r.Bottom(), color, width, box.node);
		if (sides & border::kRight)
			Segment(r.Right(), r.y, r.Right(), r.Bottom(), color, width, box.node);
	}

	void Box(const LayoutBox &box) {
		// Boîte tournée (forme, image, paragraphe du texte d'une forme) : ses commandes aussi.
		m_rotation = box.rotation;
		m_pivotX = box.pivotX;
		m_pivotY = box.pivotY;
		BoxContent(box);
		m_rotation = m_pivotX = m_pivotY = 0;
	}

	void BoxContent(const LayoutBox &box) {
		switch (box.type) {
		case NodeType::Paragraph:
		case NodeType::Table:
		case NodeType::TableCell:
		case NodeType::Header:
		case NodeType::Footer:
		case NodeType::Drawing:
		case NodeType::TableOfContents: Decorate(box); break;
		case NodeType::TableRow: Fill(box.rect, m_styles.Get(m_view, box.node, props::BackgroundColor), box.node); break;
		case NodeType::Image: Image(box); break;
		case NodeType::Shape: Shape(box); break;
		default: break;
		}
	}

	void Image(const LayoutBox &box) {
		const double opacity = m_styles.Get(m_view, box.node, props::Opacity);
		const auto resource = ResourceId(m_styles.Get(m_view, box.node, props::Resource));
		if (m_view.FindResource(resource)) {
			DrawCommand c;
			c.kind = DrawKind::Image;
			c.rect = box.rect;
			c.image = resource;
			c.opacity = opacity;
			c.node = box.node;
			Push(std::move(c));
		} else { // ressource absente : cadre barré
			Fill(box.rect, WithOpacity(kPlaceholderFill, opacity), box.node);
			Segment(box.rect.x, box.rect.y, box.rect.Right(), box.rect.Bottom(), kPlaceholderLine, 0.75, box.node);
			Segment(box.rect.x, box.rect.Bottom(), box.rect.Right(), box.rect.y, kPlaceholderLine, 0.75, box.node);
			Stroke(box.rect, kPlaceholderLine, 0.75, box.node);
		}
		Stroke(box.rect, WithOpacity(m_styles.Get(m_view, box.node, props::BorderColor), opacity), m_styles.Get(m_view, box.node, props::BorderWidth),
			   box.node);
	}

	void Shape(const LayoutBox &box) {
		const double opacity = m_styles.Get(m_view, box.node, props::Opacity);
		const Color fill = WithOpacity(m_styles.Get(m_view, box.node, props::BackgroundColor), opacity);
		const Color stroke = WithOpacity(m_styles.Get(m_view, box.node, props::BorderColor), opacity);
		const double width = m_styles.Get(m_view, box.node, props::BorderWidth);
		const ShapeKind kind = m_styles.Get(m_view, box.node, props::Shape);
		switch (kind) {
		case ShapeKind::Rectangle:
			Fill(box.rect, fill, box.node);
			Stroke(box.rect, stroke, width, box.node);
			break;
		case ShapeKind::Ellipse:
			Fill(box.rect, fill, box.node, DrawKind::FillEllipse);
			Stroke(box.rect, stroke, width, box.node, DrawKind::StrokeEllipse);
			break;
		case ShapeKind::Line:
		case ShapeKind::ArrowLine: {
			const double y = box.rect.y + box.rect.h / 2, w = width > 0 ? width : kDefaultLineWidth;
			const double head = kind == ShapeKind::ArrowLine ? std::min(box.rect.w, std::max(8.0, w * 4)) : 0;
			Segment(box.rect.x, y, box.rect.Right() - head * 0.8, y, stroke, w, box.node);
			if (kind == ShapeKind::ArrowLine)
				Polygon(ArrowHead(box.rect, w), stroke, 0, box.node, DrawKind::FillPolygon);
			break;
		}
		default: {
			std::vector<double> outline = ShapeOutline(kind, box.rect);
			Polygon(outline, fill, 0, box.node, DrawKind::FillPolygon);
			Polygon(std::move(outline), stroke, width, box.node, DrawKind::StrokePolygon);
			break;
		}
		}
	}

	void Polygon(std::vector<double> points, Color color, double width, NodeId node, DrawKind kind) {
		if (color.a == 0 || points.size() < 6 || (kind == DrawKind::StrokePolygon && width <= 0))
			return;
		double x0 = points[0], y0 = points[1], x1 = x0, y1 = y0;
		for (size_t i = 0; i + 1 < points.size(); i += 2) {
			x0 = std::min(x0, points[i]);
			x1 = std::max(x1, points[i]);
			y0 = std::min(y0, points[i + 1]);
			y1 = std::max(y1, points[i + 1]);
		}
		DrawCommand c;
		c.kind = kind;
		c.rect = Rect(x0, y0, x1 - x0, y1 - y0);
		c.color = color;
		c.lineWidth = width;
		c.node = node;
		c.points = std::move(points);
		Push(std::move(c));
	}

	void Line(const PlacedLine &line) {
		m_rotation = line.rotation; // texte d'une forme tournée
		m_pivotX = line.pivotX;
		m_pivotY = line.pivotY;
		LineContent(line);
		m_rotation = m_pivotX = m_pivotY = 0;
	}

	void LineContent(const PlacedLine &line) {
		for (const GlyphRun &run : line.runs) {
			const Rect area(run.x, line.rect.y, run.width, line.rect.h);
			Fill(area, run.style.background, run.node); // surlignage
			DrawCommand c;
			c.kind = DrawKind::Text;
			c.rect = area;
			c.color = run.style.color;
			c.node = run.node;
			c.text = run.text;
			c.font = run.style.font;
			c.size = run.style.size;
			c.baseline = line.baseline - run.style.rise; // exposant / indice
			if (c.color.a != 0 && !c.text.empty())
				Push(std::move(c));
			const double baseline = line.baseline - run.style.rise;
			const double thickness = run.style.font.UnderlineThickness(run.style.size);
			if (run.style.underline) {
				const double y = baseline + run.style.font.UnderlineOffset(run.style.size);
				Segment(run.x, y, run.x + run.width, y, run.style.color, thickness, run.node);
			}
			if (run.style.strike) { // barré : à mi-hauteur des minuscules
				const double y = baseline - run.style.size * 0.28;
				Segment(run.x, y, run.x + run.width, y, run.style.color, thickness, run.node);
			}
		}
	}

	const DocumentView &m_view;
	const LayoutResult &m_layout;
	const RenderOptions &m_options;
	PropertyCache m_styles;
	std::vector<DrawCommand> *m_out = nullptr;
	double m_rotation = 0, m_pivotX = 0, m_pivotY = 0; ///< rotation des commandes de la boîte en cours
};

} // namespace

// ── Contours des formes ──

namespace {

using Points = std::vector<std::pair<double, double>>;

/// Points du repère unité (0…1) → sommets dans la boîte.
std::vector<double> InBox(const Points &unit, const Rect &box) {
	std::vector<double> out;
	out.reserve(unit.size() * 2);
	for (const auto &[u, v] : unit) {
		out.push_back(box.x + u * box.w);
		out.push_back(box.y + v * box.h);
	}
	return out;
}

/// Points quelconques ramenés au repère unité (boîte englobante → 0…1).
Points Normalized(Points raw) {
	double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
	for (const auto &[x, y] : raw) {
		x0 = std::min(x0, x);
		x1 = std::max(x1, x);
		y0 = std::min(y0, y);
		y1 = std::max(y1, y);
	}
	for (auto &[x, y] : raw) {
		x = x1 > x0 ? (x - x0) / (x1 - x0) : 0.5;
		y = y1 > y0 ? (y - y0) / (y1 - y0) : 0.5;
	}
	return raw;
}

/// Polygone régulier (ou étoile si `inner` > 0 : rayon des creux), premier sommet à `startDegrees`.
Points Regular(int sides, double startDegrees, double inner = 0) {
	Points p;
	const int count = inner > 0 ? sides * 2 : sides;
	for (int i = 0; i < count; ++i) {
		const double a = (startDegrees + 360.0 * i / count) * std::numbers::pi / 180.0;
		const double r = inner > 0 && i % 2 ? inner : 1.0;
		p.emplace_back(r * std::cos(a), r * std::sin(a));
	}
	return Normalized(std::move(p));
}

Points Ellipse(int segments) {
	Points p;
	for (int i = 0; i < segments; ++i) {
		const double a = 2 * std::numbers::pi * i / segments;
		p.emplace_back(0.5 + 0.5 * std::cos(a), 0.5 + 0.5 * std::sin(a));
	}
	return p;
}

Points Heart() {
	Points p;
	constexpr int kSegments = 64;
	for (int i = 0; i < kSegments; ++i) {
		const double t = 2 * std::numbers::pi * i / kSegments;
		const double x = 16 * std::pow(std::sin(t), 3);
		const double y = -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t));
		p.emplace_back(x, y);
	}
	return Normalized(std::move(p));
}

/// Rectangle aux coins arrondis (rayon : 15 % du petit côté), directement dans la boîte.
std::vector<double> RoundedRectangle(const Rect &box) {
	const double r = std::min(box.w, box.h) * 0.15;
	std::vector<double> out;
	constexpr int kSteps = 6;
	const double cx[4] = {box.Right() - r, box.Right() - r, box.x + r, box.x + r};
	const double cy[4] = {box.y + r, box.Bottom() - r, box.Bottom() - r, box.y + r};
	for (int corner = 0; corner < 4; ++corner)
		for (int i = 0; i <= kSteps; ++i) {
			const double a = (-90.0 + 90.0 * corner + 90.0 * i / kSteps) * std::numbers::pi / 180.0;
			out.push_back(cx[corner] + r * std::cos(a));
			out.push_back(cy[corner] + r * std::sin(a));
		}
	return out;
}

} // namespace

std::vector<double> ShapeOutline(ShapeKind kind, const Rect &box) {
	switch (kind) {
	case ShapeKind::Rectangle: return InBox({{0, 0}, {1, 0}, {1, 1}, {0, 1}}, box);
	case ShapeKind::Ellipse: return InBox(Ellipse(48), box);
	case ShapeKind::Line:
	case ShapeKind::ArrowLine: return {};
	case ShapeKind::RoundedRectangle: return RoundedRectangle(box);
	case ShapeKind::Triangle: return InBox({{0.5, 0}, {1, 1}, {0, 1}}, box);
	case ShapeKind::RightTriangle: return InBox({{0, 0}, {1, 1}, {0, 1}}, box);
	case ShapeKind::Diamond: return InBox({{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}}, box);
	case ShapeKind::Parallelogram: return InBox({{0.25, 0}, {1, 0}, {0.75, 1}, {0, 1}}, box);
	case ShapeKind::Trapezoid: return InBox({{0.2, 0}, {0.8, 0}, {1, 1}, {0, 1}}, box);
	case ShapeKind::Pentagon: return InBox(Regular(5, -90), box);
	case ShapeKind::Hexagon: return InBox(Regular(6, 0), box);
	case ShapeKind::Octagon: return InBox(Regular(8, 22.5), box);
	case ShapeKind::Star5: return InBox(Regular(5, -90, 0.382), box);
	case ShapeKind::Star6: return InBox(Regular(6, -90, 0.5), box);
	case ShapeKind::Cross: {
		constexpr double a = 1.0 / 3, b = 2.0 / 3;
		return InBox({{a, 0}, {b, 0}, {b, a}, {1, a}, {1, b}, {b, b}, {b, 1}, {a, 1}, {a, b}, {0, b}, {0, a}, {a, a}}, box);
	}
	case ShapeKind::ArrowRight: return InBox({{0, 0.3}, {0.6, 0.3}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.7}, {0, 0.7}}, box);
	case ShapeKind::ArrowLeft: return InBox({{1, 0.3}, {0.4, 0.3}, {0.4, 0}, {0, 0.5}, {0.4, 1}, {0.4, 0.7}, {1, 0.7}}, box);
	case ShapeKind::ArrowUp: return InBox({{0.3, 1}, {0.3, 0.4}, {0, 0.4}, {0.5, 0}, {1, 0.4}, {0.7, 0.4}, {0.7, 1}}, box);
	case ShapeKind::ArrowDown: return InBox({{0.3, 0}, {0.3, 0.6}, {0, 0.6}, {0.5, 1}, {1, 0.6}, {0.7, 0.6}, {0.7, 0}}, box);
	case ShapeKind::DoubleArrow:
		return InBox({{0, 0.5}, {0.25, 0}, {0.25, 0.3}, {0.75, 0.3}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0.75, 0.7}, {0.25, 0.7}, {0.25, 1}}, box);
	case ShapeKind::Chevron: return InBox({{0, 0}, {0.7, 0}, {1, 0.5}, {0.7, 1}, {0, 1}, {0.3, 0.5}}, box);
	case ShapeKind::Heart: return InBox(Heart(), box);
	case ShapeKind::Callout: return InBox({{0, 0}, {1, 0}, {1, 0.75}, {0.45, 0.75}, {0.2, 1}, {0.25, 0.75}, {0, 0.75}}, box);
	case ShapeKind::Lightning: return InBox(Normalized({{0.62, 0}, {0, 0.6}, {0.42, 0.6}, {0.28, 1}, {1, 0.38}, {0.58, 0.38}, {0.84, 0}}), box);
	}
	return {};
}

std::vector<double> ArrowHead(const Rect &box, double width) {
	const double length = std::min(box.w, std::max(8.0, width * 4)), half = length * 0.45, y = box.y + box.h / 2;
	return {box.Right(), y, box.Right() - length, y - half, box.Right() - length, y + half};
}

DisplayList RenderPage(const DocumentView &view, const LayoutResult &layout, size_t page, const RenderOptions &options) {
	return Renderer(view, layout, options).Page(page);
}

std::vector<DisplayList> RenderDocument(const DocumentView &view, const LayoutResult &layout, const RenderOptions &options) {
	std::vector<DisplayList> pages(layout.pages.size());
	if (options.parallel && pages.size() > 1) {
		jobs::ThreadPool &pool = options.pool ? *options.pool : jobs::ThreadPool::Global();
		jobs::ParallelFor(pool, 0, pages.size(), 1, [&](size_t i) { pages[i] = RenderPage(view, layout, i, options); });
	} else {
		for (size_t i = 0; i < pages.size(); ++i)
			pages[i] = RenderPage(view, layout, i, options);
	}
	return pages;
}

} // namespace doc
