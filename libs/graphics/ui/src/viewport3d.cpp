#include "ui/viewport3d.hpp"

#include <algorithm>
#include <cmath>

#include <core/log.hpp>

#include "ui/ui.hpp"

namespace ui {

namespace {
/// Intersection rayon / AABB (méthode des plaques) ; distance d'entrée, -1 sinon.
float RayAabb(const math::FVector3 &o, const math::FVector3 &d, const math::FVector3 &mn, const math::FVector3 &mx) {
	float tmin = 0.f, tmax = 1e30f;
	for (int a = 0; a < 3; ++a) {
		const float oa = o[a], da = d[a], mna = mn[a], mxa = mx[a];
		if (std::fabs(da) < 1e-8f) {
			if (oa < mna || oa > mxa)
				return -1.f;
		} else {
			float t1 = (mna - oa) / da, t2 = (mxa - oa) / da;
			if (t1 > t2)
				std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return -1.f;
		}
	}
	return tmin;
}
} // namespace

Viewport3D::Viewport3D() {
	m_camera.yaw = 0.6f;
	m_camera.pitch = -0.55f;
	m_camera.zFar = 2000.f;
	SyncCamera();
}

void Viewport3D::SyncCamera() noexcept {
	const math::FVector3 f = math::DirectionFromYawPitch(m_camera.yaw, m_camera.pitch);
	m_camera.position = m_target - f * m_distance;
}

void Viewport3D::Resize(sdl3::GpuDevice &dev, sdl3::Renderer &ren2d, int w, int h, SDL_GPUTextureFormat depthFormat) {
	if (w <= 0 || h <= 0)
		return;
	if (w == m_width && h == m_height && m_color && m_view)
		return;
	if (!dev.TextureSupportsFormat(depthFormat, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
		depthFormat = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	(void)dev.WaitIdle();
	m_view.reset();
	m_color = {};
	m_depth = {};

	SDL_GPUTextureCreateInfo ci{};
	ci.type = SDL_GPU_TEXTURETYPE_2D;
	ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ci.width = static_cast<uint32_t>(w);
	ci.height = static_cast<uint32_t>(h);
	ci.layer_count_or_depth = 1;
	ci.num_levels = 1;
	auto color = dev.CreateTexture(ci);
	ci.format = depthFormat;
	ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
	auto depth = dev.CreateTexture(ci);
	if (!color || !depth) {
		core::log::Error("viewport3d : cibles {}x{} : {}", w, h, color ? depth.Error() : color.Error());
		return;
	}
	m_color = std::move(color.Value());
	m_depth = std::move(depth.Value());
	auto view = sdl3::Texture::FromGpuTexture(ren2d, m_color.Get(), w, h, sdl3::PixelFormat::RGBA32);
	if (view)
		m_view.emplace(std::move(view.Value()));
	m_width = w;
	m_height = h;
}

void Viewport3D::Render(sdl3::GpuDevice &dev, voxel::Renderer &renderer, const voxel::SceneParams &params,
						std::span<const voxel::BoxInstance> entityBoxes, const voxel::BoxInstance *outline) {
	if (!Ready())
		return;
	SyncCamera();
	// Boîtes : entités + gizmo (si une sélection est active).
	m_boxes.clear();
	m_boxes.insert(m_boxes.end(), entityBoxes.begin(), entityBoxes.end());
	if (m_hasSelection)
		BuildGizmo(m_boxes);

	sdl3::GpuCommandBuffer cmd = dev.AcquireCommandBuffer();
	if (!cmd)
		return;
	const float aspect = m_height > 0 ? float(m_width) / float(m_height) : 1.f;
	renderer.RenderPass(cmd, m_color.Get(), m_depth.Get(), aspect, m_camera, params, m_boxes, outline);
	(void)cmd.Submit();
}

void Viewport3D::BuildGizmo(std::vector<voxel::BoxInstance> &out) const noexcept {
	const float len = GizmoScale();
	const float t = len * 0.08f;
	const float hl = len * 0.5f;
	const math::FVector3 p = m_selPos;
	out.push_back({{p.x + hl, p.y, p.z}, {hl, t, t}, 230, 70, 70, 255});	// X
	out.push_back({{p.x, p.y + hl, p.z}, {t, hl, t}, 96, 200, 96, 255});	// Y
	out.push_back({{p.x, p.y, p.z + hl}, {t, t, hl}, 90, 130, 240, 255}); // Z
}

int Viewport3D::PickGizmoAxis(const math::FVector3 &o, const math::FVector3 &d) const noexcept {
	const float len = GizmoScale();
	const float t = len * 0.08f;
	const math::FVector3 p = m_selPos;
	const math::FAABB bars[3] = {
		{{p.x, p.y - t, p.z - t}, {p.x + len, p.y + t, p.z + t}},
		{{p.x - t, p.y, p.z - t}, {p.x + t, p.y + len, p.z + t}},
		{{p.x - t, p.y - t, p.z}, {p.x + t, p.y + t, p.z + len}},
	};
	int best = -1;
	float bestT = 1e30f;
	for (int i = 0; i < 3; ++i) {
		const float hit = RayAabb(o, d, bars[i].min, bars[i].max);
		if (hit >= 0.f && hit < bestT) {
			bestT = hit;
			best = i;
		}
	}
	return best;
}

float Viewport3D::AxisParam(const math::FVector3 &o, const math::FVector3 &d, int axis) const noexcept {
	const math::FVector3 dir(axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f);
	const math::FVector3 w0 = m_selPos - o;
	const float a = d.Dot(d), b = d.Dot(dir), dd = d.Dot(w0), e = dir.Dot(w0);
	const float denom = a - b * b; // c = dir·dir = 1
	if (std::fabs(denom) < 1e-8f)
		return 0.f;
	return (a * e - b * dd) / denom;
}

math::FVector3 Viewport3D::RayDirection(float mouseX, float mouseY, const sdl3::FRect &area) const noexcept {
	const float ndcX = (mouseX - area.x) / area.w * 2.f - 1.f;
	const float ndcY = 1.f - (mouseY - area.y) / area.h * 2.f;
	const float tanF = std::tan(m_camera.fovY * 0.5f);
	const float aspect = area.h > 0 ? area.w / area.h : 1.f;
	const math::FVector3 f = m_camera.Forward();
	const math::FVector3 right{std::cos(m_camera.yaw), 0, std::sin(m_camera.yaw)};
	const math::FVector3 up = right.Cross(f);
	return (f + right * (ndcX * tanF * aspect) + up * (ndcY * tanF)).Normalized();
}

void Viewport3D::Draw(Context &ctx, const sdl3::FRect &area) {
	sdl3::Canvas &c = ctx.Canvas();
	c.FillRect(area, sdl3::Rgba(16, 18, 24, 255));
	if (Ready())
		c.Image(*m_view, area);
	else
		c.TextCentered(area, "Viewport (chargement…)", ctx.Style().textDim, 14);

	const bool inside = area.Contains(ctx.MouseX(), ctx.MouseY());
	if (!m_captured && inside && (ctx.MousePressed(sdl3::MouseButton::Left) || ctx.MousePressed(sdl3::MouseButton::Right) || ctx.MousePressed(sdl3::MouseButton::Middle)))
		m_captured = true;
	if (!ctx.MouseDown(sdl3::MouseButton::Left) && !ctx.MouseDown(sdl3::MouseButton::Right) && !ctx.MouseDown(sdl3::MouseButton::Middle))
		m_captured = false;

	const float dx = ctx.MouseX() - m_lastX, dy = ctx.MouseY() - m_lastY;

	// ── Glissement du gizmo (prioritaire) ────────────────────────────────
	if (m_dragAxis >= 0) {
		if (ctx.MouseDown(sdl3::MouseButton::Left)) {
			const math::FVector3 o = RayOrigin();
			const math::FVector3 d = RayDirection(ctx.MouseX(), ctx.MouseY(), area);
			if (m_dragAxis == 3) {
				m_selYaw = m_dragStartYaw + (ctx.MouseX() - m_dragStartMouseX) * 0.01f;
			} else {
				const float p = AxisParam(o, d, m_dragAxis);
				const math::FVector3 dir(m_dragAxis == 0 ? 1.f : 0.f, m_dragAxis == 1 ? 1.f : 0.f, m_dragAxis == 2 ? 1.f : 0.f);
				m_selPos = m_dragStartPos + dir * (p - m_dragStartParam);
			}
		} else {
			m_dragAxis = -1;
		}
	}

	// ── Clic gauche : gizmo ou picking ────────────────────────────────────
	if (inside && ctx.MousePressed(sdl3::MouseButton::Left)) {
		const math::FVector3 o = RayOrigin();
		const math::FVector3 d = RayDirection(ctx.MouseX(), ctx.MouseY(), area);
		if (m_hasSelection && m_gizmo == Gizmo::Translate) {
			const int axis = PickGizmoAxis(o, d);
			if (axis >= 0) {
				m_dragAxis = axis;
				m_dragStartPos = m_selPos;
				m_dragStartParam = AxisParam(o, d, axis);
			} else {
				m_pickRequested = true;
			}
		} else if (m_hasSelection && m_gizmo == Gizmo::Rotate) {
			m_dragAxis = 3;
			m_dragStartYaw = m_selYaw;
			m_dragStartMouseX = ctx.MouseX();
		} else {
			m_pickRequested = true;
		}
	}

	// ── Caméra orbitale (hors glissement du gizmo) ───────────────────────
	if (m_captured && m_dragAxis < 0) {
		const bool orbit = ctx.MouseDown(sdl3::MouseButton::Left);
		const bool pan = ctx.MouseDown(sdl3::MouseButton::Right) || (ctx.MouseDown(sdl3::MouseButton::Left) && ctx.Shift());
		if (orbit && !pan) {
			m_camera.yaw += dx * 0.008f;
			m_camera.pitch = std::clamp(m_camera.pitch - dy * 0.008f, -1.55f, 1.55f);
		} else if (pan) {
			const math::FVector3 f = m_camera.Forward();
			const math::FVector3 right{std::cos(m_camera.yaw), 0, std::sin(m_camera.yaw)};
			const math::FVector3 up = right.Cross(f);
			const float s = m_distance * 0.0015f;
			m_target -= right * (dx * s);
			m_target += up * (dy * s);
		}
	}
	if (inside && ctx.Events().WheelY() != 0)
		m_distance = std::clamp(m_distance * std::exp(-ctx.Events().WheelY() * 0.12f), 2.f, 400.f);
	m_lastX = ctx.MouseX();
	m_lastY = ctx.MouseY();
}

} // namespace ui
