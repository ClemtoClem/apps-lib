#include "sdl3/render/mesh3d.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sdl3 {

namespace {

constexpr float kTau = 2.f * std::numbers::pi_v<float>;
constexpr float kPi = std::numbers::pi_v<float>;

math::FVector3 Mul(const math::FVector3 &a, const math::FVector3 &b) noexcept { return {a.x * b.x, a.y * b.y, a.z * b.z}; }

/// Grille (rangées × colonnes) de sommets déjà émis à partir de `base` : deux triangles par
/// cellule ; `flip` inverse le sens de parcours (paramétrisations d'orientation opposée).
void GridIndices(std::vector<uint32_t> &out, uint32_t base, int rows, int cols, bool flip = false) {
	const uint32_t stride = uint32_t(cols) + 1;
	for (int j = 1; j <= rows; ++j)
		for (int i = 1; i <= cols; ++i) {
			const uint32_t a = base + stride * uint32_t(j - 1) + uint32_t(i - 1);
			const uint32_t b = base + stride * uint32_t(j) + uint32_t(i - 1);
			const uint32_t c = base + stride * uint32_t(j) + uint32_t(i);
			const uint32_t d = base + stride * uint32_t(j - 1) + uint32_t(i);
			if (flip)
				out.insert(out.end(), {a, d, b, b, d, c});
			else
				out.insert(out.end(), {a, b, d, b, c, d});
		}
}

} // namespace

math::FAABB MeshData::Bounds() const noexcept {
	if (vertices.empty())
		return {};
	math::FAABB box{vertices[0].position, vertices[0].position};
	for (const MeshVertex &v : vertices) {
		box.min = {std::min(box.min.x, v.position.x), std::min(box.min.y, v.position.y), std::min(box.min.z, v.position.z)};
		box.max = {std::max(box.max.x, v.position.x), std::max(box.max.y, v.position.y), std::max(box.max.z, v.position.z)};
	}
	return box;
}

