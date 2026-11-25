// Calque 2D du sandbox : dessin (mode Jeu et Vue 2D), édition à la souris, outils, propriétés.
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>

#include <sdl3/render/image.hpp>

#include "sandbox/app/sandbox_app.hpp"

namespace sandbox {

namespace {

sdl3::Color Ink(const modeling::Color &c) {
	const auto byte = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
	return sdl3::Rgba(byte(c.r), byte(c.g), byte(c.b), byte(c.a));
}

modeling::Color FromInk(sdl3::Color c, float alpha) { return {float(c.r) / 255.f, float(c.g) / 255.f, float(c.b) / 255.f, alpha}; }

/// Mesure d'un texte par la police du canvas (sélection au point des textes).
float Measure(std::string_view text, float size, void *canvas) { return static_cast<sdl3::Canvas *>(canvas)->MeasureText(text, size); }

/// Rectangle d'écran d'une boîte du repère de référence.
sdl3::FRect ScreenRect(const Box2D &b, const OverlayView &v) {
	const math::FVector2 a = v.ToScreen(b.min), z = v.ToScreen(b.max);
	return {a.x, a.y, z.x - a.x, z.y - a.y};
}

/// Point de la poignée de redimensionnement (coin bas-droit, bout d'une ligne).
math::FVector2 HandleOf(const Item2D &i, const Box2D &box) {
	if (i.shape == Shape2D::Line)
		return i.position + i.size;
	return box.max;
}

/// Accroche le déplacement d'un élément aux bords et centres des autres éléments et de
/// l'écran de référence. Rend la position corrigée et pose `guideX`/`guideY` (−1e30 : aucun).
math::FVector2 AlignSnap(const Layer2D &layer, const Box2D &box, const math::FVector2 &pos, uint32_t self, float threshold,
						 float &guideX, float &guideY) {
	std::vector<float> xs = {0.f, layer.reference.x * 0.5f, layer.reference.x};
	std::vector<float> ys = {0.f, layer.reference.y * 0.5f, layer.reference.y};
	for (const Item2D &i : layer.items) {
		if (i.id == self || !i.visible)
			continue;
		const Box2D b = BoundsOf(i);
		xs.push_back(b.min.x);
		xs.push_back((b.min.x + b.max.x) * 0.5f);
		xs.push_back(b.max.x);
		ys.push_back(b.min.y);
		ys.push_back((b.min.y + b.max.y) * 0.5f);
		ys.push_back(b.max.y);
	}
	const float w = box.max.x - box.min.x, h = box.max.y - box.min.y;
	math::FVector2 result = pos;
	guideX = guideY = -1e30f;
	float best = threshold;
	for (float x : xs)
		for (float edge : {box.min.x, box.min.x + w * 0.5f, box.max.x}) {
			const float d = std::fabs(x - edge);
			if (d < best) {
				best = d;
				result.x = pos.x + (x - edge);
				guideX = x;
			}
		}
	best = threshold;
	for (float y : ys)
		for (float edge : {box.min.y, box.min.y + h * 0.5f, box.max.y}) {
			const float d = std::fabs(y - edge);
			if (d < best) {
				best = d;
				result.y = pos.y + (y - edge);
				guideY = y;
			}
		}
	return result;
}

} // namespace

// ── Dessin ───────────────────────────────────────────────────────────────────

sdl3::Texture *SandboxApp::OverlayImage(const String &file) {
	if (file.IsEmpty())
		return nullptr;
	const std::filesystem::path base = m_path.empty() ? std::filesystem::path() : std::filesystem::path(m_path).parent_path();
	const std::string path = (base / file.c_str()).string();
	auto [it, inserted] = m_images.try_emplace(path);
	if (inserted)
		if (auto tex = sdl3::ImgLoadTexture(Renderer(), String(path)); tex.IsOk())
			it->second.emplace(std::move(tex.Value()));
	return it->second ? &*it->second : nullptr;
}

void SandboxApp::DrawItem2D(const Item2D &i, const OverlayView &v) {
	sdl3::Canvas &c = Canvas();
	const float s = v.scale;
	const bool stroke = i.stroke.a > 0.f && i.strokeWidth > 0.f;
	switch (i.shape) {
	case Shape2D::Rect: {
		const sdl3::FRect r = ScreenRect(BoundsOf(i), v);
		if (i.fill.a > 0.f)
			c.FillRoundRect(r, i.cornerRadius * s, Ink(i.fill));
		if (stroke)
			c.StrokeRoundRect(r, i.cornerRadius * s, Ink(i.stroke), i.strokeWidth * s);
		break;
	}
	case Shape2D::Circle: {
		const math::FVector2 center = v.ToScreen(i.position);
		const float radius = std::abs(i.size.x) * 0.5f * s;
		if (i.fill.a > 0.f)
			c.FillCircle(center.x, center.y, radius, Ink(i.fill));
		if (stroke) {
			std::array<float, 2 * 49> ring{};
			for (size_t k = 0; k <= 48; ++k) {
				const float a = float(k) / 48.f * 2.f * math::PI;
				ring[2 * k] = center.x + std::cos(a) * radius;
				ring[2 * k + 1] = center.y + std::sin(a) * radius;
			}
			c.Polyline(ring.data(), 49, Ink(i.stroke), i.strokeWidth * s);
		}
		break;
	}
	case Shape2D::Line: {
		const math::FVector2 a = v.ToScreen(i.position), b = v.ToScreen(i.position + i.size);
		c.Line(a.x, a.y, b.x, b.y, Ink(i.stroke.a > 0.f ? i.stroke : i.fill), std::max(1.f, i.strokeWidth * s));
		break;
	}
	case Shape2D::Text: {
		const math::FVector2 p = v.ToScreen(i.position);
		c.Text(p.x, p.y, i.text, Ink(i.fill), std::max(4.f, i.fontSize * s));
		break;
	}
	case Shape2D::Image: {
		const sdl3::FRect r = ScreenRect(BoundsOf(i), v);
		if (sdl3::Texture *tex = OverlayImage(i.image))
			c.Image(*tex, r, Ink(i.fill));
		else { // fichier absent : cadre de remplacement
			c.FillRect(r, sdl3::Rgba(40, 44, 54, 160));
			c.StrokeRect(r, sdl3::Rgba(200, 200, 210, 200), 1.f);
			c.TextCentered(r, i.image.IsEmpty() ? std::string_view("image") : std::string_view(i.image), sdl3::Rgba(220, 220, 230), 12.f);
		}
		break;
	}
	}
}

void SandboxApp::DrawTilemap(const TilemapLayer &tm, const OverlayView &v) {
	if (tm.Empty())
		return;
	sdl3::Canvas &c = Canvas();
	for (int y = 0; y < tm.height; ++y)
		for (int x = 0; x < tm.width; ++x) {
			const int8_t t = tm.At(x, y);
			if (t == 0)
				continue;
			const math::FVector2 p = v.ToScreen(tm.origin + math::FVector2{float(x) * tm.tileSize, float(y) * tm.tileSize});
			c.FillRect({p.x, p.y, tm.tileSize * v.scale, tm.tileSize * v.scale}, Ink(tm.ColorOf(t)));
		}
}

void SandboxApp::DrawLayer2D(const Layer2D &layer, std::span<const Item2D> extra, const sdl3::FRect &view) {
	const OverlayView v = OverlayView::Fit(view.x, view.y, view.w, view.h, layer.reference);
	Canvas().PushClip(view);
	DrawTilemap(layer.tilemap, v); // calque de tuiles, sous les éléments
	for (const Item2D *i : layer.DrawOrder())
		if (i->visible)
			DrawItem2D(*i, v);
	for (const Item2D &i : extra) // dessins des scripts, par-dessus
		DrawItem2D(i, v);
	Canvas().PopClip();
}

// ── Vue 2D (édition) ─────────────────────────────────────────────────────────

void SandboxApp::DrawOverlayEditor(const sdl3::FRect &area) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Editor &ed = m_editor;
	Layer2D &layer = ed.Doc().Overlay();
	// Fond : la scène 3D, rendue aux proportions de l'écran de référence.
	c.FillRect(area, th.background);
	const sdl3::FRect inner{area.x + 16.f, area.y + 16.f, area.w - 32.f, area.h - 32.f};
	const OverlayView v = OverlayView::Fit(inner.x, inner.y, inner.w, inner.h, layer.reference);
	const math::FVector2 a = v.ToScreen({0.f, 0.f}), b = v.ToScreen(layer.reference);
	const sdl3::FRect frame{a.x, a.y, b.x - a.x, b.y - a.y};
	m_view = frame; // taille du rendu hors écran de l'image suivante
	m_viewShown = true;
	if (sdl3::Texture *tex = m_viewport.View())
		c.Image(*tex, frame, sdl3::Rgba(255, 255, 255, 150));
	c.PushClip(area);
	DrawTilemap(layer.tilemap, v); // calque de tuiles, sous les éléments
	for (const Item2D *i : layer.DrawOrder())
		if (i->visible)
			DrawItem2D(*i, v);
	c.StrokeRect(frame, th.accent, 1.f);
	c.Text(frame.x, frame.Bottom() + 2.f, std::format("{:.0f} × {:.0f}", layer.reference.x, layer.reference.y), th.textDim, 11.f);
	// Sélection : cadres et poignée.
	const Item2D *sel = layer.Find(m_item2D);
	if (!sel)
		m_item2D = 0;
	// Sélection multiple : cadres bleus sur les autres éléments.
	for (uint32_t id : m_items2D)
		if (const Item2D *i = layer.Find(id); i && i->id != m_item2D)
			c.StrokeRect(ScreenRect(BoundsOf(*i, Measure, &c), v), sdl3::Rgba(120, 200, 255), 1.5f);
	if (sel) {
		const Box2D box = BoundsOf(*sel, Measure, &c);
		c.StrokeRect(ScreenRect(box, v), sdl3::Rgba(255, 210, 80), 1.5f);
		const math::FVector2 h = v.ToScreen(HandleOf(*sel, box));
		c.FillRect({h.x - 5.f, h.y - 5.f, 10.f, 10.f}, sdl3::Rgba(255, 210, 80));
	}
	c.PopClip();
	// Souris : appui = sélection (poignée : redimensionner), glissé = déplacer, au pixel près
	// (Maj : pas de 10). Un seul pas d'annulation par glissé, créé au premier mouvement.
	const math::FVector2 mouse = v.ToReference({u.MouseX(), u.MouseY()});
	const bool over = area.Contains(u.MouseX(), u.MouseY()) && !u.MenuOpen();
	// Peinture de tuiles : outil « terrain » actif (m_paintTerrain > 0). Clic gauche pose le
	// terrain choisi, clic droit efface. Les cellules d'un même trait fusionnent en un seul
	// pas d'annulation (Change avec fusion).
	if (m_paintTerrain > 0 && !layer.tilemap.Empty() && over &&
		(u.MouseDown(sdl3::MouseButton::Left) || u.MouseDown(sdl3::MouseButton::Right))) {
		const TilemapLayer &tm = layer.tilemap;
		const math::FVector2 rel = mouse - tm.origin;
		const int cx = int(std::floor(rel.x / tm.tileSize));
		const int cy = int(std::floor(rel.y / tm.tileSize));
		const int8_t target = u.MouseDown(sdl3::MouseButton::Right) ? int8_t(0) : m_paintTerrain;
		if (cx >= 0 && cy >= 0 && cx < tm.width && cy < tm.height && tm.At(cx, cy) != target) {
			ed.Change("Peindre une tuile", true);
			layer.tilemap.Set(cx, cy, target);
		}
		return;
	}
	if (over && u.MousePressed()) {
		m_drag2D = Drag2D::None;
		m_guide2DX = m_guide2DY = -1e30f;
		if (sel) {
			const math::FVector2 h = v.ToScreen(HandleOf(*sel, BoundsOf(*sel, Measure, &c)));
			if (std::abs(u.MouseX() - h.x) <= 7.f && std::abs(u.MouseY() - h.y) <= 7.f)
				m_drag2D = Drag2D::Resize;
		}
		if (m_drag2D == Drag2D::None) {
			const uint32_t picked = Pick(layer, mouse, 4.f / v.scale, Measure, &c);
			if (picked && u.Ctrl()) { // Ctrl+clic : bascule dans la sélection multiple (sans glisser)
				if (!m_items2D.erase(picked))
					m_items2D.insert(picked);
				m_item2D = picked;
				return;
			}
			m_item2D = picked;
			m_items2D.clear();
			if (picked)
				m_items2D.insert(picked);
			m_drag2D = picked ? Drag2D::Move : Drag2D::None;
		}
		if (const Item2D *i = layer.Find(m_item2D)) {
			m_dragStart = *i;
			m_dragFrom = mouse;
			m_dragDirty = false;
		}
	}
	if (m_drag2D != Drag2D::None && u.MouseDown()) {
		math::FVector2 d = mouse - m_dragFrom;
		m_guide2DX = m_guide2DY = -1e30f;
		if (m_drag2D == Drag2D::Move) {
			if (u.Shift()) { // aimantation à la grille (pas configurable)
				const float step = std::max(1.f, m_overlaySnap);
				d = {std::round(d.x / step) * step, std::round(d.y / step) * step};
			} else { // guides d'alignement : bords et centres des autres éléments et de l'écran
				const Box2D base = BoundsOf(m_dragStart, Measure, &c);
				const Box2D box{base.min + d, base.max + d};
				const math::FVector2 pos = AlignSnap(layer, box, m_dragStart.position + d, m_item2D, 6.f / v.scale, m_guide2DX, m_guide2DY);
				d = pos - m_dragStart.position;
			}
		}
		Item2D next = m_dragStart;
		if (m_drag2D == Drag2D::Move)
			next.position = m_dragStart.position + d;
		else if (next.shape == Shape2D::Circle)
			next.size.x = next.size.y = std::max(4.f, m_dragStart.size.x + 2.f * std::max(d.x, d.y));
		else if (next.shape == Shape2D::Text)
			next.fontSize = std::max(6.f, m_dragStart.fontSize + d.y);
		else if (next.shape == Shape2D::Line)
			next.size = m_dragStart.size + d;
		else
			next.size = {std::max(4.f, m_dragStart.size.x + d.x), std::max(4.f, m_dragStart.size.y + d.y)};
		if (Item2D *i = layer.Find(m_item2D); i && !(next == *i)) {
			if (!m_dragDirty) {
				ed.Checkpoint(m_drag2D == Drag2D::Move ? "Déplacer (2D)" : "Redimensionner (2D)");
				m_dragDirty = true;
			}
			*ed.Doc().Overlay().Find(m_item2D) = next;
		}
	} else if (!u.MouseDown()) {
		m_drag2D = Drag2D::None;
		m_guide2DX = m_guide2DY = -1e30f;
	}
	// Guides d'alignement (pendant un déplacement).
	if (m_drag2D == Drag2D::Move && (m_guide2DX > -1e29f || m_guide2DY > -1e29f)) {
		c.PushClip(area);
		if (m_guide2DX > -1e29f) {
			const float sx = v.ToScreen({m_guide2DX, 0.f}).x;
			c.Line(sx, area.y, sx, area.Bottom(), sdl3::Rgba(255, 80, 180), 1.f);
		}
		if (m_guide2DY > -1e29f) {
			const float sy = v.ToScreen({0.f, m_guide2DY}).y;
			c.Line(area.x, sy, area.Right(), sy, sdl3::Rgba(255, 80, 180), 1.f);
		}
		c.PopClip();
	}
	HandleOverlayKeys();
}

