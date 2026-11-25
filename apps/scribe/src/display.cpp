#include "scribe/display.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <pdf/fonts.hpp>

#include "scribe/rulers.hpp"

namespace scribe {

namespace {

constexpr float kMinTextPixels = 3.5f;	///< en dessous : texte figuré par des barres (zoom arrière)
constexpr int kEllipseSegments = 48;

sdl3::Color Px(const doc::Color &c, double opacity = 1.0) {
	return sdl3::Color(c.r, c.g, c.b, uint8_t(std::clamp(double(c.a) * opacity, 0.0, 255.0)));
}

void EllipsePoints(const sdl3::FRect &r, std::vector<std::pair<float, float>> &out) {
	out.clear();
	const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
	for (int i = 0; i <= kEllipseSegments; ++i) {
		const float a = float(i) / kEllipseSegments * 2.f * std::numbers::pi_v<float>;
		out.emplace_back(cx + std::cos(a) * r.w / 2, cy + std::sin(a) * r.h / 2);
	}
}

/// Texte d'un segment : chaque mot à l'abscisse donnée par les métriques du moteur.
void DrawText(sdl3::Canvas &canvas, const doc::DrawCommand &c, const PageView &page, FontCache &fonts) {
	const float sizePx = float(c.size) * page.scale;
	const float baseline = page.rect.y + float(c.baseline) * page.scale;
	const sdl3::Color color = Px(c.color);
	if (sizePx < kMinTextPixels) { // figuré : un trait par segment
		const sdl3::FRect r = PageToScreen(page, c.rect);
		canvas.FillRect({r.x, baseline - sizePx * 0.5f, r.w, std::max(1.f, sizePx * 0.4f)}, sdl3::Color(color.r, color.g, color.b, 90));
		return;
	}
	const FontCache::Face face = fonts.Get(canvas, c.font);
	// Même fichier que la mesure : les largeurs concordent (aux arrondis de rastérisation près).
	// Police de repli (aucun fichier) : le corps est réduit si elle est plus large que la mesure
	// du moteur, pour que les mots n'empiètent pas sur les espaces.
	float drawPx = sizePx;
	const float engineWidth = float(c.rect.w) * page.scale;
	if (const float canvasWidth = canvas.MeasureText(c.text, sizePx, face.font); canvasWidth > engineWidth * 1.02f && canvasWidth > 0)
		drawPx = std::max(kMinTextPixels, sizePx * engineWidth / canvasWidth);
	const float top = baseline - drawPx * float(face.ascent / 1000.0);
	size_t start = 0;
	while (start < c.text.size()) {
		while (start < c.text.size() && c.text[start] == ' ')
			++start;
		if (start >= c.text.size())
			break;
		size_t end = c.text.find(' ', start);
		if (end == std::string::npos)
			end = c.text.size();
		const double offset = c.font.TextWidth(std::string_view(c.text).substr(0, start), c.size);
		canvas.Text(page.rect.x + float(c.rect.x + offset) * page.scale, top, std::string_view(c.text).substr(start, end - start), color, drawPx,
					face.font);
		start = end;
	}
}

} // namespace

PageLayoutResult PlacePages(const std::vector<doc::DisplayList> &pages, const sdl3::FRect &area, float zoom, float scrollY) {
	PageLayoutResult out;
	const float scale = zoom * kPixelsPerPoint;
	float y = area.y + kPageGap - scrollY;
	for (size_t i = 0; i < pages.size(); ++i) {
		const float w = float(pages[i].width) * scale, h = float(pages[i].height) * scale;
		const float x = area.x + std::max(kPageGap, (area.w - w) / 2);
		out.pages.push_back({i, {x, y, w, h}, scale});
		y += h + kPageGap;
		out.contentWidth = std::max(out.contentWidth, w + 2 * kPageGap);
	}
	out.contentHeight = y + scrollY - area.y;
	return out;
}

float FitWidthZoom(const std::vector<doc::DisplayList> &pages, float areaWidth) {
	double widest = 0;
	for (const doc::DisplayList &p : pages)
		widest = std::max(widest, p.width);
	if (widest <= 0)
		return 1.f;
	return std::clamp((areaWidth - 2 * kPageGap) / (float(widest) * kPixelsPerPoint), 0.1f, 8.f);
}

std::optional<PagePoint> ScreenToPage(const std::vector<PageView> &views, float x, float y) {
	for (const PageView &v : views)
		if (x >= v.rect.x && x <= v.rect.x + v.rect.w && y >= v.rect.y && y <= v.rect.y + v.rect.h)
			return PagePoint{v.index, double((x - v.rect.x) / v.scale), double((y - v.rect.y) / v.scale)};
	return std::nullopt;
}

sdl3::FRect PageToScreen(const PageView &view, const doc::Rect &r) {
	return {view.rect.x + float(r.x) * view.scale, view.rect.y + float(r.y) * view.scale, float(r.w) * view.scale, float(r.h) * view.scale};
}

sdl3::Texture *ImageCache::Get(sdl3::Renderer &renderer, const doc::DocumentView &view, doc::ResourceId id) {
	if (view.Stamp() != m_stamp) { // autre document : identifiants de ressources sans rapport
		m_textures.clear();
		m_stamp = view.Stamp();
	}
	if (auto it = m_textures.find(id); it != m_textures.end())
		return &it->second;
	const doc::ImageResource *img = view.FindResource(id);
	if (!img)
		return nullptr;
	auto surface = sdl3::Surface::FromRgba8(img->rgba, int(img->width), int(img->height));
	if (!surface)
		return nullptr;
	auto texture = sdl3::Texture::CreateFromSurface(renderer, surface.Value());
	if (!texture)
		return nullptr;
	return &m_textures.emplace(id, std::move(texture.Value())).first->second;
}

void FontCache::SetLibrary(std::shared_ptr<const doc::FontLibrary> fonts) {
	m_fonts = std::move(fonts);
	m_faces.clear();
}

std::span<const std::string_view> FontCache::Substitutes(pdf::StandardFont font) {
	static constexpr std::string_view kSans[] = {"Nimbus Sans", "Liberation Sans", "Arimo", "Arial"};
	static constexpr std::string_view kSerif[] = {"Nimbus Roman", "Liberation Serif", "Tinos", "Times New Roman"};
	static constexpr std::string_view kMono[] = {"Nimbus Mono PS", "Liberation Mono", "Cousine", "Courier New"};
	const size_t family = size_t(font) / 4; // 4 styles par famille : Helvetica, Times, Courier
	return family == 0 ? std::span<const std::string_view>(kSans) : family == 1 ? std::span<const std::string_view>(kSerif) : std::span<const std::string_view>(kMono);
}

FontCache::Face FontCache::Get(sdl3::Canvas &canvas, const pdf::FontRef &font) {
	const auto key = std::pair{font.face ? -1 : int(font.standard), static_cast<const void *>(font.face.get())};
	if (const auto it = m_faces.find(key); it != m_faces.end())
		return it->second;
	Face face;
	face.font = pdf::IsBold(font.standard) ? sdl3::CanvasFont::Bold : sdl3::CanvasFont::Regular;
	pdf::FontRef file = font;
	if (!file.face && m_fonts) // police standard : substitut installé aux mêmes métriques
		for (std::string_view family : Substitutes(font.standard))
			if (pdf::FontRef r = m_fonts->Resolve(family, pdf::IsBold(font.standard), pdf::IsItalic(font.standard)); r.face) {
				file = r;
				break;
			}
	if (file.face) {
		const sdl3::CanvasFont added = canvas.AddFont(file.face->Path().string());
		if (int(added) >= 4) { // fichier accepté par le canevas
			face.font = added;
			face.ascent = file.face->Ascent();
		}
	}
	m_faces.emplace(key, face);
	return face;
}

void DrawPage(sdl3::Canvas &canvas, sdl3::Renderer &renderer, const doc::DocumentView &view, const doc::DisplayList &list, const PageView &page,
			  ImageCache &images, FontCache &fonts) {
	canvas.FillRect({page.rect.x + 3, page.rect.y + 4, page.rect.w, page.rect.h}, sdl3::Color(0, 0, 0, 90)); // ombre
	canvas.PushClip(page.rect);
	std::vector<std::pair<float, float>> points;
	for (const doc::DrawCommand &c : list.commands) {
		const bool rotated = c.rotation != 0.0;
		if (rotated)
			canvas.PushRotation(float(c.rotation), page.rect.x + float(c.pivotX) * page.scale, page.rect.y + float(c.pivotY) * page.scale);
		const sdl3::FRect r = PageToScreen(page, c.rect);
		const float width = std::max(1.f, float(c.lineWidth) * page.scale);
		switch (c.kind) {
		case doc::DrawKind::FillRect: canvas.FillRect(r, Px(c.color)); break;
		case doc::DrawKind::StrokeRect: canvas.StrokeRect(r, Px(c.color), width); break;
		case doc::DrawKind::Line: canvas.Line(r.x, r.y, r.x + r.w, r.y + r.h, Px(c.color), width); break;
		case doc::DrawKind::FillEllipse: {
			EllipsePoints(r, points);
			const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
			for (size_t i = 0; i + 1 < points.size(); ++i)
				canvas.FillTriangle(cx, cy, points[i].first, points[i].second, points[i + 1].first, points[i + 1].second, Px(c.color));
			break;
		}
		case doc::DrawKind::StrokeEllipse:
			EllipsePoints(r, points);
			for (size_t i = 0; i + 1 < points.size(); ++i)
				canvas.Line(points[i].first, points[i].second, points[i + 1].first, points[i + 1].second, Px(c.color), width);
			break;
		case doc::DrawKind::Text: DrawText(canvas, c, page, fonts); break;
		case doc::DrawKind::FillPolygon:
		case doc::DrawKind::StrokePolygon: {
			std::vector<float> xy(c.points.size());
			for (size_t i = 0; i + 1 < c.points.size(); i += 2) {
				xy[i] = page.rect.x + float(c.points[i]) * page.scale;
				xy[i + 1] = page.rect.y + float(c.points[i + 1]) * page.scale;
			}
			if (c.kind == doc::DrawKind::FillPolygon)
				canvas.FillPolygon(xy.data(), xy.size() / 2, Px(c.color));
			else
				canvas.StrokePolygon(xy.data(), xy.size() / 2, Px(c.color), width);
			break;
		}
		case doc::DrawKind::Image:
			if (sdl3::Texture *t = images.Get(renderer, view, c.image))
				canvas.Image(*t, r, sdl3::Color(255, 255, 255, uint8_t(std::clamp(c.opacity, 0.0, 1.0) * 255)));
			break;
		}
		if (rotated)
			canvas.PopRotation();
	}
	canvas.PopClip();
}

// ── Cadre de transformation ──

namespace {

constexpr double kHandleFx[kHandleCount] = {0, 0.5, 1, 1, 1, 0.5, 0, 0};
constexpr double kHandleFy[kHandleCount] = {0, 0, 0, 0.5, 1, 1, 1, 0.5};

/// Rotation horaire (axe y vers le bas) de `degrees` autour de (cx, cy).
std::pair<double, double> RotateAround(double x, double y, double cx, double cy, double degrees) {
	const double r = degrees * std::numbers::pi / 180.0, c = std::cos(r), s = std::sin(r);
	const double dx = x - cx, dy = y - cy;
	return {cx + dx * c - dy * s, cy + dx * s + dy * c};
}

} // namespace

ShapeFrame FrameOf(const doc::LayoutBox &box) { return {box.rect, box.rotation, box.pivotX, box.pivotY}; }

std::pair<double, double> HandlePoint(const ShapeFrame &f, int handle) {
	const size_t h = size_t(std::clamp(handle, 0, kHandleCount - 1));
	return RotateAround(f.rect.x + kHandleFx[h] * f.rect.w, f.rect.y + kHandleFy[h] * f.rect.h, f.pivotX, f.pivotY, f.rotation);
}

int FrameHit(const ShapeFrame &f, double x, double y, double tolerance) {
	for (int h = 0; h < kHandleCount; ++h) {
		const auto [hx, hy] = HandlePoint(f, h);
		if (std::abs(hx - x) <= tolerance && std::abs(hy - y) <= tolerance)
			return h;
	}
	const auto [lx, ly] = RotateAround(x, y, f.pivotX, f.pivotY, -f.rotation); // repère de la forme
	const doc::Rect &r = f.rect;
	const bool outer = lx >= r.x - tolerance && lx <= r.x + r.w + tolerance && ly >= r.y - tolerance && ly <= r.y + r.h + tolerance;
	if (!outer)
		return -1;
	const bool inner = lx > r.x + tolerance && lx < r.x + r.w - tolerance && ly > r.y + tolerance && ly < r.y + r.h - tolerance;
	return inner ? kFrameInside : kFrameBorder;
}

// ── Formes ──

namespace {

constexpr ShapeInfo kShapes[] = {
	{doc::ShapeKind::Rectangle, "Rectangle"},
	{doc::ShapeKind::Ellipse, "Ellipse"},
	{doc::ShapeKind::Line, "Trait"},
	{doc::ShapeKind::RoundedRectangle, "Rectangle arrondi"},
	{doc::ShapeKind::Triangle, "Triangle"},
	{doc::ShapeKind::RightTriangle, "Triangle rectangle"},
	{doc::ShapeKind::Diamond, "Losange"},
	{doc::ShapeKind::Parallelogram, "Parallélogramme"},
	{doc::ShapeKind::Trapezoid, "Trapèze"},
	{doc::ShapeKind::Pentagon, "Pentagone"},
	{doc::ShapeKind::Hexagon, "Hexagone"},
	{doc::ShapeKind::Octagon, "Octogone"},
	{doc::ShapeKind::Star5, "Étoile à 5 branches"},
	{doc::ShapeKind::Star6, "Étoile à 6 branches"},
	{doc::ShapeKind::Cross, "Croix"},
	{doc::ShapeKind::ArrowRight, "Flèche droite"},
	{doc::ShapeKind::ArrowLeft, "Flèche gauche"},
	{doc::ShapeKind::ArrowUp, "Flèche haut"},
	{doc::ShapeKind::ArrowDown, "Flèche bas"},
	{doc::ShapeKind::DoubleArrow, "Double flèche"},
	{doc::ShapeKind::Chevron, "Chevron"},
	{doc::ShapeKind::Heart, "Cœur"},
	{doc::ShapeKind::Callout, "Bulle"},
	{doc::ShapeKind::Lightning, "Éclair"},
	{doc::ShapeKind::ArrowLine, "Flèche (trait)"},
};

} // namespace

std::span<const ShapeInfo> ShapeCatalog() noexcept { return kShapes; }

std::string_view ShapeName(doc::ShapeKind kind) noexcept {
	for (const ShapeInfo &s : kShapes)
		if (s.kind == kind)
			return s.name;
	return {};
}

void DrawShapePreview(sdl3::Canvas &canvas, const sdl3::FRect &r, doc::ShapeKind kind, sdl3::Color fill, sdl3::Color stroke) {
	const doc::Rect box(r.x, r.y, r.w, r.h);
	if (doc::IsLineShape(kind)) {
		const float y = r.y + r.h / 2;
		canvas.Line(r.x, y, r.x + r.w - (kind == doc::ShapeKind::ArrowLine ? 5.f : 0.f), y, stroke, 2.f);
		if (kind == doc::ShapeKind::ArrowLine) {
			const std::vector<double> head = doc::ArrowHead(box, 2.0);
			canvas.FillTriangle(float(head[0]), float(head[1]), float(head[2]), float(head[3]), float(head[4]), float(head[5]), stroke);
		}
		return;
	}
	const std::vector<double> outline = doc::ShapeOutline(kind, box);
	std::vector<float> xy(outline.begin(), outline.end());
	canvas.FillPolygon(xy.data(), xy.size() / 2, fill);
	canvas.StrokePolygon(xy.data(), xy.size() / 2, stroke, 1.5f);
}

double SnapToGrid(double value, double step) { return step > 0 ? std::round(value / step) * step : value; }

std::pair<double, double> SnappedMove(const doc::Rect &rect, double dx, double dy, double step, bool lockAxis) {
	if (lockAxis)
		(std::abs(dx) >= std::abs(dy) ? dy : dx) = 0;
	if (step > 0) {
		if (!lockAxis || dx != 0)
			dx = SnapToGrid(rect.x + dx, step) - rect.x;
		if (!lockAxis || dy != 0)
			dy = SnapToGrid(rect.y + dy, step) - rect.y;
	}
	return {dx, dy};
}

void DrawGrid(sdl3::Canvas &canvas, const PageView &page, double step, sdl3::Color color) {
	if (step <= 0 || page.scale <= 0)
		return;
	while (step * double(page.scale) < 6)
		step *= 2;
	const double width = double(page.rect.w / page.scale), height = double(page.rect.h / page.scale);
	canvas.PushClip(page.rect);
	for (double x = step; x < width; x += step) {
		const float sx = std::round(page.rect.x + float(x) * page.scale) + 0.5f;
		canvas.Line(sx, page.rect.y, sx, page.rect.y + page.rect.h, color, 1.f);
	}
	for (double y = step; y < height; y += step) {
		const float sy = std::round(page.rect.y + float(y) * page.scale) + 0.5f;
		canvas.Line(page.rect.x, sy, page.rect.x + page.rect.w, sy, color, 1.f);
	}
	canvas.PopClip();
}

doc::Rect ResizedRect(const ShapeFrame &f, int handle, double dx, double dy, bool keepRatio) {
	const size_t h = size_t(std::clamp(handle, 0, kHandleCount - 1));
	const double a = f.rotation * std::numbers::pi / 180.0, c = std::cos(a), s = std::sin(a);
	const double lx = dx * c + dy * s, ly = -dx * s + dy * c; // rotation inverse du déplacement
	doc::Rect r = f.rect;
	constexpr double kMin = 1.0;
	if (kHandleFx[h] == 0) {
		const double right = r.x + r.w;
		r.w = std::max(kMin, r.w - lx);
		r.x = right - r.w;
	} else if (kHandleFx[h] == 1) {
		r.w = std::max(kMin, r.w + lx);
	}
	if (kHandleFy[h] == 0) {
		const double bottom = r.y + r.h;
		r.h = std::max(kMin, r.h - ly);
		r.y = bottom - r.h;
	} else if (kHandleFy[h] == 1) {
		r.h = std::max(kMin, r.h + ly);
	}
	if (!keepRatio || f.rect.w <= 0 || f.rect.h <= 0)
		return r;
	const doc::Rect &o = f.rect;
	const bool corner = kHandleFx[h] != 0.5 && kHandleFy[h] != 0.5;
	if (corner) { // la plus grande variation relative l'emporte ; coin opposé fixe
		const double scale = std::max(r.w / o.w, r.h / o.h);
		r.w = std::max(kMin, o.w * scale);
		r.h = std::max(kMin, o.h * scale);
		r.x = kHandleFx[h] == 0 ? o.x + o.w - r.w : o.x;
		r.y = kHandleFy[h] == 0 ? o.y + o.h - r.h : o.y;
	} else if (kHandleFy[h] == 0.5) { // bord gauche / droit : la hauteur suit, centrée
		r.h = std::max(kMin, r.w * o.h / o.w);
		r.y = o.y + (o.h - r.h) / 2;
	} else { // bord haut / bas : la largeur suit, centrée
		r.w = std::max(kMin, r.h * o.w / o.h);
		r.x = o.x + (o.w - r.w) / 2;
	}
	return r;
}

double DraggedRotation(const ShapeFrame &f, double fromX, double fromY, double toX, double toY, bool snap) {
	const double a0 = std::atan2(fromY - f.pivotY, fromX - f.pivotX), a1 = std::atan2(toY - f.pivotY, toX - f.pivotX);
	double degrees = f.rotation + (a1 - a0) * 180.0 / std::numbers::pi;
	if (snap)
		degrees = std::round(degrees / 15.0) * 15.0;
	degrees = std::fmod(degrees, 360.0);
	if (degrees > 180)
		degrees -= 360;
	if (degrees <= -180)
		degrees += 360;
	return degrees;
}

void DrawShapeFrame(sdl3::Canvas &canvas, const PageView &page, const ShapeFrame &f, bool rotateMode) {
	const auto screen = [&](std::pair<double, double> p) {
		return std::pair{page.rect.x + float(p.first) * page.scale, page.rect.y + float(p.second) * page.scale};
	};
	const sdl3::Color color = rotateMode ? sdl3::Color(230, 50, 50, 255) : sdl3::Color(79, 157, 255, 255);
	// Contour : coins 0, 2, 4, 6.
	for (int i = 0; i < 4; ++i) {
		const auto [x0, y0] = screen(HandlePoint(f, i * 2));
		const auto [x1, y1] = screen(HandlePoint(f, (i * 2 + 2) % kHandleCount));
		canvas.Line(x0, y0, x1, y1, color, rotateMode ? 2.f : 1.5f);
	}
	constexpr float kHalf = 4.f;
	for (int h = 0; h < kHandleCount; ++h) {
		const auto [x, y] = screen(HandlePoint(f, h));
		const sdl3::FRect r{x - kHalf, y - kHalf, kHalf * 2, kHalf * 2};
		if (rotateMode) {
			canvas.FillCircle(x, y, kHalf, sdl3::Color(255, 255, 255, 255));
			canvas.FillCircle(x, y, kHalf - 1.5f, color);
		} else {
			canvas.FillRect(r, sdl3::Color(255, 255, 255, 255));
			canvas.StrokeRect(r, color, 1.5f);
		}
	}
	if (rotateMode) { // centre de rotation
		const auto [x, y] = screen({f.pivotX, f.pivotY});
		canvas.FillCircle(x, y, 6.f, sdl3::Color(255, 255, 255, 255));
		canvas.FillCircle(x, y, 4.5f, color);
		canvas.Line(x - 9, y, x + 9, y, color, 1.f);
		canvas.Line(x, y - 9, x, y + 9, color, 1.f);
	}
}

// ── Rembourrage (marges intérieures) ──

double PaddingHandle::Value() const noexcept {
	switch (side) {
	case PaddingSide::Top: return padding.top;
	case PaddingSide::Right: return padding.right;
	case PaddingSide::Bottom: return padding.bottom;
	case PaddingSide::Left: return padding.left;
	}
	return 0;
}

double PaddingHandle::Edge() const noexcept {
	switch (side) {
	case PaddingSide::Top: return box.y + border + padding.top;
	case PaddingSide::Right: return box.Right() - border - padding.right;
	case PaddingSide::Bottom: return box.Bottom() - border - padding.bottom;
	case PaddingSide::Left: return box.x + border + padding.left;
	}
	return 0;
}

doc::PropertyId PaddingHandle::Property() const noexcept {
	switch (side) {
	case PaddingSide::Top: return doc::PropertyId::PaddingTop;
	case PaddingSide::Right: return doc::PropertyId::PaddingRight;
	case PaddingSide::Bottom: return doc::PropertyId::PaddingBottom;
	case PaddingSide::Left: return doc::PropertyId::PaddingLeft;
	}
	return doc::PropertyId::PaddingTop;
}

std::string_view PaddingName(PaddingSide side) noexcept {
	switch (side) {
	case PaddingSide::Top: return "Rembourrage haut";
	case PaddingSide::Right: return "Rembourrage droit";
	case PaddingSide::Bottom: return "Rembourrage bas";
	case PaddingSide::Left: return "Rembourrage gauche";
	}
	return {};
}

// ── Marges de la page ──

std::string_view MarginName(MarginSide side) noexcept {
	switch (side) {
	case MarginSide::Top: return "Marge haute";
	case MarginSide::Right: return "Marge droite";
	case MarginSide::Bottom: return "Marge basse";
	case MarginSide::Left: return "Marge gauche";
	}
	return {};
}

doc::PropertyId MarginProperty(MarginSide side) noexcept {
	switch (side) {
	case MarginSide::Top: return doc::PropertyId::MarginTop;
	case MarginSide::Right: return doc::PropertyId::MarginRight;
	case MarginSide::Bottom: return doc::PropertyId::MarginBottom;
	case MarginSide::Left: return doc::PropertyId::MarginLeft;
	}
	return doc::PropertyId::MarginTop;
}

double MarginOf(const doc::Insets &margins, MarginSide side) noexcept {
	switch (side) {
	case MarginSide::Top: return margins.top;
	case MarginSide::Right: return margins.right;
	case MarginSide::Bottom: return margins.bottom;
	case MarginSide::Left: return margins.left;
	}
	return 0;
}

double MaxMargin(const doc::PageGeometry &page, MarginSide side) noexcept {
	constexpr double kMinContent = 36;
	switch (side) {
	case MarginSide::Top: return std::max(0.0, page.height - page.margins.bottom - kMinContent);
	case MarginSide::Bottom: return std::max(0.0, page.height - page.margins.top - kMinContent);
	case MarginSide::Left: return std::max(0.0, page.width - page.margins.right - kMinContent);
	case MarginSide::Right: return std::max(0.0, page.width - page.margins.left - kMinContent);
	}
	return 0;
}

std::optional<PaddingHandle> PaddingAt(const doc::DocumentView &view, const doc::PageLayout &page, double x, double y, double tolerance) {
	std::optional<PaddingHandle> best;
	uint16_t bestDepth = 0;
	doc::PropertyCache styles;
	for (const doc::LayoutBox &box : page.boxes) {
		if (box.type != doc::NodeType::Paragraph && box.type != doc::NodeType::TableCell && box.type != doc::NodeType::TableOfContents)
			continue;
		if (!view.Contains(box.node) || (best && box.depth < bestDepth))
			continue;
		PaddingHandle h;
		h.node = box.node;
		h.type = box.type;
		h.box = box.rect;
		h.border = styles.Get(view, box.node, doc::props::BorderWidth);
		h.padding = {styles.Get(view, box.node, doc::props::PaddingTop), styles.Get(view, box.node, doc::props::PaddingRight),
					 styles.Get(view, box.node, doc::props::PaddingBottom), styles.Get(view, box.node, doc::props::PaddingLeft)};
		const bool inX = x >= box.rect.x && x <= box.rect.Right(), inY = y >= box.rect.y && y <= box.rect.Bottom();
		for (PaddingSide side : {PaddingSide::Left, PaddingSide::Right, PaddingSide::Top, PaddingSide::Bottom}) {
			h.side = side;
			if (h.Value() <= 0 || (side == PaddingSide::Top && box.continued) || (side == PaddingSide::Bottom && box.continues))
				continue;
			const bool hit = h.Horizontal() ? inY && std::abs(x - h.Edge()) <= tolerance : inX && std::abs(y - h.Edge()) <= tolerance;
			if (hit) {
				best = h;
				bestDepth = box.depth;
				break;
			}
		}
	}
	return best;
}

double DraggedPadding(const PaddingHandle &h, double coordinate, bool fine) {
	const doc::Rect &b = h.box;
	double value = 0, span = 0, opposite = 0;
	switch (h.side) {
	case PaddingSide::Left:
		value = coordinate - (b.x + h.border), span = b.w, opposite = h.padding.right;
		break;
	case PaddingSide::Right:
		value = (b.Right() - h.border) - coordinate, span = b.w, opposite = h.padding.left;
		break;
	case PaddingSide::Top:
		value = coordinate - (b.y + h.border), span = b.h, opposite = h.padding.bottom;
		break;
	case PaddingSide::Bottom:
		value = (b.Bottom() - h.border) - coordinate, span = b.h, opposite = h.padding.top;
		break;
	}
	const double max = std::max(0.0, span - 2 * h.border - opposite - 6);
	return std::clamp(SnapLength(value, fine), 0.0, max);
}

} // namespace scribe
