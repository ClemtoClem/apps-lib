#include "abyssale/world.hpp"

#include <algorithm>
#include <cmath>

#include <core/hash.hpp>
#include <math/noise.hpp>
#include "abyssale/meshes.hpp"
#include <sdl3/render/scene3d.hpp>

namespace abyssale {

namespace {

using math::FMatrix4;
using math::FQuaternion;
using math::FVector2;
using math::FVector3;
using math::FVector4;

float Smoothstep(float a, float b, float x) {
	x = std::clamp((x - a) / (b - a), 0.f, 1.f);
	return x * x * (3.f - 2.f * x);
}

/// Proximité (1 au centre, 0 au-delà de `scale` × rayon) d'un lieu.
float Near(const Landmark &l, float x, float z, float scale = 1.f) {
	return Smoothstep(l.radius * scale, l.radius * scale * 0.35f, std::hypot(x - l.center.x, z - l.center.y));
}

FMatrix4 Place(FVector3 p, float yaw, float tiltX, float tiltZ, FVector3 scale) {
	return sdl3::Transform{p, FQuaternion::FromEuler(tiltX, yaw, tiltZ), scale}.Matrix();
}

/// Tirage par rejet : point du disque de nage dont la densité `density(x, z)` l'accepte.
template <typename Density>
bool Pick(math::Rng &rng, Density density, FVector2 center, float &x, float &z, int tries = 40) {
	for (int i = 0; i < tries; ++i) {
		const float a = rng.NextFloat() * kTau, r = std::sqrt(rng.NextFloat()) * (kHalfExtent - 6.f);
		x = center.x + std::cos(a) * r;
		z = center.y + std::sin(a) * r;
		if (rng.NextFloat() < density(x, z))
			return true;
	}
	return false;
}

/// Pose un objet sur une surface : son axe +Y suit `normal`, tourné de `yaw` autour.
FMatrix4 PlaceOn(FVector3 p, FVector3 normal, float yaw, float scale) {
	const FVector3 up{0.f, 1.f, 0.f}, n = normal.Normalized();
	const FVector3 axis = up.Cross(n);
	const float sinAngle = axis.Length();
	FQuaternion q = FQuaternion::AxisAngle(up, yaw);
	if (sinAngle > 1e-5f)
		q = FQuaternion::AxisAngle(axis / sinAngle, std::atan2(sinAngle, up.Dot(n))) * q;
	return sdl3::Transform{p, q, {scale, scale, scale}}.Matrix();
}

FVector4 Jitter(math::Rng &rng, FVector3 c, float amount) {
	const float k = 1.f + (rng.NextFloat() - 0.5f) * amount;
	return {c.x * k * (1.f + (rng.NextFloat() - 0.5f) * amount * 0.5f), c.y * k, c.z * k * (1.f + (rng.NextFloat() - 0.5f) * amount * 0.5f), 1.f};
}

/// Densité de végétation d'un template (0 : roche nue, 1 : jardin luxuriant).
float Lushness(ChunkKind k) noexcept {
	switch (k) {
	case ChunkKind::Entry: return 1.f;
	case ChunkKind::KelpForest: return 1.05f;
	case ChunkKind::CoralBarrier: return 0.85f;
	case ChunkKind::Shipwreck: return 0.6f;
	case ChunkKind::StatueGarden: return 0.55f;
	case ChunkKind::Hydrothermal: return 0.5f;
	case ChunkKind::WhaleGraveyard: return 0.45f;
	case ChunkKind::RayGathering: return 0.22f;
	case ChunkKind::BrineLake: return 0.12f;
	default: return 0.12f;
	}
}

} // namespace

float YawTowards(const FVector3 &from, const FVector3 &to) noexcept { return std::atan2(to.x - from.x, -(to.z - from.z)); }

World::World(const Terrain &t, const Quality &q, uint64_t seed, int32_t cx, int32_t cz, ChunkKind kind) : m_kind(kind) {
	const FVector2 origin{float(cx) * kChunkSize, float(cz) * kChunkSize};
	for (const Landmark &l : LandmarksFor(kind))
		m_landmarks.push_back({l.center + origin, l.radius});
	math::Rng rng(core::Mix64(seed ^ MakeChunkKey(cx, cz)));
	using namespace landmarks;
	const auto floorAt = [&](float x, float z) { return FVector3{x, t.Height(x, z), z}; };
	const auto flatFloor = [&](float x, float z) { return t.Openness(x, z) > 0.65f && t.Slope(x, z) < 0.25f; };
	const auto lowestFloor = [&](float x, float z, float r) {
		float y = t.Height(x, z);
		for (int k = 0; k < 8; ++k) {
			const float a = float(k) * (kTau / 8.f);
			y = std::min(y, t.Height(x + std::cos(a) * r, z + std::sin(a) * r));
		}
		return FVector3{x, y, z};
	};
	const auto nearLm = [&](size_t i, float x, float z, float scale) { return i < m_landmarks.size() ? Near(m_landmarks[i], x, z, scale) : 0.f; };
	const float lush = Lushness(kind);

	// ── Varech : colonnes denses dans les cuvettes, bosquets ailleurs ─────────
	const auto kelpDensity = [&](float x, float z) {
		if (!flatFloor(x, z))
			return 0.f;
		float d = 0.06f * lush;
		for (size_t i = 0; i < m_landmarks.size(); ++i)
			d = std::max(d, nearLm(i, x, z, 1.15f) * (0.9f - 0.1f * float(i)));
		return d;
	};
	for (int i = 0; i < int(float(q.kelp) * lush); ++i) {
		float x, z;
		if (!Pick(rng, kelpDensity, origin, x, z))
			continue;
		const FVector3 p = floorAt(x, z) + FVector3{0.f, -0.3f, 0.f};
		const size_t v = size_t(rng.NextInt(0, 2));
		const float room = kSurfaceY - 1.f - 5.f * rng.NextFloat() - p.y;
		const float s = std::clamp(room / kKelpHeights[v], 0.45f, 1.25f);
		kelp[v].push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s, s, s}), Jitter(rng, {0.95f, 1.f, 0.9f}, 0.35f)});
		colliders.push_back({p + FVector3{0.f, 2.f, 0.f}, 0.9f});
		for (int g = 0; g < 4; ++g) {
			const float h = (0.25f + 0.7f * rng.NextFloat()) * kKelpHeights[v] * s;
			const float a = rng.NextFloat() * kTau, r = 0.4f + 1.4f * rng.NextFloat();
			glintAnchors.push_back(p + FVector3{std::cos(a) * r, h, std::sin(a) * r});
		}
	}

	// ── Algues irisées ─────────────────────────────────────────────────────────
	const auto frondDensity = [&](float x, float z) {
		if (t.Openness(x, z) < 0.5f)
			return 0.f;
		float d = 0.02f * lush;
		for (size_t i = 0; i < m_landmarks.size(); ++i)
			d = std::max(d, nearLm(i, x, z, 1.6f) * 0.5f);
		return d;
	};
	const std::array<FVector3, 3> frondColors{FVector3{0.62f, 0.38f, 0.95f}, FVector3{0.95f, 0.82f, 0.32f}, FVector3{0.45f, 0.85f, 0.42f}};
	for (int i = 0; i < int(float(q.fronds) * lush); ++i) {
		float x, z;
		if (!Pick(rng, frondDensity, origin, x, z))
			continue;
		const size_t v = size_t(rng.NextInt(0, 1));
		const float s = 0.6f + 0.8f * rng.NextFloat();
		const FVector3 c = frondColors[size_t(rng.NextInt(0, 2))];
		fronds[v].push_back({Place(floorAt(x, z), rng.NextFloat() * kTau, 0.f, 0.f, {s, s, s}), Jitter(rng, c, 0.3f)});
	}

	// ── Tapis magenta ──────────────────────────────────────────────────────────
	const auto tuftDensity = [&](float x, float z) {
		if (!flatFloor(x, z))
			return 0.f;
		float d = t.Cover(x, z);
		for (size_t i = 0; i < m_landmarks.size(); ++i)
			d = std::max(d, nearLm(i, x, z, 1.3f) * 0.5f);
		return d;
	};
	const std::array<FVector3, 3> tuftColors{FVector3{1.f, 0.18f, 0.72f}, FVector3{0.74f, 0.24f, 0.98f}, FVector3{1.f, 0.38f, 0.6f}};
	for (int i = 0; i < int(float(q.tufts) * lush); ++i) {
		float x, z;
		if (!Pick(rng, tuftDensity, origin, x, z, 12))
			continue;
		const FVector3 n = t.Normal(x, z);
		const float s = 0.7f + 0.7f * rng.NextFloat();
		tufts.push_back({Place(floorAt(x, z), rng.NextFloat() * kTau, n.z * 0.8f, -n.x * 0.8f, {s, s, s}),
						 Jitter(rng, tuftColors[size_t(rng.NextInt(0, 2))], 0.35f)});
	}

	// ── Blocs : éboulis au pied des falaises, surplombs au bord du plateau ────
	for (int i = 0; i < int(float(q.rocks) * std::max(0.4f, lush)); ++i) {
		float x, z;
		const bool overhang = i % 4 == 0;
		const auto density = [&](float px, float pz) {
			const float o = t.Openness(px, pz);
			return overhang ? Smoothstep(0.15f, 0.35f, o) * Smoothstep(0.65f, 0.45f, o) : Smoothstep(0.35f, 0.6f, o) * Smoothstep(1.f, 0.75f, o) + 0.08f;
		};
		if (!Pick(rng, density, origin, x, z))
			continue;
		const size_t v = overhang ? 2 : size_t(rng.NextInt(0, 1));
		const float yaw = rng.NextFloat() * kTau;
		if (overhang) {
			const float s = 6.f + 6.f * rng.NextFloat();
			const FVector3 p = floorAt(x, z) + FVector3{0.f, s * 0.15f, 0.f};
			rocks[v].push_back({Place(p, yaw, (rng.NextFloat() - 0.5f) * 0.25f, (rng.NextFloat() - 0.5f) * 0.25f, {s * 1.3f, s * 0.7f, s}), {0.85f, 0.9f, 0.92f, 1.f}});
			colliders.push_back({p, s * 0.95f});
		} else {
			const float s = 0.7f + 3.6f * rng.NextFloat() * rng.NextFloat();
			const FVector3 p = lowestFloor(x, z, s * 0.9f) + FVector3{0.f, s * 0.12f, 0.f};
			rocks[v].push_back({Place(p, yaw, (rng.NextFloat() - 0.5f) * 0.4f, (rng.NextFloat() - 0.5f) * 0.4f, {s, s * (0.55f + 0.4f * rng.NextFloat()), s}), {0.92f, 0.95f, 0.95f, 1.f}});
			colliders.push_back({p, s * 0.8f});
		}
	}
	// Arche rocheuse.
	const auto arch = [&](FVector2 c, float yaw, float scale) {
		const FVector3 p = floorAt(c.x, c.y) + FVector3{0.f, -1.f, 0.f};
		arches.push_back({Place(p, yaw, 0.f, 0.f, {scale, scale, scale}), {0.9f, 0.92f, 0.95f, 1.f}});
		const FMatrix4 m = arches.back().model;
		for (const float a : {0.2f, 0.9f, 1.57f, 2.25f, 2.95f})
			colliders.push_back({m.TransformPoint({std::cos(a) * 9.f, std::sin(a) * 7.6f, 0.f}), 2.6f * scale});
	};

	// ── Éponges et coraux ─────────────────────────────────────────────────────
	const auto gardenDensity = [&](float x, float z) {
		if (!flatFloor(x, z))
			return 0.f;
		float d = 0.05f * lush;
		for (size_t i = 0; i < m_landmarks.size(); ++i)
			d = std::max(d, nearLm(i, x, z, 1.3f) * 0.6f);
		return d;
	};
	const std::array<FVector3, 4> spongeColors{FVector3{0.46f, 0.42f, 0.64f}, FVector3{0.64f, 0.52f, 0.34f}, FVector3{0.7f, 0.64f, 0.5f}, FVector3{0.54f, 0.4f, 0.54f}};
	for (int i = 0; i < int(float(q.sponges) * lush); ++i) {
		float x, z;
		if (!Pick(rng, gardenDensity, origin, x, z))
			continue;
		const FVector3 p = floorAt(x, z) + FVector3{0.f, -0.1f, 0.f};
		const FVector4 tint = Jitter(rng, spongeColors[size_t(rng.NextInt(0, 3))], 0.25f);
		if (i % 3 != 2) {
			const float s = 0.6f + 1.2f * rng.NextFloat();
			barrels.push_back({Place(p, rng.NextFloat() * kTau, (rng.NextFloat() - 0.5f) * 0.2f, (rng.NextFloat() - 0.5f) * 0.2f, {s, s * (0.8f + 0.4f * rng.NextFloat()), s}), tint});
			colliders.push_back({p + FVector3{0.f, s * 0.6f, 0.f}, s * 0.75f});
		} else {
			const float s = 0.7f + 0.8f * rng.NextFloat();
			tubes.push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s, s, s}), tint});
			colliders.push_back({p + FVector3{0.f, s * 0.6f, 0.f}, s * 0.6f});
		}
	}
	const std::array<FVector3, 4> coralColors{FVector3{0.95f, 0.52f, 0.62f}, FVector3{0.92f, 0.86f, 0.84f}, FVector3{0.66f, 0.42f, 0.9f}, FVector3{0.95f, 0.66f, 0.4f}};
	for (int i = 0; i < int(float(q.corals) * lush); ++i) {
		float x, z;
		if (!Pick(rng, gardenDensity, origin, x, z))
			continue;
		const FVector3 p = floorAt(x, z);
		const FVector4 tint = Jitter(rng, coralColors[size_t(rng.NextInt(0, 3))], 0.2f);
		const float s = 0.6f + 1.2f * rng.NextFloat();
		if (i % 4 == 3)
			fans.push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s * 1.6f, s * 1.6f, s * 1.6f}), tint});
		else
			corals.push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s, s, s}), tint});
		colliders.push_back({p + FVector3{0.f, s * 0.6f, 0.f}, s * 0.7f});
	}

	// ── Pièces maîtresses par template ────────────────────────────────────────
	switch (m_kind) {
	case ChunkKind::Entry: {
		const FVector2 c = m_landmarks[2].center;
		const FVector3 p = floorAt(c.x, c.y) + FVector3{0.f, 4.2f, 0.f};
		wreck = Place(p - FVector3{0.f, 1.4f, 0.f}, 0.7f, 0.35f, -0.28f, {1.3f, 1.3f, 1.3f}) * FMatrix4::Rotation({0.f, 0.f, 1.f}, 0.22f);
		for (const float x : {-11.f, -4.f, 3.f, 10.f})
			colliders.push_back({wreck.TransformPoint({x, -2.5f, 0.f}), 5.6f});
		const FVector2 fc = m_landmarks[4].center;
		fluke = Place(floorAt(fc.x, fc.y), std::atan2(-4.f, 18.f), -0.42f, 0.08f, {1.15f, 1.15f, 1.15f});
		colliders.push_back({fluke.TransformPoint({0.f, 1.5f, 0.f}), 2.4f});
		for (const float s : {-1.f, 1.f})
			for (const float u : {0.25f, 0.6f, 0.9f})
				colliders.push_back({fluke.TransformPoint({s * 7.4f * u, 5.f + 4.f * u, 0.f}), 1.9f * (1.2f - u)});
		arch(m_landmarks[1].center + FVector2{-6.f, 8.f}, 0.9f, 1.f);
		arch(m_landmarks[5].center + FVector2{14.f, -6.f}, 2.2f, 0.8f);
		arch(m_landmarks[2].center + FVector2{14.f, -12.f}, -0.6f, 0.9f);
		break;
	}
	case ChunkKind::Shipwreck: {
		const FVector2 c = m_landmarks[0].center;
		const FVector3 p = floorAt(c.x, c.y) + FVector3{0.f, 6.f, 0.f};
		wreck = Place(p - FVector3{0.f, 2.f, 0.f}, 1.1f, 0.3f, -0.22f, {1.8f, 1.8f, 1.8f}) * FMatrix4::Rotation({0.f, 0.f, 1.f}, 0.18f);
		for (const float x : {-16.f, -6.f, 4.f, 14.f})
			colliders.push_back({wreck.TransformPoint({x, -3.f, 0.f}), 7.5f});
		break;
	}
	case ChunkKind::Hydrothermal: {
		const std::array<FVector3, 3> tints{FVector3{0.5f, 0.46f, 0.42f}, FVector3{0.55f, 0.5f, 0.44f}, FVector3{0.46f, 0.44f, 0.46f}};
		for (int i = 0; i < 9; ++i) {
			const Landmark &lm = m_landmarks[size_t(i) % m_landmarks.size()];
			const float a = rng.NextFloat() * kTau, r = rng.NextFloat() * lm.radius * 0.7f;
			const float x = lm.center.x + std::cos(a) * r, z = lm.center.y + std::sin(a) * r;
			const float s = 0.8f + 1.4f * rng.NextFloat();
			const FVector3 p = floorAt(x, z);
			chimneys.push_back({Place(p - FVector3{0.f, 0.2f, 0.f}, rng.NextFloat() * kTau, (rng.NextFloat() - 0.5f) * 0.12f, (rng.NextFloat() - 0.5f) * 0.12f, {s, s, s}),
								{FVector4{tints[size_t(i) % 3], 1.f}}});
			colliders.push_back({p + FVector3{0.f, 1.6f * s, 0.f}, 0.8f * s});
			vents.push_back(p + FVector3{0.f, 2.5f * s, 0.f});
		}
		break;
	}
	case ChunkKind::WhaleGraveyard: {
		// Carcasse : cage thoracique d'arches, crâne de blocs.
		for (int i = 0; i < 6; ++i)
			arch({m_landmarks[0].center.x + float(i - 2) * 8.f, m_landmarks[0].center.y - 6.f + 3.f * std::sin(float(i) * 0.9f)}, 0.f, 0.55f + 0.1f * float(i % 2));
		const FVector3 skull = floorAt(m_landmarks[0].center.x + 18.f, m_landmarks[0].center.y);
		rocks[0].push_back({Place(skull, 0.4f, 0.f, 0.f, {4.f, 3.f, 4.f}), {0.9f, 0.88f, 0.82f, 1.f}});
		colliders.push_back({skull + FVector3{0.f, 1.5f, 0.f}, 3.5f});
		break;
	}
	case ChunkKind::StatueGarden: {
		for (int i = 0; i < 11; ++i) {
			const Landmark &lm = m_landmarks[size_t(i) % m_landmarks.size()];
			const float a = rng.NextFloat() * kTau, r = rng.NextFloat() * lm.radius * 0.8f;
			const float x = lm.center.x + std::cos(a) * r, z = lm.center.y + std::sin(a) * r;
			const bool fallen = rng.NextFloat() < 0.35f;
			const float s = 0.8f + 1.2f * rng.NextFloat();
			const FVector3 p = floorAt(x, z);
			FMatrix4 m = Place(p, rng.NextFloat() * kTau, fallen ? (rng.NextFloat() - 0.5f) * 1.2f : 0.f, fallen ? (rng.NextFloat() - 0.5f) * 1.2f : 0.f, {s, s, s});
			statues.push_back({m, {0.82f, 0.78f, 0.68f, 1.f}});
			colliders.push_back({p + FVector3{0.f, 1.5f * s, 0.f}, 0.7f * s});
		}
		break;
	}
	case ChunkKind::CoralBarrier: {
		// Mur de coraux traversant le chunk.
		for (float bx = -kHalfExtent + 20.f; bx < kHalfExtent - 20.f; bx += 7.f) {
			const float x = m_landmarks[0].center.x + bx, z = m_landmarks[0].center.y + (rng.NextFloat() - 0.5f) * 10.f;
			const FVector3 p = floorAt(x, z);
			const float s = 1.2f + 1.6f * rng.NextFloat();
			switch (int(std::abs(bx)) % 3) {
			case 0: corals.push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s, s * 1.4f, s}), {0.95f, 0.55f, 0.62f, 1.f}}); break;
			case 1: fans.push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s, s, s}), {0.92f, 0.86f, 0.84f, 1.f}}); break;
			default: barrels.push_back({Place(p, rng.NextFloat() * kTau, 0.f, 0.f, {s * 0.8f, s, s * 0.8f}), {0.64f, 0.52f, 0.34f, 1.f}}); break;
			}
			colliders.push_back({p + FVector3{0.f, s * 0.7f, 0.f}, s * 0.8f});
		}
		break;
	}
	case ChunkKind::BrineLake: {
		// Rebord de roches sombres autour de la cuvette.
		const Landmark &lm = m_landmarks[0];
		for (int i = 0; i < 24; ++i) {
			const float a = float(i) * (kTau / 24.f), r = lm.radius * 0.85f;
			const float x = lm.center.x + std::cos(a) * r, z = lm.center.y + std::sin(a) * r;
			const float s = 1.f + 2.f * rng.NextFloat();
			rocks[0].push_back({Place(floorAt(x, z), rng.NextFloat() * kTau, 0.f, 0.f, {s, s * 0.7f, s}), {0.42f, 0.44f, 0.48f, 1.f}});
		}
		break;
	}
	default: break;
	}

	// ── Sources de bulles ──────────────────────────────────────────────────────
	const int nVents = m_kind == ChunkKind::Hydrothermal ? 10 : 6;
	for (int i = 0; i < nVents; ++i) {
		float x, z;
		if (Pick(rng, [&](float px, float pz) { return flatFloor(px, pz) ? 1.f : 0.f; }, origin, x, z))
			vents.push_back(floorAt(x, z));
	}

	// ── Petite faune fixée : herbiers, coraux, pennatules, serpules, balanes ──
	math::Rng brng(core::Mix64(seed ^ MakeChunkKey(cx, cz) ^ 0xBE27405ull));
	const auto meadowDensity = [&](float x, float z) {
		if (!flatFloor(x, z))
			return 0.f;
		float d = 0.03f * lush;
		for (size_t i = 0; i < m_landmarks.size(); ++i)
			d = std::max(d, nearLm(i, x, z, 1.5f) * 0.5f);
		return d;
	};
	const std::array<FVector3, 3> grassColors{FVector3{0.55f, 0.8f, 0.38f}, FVector3{0.72f, 0.78f, 0.36f}, FVector3{0.42f, 0.7f, 0.46f}};
	for (int placed = 0; placed < int(float(q.seagrass) * lush);) {
		float x, z;
		if (!Pick(brng, meadowDensity, origin, x, z)) {
			++placed;
			continue;
		}
		const FVector3 color = grassColors[size_t(brng.NextInt(0, 2))];
		const int patch = brng.NextInt(3, 6);
		for (int k = 0; k < patch && placed < int(float(q.seagrass) * lush); ++k, ++placed) {
			const float a = brng.NextFloat() * kTau, r = 0.4f + 1.4f * std::sqrt(brng.NextFloat());
			const float px = x + std::cos(a) * r, pz = z + std::sin(a) * r;
			const float s = 0.8f + 0.5f * brng.NextFloat();
			seagrass[size_t(brng.NextInt(0, 2))].push_back(
				{Place(floorAt(px, pz) - FVector3{0.f, 0.05f, 0.f}, brng.NextFloat() * kTau, 0.f, 0.f, {s, s, s}), Jitter(brng, color, 0.25f)});
		}
	}
	const std::array<FVector3, 3> whipColors{FVector3{0.95f, 0.55f, 0.3f}, FVector3{0.75f, 0.42f, 0.9f}, FVector3{0.95f, 0.82f, 0.35f}};
	const std::array<FVector3, 2> mushroomColors{FVector3{0.9f, 0.76f, 0.56f}, FVector3{0.78f, 0.58f, 0.82f}};
	const std::array<FVector3, 2> penColors{FVector3{0.96f, 0.86f, 0.62f}, FVector3{0.95f, 0.56f, 0.42f}};
	const std::array<FVector3, 3> wormColors{FVector3{0.92f, 0.9f, 0.84f}, FVector3{0.95f, 0.62f, 0.5f}, FVector3{0.62f, 0.78f, 0.95f}};
	for (int i = 0; i < int(float(q.benthos) * lush); ++i) {
		float x = 0.f, z = 0.f;
		const float yaw = brng.NextFloat() * kTau;
		switch (i % 5) {
		case 0:
			if (Pick(brng, gardenDensity, origin, x, z)) {
				const float s = 0.8f + 0.8f * brng.NextFloat();
				seaWhips.push_back({Place(floorAt(x, z) - FVector3{0.f, 0.05f, 0.f}, yaw, 0.f, 0.f, {s, s, s}), Jitter(brng, whipColors[size_t(brng.NextInt(0, 2))], 0.2f)});
			}
			break;
		case 1:
			if (Pick(brng, gardenDensity, origin, x, z)) {
				const float s = 0.6f + 0.6f * brng.NextFloat();
				mushroomCorals.push_back({Place(floorAt(x, z) - FVector3{0.f, 0.03f, 0.f}, yaw, 0.f, 0.f, {s, s, s}), Jitter(brng, mushroomColors[size_t(brng.NextInt(0, 1))], 0.2f)});
			}
			break;
		case 2:
			if (Pick(brng, meadowDensity, origin, x, z)) {
				const float s = 0.7f + 0.5f * brng.NextFloat();
				seaPens.push_back({Place(floorAt(x, z) - FVector3{0.f, 0.08f, 0.f}, yaw, 0.f, 0.f, {s, s, s}), Jitter(brng, penColors[size_t(brng.NextInt(0, 1))], 0.2f)});
			}
			break;
		case 3: {
			const FVector4 tint = Jitter(brng, wormColors[size_t(brng.NextInt(0, 2))], 0.2f);
			const float s = 1.f + 0.6f * brng.NextFloat();
			if (Pick(brng, gardenDensity, origin, x, z))
				tubeWorms.push_back({Place(floorAt(x, z) - FVector3{0.f, 0.03f, 0.f}, yaw, 0.f, 0.f, {s, s, s}), tint});
			break;
		}
		default:
			// Balanes : nécessitent des blocs ; sans bloc assez grand, on les colle au sol.
			if (Pick(brng, gardenDensity, origin, x, z))
				barnacles.push_back({PlaceOn(floorAt(x, z), FVector3{0.f, 1.f, 0.f}, yaw, 1.6f + 1.2f * brng.NextFloat()), {0.86f, 0.83f, 0.76f, 1.f}});
			break;
		}
	}

	// ── Points de vue et survol (carte d'entrée seulement) ────────────────────
	if (m_kind == ChunkKind::Entry) {
		const auto ground = [&](FVector2 c, float h) { return floorAt(c.x, c.y) + FVector3{0.f, h, 0.f}; };
		const auto at = [&](FVector2 c, FVector2 off, float h) { return floorAt(c.x + off.x, c.y + off.y) + FVector3{0.f, h, 0.f}; };
		tour = {
			{at(m_landmarks[0].center, {30.f, 14.f}, 10.f), at(m_landmarks[0].center, {0.f, 0.f}, 16.f)},
			{at(m_landmarks[0].center, {20.f, -16.f}, 12.f), at(m_landmarks[0].center, {-6.f, -4.f}, 18.f)},
			{at(m_landmarks[1].center, {-26.f, 18.f}, 8.f), at(m_landmarks[1].center, {0.f, 0.f}, 4.f)},
			{at(m_landmarks[1].center, {-8.f, 10.f}, 6.f), at(m_landmarks[3].center, {0.f, 0.f}, 3.f)},
			{at(m_landmarks[3].center, {-14.f, -10.f}, 5.f), at(m_landmarks[3].center, {4.f, 4.f}, 1.f)},
			{at(m_landmarks[5].center, {14.f, -10.f}, 8.f), at(m_landmarks[5].center, {-4.f, 4.f}, 6.f)},
			{at(m_landmarks[2].center, {22.f, -8.f}, 9.f), at(m_landmarks[2].center, {0.f, 0.f}, 5.f)},
			{at(m_landmarks[4].center, {-6.f, 24.f}, 7.f), at(m_landmarks[4].center, {0.f, 0.f}, 7.f)},
		};
		const auto nearTour = [&](float x, float z) {
			for (size_t i = 0; i + 1 < tour.size(); ++i) {
				const FVector2 a2{tour[i].pos.x, tour[i].pos.z}, b2{tour[i + 1].pos.x, tour[i + 1].pos.z}, p2{x, z};
				const FVector2 ab = b2 - a2;
				const float u = std::clamp(math::Dot(p2 - a2, ab) / std::max(math::Dot(ab, ab), 1e-6f), 0.f, 1.f);
				if (math::Length(p2 - (a2 + ab * u)) < 5.f)
					return true;
			}
			return false;
		};
		(void)nearTour;
		views[0] = Frame(t, ground(m_landmarks[0].center + FVector2{-3.f, -2.f}, 24.f), 13.f, 2.f, 0.6f, "forêt de varech");
		views[1] = Frame(t, ground(m_landmarks[5].center + FVector2{-2.f, -6.f}, 8.f), 18.f, 3.f, 1.2f, "rayons de lumière");
		views[2] = Frame(t, ground(m_landmarks[1].center + FVector2{-4.f, 6.f}, 5.f), 20.f, 3.f, -0.9f, "grotte violette");
		views[3] = Frame(t, wreck.TransformPoint({4.f, 0.f, 0.f}), 24.f, 1.2f, std::atan2(wreck.m[2][2], wreck.m[2][0]) + 0.65f, "épave");
		views[4] = Frame(t, ground(m_landmarks[3].center + FVector2{4.f, -6.f}, 1.5f), 13.f, 2.f, 0.9f, "jardin d'éponges");
		views[5] = Frame(t, fluke.TransformPoint({0.f, 6.f, 0.f}), 15.f, 2.5f, std::atan2(18.f, -4.f), "nageoire de baleine");
		DressViews(t);
		// Dégage les lignes de visée des vues (décor retiré).
		const auto blocks = [&](const sdl3::MeshInstance &inst) {
			const FVector2 p{inst.model.m[3][0], inst.model.m[3][2]};
			for (const Viewpoint &v : views) {
				const FVector2 a2{v.position.x, v.position.z}, b2{v.target.x, v.target.z};
				const FVector2 ab = b2 - a2;
				const float len = math::Length(ab);
				const float u = std::clamp(math::Dot(p - a2, ab) / std::max(len * len, 1e-6f), 0.f, 1.f);
				if (u * len > 1.f && u < 0.9f && math::Length(p - (a2 + ab * u)) < 2.5f + len * u * 0.28f)
					return true;
			}
			return false;
		};
		for (auto &list : kelp)
			std::erase_if(list, blocks);
		for (auto &list : fronds)
			std::erase_if(list, blocks);
		for (auto &list : seagrass)
			std::erase_if(list, blocks);
		std::erase_if(seaWhips, blocks);
		std::erase_if(seaPens, blocks);
	} else {
		views[0] = Frame(t, floorAt(origin.x, origin.y) + FVector3{0.f, 6.f, 0.f}, 14.f, 3.f, 0.f, ChunkKindName(m_kind));
	}
}