void SandboxApp::HandleOverlayKeys() {
	ui::Context &u = Ui();
	if (u.WantsKeyboard() || u.MenuOpen() || !m_item2D)
		return;
	const sdl3::EventsManager &ev = Events();
	Layer2D &layer = m_editor.Doc().Overlay();
	if (ev.KeycodePressed(SDLK_DELETE) || ev.KeycodePressed(SDLK_BACKSPACE)) {
		m_editor.Checkpoint("Effacer (2D)");
		(void)m_editor.Doc().Overlay().Remove(m_item2D);
		m_items2D.erase(m_item2D);
		m_item2D = 0;
		return;
	}
	const float step = u.Shift() ? std::max(1.f, m_overlaySnap) : 1.f;
	math::FVector2 d{};
	if (ev.KeycodePressed(SDLK_LEFT))
		d.x -= step;
	if (ev.KeycodePressed(SDLK_RIGHT))
		d.x += step;
	if (ev.KeycodePressed(SDLK_UP))
		d.y -= step;
	if (ev.KeycodePressed(SDLK_DOWN))
		d.y += step;
	if ((d.x != 0.f || d.y != 0.f) && layer.Find(m_item2D)) {
		m_editor.Change("Déplacer (2D)", true);
		m_editor.Doc().Overlay().Find(m_item2D)->position += d;
	}
}

