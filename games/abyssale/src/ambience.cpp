#include "abyssale/ambience.hpp"

#include <algorithm>
#include <cmath>

#include <math/noise.hpp>
#include <sdl3/render/scene3d.hpp>

namespace abyssale {

namespace {

using math::FMatrix4;
using math::FQuaternion;
using math::FVector3;
using math::FVector4;

float Smoothstep(float a, float b, float x) {
	x = std::clamp((x - a) / (b - a), 0.f, 1.f);
	return x * x * (3.f - 2.f * x);
}

/// Rotation amenant `from` sur `to` (vecteurs unitaires).
FQuaternion FromTo(FVector3 from, FVector3 to) {
	const FVector3 axis = from.Cross(to);
	const float s = axis.Length(), c = math::Dot(from, to);
	if (s < 1e-5f)
		return c > 0.f ? FQuaternion::Identity() : FQuaternion::AxisAngle({1.f, 0.f, 0.f}, 3.1415927f);
	return FQuaternion::AxisAngle(axis / s, std::atan2(s, c));
}

/// Hachage entier → [0, 1).
float Hash(int x, int z, int k) { return math::Hash01(x, 0, z * 31 + k, 0x5AF7); }

} // namespace

Ambience::Ambience(const Terrain &terrain, const Quality &q, uint64_t seed) : m_terrain(terrain) {
	math::Rng rng(seed);
	m_motes.resize(size_t(q.motes));
	m_moteSize.resize(m_motes.size());
	for (size_t i = 0; i < m_motes.size(); ++i) {
		m_motes[i] = {(rng.NextFloat() - 0.5f) * 44.f, (rng.NextFloat() - 0.5f) * 44.f, (rng.NextFloat() - 0.5f) * 44.f};
		m_moteSize[i] = 0.012f + 0.02f * rng.NextFloat() * rng.NextFloat();
	}
	// Reflets : points dispersés près du fond, recalculés autour de la caméra.
	m_glints.resize(size_t(std::max(0, q.glints)));
	for (size_t i = 0; i < m_glints.size(); ++i) {
		const float a = rng.NextFloat() * kTau, r = 8.f + 14.f * rng.NextFloat();
		m_glints[i] = {std::cos(a) * r, 0.4f + 1.8f * rng.NextFloat(), std::sin(a) * r};
	}
	m_shafts = q.shafts;
}

void Ambience::Update(float dt, float time, const FVector3 &cam, const FVector3 &sunDir) {
	// ── Particules en suspension (boîte enroulée autour de la caméra) ─────────
	motes.clear();
	constexpr float kHalf = 22.f;
	for (size_t i = 0; i < m_motes.size(); ++i) {
		FVector3 &p = m_motes[i];
		p += FVector3{std::sin(time * 0.2f + float(i)) * 0.05f, -0.03f, std::cos(time * 0.17f + float(i) * 1.3f) * 0.05f} * dt;
		FVector3 rel = p - cam;
		for (int k = 0; k < 3; ++k) {
			float &c = k == 0 ? rel.x : (k == 1 ? rel.y : rel.z);
			c = std::fmod(c + kHalf, 2.f * kHalf);
			if (c < 0.f)
				c += 2.f * kHalf;
			c -= kHalf;
		}
		p = cam + rel;
		const float dist = rel.Length();
		const float alpha = 0.6f * Smoothstep(kHalf, kHalf * 0.55f, dist) * Smoothstep(1.f, 3.f, dist);
		if (alpha <= 0.01f || p.y > kSurfaceY - 0.3f || p.y < m_terrain.Height(p.x, p.z))
			continue;
		motes.push_back({FMatrix4::Translation(p) * FMatrix4::Scale({m_moteSize[i], m_moteSize[i], m_moteSize[i]}), {0.75f, 0.95f, 1.f, alpha}});
	}

	// ── Reflets scintillants du varech (près du fond, dans les zones ouvertes) ─
	glints.clear();
	for (size_t i = 0; i < m_glints.size(); ++i) {
		FVector3 &p = m_glints[i];
		p.x += std::sin(time * 0.3f + float(i)) * 0.05f * dt;
		p.z += std::cos(time * 0.27f + float(i) * 1.3f) * 0.05f * dt;
		FVector3 rel = p - cam;
		rel.x = std::fmod(rel.x + 20.f, 40.f) - 20.f;
		rel.z = std::fmod(rel.z + 20.f, 40.f) - 20.f;
		p.x = cam.x + rel.x;
		p.z = cam.z + rel.z;
		const float floor = m_terrain.Height(p.x, p.z);
		const float open = m_terrain.Openness(p.x, p.z);
		p.y = floor + 0.4f + 2.2f * std::fmod(float(i) * 0.618f, 1.f);
		if (open < 0.35f || p.y > kSurfaceY - 0.4f)
			continue;
		const float ph = float(i) * 2.399f;
		const float twinkle = std::pow(0.5f + 0.5f * std::sin(time * (0.9f + 0.6f * std::fmod(ph, 1.f)) + ph), 6.f);
		const bool gold = i % 2 == 0;
		const float k = 0.25f + 0.9f * twinkle;
		const float size = 0.03f + 0.02f * std::fmod(ph * 3.7f, 1.f);
		glints.push_back({FMatrix4::Translation(p) * FMatrix4::Scale({size, size, size}),
						  gold ? FVector4{1.f * k, 0.82f * k, 0.35f * k, 1.f} : FVector4{0.45f * k, 1.f * k, 0.5f * k, 1.f}});
	}

	// ── Faisceaux de lumière (cellules du monde autour de la caméra) ──────────
	shafts.clear();
	constexpr float kCell = 15.f;
	const FVector3 down = sunDir.Normalized();
	const FQuaternion align = FromTo({0.f, -1.f, 0.f}, down);
	const int reach = 4;
	const int cx0 = int(std::floor(cam.x / kCell)), cz0 = int(std::floor(cam.z / kCell));
	for (int dz = -reach; dz <= reach; ++dz) {
		for (int dx = -reach; dx <= reach; ++dx) {
			const int cx = cx0 + dx, cz = cz0 + dz;
			if (Hash(cx, cz, 0) > float(m_shafts) / 81.f * 0.55f)
				continue;
			const float x = (float(cx) + Hash(cx, cz, 1)) * kCell, z = (float(cz) + Hash(cx, cz, 2)) * kCell;
			const FVector3 top{x, kSurfaceY - 0.3f, z};
			const float width = 4.f + 8.f * Hash(cx, cz, 3);
			const FVector3 rel = cam - top;
			const float along = math::Dot(rel, down);
			const float axisDist = (rel - down * along).Length();
			const float near = Smoothstep(width * 0.6f, width * 1.8f + 2.f, axisDist);
			const float flicker = 0.65f + 0.35f * std::sin(time * (0.25f + 0.2f * Hash(cx, cz, 4)) + Hash(cx, cz, 5) * kTau);
			const float k = 0.55f * near * flicker;
			if (k < 0.02f)
				continue;
			const FVector3 towardCam = rel - down * along;
			const FVector3 planeNormal = align.Rotate({0.f, 0.f, 1.f});
			float spinAngle = 0.f;
			if (towardCam.LengthSq() > 1e-4f) {
				const FVector3 tc = towardCam.Normalized();
				spinAngle = std::atan2(math::Dot(planeNormal.Cross(tc), down), math::Dot(planeNormal, tc));
			}
			const FQuaternion spin = FQuaternion::AxisAngle(down, spinAngle);
			const FMatrix4 model = sdl3::Transform{top, spin * align, {width, 85.f, width}}.Matrix();
			shafts.push_back({model, {k, k, k, 1.f}});
		}
	}
}

} // namespace abyssale
