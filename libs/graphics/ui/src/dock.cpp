#include "ui/dock.hpp"

#include <algorithm>

#include "ui/ui.hpp"

namespace ui {

namespace {
constexpr float kSplitter = 8.f; // épaisseur des séparateurs (bordures doubles)
constexpr float kTabBarH = 30.f; // hauteur des barres d'onglets
constexpr float kStrip = 24.f;   // épaisseur d'une région repliée (bande de ré-expansion)
constexpr float kMinRegion = 120.f;

/// Séparateur : bande sombre + deux filets = « bordure double ».
void DrawSplitter(sdl3::Canvas &c, const sdl3::FRect &r, bool horizontal, sdl3::Color line) {
	c.FillRect(r, sdl3::Rgba(10, 12, 16, 255));
	if (horizontal) {
		c.FillRect({r.x, r.y + 2, r.w, 1}, line);
		c.FillRect({r.x, r.y + 5, r.w, 1}, line);
	} else {
		c.FillRect({r.x + 2, r.y, 1, r.h}, line);
		c.FillRect({r.x + 5, r.y, 1, r.h}, line);
	}
}
} // namespace

void DockLayout::SetTabs(Side side, std::vector<DockTab> tabs) {
	Region &r = RegionOf(side);
	r.tabs = std::move(tabs);
	if (r.active < 0 || size_t(r.active) >= r.tabs.size())
		r.active = r.tabs.empty() ? -1 : 0;
}

const std::vector<DockTab> &DockLayout::Tabs(Side side) const { return RegionOf(side).tabs; }

DockState DockLayout::Capture() const {
	DockState out;
	for (size_t i = 0; i < 4; ++i) {
		const Region &r = m_regions[i];
		DockRegionState &o = out.regions[i];
		for (const DockTab &t : r.tabs)
			o.tabs.push_back(t.id);
		o.active = std::string(Active(Side(i)));
		o.visible = r.visible;
		o.size = r.size;
	}
	return out;
}

void DockLayout::Apply(const DockState &state, std::span<const DockTab> catalog) {
	for (size_t i = 0; i < 4; ++i) {
		const DockRegionState &src = state.regions[i];
		Region &r = m_regions[i];
		r.tabs.clear();
		for (const std::string &id : src.tabs)
			for (const DockTab &t : catalog)
				if (t.id == id) {
					r.tabs.push_back(t);
					break;
				}
		r.active = r.tabs.empty() ? -1 : 0;
		Activate(Side(i), src.active);
		r.visible = src.visible;
		r.size = std::max(kMinRegion, src.size);
	}
	m_dragIndex = -1;
	m_dragSplitter = -1;
}

DockLayout::Side DockLayout::Find(std::string_view id) const {
	for (size_t i = 0; i < 4; ++i)
		for (const DockTab &t : m_regions[i].tabs)
			if (t.id == id)
				return Side(i);
	return Side::Count;
}

void DockLayout::Activate(Side side, std::string_view id) {
	Region &r = RegionOf(side);
	for (int i = 0; i < int(r.tabs.size()); ++i)
		if (r.tabs[size_t(i)].id == id) {
			r.active = i;
			return;
		}
}

std::string_view DockLayout::Active(Side side) const {
	const Region &r = RegionOf(side);
	return r.active >= 0 && size_t(r.active) < r.tabs.size() ? std::string_view(r.tabs[size_t(r.active)].id) : std::string_view{};
}

void DockLayout::Show(Side side, bool visible) { RegionOf(side).visible = visible; }
bool DockLayout::Visible(Side side) const { return RegionOf(side).visible; }
void DockLayout::SetSize(Side side, float size) { RegionOf(side).size = std::max(kMinRegion, size); }
float DockLayout::Size(Side side) const { return RegionOf(side).size; }

void DockLayout::Draw(Context &ctx, const sdl3::FRect &area, std::function<void(Side, const sdl3::FRect &, std::string_view)> content) {
	sdl3::Canvas &c = ctx.Canvas();
	const Theme &th = ctx.Style();

	// ── 0. Redimensionnement en cours (avant le calcul des rectangles) ─────
	if (m_dragSplitter >= 0) {
		if (ctx.MouseDown(sdl3::MouseButton::Left)) {
			if (m_dragSplitter == 0)
				RegionOf(Side::Left).size = std::clamp(ctx.MouseX() - area.x, kMinRegion, area.w - RegionOf(Side::Right).size - 2 * kMinRegion);
			else if (m_dragSplitter == 1)
				RegionOf(Side::Right).size = std::clamp(area.Right() - ctx.MouseX(), kMinRegion, area.w - RegionOf(Side::Left).size - 2 * kMinRegion);
			else
				RegionOf(Side::Bottom).size = std::clamp(area.Bottom() - ctx.MouseY(), kMinRegion, area.h - 2 * kMinRegion);
		} else {
			m_dragSplitter = -1;
		}
	}

	// ── 1. Rectangles des régions ───────────────────────────────────────────
	const float lw = RegionOf(Side::Left).visible ? RegionOf(Side::Left).size : kStrip;
	const float rw = RegionOf(Side::Right).visible ? RegionOf(Side::Right).size : kStrip;
	const float bh = RegionOf(Side::Bottom).visible ? RegionOf(Side::Bottom).size : kStrip;
	const float topH = area.h - bh - kSplitter;

	sdl3::FRect left{area.x, area.y, lw, topH};
	sdl3::FRect right{area.Right() - rw, area.y, rw, topH};
	sdl3::FRect center{left.Right() + kSplitter, area.y, right.x - left.Right() - kSplitter * 2, topH};
	sdl3::FRect bottom{area.x, area.y + topH + kSplitter, area.w, bh};

	sdl3::FRect split[3] = {
		{left.Right(), area.y, kSplitter, topH},		 // 0 gauche|centre
		{right.x - kSplitter, area.y, kSplitter, topH}, // 1 centre|droite
		{area.x, area.y + topH, area.w, kSplitter},	 // 2 centre|bas
	};
	sdl3::FRect bars[4] = {
		{left.x, left.y, left.w, kTabBarH},
		{center.x, center.y, center.w, kTabBarH},
		{right.x, right.y, right.w, kTabBarH},
		{bottom.x, bottom.y, bottom.w, kTabBarH},
	};

	// ── 2. Nouvelles pressions sur un séparateur ────────────────────────────
	if (m_dragSplitter < 0 && m_dragIndex < 0) {
		for (int i = 0; i < 3; ++i)
			if (split[i].Contains(ctx.MouseX(), ctx.MouseY()) && ctx.MousePressed(sdl3::MouseButton::Left))
				m_dragSplitter = i;
	}

	// ── 3. Dépôt d'un onglet glissé (au relâchement) ────────────────────────
	if (m_dragIndex >= 0 && !ctx.MouseDown(sdl3::MouseButton::Left)) {
		Side target = Side::Count;
		for (int i = 0; i < 4; ++i)
			if (bars[i].Contains(ctx.MouseX(), ctx.MouseY())) {
				target = Side(i);
				break;
			}
		if (target != Side::Count && target != m_dragFrom) {
			Region &from = RegionOf(m_dragFrom);
			if (size_t(m_dragIndex) < from.tabs.size()) {
				DockTab tab = std::move(from.tabs[size_t(m_dragIndex)]);
				from.tabs.erase(from.tabs.begin() + m_dragIndex);
				if (from.active == m_dragIndex)
					from.active = from.tabs.empty() ? -1 : std::min<int>(from.active, int(from.tabs.size()) - 1);
				else if (from.active > m_dragIndex)
					--from.active;
				Region &to = RegionOf(target);
				to.tabs.push_back(std::move(tab));
				to.active = int(to.tabs.size()) - 1;
			}
		}
		m_dragIndex = -1;
	}

	// ── 4. Dessin ───────────────────────────────────────────────────────────
	for (int i = 0; i < 3; ++i)
		DrawSplitter(c, split[i], i == 2, th.panelBorder);

	auto drawRegion = [&](Side side, const sdl3::FRect &r, const sdl3::FRect &bar, bool collapsible) {
		Region &reg = RegionOf(side);
		c.FillRect(r, th.panel);
		c.StrokeRect(r, th.panelBorder);

		if (!reg.visible) {
			const bool hover = ctx.MouseIn(r);
			c.FillRect(r, hover ? th.widgetHover : th.widget);
			const char32_t icon = side == Side::Left ? icons::Codepoint(icons::MaterialIcons::CHEVRON_RIGHT) : side == Side::Right ? icons::Codepoint(icons::MaterialIcons::CHEVRON_LEFT) : icons::Codepoint(icons::MaterialIcons::ARROW_DROP_UP);
			c.Icon(r, icon, th.textDim, 18);
			if (hover && ctx.MousePressed(sdl3::MouseButton::Left))
				reg.visible = true;
			return;
		}

		c.FillRect(bar, th.title);
		float tx = bar.x + 4;
		for (int i = 0; i < int(reg.tabs.size()); ++i) {
			const DockTab &t = reg.tabs[size_t(i)];
			const float w = c.MeasureText(t.title, 13) + (t.icon ? 26 : 18);
			const sdl3::FRect tr{tx, bar.y + 3, w, bar.h - 6};
			const bool active = i == reg.active;
			const bool hover = ctx.MouseIn(tr);
			if (active || hover)
				c.FillRoundRect(tr, 4, active ? th.widget : th.widgetHover);
			if (t.icon)
				c.Icon({tr.x + 6, tr.y, 16, tr.h}, t.icon, active ? th.accent : th.textDim, 15);
			c.Text(tr.x + (t.icon ? 24 : 8), tr.y + (tr.h - c.LineHeight(13)) * 0.5f, t.title, active ? th.text : th.textDim, 13);
			if (active)
				c.FillRect({tr.x, tr.Bottom() - 1, tr.w, 2}, th.accent);
			if (hover && ctx.MousePressed(sdl3::MouseButton::Left)) {
				reg.active = i;
				if (m_dragIndex < 0) {
					m_dragFrom = side;
					m_dragIndex = i;
				}
			}
			tx += w + 2;
		}
		if (collapsible) {
			const sdl3::FRect bt{bar.Right() - 26, bar.y + 3, 22, bar.h - 6};
			const bool hover = ctx.MouseIn(bt);
			c.FillRoundRect(bt, 4, hover ? th.widgetHover : th.title);
			const char32_t icon = side == Side::Left ? icons::Codepoint(icons::MaterialIcons::CHEVRON_LEFT) : side == Side::Right ? icons::Codepoint(icons::MaterialIcons::CHEVRON_RIGHT) : icons::Codepoint(icons::MaterialIcons::EXPAND_MORE);
			c.Icon(bt, icon, hover ? th.text : th.textDim, 16);
			if (hover && ctx.MousePressed(sdl3::MouseButton::Left))
				reg.visible = false;
		}
		if (content) {
			const sdl3::FRect body{r.x + 1, bar.Bottom() + 1, r.w - 2, r.h - kTabBarH - 2};
			content(side, body, Active(side));
		}
	};

	drawRegion(Side::Left, left, bars[0], true);
	drawRegion(Side::Center, center, bars[1], false);
	drawRegion(Side::Right, right, bars[2], true);
	drawRegion(Side::Bottom, bottom, bars[3], true);

	// ── Fantôme de l'onglet en cours de glissement ──────────────────────────
	if (m_dragIndex >= 0 && ctx.MouseDown(sdl3::MouseButton::Left)) {
		const Region &from = RegionOf(m_dragFrom);
		if (size_t(m_dragIndex) < from.tabs.size()) {
			const std::string label = from.tabs[size_t(m_dragIndex)].title;
			const float w = c.MeasureText(label, 13) + 20;
			const sdl3::FRect g{ctx.MouseX() + 8, ctx.MouseY() + 8, w, kTabBarH - 6};
			c.FillRoundRect(g, 4, sdl3::Rgba(70, 90, 120, 235));
			c.StrokeRoundRect(g, 4, th.accent);
			c.Text(g.x + 10, g.y + (g.h - c.LineHeight(13)) * 0.5f, label, th.text, 13);
		}
	}
}

} // namespace ui
