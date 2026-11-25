#include "abyssale/meshes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <math/noise.hpp>
#include <sdl3/core/stdinc.hpp>

#include "abyssale/config.hpp"
#include "abyssale/terrain.hpp"

namespace abyssale::meshes {

namespace {

using math::FVector2;
using math::FVector3;

/// Puissance d'une base qui doit être positive : `sin(π)` vaut −8,7e-8 en flottant, et une
/// puissance non entière d'un nombre négatif donne NaN (sommets invalides, triangles NaN dans
/// la physique). La base est ramenée à 0.
float SafePow(float base, float exponent) { return std::pow(std::max(base, 0.f), exponent); }

void Tri(sdl3::MeshData &m, uint32_t a, uint32_t b, uint32_t c) { m.indices.insert(m.indices.end(), {a, b, c}); }
void Quad(sdl3::MeshData &m, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	Tri(m, a, b, c);
	Tri(m, a, c, d);
}
uint32_t Vertex(sdl3::MeshData &m, FVector3 p, FVector3 n, FVector2 uv) {
	m.vertices.push_back({p, n, uv});
	return uint32_t(m.vertices.size() - 1);
}

/// Normales lissées recalculées depuis les faces (après déformation).
void ComputeNormals(sdl3::MeshData &m) {
	for (auto &v : m.vertices)
		v.normal = {};
	for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
		auto &a = m.vertices[m.indices[i]], &b = m.vertices[m.indices[i + 1]], &c = m.vertices[m.indices[i + 2]];
		const FVector3 n = (b.position - a.position).Cross(c.position - a.position);
		a.normal += n;
		b.normal += n;
		c.normal += n;
	}
	for (auto &v : m.vertices) {
		const float l = v.normal.Length();
		v.normal = l > 1e-8f ? v.normal / l : FVector3{0.f, 1.f, 0.f};
	}
}

/// Petite sphère UV (yeux, détails ponctuels).
void SmallSphere(sdl3::MeshData &m, FVector3 center, float radius) {
	const int rings = 3, segs = 6;
	const uint32_t base = uint32_t(m.vertices.size());
	for (int r = 0; r <= rings; ++r) {
		const float th = float(r) / float(rings) * sdl3::PI_F;
		for (int q = 0; q <= segs; ++q) {
			const float az = float(q) / float(segs) * kTau;
			const FVector3 d{std::sin(th) * std::cos(az), std::cos(th), std::sin(th) * std::sin(az)};
			Vertex(m, center + d * radius, d, {float(q) / float(segs), float(r) / float(rings)});
		}
	}
	for (int r = 0; r < rings; ++r)
		for (int q = 0; q < segs; ++q) {
			const uint32_t a = base + uint32_t(r * (segs + 1) + q), b = a + uint32_t(segs + 1);
			Quad(m, a, b, b + 1, a + 1);
		}
}

/// Tube le long d'une polyligne (repères transportés : pas de torsion), rayon par point.
void Tube(sdl3::MeshData &m, const std::vector<FVector3> &pts, const std::vector<float> &radii, int sides, float vScale = 1.f) {
	if (pts.size() < 2)
		return;
	std::vector<std::vector<uint32_t>> rings;
	FVector3 up{0.31f, 0.9f, 0.3f};
	float along = 0.f;
	for (size_t k = 0; k < pts.size(); ++k) {
		const FVector3 a = pts[k == 0 ? 0 : k - 1], b = pts[std::min(k + 1, pts.size() - 1)];
		const FVector3 t = (b - a).Normalized();
		FVector3 s = up.Cross(t);
		s = s.Length() > 1e-5f ? s.Normalized() : FVector3{1.f, 0.f, 0.f};
		up = t.Cross(s);
		if (k > 0)
			along += (pts[k] - pts[k - 1]).Length();
		std::vector<uint32_t> ring;
		for (int i = 0; i <= sides; ++i) { // couture dupliquée : UV continues
			const float an = float(i) / float(sides) * kTau;
			const FVector3 d = s * std::cos(an) + up * std::sin(an);
			ring.push_back(Vertex(m, pts[k] + d * radii[k], d, {float(i) / float(sides), along * vScale}));
		}
		rings.push_back(std::move(ring));
	}
	for (size_t k = 0; k + 1 < rings.size(); ++k)
		for (size_t i = 0; i < size_t(sides); ++i)
			Quad(m, rings[k][i], rings[k][i + 1], rings[k + 1][i + 1], rings[k + 1][i]);
}

/// Tour de révolution d'un profil (rayon, hauteur), bosselé par du bruit.
void Lathe(sdl3::MeshData &m, const std::vector<FVector2> &profile, int segments, float lump, const math::SimplexNoise &n, float vScale) {
	std::vector<std::vector<uint32_t>> rings;
	float along = 0.f;
	for (size_t k = 0; k < profile.size(); ++k) {
		if (k > 0)
			along += math::Length(profile[k] - profile[k - 1]);
		std::vector<uint32_t> ring;
		for (int s = 0; s <= segments; ++s) {
			const float a = float(s % segments) / float(segments) * kTau;
			const float r = profile[k].x * (1.f + lump * n.Noise3(std::cos(a) * 1.7f, profile[k].y * 2.3f, std::sin(a) * 1.7f));
			ring.push_back(Vertex(m, {std::cos(a) * r, profile[k].y, std::sin(a) * r}, {}, {float(s) / float(segments) * 3.f, along * vScale}));
		}
		rings.push_back(std::move(ring));
	}
	for (size_t k = 0; k + 1 < rings.size(); ++k)
		for (size_t s = 0; s < size_t(segments); ++s)
			Quad(m, rings[k][s], rings[k + 1][s], rings[k + 1][s + 1], rings[k][s + 1]);
}

/// Ruban (feuille, brin) le long d'une courbe : `width(t)` en travers, normale = côté × tangente.
/// Ruban plié en V le long de sa nervure (trois sommets en travers) : normales inclinées de
/// part et d'autre, d'où un modelé réaliste au lieu d'un aplat. `fold` : profondeur relative du pli.
void Ribbon(sdl3::MeshData &m, const std::vector<FVector3> &pts, const std::vector<FVector3> &sides, const std::vector<float> &widths, float fold = 0.12f,
			float ruffle = 0.f) {
	const uint32_t base = uint32_t(m.vertices.size());
	for (size_t i = 0; i < pts.size(); ++i) {
		const FVector3 t = (pts[std::min(i + 1, pts.size() - 1)] - pts[i == 0 ? 0 : i - 1]).Normalized();
		const FVector3 n = sides[i].Cross(t).Normalized();
		const float v = float(i) / float(pts.size() - 1);
		const float half = widths[i] * 0.5f;
		// Ondulation des bords, en opposition de phase de part et d'autre de la nervure.
		const float wave = ruffle * widths[i] * std::sin(v * 19.f);
		Vertex(m, pts[i] - sides[i] * half + n * wave, (n - sides[i] * 0.55f).Normalized(), {0.f, v});
		Vertex(m, pts[i] + n * (widths[i] * fold), n, {0.5f, v});
		Vertex(m, pts[i] + sides[i] * half - n * wave, (n + sides[i] * 0.55f).Normalized(), {1.f, v});
	}
	for (uint32_t i = 0; i + 1 < uint32_t(pts.size()); ++i) {
		const uint32_t a = base + i * 3, b = a + 3;
		Quad(m, a, a + 1, b + 1, b);
		Quad(m, a + 1, a + 2, b + 2, b + 1);
	}
}

/// Surface fermée d'une lame épaisse : `mid(u, v)` (u en envergure, v en corde) et
/// `thick(u, v)` (demi-épaisseur selon `normal`) ; bords de corde d'épaisseur nulle.
void ThickBlade(sdl3::MeshData &m, int nu, int nv, const std::function<FVector3(float, float)> &mid,
				const std::function<float(float, float)> &thick, FVector3 normal, bool splitUv = false) {
	for (const float side : {1.f, -1.f}) {
		const uint32_t base = uint32_t(m.vertices.size());
		for (int i = 0; i <= nu; ++i)
			for (int j = 0; j <= nv; ++j) {
				const float u = float(i) / float(nu), v = float(j) / float(nv);
				// splitUv : dessus → v ∈ [0, ½], dessous → v ∈ [½, 1] (deux motifs dans une texture).
				const float tv = splitUv ? v * 0.5f + (side > 0.f ? 0.f : 0.5f) : v;
				Vertex(m, mid(u, v) + normal * (thick(u, v) * side), normal * side, {u, tv});
			}
		for (int i = 0; i < nu; ++i)
			for (int j = 0; j < nv; ++j) {
				const uint32_t a = base + uint32_t(i * (nv + 1) + j), b = a + uint32_t(nv + 1);
				if (side > 0.f)
					Quad(m, a, b, b + 1, a + 1);
				else
					Quad(m, a, a + 1, b + 1, b);
			}
	}
}

/// Section d'un corps fuselé le long de x : centre en y, demi-hauteur, demi-largeur.
struct Station {
	float x, y, h, w;
};

/// Corps fuselé : sections superellipses (`power` 2 : ellipse ; plus grand : plus carré).
/// UV : u le long du corps (0 à la première section), v autour (0,25 dos, 0,75 ventre).
void Loft(sdl3::MeshData &m, const std::vector<Station> &st, int segs, float power) {
	const float e = 2.f / power;
	const float x0 = st.front().x, span = st.back().x - x0;
	std::vector<std::vector<uint32_t>> rings;
	for (const Station &s : st) {
		std::vector<uint32_t> ring;
		for (int k = 0; k <= segs; ++k) {
			const float a = float(k) / float(segs) * kTau;
			const float ca = std::cos(a), sa = std::sin(a);
			const float z = std::copysign(std::pow(std::abs(ca), e), ca) * s.w;
			const float y = s.y + std::copysign(std::pow(std::abs(sa), e), sa) * s.h;
			ring.push_back(Vertex(m, {s.x, y, z}, {}, {std::abs(span) > 1e-6f ? (s.x - x0) / span : 0.f, float(k) / float(segs)}));
		}
		rings.push_back(std::move(ring));
	}
	for (size_t i = 0; i + 1 < rings.size(); ++i)
		for (size_t k = 0; k < size_t(segs); ++k)
			Quad(m, rings[i][k], rings[i][k + 1], rings[i + 1][k + 1], rings[i + 1][k]);
}

/// Pavé quelconque : `c[0..3]` une face, `c[4..7]` la face opposée dans le même ordre. Faces
/// planes à normales franches, orientées vers l'extérieur ; UV projetées sur chaque face.
void Hexa(sdl3::MeshData &m, const std::array<FVector3, 8> &c, float uvScale = 0.35f) {
	static constexpr int kFaces[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
	FVector3 centroid{};
	for (const FVector3 &p : c)
		centroid += p * 0.125f;
	for (const auto &f : kFaces) {
		std::array<FVector3, 4> q{c[size_t(f[0])], c[size_t(f[1])], c[size_t(f[2])], c[size_t(f[3])]};
		FVector3 n = (q[1] - q[0]).Cross(q[2] - q[0]) + (q[2] - q[0]).Cross(q[3] - q[0]);
		if (n.LengthSq() < 1e-12f)
			continue; // face dégénérée (planche effilée à la proue)
		if (n.Dot((q[0] + q[1] + q[2] + q[3]) * 0.25f - centroid) < 0.f) {
			std::swap(q[1], q[3]);
			n = -n;
		}
		n = n.Normalized();
		const FVector3 t1 = (q[1] - q[0]).LengthSq() > 1e-10f ? (q[1] - q[0]).Normalized() : FVector3{1.f, 0.f, 0.f};
		const FVector3 t2 = n.Cross(t1);
		uint32_t id[4];
		for (size_t i = 0; i < 4; ++i)
			id[i] = Vertex(m, q[i], n, {q[i].Dot(t1) * uvScale, q[i].Dot(t2) * uvScale});
		Quad(m, id[0], id[1], id[2], id[3]);
	}
}

/// Pavé orienté : centre et trois demi-axes.
void Box(sdl3::MeshData &m, FVector3 c, FVector3 ax, FVector3 ay, FVector3 az) {
	Hexa(m, {c - ax - ay - az, c + ax - ay - az, c + ax + ay - az, c - ax + ay - az, c - ax - ay + az, c + ax - ay + az, c + ax + ay + az, c - ax + ay + az});
}

/// Lame mince à deux faces (nageoire, uropode) : `mid(u, v)` sa surface moyenne, `half`
/// sa demi-épaisseur constante selon `normal`.
void Fin(sdl3::MeshData &m, int nu, int nv, const std::function<FVector3(float, float)> &mid, float half, FVector3 normal) {
	ThickBlade(m, nu, nv, mid, [half](float, float v) { return half * (1.f - 0.6f * v); }, normal);
}

/// Segment de patte articulée : tube effilé et sphère au genou.
void Leg(sdl3::MeshData &m, const std::vector<FVector3> &joints, float r0, float r1) {
	std::vector<float> radii;
	for (size_t i = 0; i < joints.size(); ++i)
		radii.push_back(r0 + (r1 - r0) * float(i) / float(joints.size() - 1));
	Tube(m, joints, radii, 5);
	for (size_t i = 1; i + 1 < joints.size(); ++i)
		SmallSphere(m, joints[i], radii[i] * 1.15f);
}

} // namespace

sdl3::MeshData Kelp(float height, int leaves, float leafLength, float leafWidth, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const math::SimplexNoise n(seed);
	const float ph = rng.NextFloat() * kTau;
	const auto stem = [&](float y) {
		return FVector3{0.35f * std::sin(y * 0.13f + ph) + 0.15f * std::sin(y * 0.41f + ph * 2.f), y,
						0.35f * std::cos(y * 0.11f + ph * 1.3f)};
	};
	// Tige.
	std::vector<FVector3> pts;
	std::vector<float> radii;
	const int segs = std::max(8, int(height / 1.2f));
	for (int i = 0; i <= segs; ++i) {
		const float y = float(i) / float(segs) * height;
		pts.push_back(stem(y));
		radii.push_back(0.035f - 0.018f * float(i) / float(segs));
	}
	Tube(m, pts, radii, 5, 0.05f);
	for (auto &v : m.vertices) // tige : partie vert sombre de la texture de feuille
		v.uv.x = 0.3f;
	// Crampon basal : 5 à 7 tentacules courts qui s'enfoncent dans le sable (nouveau).
	const int roots = 5 + int(rng.NextFloat() * 3.f);
	for (int r = 0; r < roots; ++r) {
		const float az = float(r) / float(roots) * kTau + rng.NextFloat() * 0.4f;
		const float len = 0.35f + 0.3f * rng.NextFloat();
		const FVector3 dir{std::cos(az) * 0.85f, -0.35f - 0.25f * rng.NextFloat(), std::sin(az) * 0.85f};
		std::vector<FVector3> rp;
		std::vector<float> rr;
		for (int i = 0; i <= 3; ++i) {
			const float u = float(i) / 3.f;
			rp.push_back(stem(0.f) + dir * (len * u) + FVector3{0.f, -0.06f * u * u, 0.f});
			rr.push_back(0.028f * (1.f - 0.8f * u) + 0.004f);
		}
		Tube(m, rp, rr, 4);
	}
	// Feuilles alternées (angle d'or), plus grandes au milieu de la hauteur.
	for (int k = 0; k < leaves; ++k) {
		const float t = (float(k) + 0.5f + rng.NextFloat() * 0.6f) / float(leaves);
		const float y = 0.6f + t * (height - 0.8f);
		const FVector3 root = stem(y);
		const float az = float(k) * 2.39996f + rng.NextFloat() * 0.6f;
		const float size = (0.7f + 0.45f * std::sin(t * sdl3::PI_F)) * (0.75f + 0.5f * rng.NextFloat());
		const float len = leafLength * size, width = leafWidth * size;
		const FVector3 out{std::cos(az), 0.15f + 0.45f * rng.NextFloat(), std::sin(az)};
		const FVector3 dir = out.Normalized();
		const FVector3 side0 = FVector3{0.f, 1.f, 0.f}.Cross(dir).Normalized();
		const float twist = (rng.NextFloat() - 0.5f) * 1.6f;
		std::vector<FVector3> lp, ls;
		std::vector<float> lw;
		const int ls_ = 6;
		for (int i = 0; i <= ls_; ++i) {
			const float u = float(i) / float(ls_);
			FVector3 p = root + dir * (len * u) + FVector3{0.f, -0.55f * len * u * u, 0.f};
			p += FVector3{n.Noise3(p.x * 3.f, p.y * 3.f, p.z * 3.f), 0.3f * n.Noise3(p.y * 2.f, p.z, p.x), n.Noise3(p.z * 3.f, p.x * 3.f, p.y * 3.f)} * (0.12f * u);
			lp.push_back(p);
			const float a = twist * u;
			ls.push_back((side0 * std::cos(a) + dir.Cross(side0) * std::sin(a)).Normalized());
			lw.push_back(width * SafePow(std::sin(sdl3::PI_F * std::min(u * 0.9f + 0.1f, 1.f)), 0.5f) * (1.f - 0.35f * u));
		}
		Ribbon(m, lp, ls, lw, 0.1f, 0.35f);
		// Flotteur : vésicule ovale (une lame sur deux), taille variable.
		if (k % 2)
			continue;
		const FVector3 bulb = root + dir * 0.09f;
		const float bs = 0.032f + 0.018f * rng.NextFloat();
		const uint32_t base = uint32_t(m.vertices.size());
		const int rings = 3, seg = 4;
		for (int r = 0; r <= rings; ++r) {
			const float th = float(r) / float(rings) * sdl3::PI_F;
			for (int q = 0; q <= seg; ++q) {
				const float az2 = float(q) / float(seg) * kTau;
				const FVector3 d{std::sin(th) * std::cos(az2), std::cos(th), std::sin(th) * std::sin(az2)};
				Vertex(m, bulb + FVector3{d.x * bs, d.y * bs * 1.6f, d.z * bs}, d, {0.9f, 0.5f});
			}
		}
		for (int r = 0; r < rings; ++r)
			for (int q = 0; q < seg; ++q) {
				const uint32_t a = base + uint32_t(r * (seg + 1) + q), b = a + uint32_t(seg + 1);
				Quad(m, a, b, b + 1, a + 1);
			}
	}
	return m;
}

sdl3::MeshData Tuft(int blades, float height, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	for (int b = 0; b < blades; ++b) {
		const float az = rng.NextFloat() * kTau;
		const float h = height * (0.45f + 0.55f * rng.NextFloat());
		const float lean = 0.2f + 0.5f * rng.NextFloat();
		const FVector3 dir{std::cos(az), 0.f, std::sin(az)};
		const FVector3 root = dir * (0.12f * std::sqrt(rng.NextFloat()));
		std::vector<FVector3> p, s;
		std::vector<float> w;
		for (int i = 0; i <= 3; ++i) {
			const float u = float(i) / 3.f;
			p.push_back(root + dir * (lean * h * u * u) + FVector3{0.f, h * u, 0.f});
			s.push_back(FVector3{-dir.z, 0.f, dir.x});
			w.push_back(0.028f * (1.f - u * 0.9f));
		}
		Ribbon(m, p, s, w, 0.f);
	}
	return m;
}

sdl3::MeshData Rock(int segments, float roughness, float flatten, uint64_t seed) {
	sdl3::MeshData m = sdl3::MeshData::Sphere(1.f, segments, std::max(4, segments / 2));
	for (auto &v : m.vertices) {
		FVector3 p = v.position * RockRadius(v.position, roughness, seed);
		if (p.y < -flatten)
			p.y = -flatten + (p.y + flatten) * 0.2f;
		v.position = p;
	}
	ComputeNormals(m);
	return m;
}

float RockRadius(const FVector3 &dir, float roughness, uint64_t seed) {
	const math::SimplexNoise n(seed);
	const float ox = float(seed % 97), oz = float(seed % 89);
	return 1.f + roughness * (n.Fbm3(dir.x * 1.2f + ox, dir.y * 1.2f, dir.z * 1.2f + oz, 4) + 0.35f * n.Noise3(dir.x * 4.f, dir.y * 4.f, dir.z * 4.f));
}

sdl3::MeshData Arch(float span, float thickness, uint64_t seed) {
	sdl3::MeshData m;
	const math::SimplexNoise n(seed);
	std::vector<FVector3> pts;
	std::vector<float> radii;
	const int segs = 40;
	for (int i = 0; i <= segs; ++i) {
		const float a = -0.22f + float(i) / float(segs) * (sdl3::PI_F + 0.44f);
		pts.push_back({std::cos(a) * span, std::sin(a) * span * 0.85f, 0.f});
		const float u = float(i) / float(segs);
		radii.push_back(thickness * (1.f + 0.6f * std::pow(std::abs(u - 0.5f) * 2.f, 3.f)) * (1.f + 0.25f * n.Noise2(u * 6.f, 1.f)));
	}
	Tube(m, pts, radii, 16);
	for (auto &v : m.vertices) {
		const FVector3 p = v.position;
		v.position = p + v.normal * (thickness * 0.35f * n.Fbm3(p.x * 0.35f, p.y * 0.35f, p.z * 0.35f, 4));
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData BarrelSponge(float r, float h, uint64_t seed) {
	sdl3::MeshData m;
	const math::SimplexNoise n(seed);
	// Profil continu : pied, panse, lèvre, puis descente le long de la paroi intérieure.
	const std::vector<FVector2> profile{{r * 0.5f, -0.1f},	 {r * 0.78f, h * 0.12f}, {r * 0.97f, h * 0.45f}, {r, h * 0.78f},
										{r * 0.93f, h},		 {r * 0.82f, h * 1.04f}, {r * 0.7f, h * 0.99f},	 {r * 0.66f, h * 0.7f},
										{r * 0.55f, h * 0.35f}, {r * 0.3f, h * 0.28f}, {0.f, h * 0.27f}};
	Lathe(m, profile, 32, 0.18f, n, 1.4f);
	for (auto &v : m.vertices) { // cannelures : 14 côtes verticales sur la paroi extérieure
		const float a = std::atan2(v.position.z, v.position.x);
		const float radial = std::hypot(v.position.x, v.position.z);
		if (radial < 1e-4f || v.position.y < 0.f)
			continue;
		const float rib = 1.f + 0.07f * std::cos(a * 14.f);
		v.position.x *= rib;
		v.position.z *= rib;
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData TubeSponges(int tubes, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const math::SimplexNoise n(seed);
	for (int t = 0; t < tubes; ++t) {
		sdl3::MeshData one;
		const float r = 0.1f + 0.08f * rng.NextFloat(), h = 0.6f + 1.1f * rng.NextFloat();
		Lathe(one, {{r * 0.8f, -0.05f}, {r, h * 0.5f}, {r * 1.1f, h}, {r * 0.75f, h * 1.01f}, {r * 0.65f, h * 0.7f}, {0.f, h * 0.6f}}, 10, 0.12f, n, 2.f);
		const float lean = (rng.NextFloat() - 0.5f) * 0.6f, az = rng.NextFloat() * kTau;
		const FVector3 off{std::cos(az) * 0.22f * float(t > 0), 0.f, std::sin(az) * 0.22f * float(t > 0)};
		const uint32_t base = uint32_t(m.vertices.size());
		for (auto v : one.vertices) {
			v.position = {v.position.x + v.position.y * lean * std::cos(az), v.position.y, v.position.z + v.position.y * lean * std::sin(az)};
			v.position += off;
			m.vertices.push_back(v);
		}
		for (uint32_t i : one.indices)
			m.indices.push_back(base + i);
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData BranchCoral(int depth, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	std::function<void(FVector3, FVector3, float, float, int)> branch = [&](FVector3 p, FVector3 d, float r, float len, int level) {
		std::vector<FVector3> pts{p};
		std::vector<float> radii{r};
		FVector3 q = p;
		for (int i = 0; i < 6; ++i) {
			d = (d + FVector3{rng.NextFloat() - 0.5f, 0.25f + rng.NextFloat() * 0.3f, rng.NextFloat() - 0.5f} * 0.3f).Normalized();
			q = q + d * (len / 6.f);
			pts.push_back(q);
			radii.push_back(r * (1.f - 0.07f * float(i + 1)));
		}
		Tube(m, pts, radii, 8, 2.f);
		if (level > 0)
			for (int i = 0; i < 2 + int(rng.NextFloat() * 1.5f); ++i)
				branch(q, (d + FVector3{rng.NextFloat() - 0.5f, 0.4f, rng.NextFloat() - 0.5f}).Normalized(), radii.back(), len * 0.75f, level - 1);
	};
	for (int trunk = 0; trunk < 3; ++trunk)
		branch({(rng.NextFloat() - 0.5f) * 0.2f, 0.f, (rng.NextFloat() - 0.5f) * 0.2f},
			   FVector3{rng.NextFloat() - 0.5f, 1.5f, rng.NextFloat() - 0.5f}.Normalized(), 0.04f, 0.42f, depth);
	return m;
}

sdl3::MeshData FanCoral(uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	std::function<void(FVector3, float, float, int)> branch = [&](FVector3 p, float angle, float len, int level) {
		const FVector3 d{std::sin(angle), std::cos(angle), (rng.NextFloat() - 0.5f) * 0.1f};
		const FVector3 q = p + d * len;
		Tube(m, {p, (p + q) * 0.5f + FVector3{0.f, 0.f, (rng.NextFloat() - 0.5f) * 0.05f}, q}, {0.025f * float(level + 1) * 0.5f + 0.01f, 0.018f * float(level + 1) * 0.5f + 0.008f, 0.012f}, 4);
		if (level > 0)
			for (const float s : {-1.f, 1.f})
				branch(q, angle + s * (0.25f + rng.NextFloat() * 0.25f), len * 0.78f, level - 1);
	};
	branch({0.f, 0.f, 0.f}, 0.f, 0.35f, 5);
	return m;
}

sdl3::MeshData Shipwreck() {
	sdl3::MeshData m;
	// Coque de 34 m (x ∈ [−17, 17], proue +X), toutes les pièces en volume : on entre par les
	// brèches des flancs, l'effondrement du pont ou l'écoutille, et on marche dans la cale.
	const float L = 17.f, B = 5.6f, D = 7.2f, T = 0.14f; // T : épaisseur des bordés
	const auto hb = [&](float x) { return B * std::sqrt(std::max(0.f, 1.f - std::pow(std::abs(x / L), x < 0.f ? 3.6f : 2.4f))); };
	const auto dp = [&](float x) { return D * (1.f - 0.3f * std::pow(std::abs(x / L), 2.5f)); };
	const auto sheer = [&](float x) { return 0.5f + (x > 0.f ? 2.6f : 3.4f) * std::pow(std::abs(x / L), 2.2f); };
	// ph ∈ [−π/2, π/2] : d'un plat-bord à l'autre en passant par la quille ; `inset` vers l'intérieur.
	const auto pt = [&](float x, float ph, float inset) {
		const float b = std::max(hb(x) - inset, 0.f), d = dp(x) - inset, s = sheer(x);
		const float c = std::pow(std::max(std::cos(ph), 0.f), 0.66f);
		return FVector3{x, s - (d + s) * c, b * (ph < 0.f ? -1.f : 1.f) * std::pow(std::abs(std::sin(ph)), 0.8f)};
	};
	const auto deckY = [&](float x) { return sheer(x) - 0.35f; };

	// ── Bordés : chaque planche est un pavé (face externe, face interne décalée de T) ──
	const int bands = 14, cols = 24;
	for (int k = 0; k < bands; ++k) {
		const float ph0 = -sdl3::PI_F * 0.5f + (float(k) + 0.02f) / float(bands) * sdl3::PI_F;
		const float ph1 = -sdl3::PI_F * 0.5f + (float(k) + 0.98f) / float(bands) * sdl3::PI_F;
		for (int j = 0; j < cols; ++j) {
			const float x0 = -L + 0.9f + (float(j) + 0.01f) / float(cols) * (2.f * L - 1.8f);
			const float x1 = -L + 0.9f + (float(j) + 0.99f) / float(cols) * (2.f * L - 1.8f);
			const float xm = (x0 + x1) * 0.5f;
			// Brèches dans les deux bandes sous le plat-bord (le bas de la coque est ensablé),
			// assez grandes pour un plongeur (≈ 3 m × 6 m) ; quelques planches arrachées.
			const bool breachA = k <= 1 && xm > -7.5f && xm < -1.f;
			const bool breachB = k >= bands - 2 && xm > -2.5f && xm < 4.f;
			const bool torn = math::Hash01(k, j, 0, 77) < 0.05f;
			if (breachA || breachB || torn)
				continue;
			Hexa(m, {pt(x0, ph0, 0.f), pt(x1, ph0, 0.f), pt(x1, ph1, 0.f), pt(x0, ph1, 0.f), pt(x0, ph0, T), pt(x1, ph0, T), pt(x1, ph1, T), pt(x0, ph1, T)});
		}
	}
	// Membrures (côtes) à l'intérieur des bordés, quille et lisses de plat-bord.
	for (int j = 0; j <= 16; ++j) {
		const float x = -L + 1.6f + float(j) / 16.f * (2.f * L - 3.2f);
		std::vector<FVector3> rib;
		std::vector<float> r;
		for (int i = 0; i <= 12; ++i) {
			rib.push_back(pt(x, -sdl3::PI_F * 0.5f + float(i) / 12.f * sdl3::PI_F, T + 0.12f));
			r.push_back(0.12f);
		}
		Tube(m, rib, r, 4, 0.3f);
	}
	{
		std::vector<FVector3> keel, railL, railR;
		std::vector<float> rk, rr;
		for (int j = 0; j <= 16; ++j) {
			const float x = -L + 0.6f + float(j) / 16.f * (2.f * L - 1.2f);
			keel.push_back(pt(x, 0.f, -0.25f));
			rk.push_back(0.28f);
			railL.push_back(pt(x, -sdl3::PI_F * 0.5f, 0.f) + FVector3{0.f, 0.15f, 0.f});
			railR.push_back(pt(x, sdl3::PI_F * 0.5f, 0.f) + FVector3{0.f, 0.15f, 0.f});
			rr.push_back(0.13f);
		}
		Tube(m, keel, rk, 6, 0.3f);
		Tube(m, railL, rr, 5, 0.3f);
		Tube(m, railR, rr, 5, 0.3f);
	}

	// ── Faux-pont : planches transversales 1,5 m sous le pont (au-dessus du sable) ──
	const auto halfWidthAt = [&](float x, float y) { // demi-largeur intérieure de la coque à la hauteur y
		for (int i = 0; i <= 40; ++i) {
			const FVector3 p = pt(x, float(i) / 40.f * sdl3::PI_F * 0.5f, T + 0.25f);
			if (p.y >= y)
				return std::abs(p.z);
		}
		return std::max(hb(x) - 0.5f, 0.f);
	};
	for (float x = -12.f; x < 12.f; x += 2.f) {
		const float y = -1.5f, w = halfWidthAt(x + 1.f, y) - 0.05f;
		for (float z = -w; z + 0.45f <= w; z += 0.5f)
			Box(m, {x + 1.f, y, z + 0.24f}, {0.98f, 0.f, 0.f}, {0.f, 0.06f, 0.f}, {0.f, 0.f, 0.23f});
	}

	// ── Pont : bordages épais ; grand effondrement au milieu, écoutille à l'avant ──
	for (int row = -4; row <= 4; ++row)
		for (int j = 0; j < 20; ++j) {
			const float x0 = -L + 2.5f + float(j) / 20.f * (2.f * L - 5.f), x1 = -L + 2.5f + float(j + 1) / 20.f * (2.f * L - 5.f);
			const float xm = (x0 + x1) * 0.5f;
			if ((std::abs(xm + 1.f) < 3.5f && std::abs(row) < 3) || (xm > 5.f && xm < 7.8f && std::abs(row) <= 1))
				continue;
			const float w = std::max(hb(xm) - 0.3f, 0.2f);
			const float z0 = w * (float(row) - 0.46f) / 4.6f, z1 = w * (float(row) + 0.46f) / 4.6f;
			const float y0 = deckY(x0), y1 = deckY(x1), t = 0.1f;
			Hexa(m, {FVector3{x0, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y0, z1}, {x0, y0 - t, z0}, {x1, y1 - t, z0}, {x1, y1 - t, z1}, {x0, y0 - t, z1}});
		}
	for (int j = 0; j < 9; ++j) { // barrots
		const float x = -L + 4.f + float(j) * 3.4f;
		const float w = hb(x) - 0.3f;
		Tube(m, {{x, deckY(x) - 0.25f, -w}, {x, deckY(x) - 0.25f, w}}, {0.16f, 0.16f}, 4, 0.3f);
	}
	// Hiloire de l'écoutille (encadrement surélevé).
	for (const float z : {-1.3f, 1.3f})
		Box(m, {6.4f, deckY(6.4f) + 0.15f, z}, {1.45f, 0.f, 0.f}, {0.f, 0.2f, 0.f}, {0.f, 0.f, 0.08f});
	for (const float x : {5.f, 7.8f})
		Box(m, {x, deckY(x) + 0.15f, 0.f}, {0.08f, 0.f, 0.f}, {0.f, 0.2f, 0.f}, {0.f, 0.f, 1.3f});

	// ── Cabine arrière : parois épaisses, porte ouverte vers l'avant, toit à demi effondré ──
	{
		const float xa = -14.f, xb = -9.5f, zw = 2.4f, h = 2.2f, t = 0.12f;
		const auto wallX = [&](float x, float za, float zb, float y0, float y1) { // paroi transversale
			Box(m, {x, deckY(x) + (y0 + y1) * 0.5f, (za + zb) * 0.5f}, {t, 0.f, 0.f}, {0.f, (y1 - y0) * 0.5f, 0.f}, {0.f, 0.f, (zb - za) * 0.5f});
		};
		wallX(xa, -zw, zw, 0.f, h);
		wallX(xb, -zw, -0.6f, 0.f, h); // porte de 1,2 m × 1,9 m
		wallX(xb, 0.6f, zw, 0.f, h);
		wallX(xb, -0.6f, 0.6f, 1.9f, h);
		for (const float z : {-zw, zw}) { // parois latérales, posées sur le pont qui remonte
			const float y0a = deckY(xa), y0b = deckY(xb);
			Hexa(m, {FVector3{xa, y0a, z - t}, {xb, y0b, z - t}, {xb, y0b + h, z - t}, {xa, y0a + h, z - t}, {xa, y0a, z + t}, {xb, y0b, z + t}, {xb, y0b + h, z + t},
					 {xa, y0a + h, z + t}});
		}
		for (int i = 0; i < 9; ++i) { // toit : planches longitudinales, trois manquantes
			if (i == 3 || i == 4 || i == 6)
				continue;
			const float z0 = -zw + float(i) / 9.f * 2.f * zw, z1 = z0 + 2.f * zw / 9.f - 0.04f;
			const float ya = deckY(xa) + h, yb = deckY(xb) + h;
			Hexa(m, {FVector3{xa, ya, z0}, {xb, yb, z0}, {xb, yb, z1}, {xa, ya, z1}, {xa, ya + 0.1f, z0}, {xb, yb + 0.1f, z0}, {xb, yb + 0.1f, z1}, {xa, ya + 0.1f, z1}});
		}
	}

	// ── Mâture : mât de misaine debout avec vergues, grand mât brisé, beaupré, gréement ──
	Tube(m, {{8.f, -D + 1.f, 0.f}, {8.1f, 4.f, 0.f}, {8.2f, 14.f, 0.f}}, {0.42f, 0.36f, 0.26f}, 8, 0.2f);
	Tube(m, {{8.15f, 10.f, -4.5f}, {8.15f, 10.2f, 4.5f}}, {0.16f, 0.16f}, 6, 0.2f);
	Tube(m, {{8.2f, 13.f, -3.f}, {8.2f, 13.1f, 3.f}}, {0.12f, 0.12f}, 6, 0.2f);
	Tube(m, {{-3.f, -D + 1.f, 0.f}, {-3.2f, 3.f, 0.2f}, {-4.4f, 10.f, 1.1f}}, {0.5f, 0.42f, 0.32f}, 8, 0.2f);
	Tube(m, {{-4.6f, 10.6f, 1.2f}, {-9.f, 1.f, 4.5f}}, {0.3f, 0.24f}, 8, 0.2f); // tronçon tombé sur le pont
	Tube(m, {{L - 1.f, sheer(L - 1.f) - 0.4f, 0.f}, {L + 6.f, sheer(L) + 2.6f, 0.f}}, {0.24f, 0.12f}, 6, 0.2f);
	for (int r = 0; r < 3; ++r) {
		const float t = float(r) / 2.f;
		Tube(m, {{8.15f, 10.f + 3.f * t, -4.5f + 9.f * t}, {-3.5f, 3.f + 7.f * t, 0.5f}}, {0.025f, 0.02f}, 3, 0.3f);
	}
	return m;
}

sdl3::MeshData WhaleFluke() {
	sdl3::MeshData m;
	// Lame unique, d'une pointe à l'autre : s ∈ [−1, 1] en envergure, c ∈ [0, 1] en corde
	// (0 : bord d'attaque, en haut ; 1 : bord de fuite, en bas). Épaisseur selon ±Z.
	const auto chord = [](float s) {
		const float a = std::abs(s);
		return 3.3f * (1.f - std::pow(a, 2.6f)) + 0.18f - 1.1f * std::exp(-(s * s) / 0.012f); // encoche médiane
	};
	const auto leading = [](float s) {
		const float a = std::abs(s);
		return 6.2f + 3.6f * std::pow(a, 1.35f) + 0.9f * std::sin(sdl3::PI_F * a); // bord d'attaque bombé, lobes relevés
	};
	const auto mid = [&](float s, float c) {
		const float a = std::abs(s);
		const float y = leading(s) - chord(s) * c;
		const float x = s * 6.6f - (s > 0.f ? 1.f : -1.f) * 1.1f * a * a * c; // pointes rabattues vers l'arrière
		return FVector3{x, y, 1.1f * a * a - 0.25f * std::sin(sdl3::PI_F * c) * (1.f - a)};
	};
	const auto thick = [](float s, float c) {
		const float a = std::abs(s);
		// Profil d'aile : maximum vers 30 % de la corde, bord de fuite fin.
		const float airfoil = SafePow(std::sin(sdl3::PI_F * std::pow(c, 0.62f)), 0.75f);
		return 0.95f * SafePow(1.f - std::pow(a, 1.6f), 0.55f) * airfoil + 0.03f;
	};
	const int ns = 48, nc = 16;
	for (const float side : {1.f, -1.f}) {
		const uint32_t base = uint32_t(m.vertices.size());
		for (int i = 0; i <= ns; ++i)
			for (int k = 0; k <= nc; ++k) {
				const float sv = float(i) / float(ns) * 2.f - 1.f, c = float(k) / float(nc);
				Vertex(m, mid(sv, c) + FVector3{0.f, 0.f, thick(sv, c) * side}, {0.f, 0.f, side}, {sv, c});
			}
		for (int i = 0; i < ns; ++i)
			for (int k = 0; k < nc; ++k) {
				const uint32_t a = base + uint32_t(i * (nc + 1) + k), b = a + uint32_t(nc + 1);
				if (side > 0.f)
					Quad(m, a, b, b + 1, a + 1);
				else
					Quad(m, a, a + 1, b + 1, b);
			}
	}
	// Pédoncule : section elliptique qui s'évase en haut pour se fondre dans la lame.
	{
		const int rings = 14, sides = 18;
		std::vector<std::vector<uint32_t>> rs;
		for (int i = 0; i <= rings; ++i) {
			const float t = float(i) / float(rings);
			const float y = -2.5f + t * 8.2f;
			const float flare = std::pow(t, 4.f);
			const float rx = 1.35f - 0.5f * t + 2.2f * flare, rz = 1.0f - 0.32f * t + 0.1f * flare;
			const float bend = 0.45f * std::sin(t * sdl3::PI_F * 0.8f);
			std::vector<uint32_t> ring;
			for (int k = 0; k <= sides; ++k) {
				const float a = float(k) / float(sides) * kTau;
				ring.push_back(Vertex(m, {std::cos(a) * rx, y, std::sin(a) * rz + bend}, {}, {float(k) / float(sides), t}));
			}
			rs.push_back(std::move(ring));
		}
		for (int i = 0; i < rings; ++i)
			for (int k = 0; k < sides; ++k)
				Quad(m, rs[size_t(i)][size_t(k)], rs[size_t(i + 1)][size_t(k)], rs[size_t(i + 1)][size_t(k + 1)], rs[size_t(i)][size_t(k + 1)]);
	}
	ComputeNormals(m);
	// Pierre érodée : creux et bosses doux, plus une fine rugosité.
	const math::SimplexNoise n(55);
	for (auto &v : m.vertices) {
		const FVector3 q = v.position;
		v.position += v.normal * (0.12f * n.Fbm3(q.x * 0.45f, q.y * 0.45f, q.z * 0.45f, 4) + 0.03f * n.Noise3(q.x * 3.f, q.y * 3.f, q.z * 3.f));
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData Fish() {
	sdl3::MeshData m;
	// Corps fusiforme comprimé latéralement (nez +X, ~0,9 m) : bosse dorsale, pédoncule fin.
	Loft(m,
		 {{0.50f, 0.000f, 0.004f, 0.003f}, {0.48f, 0.002f, 0.035f, 0.022f}, {0.44f, 0.006f, 0.068f, 0.040f}, {0.37f, 0.010f, 0.102f, 0.055f},
		  {0.27f, 0.014f, 0.132f, 0.064f}, {0.15f, 0.012f, 0.148f, 0.067f}, {0.03f, 0.008f, 0.146f, 0.064f}, {-0.09f, 0.004f, 0.126f, 0.054f},
		  {-0.19f, 0.002f, 0.092f, 0.039f}, {-0.27f, 0.f, 0.056f, 0.022f}, {-0.33f, 0.f, 0.034f, 0.012f}, {-0.37f, 0.f, 0.030f, 0.010f}},
		 20, 2.3f);
	// Caudale fourchue : lobes plus longs que le centre.
	Fin(m, 12, 4,
		[](float u, float v) {
			const float y = (u * 2.f - 1.f) * 0.19f, ay = std::abs(y) / 0.19f;
			const float chord = 0.07f + 0.13f * std::pow(ay, 1.4f);
			return FVector3{-0.355f - chord * v, y * (0.25f + 0.75f * v), 0.f};
		},
		0.004f, {0.f, 0.f, 1.f});
	// Dorsale (rayons épineux puis mous), anale, pelviennes et pectorales.
	Fin(m, 10, 4,
		[](float u, float v) {
			const float x = 0.17f - 0.32f * u;
			const float h = 0.11f * SafePow(std::sin(sdl3::PI_F * SafePow(1.f - u, 0.7f)), 0.8f);
			return FVector3{x - 0.04f * v, 0.13f + h * v - 0.05f * u, 0.f};
		},
		0.003f, {0.f, 0.f, 1.f});
	Fin(m, 6, 3, [](float u, float v) { return FVector3{-0.12f - 0.14f * u - 0.03f * v, -0.10f + 0.03f * u - 0.06f * v * std::sin(sdl3::PI_F * u), 0.f}; }, 0.003f,
		{0.f, 0.f, 1.f});
	for (const float side : {-1.f, 1.f}) {
		Fin(m, 5, 3, [side](float u, float v) { return FVector3{0.30f - 0.13f * v, -0.01f - 0.03f * v, side * (0.058f + 0.07f * u * (0.5f + v))}; }, 0.0025f,
			{0.f, 1.f, 0.f});
		Fin(m, 4, 3, [side](float u, float v) { return FVector3{0.14f - 0.09f * v, -0.13f - 0.025f * v, side * (0.02f + 0.04f * u)}; }, 0.0025f,
			{0.f, 1.f, 0.f});
	}
	ComputeNormals(m);
	// Yeux saillants (calotte claire, pupille sombre gérée par la teinte : petite sphère).
	for (const float side : {-1.f, 1.f})
		SmallSphere(m, {0.405f, 0.03f, side * 0.045f}, 0.019f);
	return m;
}

sdl3::MeshData FishFar() {
	sdl3::MeshData m;
	Loft(m,
		 {{0.50f, 0.f, 0.004f, 0.003f}, {0.44f, 0.006f, 0.068f, 0.040f}, {0.30f, 0.012f, 0.125f, 0.062f}, {0.12f, 0.012f, 0.148f, 0.067f},
		  {-0.06f, 0.005f, 0.13f, 0.056f}, {-0.22f, 0.f, 0.08f, 0.033f}, {-0.33f, 0.f, 0.034f, 0.012f}, {-0.37f, 0.f, 0.03f, 0.01f}},
		 8, 2.3f);
	Fin(m, 2, 1,
		[](float u, float v) {
			const float y = (u * 2.f - 1.f) * 0.19f, ay = std::abs(y) / 0.19f;
			return FVector3{-0.355f - (0.07f + 0.13f * ay) * v, y * (0.25f + 0.75f * v), 0.f};
		},
		0.004f, {0.f, 0.f, 1.f});
	Fin(m, 2, 1, [](float u, float v) { return FVector3{0.17f - 0.32f * u - 0.04f * v, 0.13f + 0.1f * v * (1.f - u) - 0.05f * u, 0.f}; }, 0.003f, {0.f, 0.f, 1.f});
	ComputeNormals(m);
	return m;
}

sdl3::MeshData Manta() {
	sdl3::MeshData m;
	// Disque et ailes d'une seule pièce (envergure z ∈ [−1, 1], nez +X) : bord d'attaque en
	// flèche, pointes effilées rabattues vers l'arrière, corps central épais (ventre bombé).
	const auto mid = [](float u, float v) {
		const float z = u * 2.f - 1.f, az = std::abs(z);
		const float le = 0.56f - 0.62f * std::pow(az, 1.5f);
		const float te = -0.42f + 0.30f * az;
		const float x = le + (te - le) * v;
		return FVector3{x, 0.05f * (1.f - az * az) - 0.09f * az * az * az, z * (1.f - 0.06f * v * az)};
	};
	const auto thick = [](float u, float v) {
		const float az = std::abs(u * 2.f - 1.f);
		return 0.21f * SafePow(1.f - az, 1.7f) * SafePow(std::sin(sdl3::PI_F * v), 0.6f) + 0.01f * (1.f - az * 0.6f);
	};
	ThickBlade(m, 36, 12, mid, thick, {0.f, 1.f, 0.f}, true);
	// Nageoires céphaliques : lobes aplatis enroulés vers le bas, de part et d'autre de la bouche.
	for (const float sd : {-1.f, 1.f})
		Fin(m, 6, 4,
			[sd](float u, float v) {
				return FVector3{0.50f + 0.24f * u - 0.03f * v, 0.02f - v * 0.11f * (1.f - 0.4f * u), sd * (0.12f + 0.025f * u + 0.035f * v * v)};
			},
			0.012f, {0.f, 0.f, 1.f});
	// Queue fine qui naît dans le corps (pas d'interstice), petite dorsale à sa base.
	Tube(m, {{-0.25f, 0.03f, 0.f}, {-0.45f, 0.03f, 0.f}, {-0.9f, 0.04f, 0.f}, {-1.45f, 0.06f, 0.f}}, {0.05f, 0.035f, 0.018f, 0.004f}, 6);
	Fin(m, 4, 2, [](float u, float v) { return FVector3{-0.38f - 0.1f * u, 0.07f + 0.06f * v * (1.f - u), 0.f}; }, 0.006f, {0.f, 0.f, 1.f});
	ComputeNormals(m);
	// Yeux sur les flancs de la tête.
	for (const float sd : {-1.f, 1.f})
		SmallSphere(m, {0.47f, 0.05f, sd * 0.14f}, 0.024f);
	return m;
}

sdl3::MeshData Mote() {
	sdl3::MeshData m;
	const FVector3 p[6] = {{1.f, 0.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f, -1.f}};
	for (const FVector3 &v : p)
		Vertex(m, v, v, {0.5f, 0.5f});
	const uint32_t faces[8][3] = {{0, 2, 4}, {4, 2, 1}, {1, 2, 5}, {5, 2, 0}, {4, 3, 0}, {1, 3, 4}, {5, 3, 1}, {0, 3, 5}};
	for (const auto &f : faces)
		Tri(m, f[0], f[1], f[2]);
	return m;
}

sdl3::MeshData Shaft() {
	sdl3::MeshData m;
	for (int plane = 0; plane < 2; ++plane) {
		const FVector3 side = plane == 0 ? FVector3{0.5f, 0.f, 0.f} : FVector3{0.f, 0.f, 0.5f};
		const FVector3 n = plane == 0 ? FVector3{0.f, 0.f, 1.f} : FVector3{1.f, 0.f, 0.f};
		const uint32_t a = Vertex(m, side * -1.f, n, {0.f, 0.f}), b = Vertex(m, side, n, {1.f, 0.f});
		const uint32_t c = Vertex(m, side + FVector3{0.f, -1.f, 0.f}, n, {1.f, 1.f}), d = Vertex(m, side * -1.f + FVector3{0.f, -1.f, 0.f}, n, {0.f, 1.f});
		Quad(m, a, b, c, d);
	}
	return m;
}

sdl3::MeshData Surface(float size) {
	sdl3::MeshData m;
	const int n = 32;
	for (int j = 0; j <= n; ++j)
		for (int i = 0; i <= n; ++i)
			Vertex(m, {(float(i) / float(n) - 0.5f) * size, 0.f, (float(j) / float(n) - 0.5f) * size}, {0.f, -1.f, 0.f}, {float(i), float(j)});
	for (int j = 0; j < n; ++j)
		for (int i = 0; i < n; ++i) {
			const uint32_t a = uint32_t(j * (n + 1) + i);
			Quad(m, a, a + 1, a + uint32_t(n) + 2, a + uint32_t(n) + 1);
		}
	return m;
}

sdl3::MeshData Seagrass(int blades, float height, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	for (int b = 0; b < blades; ++b) {
		const float az = rng.NextFloat() * kTau;
		const float h = height * (0.5f + 0.5f * rng.NextFloat());
		const float lean = 0.12f + 0.3f * rng.NextFloat();
		const FVector3 dir{std::cos(az), 0.f, std::sin(az)};
		const FVector3 root = dir * (0.25f * std::sqrt(rng.NextFloat()));
		std::vector<FVector3> p, s;
		std::vector<float> w;
		const int segs = 5;
		for (int i = 0; i <= segs; ++i) {
			const float u = float(i) / float(segs);
			p.push_back(root + dir * (lean * h * u * u) + FVector3{0.f, h * u, 0.f});
			s.push_back(FVector3{-dir.z, 0.f, dir.x});
			w.push_back(0.032f * (1.f - u) * (1.f + 0.15f * std::sin(u * 10.f + float(b))));
		}
		Ribbon(m, p, s, w, 0.05f, 0.05f);
	}
	return m;
}

sdl3::MeshData Anemone(int tentacles, float height, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const math::SimplexNoise n(seed);
	// Colonne charnue.
	Lathe(m, {{0.15f, 0.f}, {0.18f, height * 0.3f}, {0.16f, height * 0.7f}, {0.2f, height * 0.95f}, {0.15f, height}}, 16, 0.1f, n, 2.f);
	// Pas de disque oral modélisé : la couronne de tentacules le cache entièrement.
	// Couronne de tentacules, en 3 anneaux concentriques.
	const int rings = 3;
	for (int ring = 0; ring < rings; ++ring) {
		const float frac = float(ring) / float(rings - 1);
		const float rad = 0.06f + 0.14f * frac;
		const int count = tentacles / rings + (ring == 0 ? 1 : 0);
		const float baseY = height * (0.97f - 0.08f * frac);
		for (int i = 0; i < count; ++i) {
			const float az = float(i) / float(count) * kTau + float(ring) * 0.35f + rng.NextFloat() * 0.2f;
			const FVector3 base{std::cos(az) * rad, baseY, std::sin(az) * rad};
			const float len = height * (0.35f + 0.3f * rng.NextFloat());
			const FVector3 dir = (FVector3{std::cos(az) * 0.5f, 1.f, std::sin(az) * 0.5f}).Normalized();
			std::vector<FVector3> pts;
			std::vector<float> radii;
			for (int k = 0; k <= 3; ++k) {
				const float u = float(k) / 3.f;
				pts.push_back(base + dir * (len * u) + FVector3{std::cos(az) * 0.12f * u * u, 0.f, std::sin(az) * 0.12f * u * u});
				radii.push_back(0.016f * (1.f - 0.85f * u) + 0.002f);
			}
			Tube(m, pts, radii, 4);
		}
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData SeaWhip(float length, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const float ph = rng.NextFloat() * kTau;
	std::vector<FVector3> pts;
	std::vector<float> radii;
	const int segs = 20;
	for (int i = 0; i <= segs; ++i) {
		const float t = float(i) / float(segs);
		pts.push_back({t * length * 0.35f + std::sin(t * 3.f + ph) * 0.3f, t * length, std::cos(t * 2.f + ph) * 0.4f});
		radii.push_back(0.022f * (1.f - 0.6f * t));
	}
	Tube(m, pts, radii, 6);
	// Polypes latéraux (petits rameaux).
	for (size_t i = 1; i < pts.size(); ++i) {
		const int k = 3 + int(rng.NextFloat() * 3.f);
		for (int j = 0; j < k; ++j) {
			const float az = rng.NextFloat() * kTau;
			const FVector3 dirOut = (FVector3{std::cos(az), 0.4f * rng.NextFloat(), std::sin(az)}).Normalized();
			const FVector3 tip = pts[i] + dirOut * (0.08f + 0.08f * rng.NextFloat());
			Tube(m, {pts[i], tip}, {0.010f, 0.003f}, 3);
		}
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData MushroomCoral(float radius, uint64_t seed) {
	sdl3::MeshData m;
	const math::SimplexNoise n(seed);
	// Pied court.
	Tube(m, {{0.f, 0.f, 0.f}, {0.f, radius * 0.4f, 0.f}}, {radius * 0.15f, radius * 0.12f}, 8);
	// Chapeau : disque plissé avec lèvre enroulée.
	const int segs = 24;
	std::vector<uint32_t> rimBottom, rimTop;
	for (int i = 0; i <= segs; ++i) {
		const float az = float(i) / float(segs) * kTau;
		const float r = radius * (1.f + 0.08f * n.Noise2(std::cos(az) * 2.f, std::sin(az) * 2.f));
		const float y = radius * 0.4f + 0.025f * std::sin(az * 8.f);
		rimBottom.push_back(Vertex(m, {std::cos(az) * r, y, std::sin(az) * r}, {}, {float(i) / float(segs), 0.f}));
		rimTop.push_back(Vertex(m, {std::cos(az) * r * 0.92f, y + radius * 0.06f, std::sin(az) * r * 0.92f}, {}, {float(i) / float(segs), 1.f}));
	}
	const uint32_t cBot = Vertex(m, {0.f, radius * 0.4f, 0.f}, {}, {0.5f, 0.5f});
	const uint32_t cTop = Vertex(m, {0.f, radius * 0.46f, 0.f}, {}, {0.5f, 0.5f});
	for (size_t i = 0; i < size_t(segs); ++i) {
		Tri(m, cBot, rimBottom[i + 1], rimBottom[i]);
		Tri(m, cTop, rimTop[i], rimTop[i + 1]);
		Quad(m, rimBottom[i], rimTop[i], rimTop[i + 1], rimBottom[i + 1]);
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData SeaPen(uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	// Axe principal dressé, légèrement courbé.
	const float len = 0.9f + 0.4f * rng.NextFloat();
	std::vector<FVector3> spine;
	std::vector<float> radii;
	const int segs = 8;
	for (int i = 0; i <= segs; ++i) {
		const float u = float(i) / float(segs);
		spine.push_back({0.05f * std::sin(u * 4.f + rng.NextFloat()), u * len, 0.04f * std::cos(u * 3.f)});
		radii.push_back(0.03f * (1.f - 0.4f * u));
	}
	Tube(m, spine, radii, 5);
	// Barbules plumeuses sur les deux côtés de l'axe.
	for (size_t i = 1; i + 1 < spine.size(); ++i) {
		const FVector3 axis = (spine[i + 1] - spine[i - 1]).Normalized();
		const FVector3 side = FVector3{1.f, 0.f, 0.f}.Cross(axis).Normalized();
		const int perSide = 4;
		for (const float s : {-1.f, 1.f}) {
			for (int k = 0; k < perSide; ++k) {
				const float t = 0.15f + 0.85f * float(k) / float(perSide);
				const float reach = 0.20f * (1.f - 0.6f * t);
				const FVector3 base = spine[i];
				const FVector3 tip = base + side * (s * reach) + axis * (0.06f * (1.f - t));
				Tube(m, {base, base + (tip - base) * 0.5f + axis * 0.01f, tip}, {0.010f, 0.007f, 0.002f}, 3);
				// Filaments secondaires.
				for (int j = 1; j <= 2; ++j) {
					const float u2 = float(j) / 3.f;
					const FVector3 at = base + (tip - base) * u2;
					const FVector3 b2 = at + axis * 0.03f + side * (s * 0.02f);
					Tube(m, {at, b2}, {0.004f, 0.001f}, 2);
				}
			}
		}
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData FeatherDuster(uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const float ph = rng.NextFloat() * kTau;
	// Tube coriace.
	std::vector<FVector3> tp;
	std::vector<float> tr;
	for (int i = 0; i <= 5; ++i) {
		const float u = float(i) / 5.f;
		tp.push_back({0.03f * std::sin(u * 3.f + ph), u * 0.4f, 0.03f * std::cos(u * 2.f + ph)});
		tr.push_back(0.05f * (1.f - 0.3f * u));
	}
	Tube(m, tp, tr, 8);
	// Couronne de radioles.
	const FVector3 crownTop = tp.back();
	const int nRad = 20;
	for (int i = 0; i < nRad; ++i) {
		const float az = float(i) / float(nRad) * kTau + rng.NextFloat() * 0.1f;
		const FVector3 base = crownTop + FVector3{std::cos(az) * 0.03f, 0.f, std::sin(az) * 0.03f};
		const FVector3 tip = base + FVector3{std::cos(az) * 0.22f, 0.35f + 0.15f * rng.NextFloat(), std::sin(az) * 0.22f};
		Tube(m, {base, base + (tip - base) * 0.5f + FVector3{0.f, 0.02f, 0.f}, tip}, {0.007f, 0.005f, 0.002f}, 3);
		// Barbules latérales.
		const FVector3 axis = (tip - base).Normalized();
		const FVector3 side = FVector3{-std::sin(az), 0.f, std::cos(az)};
				for (int k = 0; k < 3; ++k) {
			const float u = 0.2f + 0.6f * float(k) / 3.f;
			const FVector3 at = base + (tip - base) * u;
			for (const float s2 : {-1.f, 1.f}) {
				const FVector3 bTip = at + side * (s2 * 0.022f) + axis * 0.018f;
				Tube(m, {at, bTip}, {0.0035f, 0.001f}, 2);
			}
		}
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData SpiralTubeWorm(uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const int segs = 40;
	const float turns = 2.2f + 0.6f * rng.NextFloat();
	const float height = 0.28f + 0.18f * rng.NextFloat();
	const float baseR = 0.055f + 0.03f * rng.NextFloat();
	const float ph = rng.NextFloat() * kTau;
	std::vector<FVector3> pts;
	std::vector<float> radii;
	for (int i = 0; i <= segs; ++i) {
		const float u = float(i) / float(segs);
		const float az = u * turns * kTau + ph;
		const float r = baseR * (1.f - 0.65f * u);
		pts.push_back({std::cos(az) * r, u * height, std::sin(az) * r});
		radii.push_back(0.030f * (1.f - 0.35f * u));
	}
	Tube(m, pts, radii, 8);
	const FVector3 top = pts.back();
	// Ouverture frangée (couronne de petites dents).
	const int frills = 12;
	for (int i = 0; i < frills; ++i) {
		const float az = float(i) / float(frills) * kTau;
		const FVector3 dir{std::cos(az), 0.2f, std::sin(az)};
		Tube(m, {top + dir * 0.020f, top + dir * 0.045f}, {0.007f, 0.002f}, 3);
	}
	ComputeNormals(m);
	return m;
}

// ════════════════════════════════════════════════════════════════════════════
// Crustacés
// ════════════════════════════════════════════════════════════════════════════

sdl3::MeshData Crab(float size, uint64_t seed) {
	sdl3::MeshData m;
	const math::SimplexNoise n(seed);
	const float k = size;
	// Carapace (nez +X) : dôme bosselé, dessous plat, plus large devant, dents sur le bord
	// antéro-latéral. Demi-largeur 0,24 m, l'origine au niveau du bord.
	const int rings = 12, segs = 28;
	std::vector<std::vector<uint32_t>> car;
	for (int r = 0; r <= rings; ++r) {
		const float th = float(r) / float(rings) * sdl3::PI_F;
		std::vector<uint32_t> ring;
		for (int q = 0; q <= segs; ++q) {
			const float az = float(q) / float(segs) * kTau;
			const FVector3 d{std::sin(th) * std::cos(az), std::cos(th), std::sin(th) * std::sin(az)};
			const float rim = SafePow(1.f - std::abs(d.y), 4.f);
			const float teeth = 0.07f * rim * std::max(0.f, d.x) * std::max(0.f, std::sin(az * 9.f));
			const float bump = 1.f + 0.05f * n.Noise3(d.x * 4.f, d.y * 4.f, d.z * 4.f) + teeth;
			FVector3 p = d * bump;
			p.x *= 0.16f * k;
			p.z *= 0.24f * k * (1.f - 0.2f * std::max(0.f, -d.x));
			p.y *= (d.y > 0.f ? 0.09f : 0.035f) * k;
			ring.push_back(Vertex(m, p, d, {float(q) / float(segs), float(r) / float(rings)}));
		}
		car.push_back(std::move(ring));
	}
	for (size_t r = 0; r < size_t(rings); ++r)
		for (size_t q = 0; q < size_t(segs); ++q)
			Quad(m, car[r][q], car[r + 1][q], car[r + 1][q + 1], car[r][q + 1]);
	// Quatre paires de pattes à quatre articles : hanche, genou haut, cheville, pointe au sol
	// (pointes : CrabFeet).
	const std::vector<FVector3> feet = CrabFeet(size);
	for (size_t f = 0; f < feet.size(); ++f) {
		const float sd = feet[f].z < 0.f ? -1.f : 1.f;
		const float i = float(f % 4);
		const float x = (0.07f - 0.065f * i) * k, rake = (0.05f - 0.035f * i) * k;
		Leg(m,
			{{x, -0.01f * k, sd * 0.19f * k}, {x + rake * 0.3f, 0.01f * k, sd * 0.27f * k}, {x + rake, 0.08f * k, sd * 0.40f * k},
			 {x + rake * 1.5f, -0.02f * k, sd * 0.50f * k}, feet[f]},
			0.022f * k, 0.006f * k);
	}
	// Pinces : bras, carpe, paume renflée, doigt fixe et doigt mobile qui se rejoignent.
	for (const float sd : {-1.f, 1.f}) {
		Leg(m, {{0.12f * k, -0.01f * k, sd * 0.13f * k}, {0.20f * k, 0.04f * k, sd * 0.22f * k}, {0.28f * k, 0.03f * k, sd * 0.235f * k}}, 0.026f * k, 0.03f * k);
		Tube(m, {{0.28f * k, 0.03f * k, sd * 0.225f * k}, {0.34f * k, 0.028f * k, sd * 0.21f * k}, {0.40f * k, 0.022f * k, sd * 0.19f * k}},
			 {0.034f * k, 0.05f * k, 0.04f * k}, 8);
		Tube(m, {{0.40f * k, 0.004f * k, sd * 0.19f * k}, {0.46f * k, -0.006f * k, sd * 0.15f * k}, {0.50f * k, 0.004f * k, sd * 0.12f * k}},
			 {0.022f * k, 0.014f * k, 0.004f * k}, 5);
		Tube(m, {{0.41f * k, 0.042f * k, sd * 0.19f * k}, {0.47f * k, 0.04f * k, sd * 0.15f * k}, {0.505f * k, 0.016f * k, sd * 0.12f * k}},
			 {0.02f * k, 0.012f * k, 0.004f * k}, 5);
	}
	// Yeux pédonculés et courtes antennes.
	for (const float sd : {-1.f, 1.f}) {
		Tube(m, {{0.14f * k, 0.06f * k, sd * 0.05f * k}, {0.17f * k, 0.11f * k, sd * 0.06f * k}}, {0.008f * k, 0.007f * k}, 4);
		SmallSphere(m, {0.17f * k, 0.115f * k, sd * 0.06f * k}, 0.016f * k);
		Tube(m, {{0.15f * k, 0.03f * k, sd * 0.02f * k}, {0.21f * k, 0.06f * k, sd * 0.04f * k}, {0.25f * k, 0.05f * k, sd * 0.07f * k}},
			 {0.004f * k, 0.003f * k, 0.001f * k}, 3);
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData Lobster(uint64_t seed) {
	sdl3::MeshData m;
	(void)seed;
	// Céphalothorax lisse (nez +X) et rostre.
	Loft(m, {{0.22f, 0.015f, 0.01f, 0.008f}, {0.19f, 0.012f, 0.035f, 0.03f}, {0.12f, 0.01f, 0.055f, 0.05f}, {0.02f, 0.008f, 0.06f, 0.055f}, {-0.06f, 0.005f, 0.055f, 0.05f}},
		 16, 2.2f);
	Tube(m, {{0.21f, 0.03f, 0.f}, {0.30f, 0.04f, 0.f}}, {0.012f, 0.002f}, 5);
	// Abdomen : six anneaux qui se chevauchent, de plus en plus fins, légèrement arqués.
	for (int i = 0; i < 6; ++i) {
		const float x0 = -0.045f - 0.046f * float(i), x1 = x0 - 0.056f, f = 1.f - 0.08f * float(i), y = -0.006f * float(i);
		Loft(m, {{x0, y, 0.044f * f, 0.042f * f}, {x0 - 0.01f, y, 0.05f * f, 0.048f * f}, {x1, y - 0.004f, 0.044f * f, 0.044f * f}}, 14, 2.4f);
	}
	// Éventail caudal : telson central et deux uropodes de chaque côté.
	for (int i = -2; i <= 2; ++i) {
		const float a = float(i) * 0.33f, root = -0.32f;
		Fin(m, 3, 4,
			[a, root](float u, float v) {
				const float len = 0.10f - 0.012f * std::abs(a) * 3.f, wide = 0.018f + 0.022f * v;
				const float x = root - len * v, z = (u - 0.5f) * 2.f * wide;
				return FVector3{x * std::cos(a) - z * std::sin(a) - root * (std::cos(a) - 1.f), -0.035f - 0.01f * v, x * std::sin(a) + z * std::cos(a) - root * std::sin(a)};
			},
			0.003f, {0.f, 1.f, 0.f});
	}
	// Pattes marcheuses (4 paires) et grandes pinces (broyeuse plus forte à droite).
	for (const float sd : {-1.f, 1.f}) {
		for (int i = 0; i < 4; ++i) {
			const float x = 0.10f - 0.045f * float(i);
			Leg(m, {{x, -0.02f, sd * 0.045f}, {x + 0.01f, 0.f, sd * 0.11f}, LobsterFeet()[size_t(sd < 0.f ? i : 4 + i)]}, 0.011f, 0.004f);
		}
		const float g = sd > 0.f ? 1.2f : 1.f;
		Leg(m, {{0.17f, -0.01f, sd * 0.04f}, {0.25f, 0.01f, sd * 0.10f}, {0.31f, 0.01f, sd * 0.12f}}, 0.018f, 0.022f);
		Tube(m, {{0.31f, 0.01f, sd * 0.12f}, {0.38f, 0.005f, sd * 0.13f}, {0.46f, 0.f, sd * 0.12f}}, {0.028f * g, 0.044f * g, 0.034f * g}, 8);
		Tube(m, {{0.46f, -0.012f, sd * 0.12f}, {0.53f, -0.014f, sd * 0.11f}, {0.58f, -0.004f, sd * 0.1f}}, {0.02f * g, 0.012f * g, 0.003f}, 5);
		Tube(m, {{0.46f, 0.014f, sd * 0.125f}, {0.535f, 0.018f, sd * 0.115f}, {0.58f, 0.004f, sd * 0.1f}}, {0.018f * g, 0.011f * g, 0.003f}, 5);
		// Antennes : longs fouets rabattus vers l'arrière ; yeux sur le front.
		Tube(m, {{0.20f, 0.03f, sd * 0.02f}, {0.30f, 0.06f, sd * 0.08f}, {0.15f, 0.10f, sd * 0.30f}, {-0.15f, 0.08f, sd * 0.40f}}, {0.006f, 0.004f, 0.002f, 0.0008f}, 3);
		Tube(m, {{0.21f, 0.025f, sd * 0.012f}, {0.27f, 0.045f, sd * 0.03f}}, {0.003f, 0.001f}, 3);
		SmallSphere(m, {0.19f, 0.048f, sd * 0.026f}, 0.012f);
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData Shrimp(uint64_t seed) {
	sdl3::MeshData m;
	(void)seed;
	// Corps translucide (nez +X, ~22 cm) : carapace puis abdomen annelé qui s'incurve vers le bas.
	Loft(m,
		 {{0.10f, 0.004f, 0.004f, 0.003f}, {0.085f, 0.004f, 0.014f, 0.010f}, {0.06f, 0.003f, 0.020f, 0.014f}, {0.02f, 0.002f, 0.021f, 0.015f},
		  {-0.01f, 0.f, 0.019f, 0.013f}, {-0.025f, -0.001f, 0.0195f, 0.0135f}, {-0.04f, -0.002f, 0.017f, 0.011f}, {-0.055f, -0.004f, 0.0165f, 0.0105f},
		  {-0.07f, -0.006f, 0.014f, 0.009f}, {-0.085f, -0.009f, 0.0125f, 0.0078f}, {-0.095f, -0.012f, 0.010f, 0.006f}, {-0.11f, -0.016f, 0.007f, 0.004f}},
		 12, 2.2f);
	Tube(m, {{0.09f, 0.012f, 0.f}, {0.135f, 0.018f, 0.f}}, {0.004f, 0.0008f}, 4); // rostre
	// Éventail caudal.
	for (int i = -2; i <= 2; ++i) {
		const float a = float(i) * 0.35f;
		Fin(m, 2, 3,
			[a](float u, float v) {
				const float len = 0.04f, wide = 0.004f + 0.007f * v;
				const float x = -len * v, z = (u - 0.5f) * 2.f * wide;
				return FVector3{-0.108f + x * std::cos(a) - z * std::sin(a), -0.017f - 0.004f * v, x * std::sin(a) + z * std::cos(a)};
			},
			0.001f, {0.f, 1.f, 0.f});
	}
	for (const float sd : {-1.f, 1.f}) {
		// Pattes marcheuses (5 paires) sous la carapace, pattes nageuses (5) sous l'abdomen :
		// toutes sous y = −0,024 (battement du mode crevette).
		for (int i = 0; i < 5; ++i) {
			const float x = 0.07f - 0.014f * float(i);
			Leg(m, {{x, -0.015f, sd * 0.008f}, {x, -0.03f, sd * 0.02f}, {x + 0.006f, -0.046f, sd * 0.025f}}, 0.0025f, 0.001f);
			const float a = -0.012f - 0.018f * float(i);
			Tube(m, {{a, -0.014f, sd * 0.006f}, {a - 0.004f, -0.036f, sd * 0.01f}}, {0.002f, 0.0008f}, 3);
		}
		// Longues antennes, antennules, yeux pédonculés.
		Tube(m, {{0.085f, 0.01f, sd * 0.006f}, {0.12f, 0.03f, sd * 0.03f}, {0.05f, 0.05f, sd * 0.12f}, {-0.08f, 0.03f, sd * 0.18f}}, {0.0015f, 0.001f, 0.0006f, 0.0003f}, 3);
		Tube(m, {{0.09f, 0.012f, sd * 0.004f}, {0.12f, 0.02f, sd * 0.01f}}, {0.001f, 0.0004f}, 3);
		Tube(m, {{0.08f, 0.012f, sd * 0.006f}, {0.088f, 0.016f, sd * 0.011f}}, {0.002f, 0.002f}, 3);
		SmallSphere(m, {0.089f, 0.017f, sd * 0.012f}, 0.0045f);
	}
	ComputeNormals(m);
	return m;
}

sdl3::MeshData Barnacles(int count, uint64_t seed) {
	sdl3::MeshData m;
	math::Rng rng(seed);
	const math::SimplexNoise n(seed);
	count = std::max(1, count);
	for (int i = 0; i < count; ++i) {
		const float az = rng.NextFloat() * kTau;
		const float r = 0.15f * std::sqrt(rng.NextFloat());
		const FVector3 base{std::cos(az) * r, 0.f, std::sin(az) * r};
		const float sz = 0.020f + 0.015f * rng.NextFloat();
		const float lean = 0.1f + 0.2f * rng.NextFloat();
		// Petit cône calcifié, aplati, à ouverture au sommet.
		sdl3::MeshData one;
		const std::vector<FVector2> prof{{sz * 0.90f, 0.f}, {sz * 0.75f, sz * 1.2f}, {sz * 0.55f, sz * 1.8f}, {sz * 0.40f, sz * 2.0f}};
		Lathe(one, prof, 10, 0.15f, n, 2.f);
		const uint32_t offset = uint32_t(m.vertices.size());
		for (auto v : one.vertices) {
			v.position.x += v.position.y * lean * std::cos(az);
			v.position.z += v.position.y * lean * std::sin(az);
			v.position += base;
			m.vertices.push_back(v);
		}
		for (auto idx : one.indices)
			m.indices.push_back(offset + idx);
	}
	ComputeNormals(m);
	return m;
}

std::vector<FVector3> CrabFeet(float size) {
	std::vector<FVector3> feet;
	for (const float sd : {-1.f, 1.f})
		for (int i = 0; i < 4; ++i) {
			const float x = (0.07f - 0.065f * float(i)) * size, rake = (0.05f - 0.035f * float(i)) * size;
			feet.push_back({x + rake * 1.7f, -0.12f * size, sd * 0.54f * size});
		}
	return feet;
}

std::vector<FVector3> LobsterFeet() {
	std::vector<FVector3> feet;
	for (const float sd : {-1.f, 1.f})
		for (int i = 0; i < 4; ++i)
			feet.push_back({0.10f - 0.045f * float(i) - 0.02f, -0.085f, sd * 0.14f});
	return feet;
}

sdl3::MeshData VentChimney(uint64_t seed) {
	sdl3::MeshData m;
	const math::SimplexNoise n(seed);
	// Empilement de troncs de cône : base large, corps rétréci, gueule évasée.
	Lathe(m, {{0.5f, 0.f}, {0.62f, 0.9f}, {0.4f, 1.7f}, {0.28f, 2.5f}, {0.3f, 3.2f}, {0.46f, 3.4f}}, 14, 0.22f, n, 1.f);
	ComputeNormals(m);
	return m;
}

sdl3::MeshData Statue(uint64_t seed) {
	sdl3::MeshData m;
	const math::SimplexNoise n(seed);
	// Piédestal évasé, colonne cannelée, épaules et tête érodées.
	Lathe(m, {{0.6f, 0.f}, {0.72f, 0.18f}, {0.56f, 0.5f}}, 16, 0.1f, n, 1.f);
	Lathe(m, {{0.4f, 0.5f}, {0.34f, 1.6f}, {0.3f, 2.2f}}, 12, 0.1f, n, 1.f);
	Lathe(m, {{0.38f, 2.2f}, {0.42f, 2.5f}, {0.28f, 2.75f}}, 12, 0.14f, n, 1.f);
	SmallSphere(m, {0.f, 3.05f, 0.f}, 0.26f);
	ComputeNormals(m);
	return m;
}

sdl3::MeshData ModelMesh(Model model, [[maybe_unused]] const Terrain &terrain) {
	const size_t m = size_t(model);
	const auto in = [m](Model first, size_t count) { return m >= size_t(first) && m < size_t(first) + count; };
	if (in(Model::Rock0, 3)) {
		const RockShape &shape = kRockShapes[m - size_t(Model::Rock0)];
		return Rock(48, shape.roughness, shape.flatten, shape.seed);
	}
	if (in(Model::Kelp0, 3)) {
		const size_t i = m - size_t(Model::Kelp0);
		return Kelp(kKelpHeights[i], int(kKelpHeights[i] * 6.f), 1.55f, 0.2f, 100 + i);
	}
	if (in(Model::Frond0, 2)) {
		const size_t i = m - size_t(Model::Frond0);
		return Kelp(kFrondHeights[i], int(kFrondHeights[i] * 8.f), 0.75f, 0.13f, 200 + i);
	}
	if (in(Model::Seagrass0, 3)) {
		const size_t i = m - size_t(Model::Seagrass0);
		return Seagrass(8 + int(i) * 3, kSeagrassHeights[i], 400 + i);
	}
	switch (model) {
	case Model::Terrain: return {}; // maillé par chunk (Terrain::BuildChunkMesh)
	case Model::Arch: return Arch(9.f, 2.4f, 11);
	case Model::Wreck: return Shipwreck();
	case Model::Fluke: return WhaleFluke();
	case Model::VentChimney: return VentChimney(52);
	case Model::Statue: return Statue(53);
	case Model::Tuft: return Tuft(12, 0.38f, 7);
	case Model::Barrel0: return BarrelSponge(0.75f, 1.6f, 12);
	case Model::Barrel1: return BarrelSponge(0.7f, 1.2f, 22);
	case Model::Tubes: return TubeSponges(5, 13);
	case Model::Coral: return BranchCoral(3, 14);
	case Model::Fan: return FanCoral(15);
	case Model::SeaWhip: return SeaWhip(1.2f, 41);
	case Model::Mushroom: return MushroomCoral(0.6f, 42);
	case Model::SeaPen: return SeaPen(43);
	case Model::TubeWorm: return SpiralTubeWorm(47);
	case Model::Barnacles: return Barnacles(9, 48);
	case Model::Anemone0: return Anemone(24, 0.35f, 44);
	case Model::Anemone1: return Anemone(36, 0.65f, 45);
	case Model::FeatherDuster: return FeatherDuster(46);
	case Model::Fish: return Fish();
	case Model::Manta: return Manta();
	case Model::Crab: return Crab(1.f, 49);
	case Model::Lobster: return Lobster(50);
	case Model::Shrimp: return Shrimp(51);
	case Model::Bubble: return sdl3::MeshData::Sphere(1.f, 14, 10);
	default: return {};
	}
}

} // namespace abyssale::meshes