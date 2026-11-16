#include "sdl3/render/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#include <SDL3/SDL.h>

#include <core/log.hpp>
#include <core/string.hpp>
#include <diagnostics/trace.hpp>

namespace sdl3 {
namespace {
constexpr int kAtlasSize = 1024;
// Points de code rastérisés en plus du Latin-1 (typographie française, flèches, symboles d'UI).
constexpr char32_t kExtraCodepoints[] = {U'œ', U'Œ', U'€', U'…', U'–', U'—', U'‘', U'’', U'“', U'”', U'«', U'»', U'•', U'→', U'←', U'↑', U'↓',
										 U'▲', U'▼', U'▶', U'◀', U'✓', U'✗', U'×', U'÷', U'°', U'±', U'≤', U'≥', U'≠', U'∞', U'√', U'·', U'■', U'□', U'●', U'○',
										 U'²', U'³', U'‹', U'›', U' ', U'♪', U'Ÿ',
										 // Scripts des mods : flèches, signe moins, bordures de sections (box-drawing).
										 U'↔', U'⇒', U'−', U'─', U'│', U'└', U'┘', U'═'};

constexpr float Pi() noexcept { return 3.14159265358979f; }

std::mutex gIconMutex;
std::vector<char32_t> &IconCodepoints() {
	static std::vector<char32_t> icons;
	return icons;
}

sdl3::FColor ToF(Color c) { return sdl3::FColor{float(c.r) / 255.f, float(c.g) / 255.f, float(c.b) / 255.f, float(c.a) / 255.f}; }
} // namespace

Color MixColor(Color a, Color b, float t) noexcept {
	auto m = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(float(x) + (float(y) - float(x)) * t)); };
	return Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

void Canvas::RegisterIcons(std::initializer_list<char32_t> codepoints) {
	std::scoped_lock lock(gIconMutex);
	auto &v = IconCodepoints();
	for (char32_t c : codepoints)
		if (std::ranges::find(v, c) == v.end())
			v.push_back(c);
}

Canvas::Canvas() = default;
Canvas::~Canvas() = default;

bool Canvas::Init(sdl3::Renderer &renderer, const CanvasFontSet &fonts, float scale) {
	m_renderer = &renderer;
	m_scale = scale;
	auto ttf = sdl3::TtfContext::Create();
	if (!ttf) {
		core::log::Error("SDL_ttf : {}", ttf.Error());
		return false;
	}
	m_ttf.emplace(std::move(ttf.Value()));
	auto usable = [](const std::string &p) { return !p.empty() && sdl3::Font::Open(String(p), 12.f).IsOk(); };
	m_paths[0] = usable(fonts.regular) ? fonts.regular : std::string();
	if (m_paths[0].empty())
		if (auto found = sdl3::FindFontPath({"DejaVuSans", "DejaVu Sans", "Ubuntu", "Noto Sans", "Liberation Sans"}))
			m_paths[0] = found.Value();
	if (m_paths[0].empty()) {
		core::log::Error("aucune police TTF trouvée");
		return false;
	}
	m_paths[1] = usable(fonts.bold) ? fonts.bold : m_paths[0];
	m_paths[2] = usable(fonts.mono) ? fonts.mono : std::string();
	if (m_paths[2].empty()) {
		auto found = sdl3::FindFontPath({"DejaVuSansMono", "DejaVu Sans Mono", "Ubuntu Mono", "Liberation Mono"});
		m_paths[2] = found ? found.Value() : m_paths[0];
	}
	m_paths[3] = usable(fonts.icons) ? fonts.icons : std::string();
	return Page(CanvasFont::Regular, 16.f) != nullptr;
}

void Canvas::SetScale(float scale) {
	if (scale != m_scale) {
		m_scale = scale;
		m_pages.clear(); // les pages sont rastérisées à la taille physique
		m_current = nullptr;
	}
}