MeshData MeshData::Box(float w, float h, float d) {
	struct Face {
		math::FVector3 n, u, v; // u × v = n : sens direct vu de l'extérieur
	};
	static constexpr Face faces[6] = {
		{{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
		{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},	{{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
	};
	const math::FVector3 half{w * 0.5f, h * 0.5f, d * 0.5f};
	MeshData m;
	m.vertices.reserve(24);
	m.indices.reserve(36);
	for (const Face &f : faces) {
		const uint32_t base = uint32_t(m.vertices.size());
		static constexpr float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
		for (const auto &c : corners) {
			const math::FVector3 p = Mul(f.n, half) + Mul(f.u, half) * c[0] + Mul(f.v, half) * c[1];
			m.vertices.push_back({p, f.n, {(c[0] + 1.f) * 0.5f, (1.f - c[1]) * 0.5f}});
		}
		m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	return m;
}

MeshData MeshData::Sphere(float radius, int segments, int rings) {
	segments = std::max(segments, 3);
	rings = std::max(rings, 2);
	MeshData m;
	m.vertices.reserve(size_t(segments + 1) * size_t(rings + 1));
	for (int iy = 0; iy <= rings; ++iy) {
		const float v = float(iy) / float(rings);
		for (int ix = 0; ix <= segments; ++ix) {
			const float u = float(ix) / float(segments);
			const math::FVector3 n{-std::cos(u * kTau) * std::sin(v * kPi), std::cos(v * kPi), std::sin(u * kTau) * std::sin(v * kPi)};
			m.vertices.push_back({n * radius, n, {u, v}});
		}
	}
	const uint32_t stride = uint32_t(segments) + 1;
	for (int iy = 0; iy < rings; ++iy)
		for (int ix = 0; ix < segments; ++ix) {
			const uint32_t a = stride * uint32_t(iy) + uint32_t(ix) + 1, b = stride * uint32_t(iy) + uint32_t(ix);
			const uint32_t c = stride * uint32_t(iy + 1) + uint32_t(ix), d = stride * uint32_t(iy + 1) + uint32_t(ix) + 1;
			if (iy != 0) // pôles : un seul triangle par cellule
				m.indices.insert(m.indices.end(), {a, b, d});
			if (iy != rings - 1)
				m.indices.insert(m.indices.end(), {b, c, d});
		}
	return m;
}

MeshData MeshData::Plane(float w, float d) {
	MeshData m;
	const float x = w * 0.5f, z = d * 0.5f;
	m.vertices = {{{-x, 0, -z}, {0, 1, 0}, {0, 0}}, {{x, 0, -z}, {0, 1, 0}, {1, 0}}, {{x, 0, z}, {0, 1, 0}, {1, 1}}, {{-x, 0, z}, {0, 1, 0}, {0, 1}}};
	m.indices = {0, 2, 1, 0, 3, 2};
	return m;
}

MeshData MeshData::Cylinder(float radiusTop, float radiusBottom, float height, int segments) {
	segments = std::max(segments, 3);
	MeshData m;
	const float halfH = height * 0.5f;
	const float slope = (radiusBottom - radiusTop) / std::max(height, 1e-6f);
	// Flanc : deux rangées (haut, bas).
	for (int y = 0; y <= 1; ++y) {
		const float radius = y == 0 ? radiusTop : radiusBottom;
		for (int x = 0; x <= segments; ++x) {
			const float u = float(x) / float(segments), theta = u * kTau;
			const float s = std::sin(theta), c = std::cos(theta);
			m.vertices.push_back({{radius * s, y == 0 ? halfH : -halfH, radius * c}, math::FVector3{s, slope, c}.Normalized(), {u, float(y)}});
		}
	}
	GridIndices(m.indices, 0, 1, segments);
	// Disques (le sommet d'un cône n'en a pas).
	const auto cap = [&](bool top) {
		const float radius = top ? radiusTop : radiusBottom;
		if (radius <= 0.f)
			return;
		const float y = top ? halfH : -halfH;
		const math::FVector3 n{0.f, top ? 1.f : -1.f, 0.f};
		const uint32_t center = uint32_t(m.vertices.size());
		m.vertices.push_back({{0.f, y, 0.f}, n, {0.5f, 0.5f}});
		for (int x = 0; x <= segments; ++x) {
			const float theta = float(x) / float(segments) * kTau;
			const float s = std::sin(theta), c = std::cos(theta);
			m.vertices.push_back({{radius * s, y, radius * c}, n, {0.5f + 0.5f * s, 0.5f - 0.5f * c}});
		}
		for (uint32_t x = 0; x < uint32_t(segments); ++x) {
			const uint32_t p0 = center + 1 + x, p1 = p0 + 1;
			if (top)
				m.indices.insert(m.indices.end(), {center, p0, p1});
			else
				m.indices.insert(m.indices.end(), {center, p1, p0});
		}
	};
	cap(true);
	cap(false);
	return m;
}

MeshData MeshData::Torus(float radius, float tube, int radial, int tubular) {
	radial = std::max(radial, 3);
	tubular = std::max(tubular, 3);
	MeshData m;
	m.vertices.reserve(size_t(radial + 1) * size_t(tubular + 1));
	for (int j = 0; j <= radial; ++j) {
		const float v = float(j) / float(radial) * kTau;
		for (int i = 0; i <= tubular; ++i) {
			const float u = float(i) / float(tubular) * kTau;
			// Paramétrisation d'axe Z tournée de -90° autour de X : (x, y, z) → (x, z, -y), axe Y.
			const math::FVector3 p{(radius + tube * std::cos(v)) * std::cos(u), (radius + tube * std::cos(v)) * std::sin(u), tube * std::sin(v)};
			const math::FVector3 center{radius * std::cos(u), radius * std::sin(u), 0.f};
			const math::FVector3 n = (p - center).Normalized();
			m.vertices.push_back({{p.x, p.z, -p.y}, {n.x, n.z, -n.y}, {float(i) / float(tubular), float(j) / float(radial)}});
		}
	}
	GridIndices(m.indices, 0, radial, tubular, true);
	return m;
}

MeshData MeshData::TorusKnot(float radius, float tube, int tubular, int radial, int p, int q) {
	tubular = std::max(tubular, 3);
	radial = std::max(radial, 3);
	const auto curve = [&](float u) {
		const float qu = float(q) / float(p) * u, cs = std::cos(qu);
		return math::FVector3{radius * (2.f + cs) * 0.5f * std::cos(u), radius * (2.f + cs) * 0.5f * std::sin(u), radius * std::sin(qu) * 0.5f};
	};
	MeshData m;
	m.vertices.reserve(size_t(tubular + 1) * size_t(radial + 1));
	for (int i = 0; i <= tubular; ++i) {
		const float u = float(i) / float(tubular) * float(p) * kTau;
		const math::FVector3 p1 = curve(u), p2 = curve(u + 0.01f);
		const math::FVector3 t = p2 - p1;
		math::FVector3 n = p2 + p1;
		const math::FVector3 b = t.Cross(n).Normalized();
		n = b.Cross(t).Normalized();
		for (int j = 0; j <= radial; ++j) {
			const float v = float(j) / float(radial) * kTau;
			const math::FVector3 pos = p1 + n * (-tube * std::cos(v)) + b * (tube * std::sin(v));
			m.vertices.push_back({pos, (pos - p1).Normalized(), {float(i) / float(tubular), float(j) / float(radial)}});
		}
	}
	GridIndices(m.indices, 0, tubular, radial);
	return m;
}

} // namespace sdl3