void SandboxApp::DrawOverlayTools(float &x, float y, float size) {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	using ui::icons::MaterialIcons;
	const auto button = [&](MaterialIcons glyph, std::string_view tip, bool enabled = true) {
		const sdl3::FRect r{x, y, size, size};
		x += size + 2.f;
		return u.IconButton(r, ui::icons::Codepoint(glyph), tip, enabled);
	};
	const auto iconButton = [&](std::string_view icon, std::string_view tip, bool enabled = true) {
		const sdl3::FRect r{x, y, size, size};
		x += size + 2.f;
		u.PushId(icon);
		const auto it = enabled ? u.Interact(u.MakeId("tool2d"), r) : ui::Context::Interaction{};
		u.PopId();
		if (it.hovered || it.held)
			c.FillRoundRect(r, th.radius, it.held ? th.widgetActive : th.widgetHover);
		const sdl3::Color tint = !enabled ? sdl3::WithAlpha(th.textDim, 90) : (it.hovered ? th.text : th.textDim);
		if (sdl3::Texture *tex = ToolIcon(icon)) {
			const float pad = 5.f;
			c.Image(*tex, {r.x + pad, r.y + pad, r.w - 2.f * pad, r.h - 2.f * pad}, tint);
		}
		if (it.hovered && !tip.empty())
			u.Tooltip(tip);
		return it.leftClicked;
	};
	static constexpr std::array<std::tuple<std::string_view, std::string_view, Shape2D>, 5> kShapes = {{
		{"rectangle", "Rectangle", Shape2D::Rect},
		{"circle", "Cercle", Shape2D::Circle},
		{"line", "Ligne", Shape2D::Line},
		{"text", "Texte", Shape2D::Text},
		{"image", "Image (fichier : onglet Entité ou Ressources)", Shape2D::Image},
	}};
	Layer2D &layer = m_editor.Doc().Overlay();
	for (const auto &[icon, tip, shape] : kShapes)
		if (iconButton(icon, std::format("Ajouter : {}", tip))) {
			m_editor.Checkpoint("Ajouter (2D)");
			const math::FVector2 at = layer.reference * 0.5f - math::FVector2{100.f, 30.f};
			m_item2D = m_editor.Doc().Overlay().Add(shape, at).id;
			ShowPanel("entity");
			ShowPanel("overlay");
		}
	x += 6.f;
	Item2D *sel = layer.Find(m_item2D);
	if (button(MaterialIcons::FLIP_TO_FRONT, "Premier plan (z + 1)", sel != nullptr) && sel) {
		m_editor.Checkpoint("Ordre (2D)");
		++m_editor.Doc().Overlay().Find(m_item2D)->z;
	}
	if (button(MaterialIcons::FLIP_TO_BACK, "Arrière-plan (z − 1)", sel != nullptr) && sel) {
		m_editor.Checkpoint("Ordre (2D)");
		--m_editor.Doc().Overlay().Find(m_item2D)->z;
	}
	if (button(MaterialIcons::DELETE, "Effacer l'élément (Suppr)", sel != nullptr) && sel) {
		m_editor.Checkpoint("Effacer (2D)");
		(void)m_editor.Doc().Overlay().Remove(m_item2D);
		m_items2D.erase(m_item2D);
		m_item2D = 0;
	}
}

