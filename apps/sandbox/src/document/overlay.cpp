#include "sandbox/document/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace sandbox {

namespace {

const char *DefaultName(Shape2D shape) {
	switch (shape) {
	case Shape2D::Rect: return "Rectangle";
	case Shape2D::Circle: return "Cercle";
	case Shape2D::Line: return "Ligne";
	case Shape2D::Text: return "Texte";
	case Shape2D::Image: return "Image";
	}
	return "Élément";
}

int Hex(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/// Distance d'un point au segment [a, b].
float SegmentDistance(const math::FVector2 &p, const math::FVector2 &a, const math::FVector2 &b) {
	const math::FVector2 ab = b - a;
	const float len = ab.x * ab.x + ab.y * ab.y;
	const float t = len > 1e-9f ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len, 0.f, 1.f) : 0.f;
	const math::FVector2 q = a + ab * t;
	return std::hypot(p.x - q.x, p.y - q.y);
}

} // namespace

Item2D *Layer2D::Find(uint32_t id) noexcept {
	const auto it = std::ranges::find(items, id, &Item2D::id);
	return it == items.end() ? nullptr : &*it;
}

const Item2D *Layer2D::Find(uint32_t id) const noexcept { return const_cast<Layer2D *>(this)->Find(id); }

Item2D *Layer2D::FindByName(std::string_view name) noexcept {
	const auto it = std::ranges::find_if(items, [&](const Item2D &i) { return std::string_view(i.name) == name; });
	return it == items.end() ? nullptr : &*it;
}

Item2D &Layer2D::Add(Shape2D shape, const math::FVector2 &position) {
	Item2D item;
	for (const Item2D &i : items)
		item.id = std::max(item.id, i.id);
	++item.id;
	item.shape = shape;
	item.position = position;
	const std::string base = DefaultName(shape);
	item.name = String(base);
	for (int n = 2; FindByName(item.name); ++n)
		item.name = String(std::format("{} {}", base, n));
	switch (shape) {
	case Shape2D::Circle:
		item.size = {80.f, 80.f};
		break;
	case Shape2D::Line:
		item.size = {200.f, 0.f};
		item.stroke = {1.f, 1.f, 1.f, 1.f};
		item.strokeWidth = 3.f;
		break;
	case Shape2D::Text:
		item.text = "Texte";
		item.fill = {1.f, 1.f, 1.f, 1.f};
		break;
	case Shape2D::Image:
		item.size = {128.f, 128.f};
		item.fill = {1.f, 1.f, 1.f, 1.f};
		break;
	case Shape2D::Rect: break;
	}
	items.push_back(std::move(item));
	return items.back();
}

bool Layer2D::Remove(uint32_t id) { return std::erase_if(items, [id](const Item2D &i) { return i.id == id; }) > 0; }

std::vector<const Item2D *> Layer2D::DrawOrder() const {
	std::vector<const Item2D *> order;
	order.reserve(items.size());
	for (const Item2D &i : items)
		order.push_back(&i);
	std::ranges::stable_sort(order, {}, &Item2D::z);
	return order;
}

Box2D BoundsOf(const Item2D &item, MeasureText2D measure, void *user) {
	const math::FVector2 p = item.position, s = item.size;
	switch (item.shape) {
	case Shape2D::Circle: {
		const float r = std::abs(s.x) * 0.5f;
		return {{p.x - r, p.y - r}, {p.x + r, p.y + r}};
	}
	case Shape2D::Line: {
		const math::FVector2 e = p + s;
		return {{std::min(p.x, e.x), std::min(p.y, e.y)}, {std::max(p.x, e.x), std::max(p.y, e.y)}};
	}
	case Shape2D::Text: {
		float width = 0.f;
		if (measure)
			width = measure(std::string_view(item.text), item.fontSize, user);
		else
			for (const char c : std::string_view(item.text))
				width += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? item.fontSize * 0.56f : 0.f;
		return {p, {p.x + width, p.y + item.fontSize * 1.25f}};
	}
	case Shape2D::Rect:
	case Shape2D::Image: break;
	}
	return {{std::min(p.x, p.x + s.x), std::min(p.y, p.y + s.y)}, {std::max(p.x, p.x + s.x), std::max(p.y, p.y + s.y)}};
}

uint32_t Pick(const Layer2D &layer, const math::FVector2 &p, float slack, MeasureText2D measure, void *user) {
	const std::vector<const Item2D *> order = layer.DrawOrder();
	for (auto it = order.rbegin(); it != order.rend(); ++it) { // le dernier dessiné est devant
		const Item2D &i = **it;
		if (!i.visible)
			continue;
		bool hit = false;
		switch (i.shape) {
		case Shape2D::Circle: hit = std::hypot(p.x - i.position.x, p.y - i.position.y) <= std::abs(i.size.x) * 0.5f + slack; break;
		case Shape2D::Line: hit = SegmentDistance(p, i.position, i.position + i.size) <= i.strokeWidth * 0.5f + slack; break;
		default: hit = BoundsOf(i, measure, user).Contains(p, slack); break;
		}
		if (hit)
			return i.id;
	}
	return 0;
}

OverlayView OverlayView::Fit(float x, float y, float w, float h, const math::FVector2 &reference) noexcept {
	OverlayView v;
	v.scale = std::max(1e-4f, std::min(w / std::max(reference.x, 1.f), h / std::max(reference.y, 1.f)));
	v.origin = {x + (w - reference.x * v.scale) * 0.5f, y + (h - reference.y * v.scale) * 0.5f};
	return v;
}

std::optional<modeling::Color> ParseColor(std::string_view text) {
	if (text.starts_with('#'))
		text.remove_prefix(1);
	if (text.size() != 3 && text.size() != 6 && text.size() != 8)
		return std::nullopt;
	float c[4] = {0.f, 0.f, 0.f, 1.f};
	const bool shortForm = text.size() == 3;
	for (size_t k = 0; k < (shortForm ? 3u : text.size() / 2); ++k) {
		const int hi = Hex(text[shortForm ? k : 2 * k]), lo = Hex(text[shortForm ? k : 2 * k + 1]);
		if (hi < 0 || lo < 0)
			return std::nullopt;
		c[k] = float(hi * 16 + lo) / 255.f;
	}
	return modeling::Color{c[0], c[1], c[2], c[3]};
}

String FormatColor(const modeling::Color &c) {
	const auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
	return c.a >= 0.999f ? String(std::format("#{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b)))
						 : String(std::format("#{:02x}{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b), byte(c.a)));
}

} // namespace sandbox