Canvas::CanvasFontPage *Canvas::Page(CanvasFont font, float logicalSize) {
	if (font == CanvasFont::Icons && m_paths[3].empty())
		return nullptr;
	const int px = std::clamp(static_cast<int>(std::lround(logicalSize * m_scale)), 6, 160);
	const int key = int(font) * 1000 + px;
	if (auto it = m_pages.find(key); it != m_pages.end())
		return it->second.get();
	auto page = std::make_unique<CanvasFontPage>();
	page->font = font;
	page->pixelSize = px;
	if (!BuildPage(*page))
		return nullptr;
	CanvasFontPage *raw = page.get();
	m_pages.emplace(key, std::move(page));
	return raw;
}

bool Canvas::BuildPage(CanvasFontPage &page) {
	DIAG_ZONE("Canvas::BuildPage");
	auto font = sdl3::Font::Open(String(m_paths[int(page.font)]), float(page.pixelSize));
	if (!font) {
		core::log::Error("police {} : {}", m_paths[int(page.font)], font.Error());
		return false;
	}
	auto atlasSurf = sdl3::Surface::Create(kAtlasSize, kAtlasSize, sdl3::PixelFormat::RGBA32);
	if (!atlasSurf)
		return false;
	sdl3::Surface &atlas = atlasSurf.Value();
	SDL_FillSurfaceRect(atlas.Get(), nullptr, 0);
	const SDL_Rect white{0, 0, 4, 4}; // texel blanc commun à toutes les pages
	SDL_FillSurfaceRect(atlas.Get(), &white, SDL_MapSurfaceRGBA(atlas.Get(), 255, 255, 255, 255));
	page.lineHeight = float(font.Value().Height());
	int penX = 6, penY = 0, rowH = 0;
	auto add = [&](uint32_t cp) -> std::optional<Glyph> {
		if (!font.Value().HasGlyph(cp))
			return std::nullopt;
		auto metrics = font.Value().GlyphMetricsOf(cp);
		if (!metrics)
			return std::nullopt;
		Glyph g{};
		g.advance = float(metrics->advance);
		if (cp == U' ' || cp == 0xA0)
			return g;
		std::string utf8;
		unicode::AppendUtf8(utf8, cp);
		auto surf = font.Value().RenderBlended(String(utf8), sdl3::Color{255, 255, 255, 255});
		if (!surf)
			return std::nullopt;
		const int w = surf.Value().GetWidth(), h = surf.Value().GetHeight();
		if (penX + w + 2 > kAtlasSize) {
			penX = 0;
			penY += rowH + 2;
			rowH = 0;
		}
		if (penY + h > kAtlasSize)
			return std::nullopt;
		SDL_SetSurfaceBlendMode(surf.Value().Get(), SDL_BLENDMODE_NONE);
		SDL_Rect dst{penX, penY, w, h};
		SDL_BlitSurface(surf.Value().Get(), nullptr, atlas.Get(), &dst);
		g.u0 = float(penX) / kAtlasSize;
		g.v0 = float(penY) / kAtlasSize;
		g.u1 = float(penX + w) / kAtlasSize;
		g.v1 = float(penY + h) / kAtlasSize;
		g.w = float(w);
		g.h = float(h);
		penX += w + 2;
		rowH = std::max(rowH, h);
		return g;
	};
	if (page.font == CanvasFont::Icons) {
		std::vector<char32_t> icons;
		{
			std::scoped_lock lock(gIconMutex);
			icons = IconCodepoints();
		}
		for (char32_t cp : icons)
			if (auto g = add(uint32_t(cp)))
				page.extra.InsertOrAssign(uint32_t(cp), *g);
	} else {
		for (uint32_t cp = 32; cp < 256; ++cp) {
			if (cp >= 127 && cp < 160)
				continue;
			if (auto g = add(cp))
				page.latin[cp] = *g;
		}
		for (char32_t cp : kExtraCodepoints)
			if (auto g = add(uint32_t(cp)))
				page.extra.InsertOrAssign(uint32_t(cp), *g);
	}
	auto tex = sdl3::Texture::CreateFromSurface(*m_renderer, atlas);
	if (!tex)
		return false;
	tex.Value().SetBlendMode(sdl3::BlendMode::BLEND);
	tex.Value().SetScaleMode(SDL_SCALEMODE_LINEAR);
	page.atlas.emplace(std::move(tex.Value()));
	return true;
}