// ── Alignement (sélection multiple) ──────────────────────────────────────────

void SandboxApp::AlignItems2D(int axis, int mode) {
	Layer2D &layer = m_editor.Doc().Overlay();
	sdl3::Canvas &c = Canvas();
	const auto coord = [&](const Item2D &i, int m) {
		const Box2D b = BoundsOf(i, Measure, &c);
		if (axis == 0)
			return m == 0 ? b.min.x : (m == 2 ? b.max.x : (b.min.x + b.max.x) * 0.5f);
		return m == 0 ? b.min.y : (m == 2 ? b.max.y : (b.min.y + b.max.y) * 0.5f);
	};
	std::vector<uint32_t> ids;
	for (uint32_t id : m_items2D)
		if (layer.Find(id))
			ids.push_back(id);
	if (ids.size() < 2)
		return;
	// Cible : bord bas = min des bords bas, centre = centre de l'union, bord haut = max des bords hauts.
	float lo = 1e30f, hi = -1e30f;
	for (uint32_t id : ids) {
		const Box2D b = BoundsOf(*layer.Find(id), Measure, &c);
		lo = std::min(lo, axis == 0 ? b.min.x : b.min.y);
		hi = std::max(hi, axis == 0 ? b.max.x : b.max.y);
	}
	const float target = mode == 0 ? lo : (mode == 2 ? hi : (lo + hi) * 0.5f);
	m_editor.Checkpoint("Aligner (2D)");
	for (uint32_t id : ids) {
		Item2D *it = layer.Find(id);
		const float delta = target - coord(*it, mode);
		if (axis == 0)
			it->position.x += delta;
		else
			it->position.y += delta;
	}
}