void World::DressViews(const Terrain &t) {
	math::Rng rng(0xD2E55);
	for (const Viewpoint &v : views) {
		const FVector3 dir = (v.target - v.position).Normalized();
		const FVector3 right = FVector3{0.f, 1.f, 0.f}.Cross(dir).Normalized();
		for (const float s : {-1.f, 1.f}) {
			const FVector3 q = v.position + dir * (7.f + 5.f * rng.NextFloat()) + right * (s * (3.f + 2.f * rng.NextFloat()));
			vents.push_back({q.x, t.Height(q.x, q.z), q.z});
		}
	}
}

bool World::LineOfSight(const Terrain &t, const FVector3 &a, const FVector3 &b) const noexcept {
	const FVector3 ab = b - a;
	const float len2 = std::max(ab.LengthSq(), 1e-6f);
	for (const Collider &c : colliders) {
		const float u = std::clamp(math::Dot(c.center - a, ab) / len2, 0.f, 1.f);
		if (u > 0.92f)
			continue;
		if ((a + ab * u - c.center).LengthSq() < c.radius * c.radius * 0.8f)
			return false;
	}
	for (int i = 1; i < 24; ++i) {
		const FVector3 p = a + ab * (float(i) / 24.f);
		if (p.y < t.Height(p.x, p.z) + 0.3f)
			return false;
	}
	return true;
}