const Canvas::Glyph *Canvas::FindGlyph(CanvasFontPage &page, uint32_t cp) const {
	if (cp < 256 && page.font != CanvasFont::Icons)
		return page.latin[cp].advance > 0 ? &page.latin[cp] : &page.latin[uint32_t('?')];
	if (const Glyph *g = page.extra.Find(cp))
		return g;
	return page.font == CanvasFont::Icons ? nullptr : &page.latin[uint32_t('?')];
}

void Canvas::Begin() {
	m_width = float(m_renderer->GetOutputWidth()) / m_scale;
	m_height = float(m_renderer->GetOutputHeight()) / m_scale;
	m_drawCalls = 0;
	m_vertices.clear();
	m_indices.clear();
	m_current = nullptr;
	m_atlasCurrent = false;
	EnsureSolidTexture();
}

void Canvas::End() {
	Flush();
	m_renderer->ClearClipRect();
	m_clips.clear();
}

void Canvas::EnsureSolidTexture() {
	if (m_current && m_atlasCurrent)
		return;
	if (CanvasFontPage *p = Page(CanvasFont::Regular, 16.f); p && p->atlas) {
		UseTexture(&*p->atlas);
		m_atlasCurrent = true;
	}
}

void Canvas::Flush() {
	if (m_indices.empty() || !m_current)
		return;
	m_renderer->RenderGeometry(*m_current, m_vertices, m_indices);
	++m_drawCalls;
	m_vertices.clear();
	m_indices.clear();
}

void Canvas::UseTexture(sdl3::Texture *tex) {
	if (tex == m_current)
		return;
	Flush();
	m_current = tex;
}

void Canvas::Vertex(float x, float y, Color c) {
	m_vertices.push_back({{x * m_scale, y * m_scale}, ToF(c), {kWhiteUV, kWhiteUV}});
}

void Canvas::Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c) {
	const int base = static_cast<int>(m_vertices.size());
	const sdl3::FColor fc = ToF(c);
	const float s = m_scale;
	m_vertices.push_back({{x0 * s, y0 * s}, fc, {u0, v0}});
	m_vertices.push_back({{x1 * s, y0 * s}, fc, {u1, v0}});
	m_vertices.push_back({{x1 * s, y1 * s}, fc, {u1, v1}});
	m_vertices.push_back({{x0 * s, y1 * s}, fc, {u0, v1}});
	const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
	m_indices.insert(m_indices.end(), idx, idx + 6);
}

void Canvas::QuadColors(float x0, float y0, float x1, float y1, Color c00, Color c10, Color c11, Color c01) {
	const int base = static_cast<int>(m_vertices.size());
	Vertex(x0, y0, c00);
	Vertex(x1, y0, c10);
	Vertex(x1, y1, c11);
	Vertex(x0, y1, c01);
	const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
	m_indices.insert(m_indices.end(), idx, idx + 6);
}

void Canvas::Triangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
	const int base = static_cast<int>(m_vertices.size());
	Vertex(x0, y0, c);
	Vertex(x1, y1, c);
	Vertex(x2, y2, c);
	m_indices.insert(m_indices.end(), {base, base + 1, base + 2});
}

void Canvas::FillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
	if (c.a == 0)
		return;
	EnsureSolidTexture();
	Triangle(x0, y0, x1, y1, x2, y2, c);
}

void Canvas::FillRect(const FRect &r, Color c) {
	if (r.w <= 0 || r.h <= 0 || c.a == 0)
		return;
	EnsureSolidTexture();
	Quad(r.x, r.y, r.x + r.w, r.y + r.h, kWhiteUV, kWhiteUV, kWhiteUV, kWhiteUV, c);
}

void Canvas::FillGradientV(const FRect &r, Color top, Color bottom) {
	if (r.w <= 0 || r.h <= 0)
		return;
	EnsureSolidTexture();
	QuadColors(r.x, r.y, r.x + r.w, r.y + r.h, top, top, bottom, bottom);
}

