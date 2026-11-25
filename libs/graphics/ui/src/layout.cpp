#include "ui/layout.hpp"

#include <algorithm>
#include <cmath>

namespace ui {

sdl3::FRect Place(const sdl3::FRect &parent, float w, float h, Anchor anchor, float marginX, float marginY) noexcept {
	w = std::clamp(w, 0.f, std::max(0.f, parent.w));
	h = std::clamp(h, 0.f, std::max(0.f, parent.h));
	float x = parent.x, y = parent.y;
	switch (anchor) {
	case Anchor::TopLeft:
	case Anchor::Left:
	case Anchor::BottomLeft: x = parent.x + marginX; break;
	case Anchor::Top:
	case Anchor::Center:
	case Anchor::Bottom: x = parent.x + (parent.w - w) * 0.5f + marginX; break;
	default: x = parent.Right() - w - marginX; break;
	}
	switch (anchor) {
	case Anchor::TopLeft:
	case Anchor::Top:
	case Anchor::TopRight: y = parent.y + marginY; break;
	case Anchor::Left:
	case Anchor::Center:
	case Anchor::Right: y = parent.y + (parent.h - h) * 0.5f + marginY; break;
	default: y = parent.Bottom() - h - marginY; break;
	}
	return {std::round(x), std::round(y), w, h};
}

float FitScale(float width, float height, float refWidth, float refHeight, float minScale, float maxScale) noexcept {
	if (refWidth <= 0.f || refHeight <= 0.f)
		return 1.f;
	return std::clamp(std::min(width / refWidth, height / refHeight), minScale, maxScale);
}

sdl3::FRect FitAspect(const sdl3::FRect &parent, float aspect) noexcept {
	if (aspect <= 0.f || parent.h <= 0.f)
		return parent;
	float w = parent.w, h = parent.w / aspect;
	if (h > parent.h) {
		h = parent.h;
		w = h * aspect;
	}
	return {parent.x + (parent.w - w) * 0.5f, parent.y + (parent.h - h) * 0.5f, w, h};
}

sdl3::FRect CutTop(sdl3::FRect &r, float h, float gap) noexcept {
	h = std::clamp(h, 0.f, r.h);
	const sdl3::FRect out{r.x, r.y, r.w, h};
	const float used = std::min(r.h, h + gap);
	r.y += used;
	r.h -= used;
	return out;
}

sdl3::FRect CutBottom(sdl3::FRect &r, float h, float gap) noexcept {
	h = std::clamp(h, 0.f, r.h);
	const sdl3::FRect out{r.x, r.Bottom() - h, r.w, h};
	r.h -= std::min(r.h, h + gap);
	return out;
}

sdl3::FRect CutLeft(sdl3::FRect &r, float w, float gap) noexcept {
	w = std::clamp(w, 0.f, r.w);
	const sdl3::FRect out{r.x, r.y, w, r.h};
	const float used = std::min(r.w, w + gap);
	r.x += used;
	r.w -= used;
	return out;
}

sdl3::FRect CutRight(sdl3::FRect &r, float w, float gap) noexcept {
	w = std::clamp(w, 0.f, r.w);
	const sdl3::FRect out{r.Right() - w, r.y, w, r.h};
	r.w -= std::min(r.w, w + gap);
	return out;
}

namespace {
std::vector<float> Distribute(float total, std::span<const FlexItem> items, float gap, float &offset, Align justify) {
	std::vector<float> sizes(items.size());
	if (items.empty())
		return sizes;
	float fixed = gap * float(items.size() - 1), grow = 0.f;
	for (size_t i = 0; i < items.size(); ++i) {
		sizes[i] = std::clamp(items[i].size, items[i].min, items[i].max);
		fixed += sizes[i];
		grow += std::max(0.f, items[i].grow);
	}
	float free = total - fixed;
	// Répartition itérative : un élément borné par `max` rend sa part aux autres.
	if (grow > 0.f && free > 0.f) {
		std::vector<bool> frozen(items.size(), false);
		for (int pass = 0; pass < 4 && free > 0.01f && grow > 0.f; ++pass) {
			float nextGrow = 0.f, used = 0.f;
			for (size_t i = 0; i < items.size(); ++i) {
				if (frozen[i] || items[i].grow <= 0.f)
					continue;
				const float want = sizes[i] + free * items[i].grow / grow;
				const float got = std::min(want, items[i].max);
				used += got - sizes[i];
				sizes[i] = got;
				if (got < want)
					frozen[i] = true;
				else
					nextGrow += items[i].grow;
			}
			free -= used;
			grow = nextGrow;
		}
	}
	// Manque de place : chaque élément rétrécit proportionnellement jusqu'à son minimum.
	if (free < 0.f) {
		float shrinkable = 0.f;
		for (size_t i = 0; i < items.size(); ++i)
			shrinkable += std::max(0.f, sizes[i] - items[i].min);
		if (shrinkable > 0.f) {
			const float k = std::min(1.f, -free / shrinkable);
			for (size_t i = 0; i < items.size(); ++i)
				sizes[i] -= std::max(0.f, sizes[i] - items[i].min) * k;
			free = std::min(0.f, free + shrinkable * k);
		}
	}
	offset = 0.f;
	if (free > 0.f) {
		if (justify == Align::Center)
			offset = free * 0.5f;
		else if (justify == Align::End)
			offset = free;
	}
	return sizes;
}
} // namespace

std::vector<sdl3::FRect> Row(const sdl3::FRect &r, std::span<const FlexItem> items, float gap, Align justify) {
	float offset = 0.f;
	const std::vector<float> sizes = Distribute(r.w, items, gap, offset, justify);
	std::vector<sdl3::FRect> out;
	out.reserve(items.size());
	float x = r.x + offset;
	for (float s : sizes) {
		out.push_back({x, r.y, s, r.h});
		x += s + gap;
	}
	return out;
}

std::vector<sdl3::FRect> Column(const sdl3::FRect &r, std::span<const FlexItem> items, float gap, Align justify) {
	float offset = 0.f;
	const std::vector<float> sizes = Distribute(r.h, items, gap, offset, justify);
	std::vector<sdl3::FRect> out;
	out.reserve(items.size());
	float y = r.y + offset;
	for (float s : sizes) {
		out.push_back({r.x, y, r.w, s});
		y += s + gap;
	}
	return out;
}

sdl3::FRect GridLayout::Cell(int index) const noexcept {
	const int c = index % std::max(columns, 1), row = index / std::max(columns, 1);
	return {bounds.x + float(c) * (cell + gap), bounds.y + float(row) * (cell + gap), cell, cell};
}

int GridLayout::HitTest(float x, float y, int count) const noexcept {
	if (cell <= 0.f || x < bounds.x || y < bounds.y)
		return -1;
	const int c = int((x - bounds.x) / (cell + gap)), row = int((y - bounds.y) / (cell + gap));
	if (c >= columns)
		return -1;
	const int i = row * columns + c;
	if (i >= count || !Cell(i).Contains(x, y))
		return -1;
	return i;
}

GridLayout Grid(const sdl3::FRect &r, int count, float cell, float gap, int columns, Align align) noexcept {
	GridLayout g;
	g.cell = cell;
	g.gap = gap;
	g.columns = columns > 0 ? columns : std::max(1, int((r.w + gap) / std::max(cell + gap, 1.f)));
	g.rows = count <= 0 ? 0 : (count + g.columns - 1) / g.columns;
	const int usedCols = std::min(g.columns, std::max(count, 1));
	const float w = float(usedCols) * cell + float(usedCols - 1) * gap;
	const float h = g.rows > 0 ? float(g.rows) * cell + float(g.rows - 1) * gap : 0.f;
	float x = r.x;
	if (align == Align::Center)
		x = r.x + std::max(0.f, (r.w - w) * 0.5f);
	else if (align == Align::End)
		x = r.Right() - w;
	g.bounds = {std::round(x), r.y, w, h};
	return g;
}

sdl3::FRect AnchorStack::Push(float w, float h, Align align) noexcept {
	sdl3::FRect out{};
	const bool vertical = m_side == Anchor::Top || m_side == Anchor::Bottom;
	if (vertical) {
		float x = m_edge.x;
		if (align == Align::Center)
			x = m_edge.x + (m_edge.w - w) * 0.5f;
		else if (align == Align::End)
			x = m_edge.Right() - w;
		else if (align == Align::Stretch)
			w = m_edge.w;
		const float y = m_side == Anchor::Top ? m_edge.y - m_gap - h : m_edge.Bottom() + m_gap;
		out = {x, y, w, h};
		m_edge = {m_edge.x, y, m_edge.w, h}; // l'élément suivant se place contre celui-ci
	} else {
		float y = m_edge.y;
		if (align == Align::Center)
			y = m_edge.y + (m_edge.h - h) * 0.5f;
		else if (align == Align::End)
			y = m_edge.Bottom() - h;
		else if (align == Align::Stretch)
			h = m_edge.h;
		const float x = m_side == Anchor::Left ? m_edge.x - m_gap - w : m_edge.Right() + m_gap;
		out = {x, y, w, h};
		m_edge = {x, m_edge.y, w, m_edge.h};
	}
	if (!m_any) {
		m_used = out;
		m_any = true;
	} else {
		const float x0 = std::min(m_used.x, out.x), y0 = std::min(m_used.y, out.y);
		const float x1 = std::max(m_used.Right(), out.Right()), y1 = std::max(m_used.Bottom(), out.Bottom());
		m_used = {x0, y0, x1 - x0, y1 - y0};
	}
	return out;
}

SliderTrack MakeSliderTrack(const sdl3::FRect &area, float thumbRadius) noexcept {
	const float r = std::clamp(thumbRadius, 0.f, area.w * 0.5f);
	return {area.x + r, std::max(area.w - 2.f * r, 1e-3f), r};
}

float SliderTrack::Fraction(float x) const noexcept { return std::clamp((x - x0) / width, 0.f, 1.f); }

sdl3::FRect KeepInside(const sdl3::FRect &r, const sdl3::FRect &bounds) noexcept {
	sdl3::FRect o = r;
	o.w = std::min(o.w, bounds.w);
	o.h = std::min(o.h, bounds.h);
	o.x = std::clamp(o.x, bounds.x, bounds.Right() - o.w);
	o.y = std::clamp(o.y, bounds.y, bounds.Bottom() - o.h);
	return o;
}

} // namespace ui
