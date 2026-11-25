// Voxelia — éditeur du monde en direct (phase 5). Cf. world_editor.hpp.
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "world_editor.hpp"

namespace voxelia {

namespace icons = ui::icons;
using Rect = sdl3::FRect;
using sdl3::Rgba;
using math::FVector3;

namespace {
constexpr float kPi = std::numbers::pi_v<float>;
constexpr sdl3::Color kAxis[3] = {sdl3::Color{226, 72, 72, 255}, sdl3::Color{86, 196, 92, 255}, sdl3::Color{76, 140, 236, 255}};

void Box(std::vector<DebugLine> &out, const FVector3 &lo, const FVector3 &hi, sdl3::Color c, float w = 1.5f) {
	const FVector3 p[8] = {{lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, lo.y, hi.z}, {lo.x, lo.y, hi.z},
						   {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}};
	for (int i = 0; i < 4; ++i) {
		out.push_back({p[i], p[(i + 1) % 4], c, w});
		out.push_back({p[i + 4], p[(i + 1) % 4 + 4], c, w});
		out.push_back({p[i], p[i + 4], c, w});
	}
}

void Circle(std::vector<DebugLine> &out, const FVector3 &center, float radius, int plane, sdl3::Color c, int segments = 40) {
	FVector3 prev{};
	for (int k = 0; k <= segments; ++k) {
		const float a = float(k) / float(segments) * 2.f * kPi;
		const float u = std::cos(a) * radius, v = std::sin(a) * radius;
		const FVector3 p = center + (plane == 0 ? FVector3{0, u, v} : plane == 1 ? FVector3{u, 0, v} : FVector3{u, v, 0});
		if (k > 0)
			out.push_back({prev, p, c, 1.f});
		prev = p;
	}
}

float DistToSegment(float px, float py, float ax, float ay, float bx, float by) {
	const float vx = bx - ax, vy = by - ay;
	const float l2 = vx * vx + vy * vy;
	const float t = l2 > 0 ? std::clamp(((px - ax) * vx + (py - ay) * vy) / l2, 0.f, 1.f) : 0.f;
	return std::hypot(ax + vx * t - px, ay + vy * t - py);
}
} // namespace

// ── Superpositions de débogage ──────────────────────────────────────────────

void CollectDebugLines(Game &game, const DebugView &view, const FVector3 &eye, ecs::Entity selected, std::vector<DebugLine> &out) {
	out.clear();
	ecs::World &w = game.Ecs();
	const float r2 = view.range * view.range;
	if (view.boxes || view.velocity)
		w.MakeQuery<const voxel::Transform, const voxel::Body>().Each([&](ecs::Entity e, const voxel::Transform &t, const voxel::Body &b) {
			if ((t.position - eye).LengthSq() > r2)
				return;
			const bool sel = e == selected;
			if (view.boxes) {
				const sdl3::Color col = sel ? Rgba(255, 210, 90) : e == game.Player() ? Rgba(86, 156, 255) : Rgba(120, 230, 140, 200);
				Box(out, t.position - b.halfExtents, t.position + b.halfExtents, col, sel ? 2.5f : 1.2f);
				// Cap de l'entité (petite flèche au sommet).
				const FVector3 top = t.position + FVector3{0, b.halfExtents.y, 0};
				out.push_back({top, top + FVector3{std::sin(t.yaw), 0, -std::cos(t.yaw)} * 0.6f, col, 1.5f});
			}
			if (view.velocity)
				if (const auto *v = w.Get<voxel::Velocity>(e); v && v->value.LengthSq() > 0.01f)
					out.push_back({t.position, t.position + v->value * 0.5f, Rgba(255, 120, 220), 2.f}); // 0,5 s de trajet
		});
	if (view.chunks) { // bordures des chunks autour de l'œil
		const math::IVec3 c = voxel::ChunkOfPosition(eye);
		for (int dy = -1; dy <= 1; ++dy)
			for (int dz = -2; dz <= 2; ++dz)
				for (int dx = -2; dx <= 2; ++dx) {
					const voxel::Chunk *ch = game.World().GetChunk(voxel::MakeChunkKey(c.x + dx, c.y + dy, c.z + dz));
					if (!ch || ch->IsEmpty())
						continue;
					const FVector3 lo = (c + math::IVec3{dx, dy, dz}).ToFloat() * float(voxel::kChunkSize);
					Box(out, lo + FVector3(0.02f), lo + FVector3(float(voxel::kChunkSize) - 0.02f),
						ch->Modified() ? Rgba(236, 160, 60, 160) : Rgba(90, 200, 255, 110), dx == 0 && dy == 0 && dz == 0 ? 2.f : 1.f);
				}
	}
	if (view.lights) // portée et rayons des lumières dynamiques (lancés jusqu'au premier bloc)
		w.MakeQuery<const voxel::LightEmitter, const voxel::Transform>().Each([&](const voxel::LightEmitter &l, const voxel::Transform &t) {
			const FVector3 p = t.position + voxel::RotateYaw(l.offset, t.yaw);
			if ((p - eye).LengthSq() > r2 || !l.enabled)
				return;
			const sdl3::Color col{uint8_t(std::min(1.f, l.color.x) * 255), uint8_t(std::min(1.f, l.color.y) * 255), uint8_t(std::min(1.f, l.color.z) * 255),
								 200};
			for (int plane = 0; plane < 3; ++plane)
				Circle(out, p, l.range, plane, sdl3::WithAlpha(col, 90));
			for (int k = 0; k < 14; ++k) { // directions réparties sur la sphère
				const float phi = std::acos(1.f - 2.f * (float(k) + 0.5f) / 14.f), theta = kPi * (1.f + std::sqrt(5.f)) * float(k);
				const FVector3 d{std::cos(theta) * std::sin(phi), std::cos(phi), std::sin(theta) * std::sin(phi)};
				const auto hit = game.World().Raycast(p, d, l.range);
				out.push_back({p, hit ? p + d * hit->distance : p + d * l.range, sdl3::WithAlpha(col, hit ? 220 : 110), 1.f});
			}
		});
	if (view.joints)
		w.MakeQuery<const voxel::Joints, const voxel::Transform>().Each([&](const voxel::Joints &js, const voxel::Transform &ta) {
			if ((ta.position - eye).LengthSq() > r2)
				return;
			for (const voxel::Joint &j : js.list) {
				const ecs::Entity b = voxel::FindByUid(w, j.target);
				const auto *tb = b ? w.Get<voxel::Transform>(b) : nullptr;
				const FVector3 anchor = ta.position + voxel::RotateYaw(j.anchor, ta.yaw);
				Box(out, anchor - FVector3(0.08f), anchor + FVector3(0.08f), Rgba(255, 200, 80), 1.5f);
				if (tb)
					out.push_back({anchor, tb->position, Rgba(255, 200, 80, 200), 1.5f});
			}
		});
}

// ── Projection ──────────────────────────────────────────────────────────────

Projector Projector::From(const voxel::Camera &camera, const Rect &rect) {
	Projector p;
	p.rect = rect;
	p.camera = camera;
	p.forward = camera.Forward();
	p.right = FVector3{std::cos(camera.yaw), 0, std::sin(camera.yaw)};
	p.up = p.right.Cross(p.forward);
	p.tanF = std::tan(camera.fovY * 0.5f);
	p.aspect = rect.h > 0 ? rect.w / rect.h : 1.f;
	return p;
}

bool Projector::Point(const FVector3 &pt, float &x, float &y) const {
	const FVector3 d = pt - camera.position;
	const float z = d.Dot(forward);
	if (z < 0.05f)
		return false;
	const float nx = d.Dot(right) / (z * tanF * aspect), ny = d.Dot(up) / (z * tanF);
	x = rect.x + (nx + 1.f) * 0.5f * rect.w;
	y = rect.y + (1.f - ny) * 0.5f * rect.h;
	return true;
}

bool Projector::Segment(FVector3 a, FVector3 b, float &x0, float &y0, float &x1, float &y1) const {
	constexpr float kNear = 0.06f;
	const float da = (a - camera.position).Dot(forward), db = (b - camera.position).Dot(forward);
	if (da < kNear && db < kNear)
		return false;
	if (da < kNear)
		a = a + (b - a) * ((kNear - da) / (db - da));
	else if (db < kNear)
		b = b + (a - b) * ((kNear - db) / (da - db));
	return Point(a, x0, y0) && Point(b, x1, y1);
}

FVector3 Projector::Ray(float x, float y) const {
	const float nx = (x - rect.x) / rect.w * 2.f - 1.f, ny = 1.f - (y - rect.y) / rect.h * 2.f;
	return (forward + right * (nx * tanF * aspect) + up * (ny * tanF)).Normalized();
}

void DrawDebugLines(sdl3::Canvas &c, const Projector &proj, std::span<const DebugLine> lines) {
	c.PushClip(proj.rect);
	for (const DebugLine &l : lines) {
		float x0, y0, x1, y1;
		if (proj.Segment(l.a, l.b, x0, y0, x1, y1))
			c.Line(x0, y0, x1, y1, l.color, l.width);
	}
	c.PopClip();
}

ecs::Entity PickEntity(Game &g, const FVector3 &o, const FVector3 &d, ecs::Entity skip) {
	ecs::Entity best;
	float bestT = 1e30f;
	g.Ecs().MakeQuery<const voxel::Transform, const voxel::Body>().Each([&](ecs::Entity e, const voxel::Transform &t, const voxel::Body &b) {
		if (e == skip || (t.position - o).LengthSq() > 160.f * 160.f)
			return;
		const FVector3 mn = t.position - b.halfExtents, mx = t.position + b.halfExtents;
		float tmin = 0.f, tmax = 1e30f;
		for (int a = 0; a < 3; ++a) {
			if (std::fabs(d[a]) < 1e-8f) {
				if (o[a] < mn[a] || o[a] > mx[a])
					return;
				continue;
			}
			float t1 = (mn[a] - o[a]) / d[a], t2 = (mx[a] - o[a]) / d[a];
			if (t1 > t2)
				std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return;
		}
		if (tmin < bestT) {
			bestT = tmin;
			best = e;
		}
	});
	// Un bloc plus proche masque l'entité.
	if (best)
		if (const auto hit = g.World().Raycast(o, d, bestT); hit && hit->distance < bestT - 0.05f)
			return {};
	return best;
}

// ── Gizmo ───────────────────────────────────────────────────────────────────

float TransformGizmo::Length(const Projector &proj, const FVector3 &p) const {
	const float z = std::max(0.1f, (p - proj.camera.position).Dot(proj.forward));
	return 90.f * z * proj.tanF * 2.f / proj.rect.h; // ≈ 90 px à l'écran
}

bool TransformGizmo::Interact(ui::Context &u, const Projector &proj, FVector3 &position, float &yaw) {
	float cx, cy;
	if (!proj.Point(position, cx, cy)) {
		m_hover = -1;
		return false;
	}
	const float L = Length(proj, position);
	m_hover = -1;
	if (m_axis < 0 && proj.rect.Contains(u.MouseX(), u.MouseY())) {
		if (mode == Mode::Translate) {
			for (int a = 0; a < 3; ++a) {
				FVector3 dir{};
				dir[a] = 1.f;
				float tx, ty;
				if (proj.Point(position + dir * L, tx, ty) && DistToSegment(u.MouseX(), u.MouseY(), cx, cy, tx, ty) < 9.f)
					m_hover = a;
			}
		} else {
			float best = 1e9f, px = 0, py = 0;
			bool havePrev = false;
			for (int k = 0; k <= 48; ++k) { // distance au cercle de cap projeté
				const float ang = float(k) / 48.f * 2.f * kPi;
				float sx, sy;
				if (!proj.Point(position + FVector3{std::cos(ang), 0, std::sin(ang)} * (L * 0.85f), sx, sy)) {
					havePrev = false;
					continue;
				}
				if (havePrev)
					best = std::min(best, DistToSegment(u.MouseX(), u.MouseY(), px, py, sx, sy));
				px = sx;
				py = sy;
				havePrev = true;
			}
			if (best < 9.f)
				m_hover = 3;
		}
	}
	if (u.MousePressed(sdl3::MouseButton::Left) && m_hover >= 0) {
		m_axis = m_hover;
		m_startX = u.MouseX();
		m_startY = u.MouseY();
		m_startPos = position;
		m_startYaw = yaw;
		m_startAngle = std::atan2(u.MouseY() - cy, u.MouseX() - cx);
		if (m_axis < 3) {
			FVector3 dir{};
			dir[m_axis] = 1.f;
			float tx, ty;
			proj.Point(position + dir * L, tx, ty);
			m_axisScreen = math::FVector2{tx - cx, ty - cy} * (1.f / L);
		}
	}
	if (m_axis >= 0) {
		if (!u.MouseDown(sdl3::MouseButton::Left)) {
			m_axis = -1;
			return true;
		}
		if (m_axis < 3) { // translation le long d'un axe : déplacement écran projeté sur l'axe
			const float l2 = m_axisScreen.Dot(m_axisScreen);
			if (l2 > 1e-6f) {
				float delta = ((u.MouseX() - m_startX) * m_axisScreen.x + (u.MouseY() - m_startY) * m_axisScreen.y) / l2;
				if (snap && !u.Ctrl())
					delta = std::round(delta * 16.f) / 16.f;
				position = m_startPos;
				position[m_axis] += delta;
			}
		} else { // cap : angle de la souris autour du centre
			const float angle = std::atan2(u.MouseY() - cy, u.MouseX() - cx);
			float d = angle - m_startAngle;
			if (proj.forward.y > 0)
				d = -d; // vu d'en dessous
			if (snap && !u.Ctrl())
				d = std::round(d / (kPi / 12.f)) * (kPi / 12.f);
			yaw = m_startYaw + d;
		}
		return true;
	}
	return m_hover >= 0;
}

void TransformGizmo::Draw(sdl3::Canvas &c, const Projector &proj, const FVector3 &position, float yaw) const {
	float cx, cy;
	if (!proj.Point(position, cx, cy))
		return;
	const float L = Length(proj, position);
	c.PushClip(proj.rect);
	if (mode == Mode::Translate) {
		for (int a = 0; a < 3; ++a) {
			FVector3 dir{};
			dir[a] = 1.f;
			float tx, ty;
			if (!proj.Point(position + dir * L, tx, ty))
				continue;
			const bool hot = a == m_axis || (m_axis < 0 && a == m_hover);
			const sdl3::Color col = hot ? Rgba(255, 230, 120) : kAxis[a];
			c.Line(cx, cy, tx, ty, col, hot ? 4.f : 3.f);
			// Pointe de flèche : éventail de segments vers la pointe (triangle plein).
			const float dx = tx - cx, dy = ty - cy, len = std::max(1.f, std::hypot(dx, dy));
			const float ux = dx / len, uy = dy / len;
			for (int k = -6; k <= 6; ++k)
				c.Line(tx + uy * float(k), ty - ux * float(k), tx + ux * 13, ty + uy * 13, col, 2.f);
		}
	} else {
		const bool hot = m_axis == 3 || (m_axis < 0 && m_hover == 3);
		std::vector<float> pts;
		for (int k = 0; k <= 48; ++k) {
			const float ang = float(k) / 48.f * 2.f * kPi;
			float sx, sy;
			if (proj.Point(position + FVector3{std::cos(ang), 0, std::sin(ang)} * (L * 0.85f), sx, sy)) {
				pts.push_back(sx);
				pts.push_back(sy);
			}
		}
		if (pts.size() >= 4)
			c.Polyline(pts.data(), pts.size() / 2, hot ? Rgba(255, 230, 120) : kAxis[1], hot ? 4.f : 3.f);
		float hx, hy; // cap actuel
		if (proj.Point(position + FVector3{std::sin(yaw), 0, -std::cos(yaw)} * (L * 0.85f), hx, hy)) {
			c.Line(cx, cy, hx, hy, Rgba(255, 230, 120), 2.f);
			c.FillCircle(hx, hy, 5, Rgba(255, 230, 120));
		}
	}
	c.FillCircle(cx, cy, 4, Rgba(240, 240, 240));
	c.PopClip();
}

// ── Mode éditeur de la fenêtre du jeu ──────────────────────────────────────

void InGameEditor::Toggle(Game &game) {
	active = !active;
	if (active)
		camera = game.Camera(); // la caméra libre part du regard du joueur
	game.SetCameraOverride(active ? std::optional<voxel::Camera>(camera) : std::nullopt);
}

void InGameEditor::Update(Game &game, const PlayerInput &in, float dt, bool fast, float wheel) {
	if (!active)
		return;
	if (wheel != 0)
		speed = std::clamp(speed * std::pow(1.25f, wheel), 1.f, 200.f);
	camera.yaw += in.lookDX * 0.0025f;
	camera.pitch = std::clamp(camera.pitch - in.lookDY * 0.0025f, -1.55f, 1.55f);
	const FVector3 f = camera.Forward();
	const FVector3 r{std::cos(camera.yaw), 0, std::sin(camera.yaw)};
	FVector3 wish = f * in.moveZ + r * in.moveX + FVector3{0, 1, 0} * ((in.jump ? 1.f : 0.f) - (in.descend ? 1.f : 0.f));
	if (wish.LengthSq() > 1.f)
		wish = wish.Normalized();
	camera.position = camera.position + wish * (speed * (fast ? 3.f : 1.f) * dt);
	game.SetCameraOverride(camera);
}

void InGameEditor::Draw(ui::Context &u, Game &game) {
	if (!active)
		return;
	sdl3::Canvas &c = u.Canvas();
	const ui::Theme &th = u.Style();
	const Rect screen{0, 0, c.Width(), c.Height()};
	const Projector proj = Projector::From(camera, screen);
	ecs::World &w = game.Ecs();
	if (selected && !w.IsAlive(selected))
		selected = {};
	// Superpositions.
	std::vector<DebugLine> lines;
	CollectDebugLines(game, view, camera.position, selected, lines);
	if (block) { // contour du bloc sélectionné
		const FVector3 lo = block->block.ToFloat();
		Box(lines, lo - FVector3(0.01f), lo + FVector3(1.01f), Rgba(255, 255, 255, 220), 2.f);
	}
	DrawDebugLines(c, proj, lines);

	// Panneau des options (haut gauche).
	const Rect panel{12, 12, 310, 334};
	const bool overPanel = panel.Contains(u.MouseX(), u.MouseY());
	// Gizmo sur l'entité sélectionnée.
	bool gizmoTook = false;
	if (selected && !overPanel)
		if (auto *t = w.Get<voxel::Transform>(selected)) {
			FVector3 pos = t->position;
			float yaw = t->yaw;
			gizmoTook = gizmo.Interact(u, proj, pos, yaw);
			if (gizmo.Dragging()) {
				t->position = pos;
				t->yaw = yaw;
				if (auto *v = w.Get<voxel::Velocity>(selected))
					v->value = {};
			}
		}
	if (selected)
		if (const auto *t = w.Get<voxel::Transform>(selected))
			gizmo.Draw(c, proj, t->position, t->yaw);
	// Sélection au clic (hors gizmo et panneau) : entité, sinon bloc.
	if (u.MousePressed(sdl3::MouseButton::Left) && !gizmoTook && !overPanel) {
		const FVector3 d = proj.Ray(u.MouseX(), u.MouseY());
		selected = PickEntity(game, camera.position, d);
		block = selected ? std::nullopt : game.World().Raycast(camera.position, d, 128.f);
	}

	c.FillRoundRect(panel, 8, Rgba(16, 18, 24, 225));
	c.StrokeRoundRect(panel, 8, th.panelBorder);
	u.BeginColumn(panel.Inset(10, 8));
	u.Label("Éditeur du monde (F4)", th.text, 15);
	u.Label(std::format("caméra libre · {:.0f} m/s (molette) · clic droit : regarder", speed), th.textDim, 12);
	u.Row({1, 1}, 26);
	if (u.ButtonAt(u.Next(), "Déplacer (G)", true, gizmo.mode == TransformGizmo::Mode::Translate))
		gizmo.mode = TransformGizmo::Mode::Translate;
	if (u.ButtonAt(u.Next(), "Tourner (R)", true, gizmo.mode == TransformGizmo::Mode::Rotate))
		gizmo.mode = TransformGizmo::Mode::Rotate;
	u.Checkbox("Boîtes AABB", view.boxes);
	u.Checkbox("Vecteurs vitesse", view.velocity);
	u.Checkbox("Bordures des chunks", view.chunks);
	u.Checkbox("Lumières (portée, rayons)", view.lights);
	u.Checkbox("Joints", view.joints);
	std::string info = "clic : sélectionner une entité ou un bloc";
	if (selected) {
		const auto *k = w.Get<voxel::Kind>(selected);
		const auto *t = w.Get<voxel::Transform>(selected);
		info = std::format("{} #{} · {:.2f} {:.2f} {:.2f}", k ? k->id : "entité", selected.index, t ? t->position.x : 0.f, t ? t->position.y : 0.f,
						   t ? t->position.z : 0.f);
	} else if (block) {
		info = std::format("bloc {} · {} {} {}", game.GetContent().blocks.Get(block->id).name, block->block.x, block->block.y, block->block.z);
	}
	u.TextIn(u.Next(18), info, {th.accentHover, 12, sdl3::CanvasFont::Regular, ui::TextOverflow::MarqueeOnHover});
	u.Row({1, 1}, 26);
	if (u.ButtonAt(u.Next(), "Joueur ici", true))
		game.Teleport(camera.position - FVector3{0, 0.72f, 0});
	if (u.ButtonAt(u.Next(), "Quitter (F4)", true))
		Toggle(game);
	u.EndColumn();
}

} // namespace voxelia