void Canvas::FillGradientH(const FRect &r, Color left, Color right) {
	if (r.w <= 0 || r.h <= 0)
		return;
	EnsureSolidTexture();
	QuadColors(r.x, r.y, r.x + r.w, r.y + r.h, left, right, right, left);
}

void Canvas::StrokeRect(const FRect &r, Color c, float t) {
	FillRect({r.x, r.y, r.w, t}, c);
	FillRect({r.x, r.y + r.h - t, r.w, t}, c);
	FillRect({r.x, r.y + t, t, r.h - 2 * t}, c);
	FillRect({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, c);
}

void Canvas::FillRoundRect(const FRect &r, float radius, Color c) {
	radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
	if (radius < 0.75f) {
		FillRect(r, c);
		return;
	}
	if (r.w <= 0 || r.h <= 0 || c.a == 0)
		return;
	EnsureSolidTexture();
	FillRect({r.x + radius, r.y, r.w - 2 * radius, r.h}, c);
	FillRect({r.x, r.y + radius, radius, r.h - 2 * radius}, c);
	FillRect({r.Right() - radius, r.y + radius, radius, r.h - 2 * radius}, c);
	const int seg = std::clamp(int(radius * m_scale * 0.6f), 3, 12);
	const float cx[4] = {r.x + radius, r.Right() - radius, r.Right() - radius, r.x + radius};
	const float cy[4] = {r.y + radius, r.y + radius, r.Bottom() - radius, r.Bottom() - radius};
	const float start[4] = {Pi(), 1.5f * Pi(), 0.f, 0.5f * Pi()};
	for (int k = 0; k < 4; ++k)
		for (int i = 0; i < seg; ++i) {
			const float a0 = start[k] + 0.5f * Pi() * float(i) / float(seg);
			const float a1 = start[k] + 0.5f * Pi() * float(i + 1) / float(seg);
			Triangle(cx[k], cy[k], cx[k] + std::cos(a0) * radius, cy[k] + std::sin(a0) * radius, cx[k] + std::cos(a1) * radius,
					 cy[k] + std::sin(a1) * radius, c);
		}
}

void Canvas::StrokeRoundRect(const FRect &r, float radius, Color c, float t) {
	radius = std::min({radius, r.w * 0.5f, r.h * 0.5f});
	FillRect({r.x + radius, r.y, r.w - 2 * radius, t}, c);
	FillRect({r.x + radius, r.Bottom() - t, r.w - 2 * radius, t}, c);
	FillRect({r.x, r.y + radius, t, r.h - 2 * radius}, c);
	FillRect({r.Right() - t, r.y + radius, t, r.h - 2 * radius}, c);
	if (radius < 0.75f)
		return;
	const int seg = std::clamp(int(radius * m_scale * 0.6f), 3, 12);
	const float cx[4] = {r.x + radius, r.Right() - radius, r.Right() - radius, r.x + radius};
	const float cy[4] = {r.y + radius, r.y + radius, r.Bottom() - radius, r.Bottom() - radius};
	const float start[4] = {Pi(), 1.5f * Pi(), 0.f, 0.5f * Pi()};
	const float rr = radius - t * 0.5f;
	for (int k = 0; k < 4; ++k)
		for (int i = 0; i < seg; ++i) {
			const float a0 = start[k] + 0.5f * Pi() * float(i) / float(seg);
			const float a1 = start[k] + 0.5f * Pi() * float(i + 1) / float(seg);
			Line(cx[k] + std::cos(a0) * rr, cy[k] + std::sin(a0) * rr, cx[k] + std::cos(a1) * rr, cy[k] + std::sin(a1) * rr, c, t);
		}
}

void Canvas::FillCircle(float cx, float cy, float radius, Color c) {
	if (radius <= 0 || c.a == 0)
		return;
	EnsureSolidTexture();
	const int seg = std::clamp(int(radius * m_scale), 8, 48);
	for (int i = 0; i < seg; ++i) {
		const float a0 = 2.f * Pi() * float(i) / float(seg), a1 = 2.f * Pi() * float(i + 1) / float(seg);
		Triangle(cx, cy, cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, c);
	}
}

void Canvas::Line(float x0, float y0, float x1, float y1, Color c, float t) {
	EnsureSolidTexture();
	const float dx = x1 - x0, dy = y1 - y0;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 1e-4f)
		return;
	const float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
	const int base = static_cast<int>(m_vertices.size());
	Vertex(x0 + nx, y0 + ny, c);
	Vertex(x1 + nx, y1 + ny, c);
	Vertex(x1 - nx, y1 - ny, c);
	Vertex(x0 - nx, y0 - ny, c);
	const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
	m_indices.insert(m_indices.end(), idx, idx + 6);
}

