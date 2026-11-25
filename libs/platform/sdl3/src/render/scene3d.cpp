#include "sdl3/render/scene3d.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sdl3 {

namespace {

float ToLinear(uint8_t c) noexcept {
	const float s = float(c) / 255.f;
	return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

math::FQuaternion Conjugate(const math::FQuaternion &q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }

} // namespace

LinearColor Srgb(uint8_t r, uint8_t g, uint8_t b) noexcept { return {ToLinear(r), ToLinear(g), ToLinear(b)}; }

LinearColor Srgb(uint32_t rgb) noexcept { return Srgb(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

math::FMatrix4 RotationMatrix(const math::FQuaternion &q) noexcept {
	const float x = q.x, y = q.y, z = q.z, w = q.w;
	math::FMatrix4 r = math::FMatrix4::Identity();
	r.m[0][0] = 1.f - 2.f * (y * y + z * z);
	r.m[0][1] = 2.f * (x * y + z * w);
	r.m[0][2] = 2.f * (x * z - y * w);
	r.m[1][0] = 2.f * (x * y - z * w);
	r.m[1][1] = 1.f - 2.f * (x * x + z * z);
	r.m[1][2] = 2.f * (y * z + x * w);
	r.m[2][0] = 2.f * (x * z + y * w);
	r.m[2][1] = 2.f * (y * z - x * w);
	r.m[2][2] = 1.f - 2.f * (x * x + y * y);
	return r;
}

math::FMatrix4 Transform::Matrix() const noexcept {
	math::FMatrix4 r = RotationMatrix(rotation);
	for (int c = 0; c < 3; ++c) {
		const float s = c == 0 ? scale.x : c == 1 ? scale.y : scale.z;
		for (int l = 0; l < 3; ++l)
			r.m[c][l] *= s;
	}
	r.m[3][0] = position.x;
	r.m[3][1] = position.y;
	r.m[3][2] = position.z;
	return r;
}

math::FVector3 Transform::InverseTransformDirection(const math::FVector3 &d) const noexcept {
	const math::FVector3 local = Conjugate(rotation).Rotate(d);
	return {local.x / scale.x, local.y / scale.y, local.z / scale.z};
}

math::FVector3 Transform::InverseTransformPoint(const math::FVector3 &p) const noexcept {
	return InverseTransformDirection(p - position);
}

std::optional<float> IntersectRayAabb(const math::FRay &ray, const math::FAABB &box) noexcept {
	float tMin = 0.f, tMax = 1e30f;
	const float o[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
	const float d[3] = {ray.dir.x, ray.dir.y, ray.dir.z};
	const float lo[3] = {box.min.x, box.min.y, box.min.z};
	const float hi[3] = {box.max.x, box.max.y, box.max.z};
	for (int i = 0; i < 3; ++i) {
		if (std::abs(d[i]) < 1e-12f) {
			if (o[i] < lo[i] || o[i] > hi[i])
				return std::nullopt;
			continue;
		}
		float t0 = (lo[i] - o[i]) / d[i], t1 = (hi[i] - o[i]) / d[i];
		if (t0 > t1)
			std::swap(t0, t1);
		tMin = std::max(tMin, t0);
		tMax = std::min(tMax, t1);
		if (tMin > tMax)
			return std::nullopt;
	}
	return tMin;
}

math::FRay Camera3D::ScreenRay(float u, float v, float aspect) const noexcept {
	const math::FVector3 forward = (target - position).Normalized();
	const math::FVector3 right = forward.Cross(up).Normalized();
	const math::FVector3 trueUp = right.Cross(forward);
	if (orthographic) { // rayons parallèles, origine dans le plan de la caméra
		const float x = (u * 2.f - 1.f) * orthoHeight * 0.5f * aspect, y = (1.f - v * 2.f) * orthoHeight * 0.5f;
		return {position + right * x + trueUp * y, forward};
	}
	const float h = std::tan(fovY * 0.5f);
	const float x = (u * 2.f - 1.f) * h * aspect, y = (1.f - v * 2.f) * h;
	return {position, (forward + right * x + trueUp * y).Normalized()};
}

void OrbitController::Rotate(float dx, float dy) noexcept {
	yaw -= dx * 0.008f;
	pitch = std::clamp(pitch + dy * 0.008f, -1.5f, 1.5f);
}

void OrbitController::Zoom(float factor) noexcept { distance = std::clamp(distance * factor, minDistance, maxDistance); }

void OrbitController::Pan(float dx, float dy) noexcept {
	const math::FVector3 forward = (target - Eye()).Normalized();
	const math::FVector3 right = forward.Cross({0.f, 1.f, 0.f}).Normalized();
	const math::FVector3 up = right.Cross(forward);
	const float k = distance * 0.0015f;
	target = target - right * (dx * k) + up * (dy * k);
}

math::FVector3 OrbitController::Eye() const noexcept {
	const float c = std::cos(pitch);
	return target + math::FVector3{c * std::sin(yaw), std::sin(pitch), c * std::cos(yaw)} * distance;
}

bool OrbitController::HandleMouse(float x, float y, bool left, bool pan, float wheel, bool over) noexcept {
	bool moved = false;
	if (m_drag == 0 && over && (left || pan)) {
		m_drag = left ? 1 : 2;
		m_lastX = x;
		m_lastY = y;
	}
	if (m_drag != 0) {
		if (!(m_drag == 1 ? left : pan)) {
			m_drag = 0;
		} else if (x != m_lastX || y != m_lastY) {
			if (m_drag == 1)
				Rotate(x - m_lastX, y - m_lastY);
			else
				Pan(x - m_lastX, y - m_lastY);
			m_lastX = x;
			m_lastY = y;
			moved = true;
		}
	}
	if (over && wheel != 0.f) {
		Zoom(std::pow(0.88f, wheel));
		moved = true;
	}
	return moved;
}

void OrbitController::Apply(Camera3D &camera) const noexcept {
	camera.position = Eye();
	camera.target = target;
	camera.up = {0.f, 1.f, 0.f};
}

Material Material::Plastic(const LinearColor &c, float roughness) noexcept {
	Material m;
	m.color = c;
	m.roughness = roughness;
	return m;
}

Material Material::Metal(const LinearColor &c, float roughness) noexcept { return Pbr(c, 1.f, roughness); }

Material Material::Pbr(const LinearColor &c, float metallic, float roughness) noexcept {
	Material m;
	m.color = c;
	m.metallic = metallic;
	m.roughness = roughness;
	return m;
}

Material Material::Unlit(const LinearColor &c) noexcept {
	Material m;
	m.color = c;
	m.unlit = true;
	m.castShadow = false;
	return m;
}

Material Material::Glass(const LinearColor &c, float opacity) noexcept {
	Material m;
	m.color = c;
	m.opacity = opacity;
	m.roughness = 0.05f;
	m.doubleSided = true;
	m.castShadow = false;
	return m;
}

Material Material::Emissive(const LinearColor &c, float strength) noexcept {
	Material m;
	m.color = c;
	m.emissive = c * strength;
	m.castShadow = false;
	return m;
}

math::FMatrix4 SunViewProjection(const DirectionalLight &sun) noexcept {
	const math::FVector3 dir = sun.direction.Normalized();
	const math::FVector3 up = std::abs(dir.y) > 0.99f ? math::FVector3{0.f, 0.f, 1.f} : math::FVector3{0.f, 1.f, 0.f};
	const float e = sun.shadowExtent;
	const math::FVector3 eye = sun.shadowCenter - dir * (e * 2.f);
	return math::FMatrix4::Ortho(-e, e, -e, e, 0.1f, e * 4.f) * math::FMatrix4::LookAt(eye, sun.shadowCenter, up);
}

namespace {

/// Suite de van der Corput en base `base` (0..1).
float Halton(uint32_t index, uint32_t base) noexcept {
	float f = 1.f, r = 0.f;
	while (index > 0) {
		f /= float(base);
		r += f * float(index % base);
		index /= base;
	}
	return r;
}

} // namespace

math::FVector2 TemporalJitter(uint32_t frame) noexcept {
	const uint32_t i = frame % 8u + 1u; // l'indice 0 donnerait (0, 0) deux fois par cycle
	return {Halton(i, 2u) - 0.5f, Halton(i, 3u) - 0.5f};
}

math::FMatrix4 JitterMatrix(const math::FVector2 &pixels, int width, int height) noexcept {
	math::FMatrix4 j = math::FMatrix4::Identity();
	if (width > 0 && height > 0) {
		// Un pixel couvre 2 / largeur en NDC ; la translation multiplie w (perspective comprise).
		j.m[3][0] = 2.f * pixels.x / float(width);
		j.m[3][1] = 2.f * pixels.y / float(height);
	}
	return j;
}

} // namespace sdl3