Viewpoint World::Frame(const Terrain &t, const FVector3 &target, float distance, float height, float angle, std::string_view name) const {
	const auto clearance = [&](const FVector3 &p) {
		const FVector3 dir = (target - p).Normalized();
		float best = 1e9f;
		for (const Collider &c : colliders) {
			const FVector3 d = c.center - p;
			const float dist = d.Length() - c.radius;
			const bool inView = math::Dot(d.Normalized(), dir) > 0.82f && dist < 8.f;
			best = std::min(best, inView ? dist * 0.5f : dist);
		}
		const FVector3 right = FVector3{0.f, 1.f, 0.f}.Cross(dir).Normalized();
		for (const float ahead : {2.f, 4.f, 7.f})
			for (const float lateral : {-0.6f, -0.3f, 0.3f, 0.6f}) {
				const FVector3 q = p + dir * ahead + right * (ahead * lateral);
				if (t.Height(q.x, q.z) > q.y - 0.5f)
					best = std::min(best, ahead * 0.3f);
			}
		return best;
	};
	FVector3 best = target + FVector3{std::cos(angle) * distance, 0.f, std::sin(angle) * distance};
	float bestScore = -1e9f;
	bool done = false;
	for (const float scale : {1.f, 0.8f, 1.25f, 0.6f}) {
		for (int k = 0; k < 24 && !done; ++k) {
			const float a = angle + float((k + 1) / 2) * (k % 2 ? 1.f : -1.f) * (kTau / 24.f);
			FVector3 p = target + FVector3{std::cos(a) * distance * scale, 0.f, std::sin(a) * distance * scale};
			p.y = t.Height(p.x, p.z) + height;
			Collide(t, p, 0.6f);
			const float clear = clearance(p);
			const bool sight = LineOfSight(t, p, target);
			const float floor = t.Openness(p.x, p.z) > 0.7f ? 40.f : 0.f;
			const float score = (sight ? 100.f : 0.f) + floor + std::min(clear, 5.f) * 6.f - float(k) * 0.5f - std::abs(scale - 1.f) * 4.f;
			if (score > bestScore) {
				bestScore = score;
				best = p;
			}
			done = sight && clear > 2.5f && floor > 0.f && k < 6 && scale == 1.f;
		}
		if (done)
			break;
	}
	const FVector3 d = target - best;
	return {best, YawTowards(best, target), std::atan2(d.y, std::hypot(d.x, d.z)), name, target};
}

void World::Collide(const Terrain &t, FVector3 &pos, float radius) const noexcept {
	for (const Collider &c : colliders) {
		const FVector3 d = pos - c.center;
		const float reach = c.radius + radius;
		const float dist2 = d.LengthSq();
		if (dist2 >= reach * reach)
			continue;
		if (dist2 < 1e-6f) {
			pos.y = c.center.y + reach;
			continue;
		}
		pos = c.center + d * (reach / std::sqrt(dist2));
	}
	pos.y = std::clamp(pos.y, t.Height(pos.x, pos.z) + radius + 0.6f, kCeilingY);
}

} // namespace abyssale