void Canvas::Polyline(const float *xy, size_t points, Color c, float t) {
	for (size_t i = 1; i < points; ++i)
		Line(xy[2 * (i - 1)], xy[2 * (i - 1) + 1], xy[2 * i], xy[2 * i + 1], c, t);
}

float Canvas::Text(float x, float y, std::string_view s, Color c, float size, CanvasFont font) {
	CanvasFontPage *page = Page(font, size);
	if (!page || !page->atlas)
		return 0.f;
	UseTexture(&*page->atlas);
	m_atlasCurrent = true;
	float penX = x * m_scale;
	const float top = std::round(y * m_scale);
	for (size_t i = 0; i < s.size();) {
		const char32_t cp = unicode::DecodeUtf8(s, i);
		if (cp == U'\n')
			continue;
		if (cp == U'\t') { // tabulation : quatre espaces (même largeur que MeasureText)
			if (const Glyph *space = FindGlyph(*page, uint32_t(' ')))
				penX += space->advance * 4.f;
			continue;
		}
		const Glyph *g = FindGlyph(*page, static_cast<uint32_t>(cp));
		if (!g)
			continue;
		if (g->w > 0) {
			const float gx = std::round(penX);
			Quad(gx / m_scale, top / m_scale, (gx + g->w) / m_scale, (top + g->h) / m_scale, g->u0, g->v0, g->u1, g->v1, c);
		}
		penX += g->advance;
	}
	return penX / m_scale - x;
}

float Canvas::TextCentered(const FRect &r, std::string_view s, Color c, float size, CanvasFont font) {
	const float w = MeasureText(s, size, font);
	if (w <= r.w + 0.5f)
		return Text(r.x + (r.w - w) * 0.5f, r.y + (r.h - LineHeight(size, font)) * 0.5f, s, c, size, font);
	// Plus large que sa boîte : aligné à gauche et rogné (le début du texte reste lisible).
	PushClip(r);
	const float drawn = Text(r.x, r.y + (r.h - LineHeight(size, font)) * 0.5f, s, c, size, font);
	PopClip();
	return drawn;
}

bool Canvas::Icon(const FRect &r, char32_t cp, Color c, float size) {
	CanvasFontPage *page = Page(CanvasFont::Icons, size);
	if (!page || !page->atlas)
		return false;
	const Glyph *g = page->extra.Find(uint32_t(cp));
	if (!g || g->w <= 0)
		return false;
	UseTexture(&*page->atlas);
	m_atlasCurrent = true;
	const float w = g->w / m_scale, h = g->h / m_scale;
	const float x = std::round((r.x + (r.w - w) * 0.5f) * m_scale) / m_scale;
	const float y = std::round((r.y + (r.h - h) * 0.5f) * m_scale) / m_scale;
	Quad(x, y, x + w, y + h, g->u0, g->v0, g->u1, g->v1, c);
	return true;
}

float Canvas::MeasureText(std::string_view s, float size, CanvasFont font) {
	CanvasFontPage *page = Page(font, size);
	if (!page)
		return 0.f;
	float w = 0;
	for (size_t i = 0; i < s.size();) {
		const char32_t cp = unicode::DecodeUtf8(s, i);
		if (const Glyph *g = FindGlyph(*page, cp == U'\t' ? uint32_t(' ') : static_cast<uint32_t>(cp)))
			w += g->advance * (cp == U'\t' ? 4.f : 1.f);
	}
	return w / m_scale;
}