// ── Propriétés ───────────────────────────────────────────────────────────────

void SandboxApp::DrawItem2DPanel() {
	ui::Context &u = Ui();
	sdl3::Canvas &c = Canvas();
	const ui::Theme &th = u.Style();
	Layer2D &layer = m_editor.Doc().Overlay();
	// Liste des éléments (clic : sélection, Ctrl+clic : multi-sélection).
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::DASHBOARD), "Calque 2D");
	u.Wrapped("Affiché par-dessus la vue en mode Jeu. Scripts : hud.text / show / move / size / color (par nom), draw2d.*", th.textDim, 11.f);
	if (m_items2D.size() >= 2) {
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::ALIGN_HORIZONTAL_LEFT), std::format("Aligner ({} éléments)", m_items2D.size()));
		u.Row({1.f, 1.f, 1.f}, 28.f);
		if (u.Button("À gauche")) AlignItems2D(0, 0);
		if (u.Button("Centrer X")) AlignItems2D(0, 1);
		if (u.Button("À droite")) AlignItems2D(0, 2);
		u.Row({1.f, 1.f, 1.f}, 28.f);
		if (u.Button("En haut")) AlignItems2D(1, 0);
		if (u.Button("Centrer Y")) AlignItems2D(1, 1);
		if (u.Button("En bas")) AlignItems2D(1, 2);
	}
	static constexpr std::array<std::string_view, 5> kShapeNames = {"rectangle", "cercle", "ligne", "texte", "image"};
	for (const Item2D *i : layer.DrawOrder()) {
		const sdl3::FRect r = u.Next(26.f);
		u.PushId(uint64_t(i->id) + 2000);
		const auto it = u.Interact(u.MakeId("item2d"), r);
		u.PopId();
		const bool on = i->id == m_item2D;
		if (on || it.hovered)
			c.FillRoundRect(r, th.radius, on ? sdl3::WithAlpha(th.accent, 70) : th.widgetHover);
		c.Text(r.x + 8.f, r.y + 5.f, c.Ellipsize(i->name, r.w - 110.f, 13.f), i->visible ? th.text : th.textDim, 13.f);
		c.Text(r.Right() - 96.f, r.y + 6.f, std::format("{} · z {}", kShapeNames[size_t(i->shape)], i->z), th.textDim, 11.f);
		if (it.leftClicked) {
			if (u.Ctrl()) {
				if (!m_items2D.erase(i->id))
					m_items2D.insert(i->id);
			} else {
				m_items2D.clear();
				m_items2D.insert(i->id);
			}
			m_item2D = i->id;
		}
	}
	if (layer.items.empty())
		u.Wrapped("Aucun élément : ajoutez-en depuis la barre d'outils.", th.textDim, 12.f);
	Item2D *sel = layer.Find(m_item2D);
	if (!sel) {
		// ── Tuiles : calque de décor en damier, sous les éléments ──
		TilemapLayer &tm = layer.tilemap;
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::GRID_ON), "Tuiles");
		if (tm.Empty()) {
			u.Wrapped("Décor en damier, rendu sous les éléments. Créez une grille puis peignez les terrains.", th.textDim, 11.f);
			static int gridW = 16, gridH = 16;
			static float gridSize = 32.f;
			u.SliderInt("Largeur (tuiles)", gridW, 2, 256);
			u.SliderInt("Hauteur (tuiles)", gridH, 2, 256);
			u.SliderFloat("Taille de tuile", gridSize, 4.f, 256.f, "{:.0f} px");
			if (u.Button("Créer la grille de tuiles")) {
				m_editor.Checkpoint("Créer une grille de tuiles");
				tm.width = gridW;
				tm.height = gridH;
				tm.tileSize = gridSize;
				tm.origin = layer.reference * 0.5f - math::FVector2{float(gridW) * gridSize * 0.5f, float(gridH) * gridSize * 0.5f};
				tm.terrain.assign(size_t(gridW) * size_t(gridH), int8_t(0));
				tm.palette = {{0.f, 0.f, 0.f, 0.f},		 // 0 : transparent (vide)
							  {0.36f, 0.72f, 0.32f, 1.f}, // 1 : herbe
							  {0.62f, 0.45f, 0.28f, 1.f}, // 2 : terre
							  {0.55f, 0.55f, 0.58f, 1.f}, // 3 : pierre
							  {0.25f, 0.45f, 0.75f, 1.f}}; // 4 : eau
			}
		} else {
			u.Wrapped(std::format("Grille {} × {} · tuile {:.0f} px. Clic gauche : peindre, clic droit : effacer.", tm.width,
								  tm.height, tm.tileSize),
					  th.textDim, 11.f);
			// Palette : une pastille par terrain (clic : activer, re-clic : désactiver).
			constexpr float kCell = 34.f, kGap = 6.f;
			const sdl3::FRect head = u.Next(0);
			const int perRow = std::max(1, int((head.w + kGap) / (kCell + kGap)));
			const int count = int(tm.palette.size()) - 1; // ignore l'index 0 (vide)
			for (int i = 1; i <= count; i += perRow) {
				const sdl3::FRect line = u.Next(kCell + 8.f);
				for (int k = i; k <= count && k < i + perRow; ++k) {
					const sdl3::FRect cell{line.x + float(k - i) * (kCell + kGap), line.y, kCell, kCell};
					u.PushId(uint64_t(k) + 10000);
					const auto it = u.Interact(u.MakeId("terrain"), cell);
					u.PopId();
					const bool active = m_paintTerrain == int8_t(k);
					c.FillRoundRect(cell, 6.f, Ink(tm.palette[size_t(k)]));
					c.StrokeRoundRect(cell, 6.f, active ? th.accent : it.hovered ? th.text : th.panelBorder, active ? 3.f : 1.f);
					if (it.leftClicked)
						m_paintTerrain = active ? int8_t(0) : int8_t(k);
				}
			}
			u.Row({1.f, 1.f}, 30.f);
			if (u.Button("Effacer les tuiles peintes")) {
				m_editor.Checkpoint("Effacer les tuiles");
				std::fill(tm.terrain.begin(), tm.terrain.end(), int8_t(0));
			}
			if (u.Button("Supprimer la grille")) {
				m_editor.Checkpoint("Supprimer la grille de tuiles");
				tm = TilemapLayer{};
				m_paintTerrain = int8_t(0);
			}
		}
		u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), "Écran de référence");
		float ref[2] = {layer.reference.x, layer.reference.y};
		if (u.DragFloats("Résolution", ref, 1.f, "{:.0f}", 64.f, 7680.f)) {
			m_editor.Change("Écran de référence", true);
			m_editor.Doc().Overlay().reference = {ref[0], ref[1]};
		}
		u.SliderFloat("Pas d'aimantation (Maj)", m_overlaySnap, 1.f, 100.f, "{:.0f} px");
		return;
	}
	Item2D next = *sel;
	u.Section(ui::icons::Codepoint(ui::icons::MaterialIcons::TUNE), std::format("Élément « {} »", sel->name.c_str()));
	std::string name = next.name;
	if (u.TextInput("item2d-name", name, nullptr, "nom (pour hud.*)"))
		next.name = String(name);
	float pos[2] = {next.position.x, next.position.y};
	if (u.DragFloats("Position", pos, 1.f, "{:.0f}"))
		next.position = {pos[0], pos[1]};
	if (next.shape == Shape2D::Text) {
		std::string text = next.text;
		if (u.TextInput("item2d-text", text, nullptr, "texte"))
			next.text = String(text);
		u.SliderFloat("Taille", next.fontSize, 6.f, 160.f, "{:.0f}");
	} else {
		float size[2] = {next.size.x, next.size.y};
		if (u.DragFloats(next.shape == Shape2D::Line ? "Vecteur" : next.shape == Shape2D::Circle ? "Diamètre" : "Taille", size, 1.f, "{:.0f}"))
			next.size = next.shape == Shape2D::Circle ? math::FVector2{size[0], size[0]} : math::FVector2{size[0], size[1]};
	}
	if (next.shape == Shape2D::Image) {
		std::string file = next.image;
		if (u.TextInput("item2d-image", file, nullptr, "fichier image (relatif au document)"))
			next.image = String(file);
	}
	if (next.shape == Shape2D::Rect)
		u.SliderFloat("Arrondi", next.cornerRadius, 0.f, 60.f, "{:.0f}");
	if (next.shape != Shape2D::Line) {
		u.Label(next.shape == Shape2D::Image ? "Teinte" : "Remplissage", th.textDim, 12.f);
		sdl3::Color fill = Ink(next.fill);
		if (u.ColorPicker("item2d-fill", fill, 110.f))
			next.fill = FromInk(fill, next.fill.a);
		u.SliderFloat("Opacité", next.fill.a, 0.f, 1.f);
	}
	if (next.shape != Shape2D::Text && next.shape != Shape2D::Image) {
		u.Label("Contour", th.textDim, 12.f);
		sdl3::Color stroke = Ink(next.stroke);
		if (u.ColorPicker("item2d-stroke", stroke, 90.f))
			next.stroke = FromInk(stroke, std::max(next.stroke.a, 0.01f) < 0.02f ? 1.f : next.stroke.a);
		u.SliderFloat("Opacité du contour", next.stroke.a, 0.f, 1.f);
		u.SliderFloat("Épaisseur", next.strokeWidth, 0.f, 20.f, "{:.1f}");
	}
	u.SliderInt("Ordre (z)", next.z, -20, 20);
	u.Checkbox("Visible au départ", next.visible);
	if (!(next == *sel)) {
		m_editor.Change("Élément 2D", true);
		*m_editor.Doc().Overlay().Find(m_item2D) = std::move(next);
	}
}

} // namespace sandbox