float Canvas::LineHeight(float size, CanvasFont font) {
	CanvasFontPage *page = Page(font, size);
	return page ? page->lineHeight / m_scale : size;
}

std::string Canvas::Ellipsize(std::string_view s, float maxWidth, float size, CanvasFont font) {
	if (MeasureText(s, size, font) <= maxWidth)
		return std::string(s);
	const float ell = MeasureText("…", size, font);
	std::string out;
	float w = 0;
	CanvasFontPage *page = Page(font, size);
	if (!page)
		return std::string(s);
	for (size_t i = 0; i < s.size();) {
		const size_t start = i;
		const char32_t cp = unicode::DecodeUtf8(s, i);
		const Glyph *g = FindGlyph(*page, static_cast<uint32_t>(cp));
		const float gw = g ? g->advance / m_scale : 0;
		if (w + gw + ell > maxWidth)
			break;
		w += gw;
		out.append(s.substr(start, i - start));
	}
	return out + "…";
}

void Canvas::Image(sdl3::Texture &tex, const FRect &dst, Color tint) {
	Flush();
	sdl3::Texture *previous = m_current;
	const bool previousAtlas = m_atlasCurrent;
	m_current = &tex;
	Quad(dst.x, dst.y, dst.x + dst.w, dst.y + dst.h, 0, 0, 1, 1, tint);
	Flush();
	m_current = previous;
	m_atlasCurrent = previousAtlas;
}

void Canvas::SetTarget(sdl3::Texture *target) {
	Flush();
	if (target)
		m_renderer->SetTarget(*target);
	else
		m_renderer->ResetTarget();
}

void Canvas::SetRenderState(SDL_GPURenderState *state) {
	Flush();
	m_renderer->SetGpuRenderState(state);
}

void Canvas::ImageRegion(sdl3::Texture &tex, float texW, float texH, const FRect &src, const FRect &dst, bool flipH, Color tint) {
	UseTexture(&tex);
	m_atlasCurrent = false;
	float u0 = src.x / texW, u1 = (src.x + src.w) / texW;
	if (flipH)
		std::swap(u0, u1);
	Quad(dst.x, dst.y, dst.x + dst.w, dst.y + dst.h, u0, src.y / texH, u1, (src.y + src.h) / texH, tint);
}

void Canvas::TexturedQuad(sdl3::Texture &tex, const float xy[8], const float uv[8], Color tint) {
	UseTexture(&tex);
	m_atlasCurrent = false;
	const int base = static_cast<int>(m_vertices.size());
	const sdl3::FColor fc = ToF(tint);
	for (int i = 0; i < 4; ++i)
		m_vertices.push_back({{xy[2 * i] * m_scale, xy[2 * i + 1] * m_scale}, fc, {uv[2 * i], uv[2 * i + 1]}});
	const int idx[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
	m_indices.insert(m_indices.end(), idx, idx + 6);
}

void Canvas::PushClip(const FRect &r, bool intersect) {
	Flush();
	const FRect clip = m_clips.empty() || !intersect ? r : m_clips.back().Intersect(r);
	m_clips.push_back(clip);
	m_renderer->SetClipRect(sdl3::Rect{int(clip.x * m_scale), int(clip.y * m_scale), int(std::ceil(clip.w * m_scale)),
									   int(std::ceil(clip.h * m_scale))});
}

void Canvas::PopClip() {
	Flush();
	if (!m_clips.empty())
		m_clips.pop_back();
	if (m_clips.empty()) {
		m_renderer->ClearClipRect();
	} else {
		const FRect &c = m_clips.back();
		m_renderer->SetClipRect(sdl3::Rect{int(c.x * m_scale), int(c.y * m_scale), int(std::ceil(c.w * m_scale)), int(std::ceil(c.h * m_scale))});
	}
}

} // namespace sdl3
