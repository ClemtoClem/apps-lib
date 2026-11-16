#include "math/polygon.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace math {

float SignedArea(std::span<const FVector2> points) noexcept {
	float area = 0.f;
	for (size_t i = 0, n = points.size(); i < n; ++i)
		area += points[i].Cross(points[(i + 1) % n]);
	return area * 0.5f;
}

bool OnPolygonBorder(std::span<const FVector2> poly, FVector2 p, float tolerance) noexcept {
	for (size_t i = 0, n = poly.size(); i < n; ++i) {
		const FVector2 a = poly[i], ab = poly[(i + 1) % n] - a;
		const float len2 = ab.LengthSq();
		const float t = len2 > 0.f ? Clamp((p - a).Dot(ab) / len2, 0.f, 1.f) : 0.f;
		if ((a + ab * t).DistanceSq(p) <= tolerance * tolerance)
			return true;
	}
	return false;
}

bool PointInPolygon(std::span<const FVector2> poly, FVector2 p) noexcept {
	bool inside = false;
	for (size_t i = 0, n = poly.size(), j = n - 1; i < n; j = i++) {
		const FVector2 a = poly[i], b = poly[j];
		if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
			inside = !inside;
	}
	return inside;
}

namespace {

using V2 = FVector2;

float Cross(V2 o, V2 a, V2 b) { return (a - o).Cross(b - o); }

bool InTriangle(V2 p, V2 a, V2 b, V2 c) {
	const float d1 = Cross(a, b, p), d2 = Cross(b, c, p), d3 = Cross(c, a, p);
	const bool neg = d1 < -1e-9f || d2 < -1e-9f || d3 < -1e-9f;
	const bool pos = d1 > 1e-9f || d2 > 1e-9f || d3 > 1e-9f;
	return !(neg && pos);
}

float Angle(V2 v) {
	const float a = std::atan2(v.y, v.x);
	return a < 0.f ? a + TAU : a;
}

/// Raccorde un trou (sens horaire) au polygone (sens trigonométrique) par un pont M→P
/// (Eberly, « Triangulation by Ear Clipping », §3).
void BridgeHole(std::vector<uint32_t> &poly, const std::vector<uint32_t> &hole, const std::vector<V2> &pts) {
	const auto at = [&](size_t i) { return pts[poly[i % poly.size()]]; };
	const size_t mIndex = size_t(std::ranges::max_element(hole, {}, [&](uint32_t i) { return pts[i].x; }) - hole.begin());
	const V2 m = pts[hole[mIndex]];

	// Rayon horizontal vers +x : arête la plus proche.
	float bestX = 1e30f;
	size_t bestEdge = SIZE_MAX;
	V2 hit{};
	for (size_t i = 0; i < poly.size(); ++i) {
		const V2 a = at(i), b = at(i + 1);
		if ((a.y > m.y) == (b.y > m.y) && a.y != m.y && b.y != m.y)
			continue;
		float x;
		if (std::fabs(b.y - a.y) < 1e-12f) {
			if (std::fabs(a.y - m.y) >= 1e-9f)
				continue;
			x = std::min(a.x, b.x);
		} else {
			const float t = (m.y - a.y) / (b.y - a.y);
			if (t < 0.f || t > 1.f)
				continue;
			x = a.x + t * (b.x - a.x);
		}
		if (x >= m.x && x < bestX) {
			bestX = x;
			bestEdge = i;
			hit = {x, m.y};
		}
	}
	size_t pIndex = 0;
	if (bestEdge == SIZE_MAX) { // repli : sommet le plus proche
		float best = 1e30f;
		for (size_t i = 0; i < poly.size(); ++i)
			if (const float d = at(i).DistanceSq(m); d < best)
				best = d, pIndex = i;
	} else {
		pIndex = at(bestEdge).x > at(bestEdge + 1).x ? bestEdge : (bestEdge + 1) % poly.size();
		const V2 p = at(pIndex);
		// Sommet réflexe dans le triangle (M, I, P) faisant le plus petit angle avec l'axe x.
		float bestCos = -2.f, bestDist = 1e30f;
		size_t candidate = SIZE_MAX;
		for (size_t i = 0; i < poly.size(); ++i) {
			const V2 r = at(i);
			if (i == pIndex || Cross(at(i + poly.size() - 1), r, at(i + 1)) > 0.f || !InTriangle(r, m, hit, p))
				continue;
			const float len = (r - m).Length();
			if (len < 1e-12f)
				continue;
			const float c = (r.x - m.x) / len;
			if (c > bestCos + 1e-6f || (std::fabs(c - bestCos) <= 1e-6f && len < bestDist))
				bestCos = c, bestDist = len, candidate = i;
		}
		if (candidate != SIZE_MAX)
			pIndex = candidate;
	}
	// Un sommet déjà utilisé par un pont figure plusieurs fois : partir de la copie dont le
	// secteur intérieur contient la direction M − P (sinon les ponts se croisent).
	const uint32_t target = poly[pIndex];
	const V2 p = pts[target];
	for (size_t k = 0; k < poly.size(); ++k) {
		if (poly[k] != target)
			continue;
		const float start = Angle(at(k + 1) - p);
		float span = Angle(at(k + poly.size() - 1) - p) - start, dir = Angle(m - p) - start;
		if (span <= 0.f)
			span += TAU;
		if (dir < 0.f)
			dir += TAU;
		if (dir <= span) {
			pIndex = k;
			break;
		}
	}
	std::vector<uint32_t> merged(poly.begin(), poly.begin() + std::ptrdiff_t(pIndex) + 1);
	for (size_t k = 0; k <= hole.size(); ++k)
		merged.push_back(hole[(mIndex + k) % hole.size()]);
	merged.push_back(poly[pIndex]);
	merged.insert(merged.end(), poly.begin() + std::ptrdiff_t(pIndex) + 1, poly.end());
	poly = std::move(merged);
}

} // namespace

bool TriangulatePolygon(std::span<const V2> outer, const std::vector<std::vector<V2>> &holes, std::vector<uint32_t> &outIndices) {
	if (outer.size() < 3)
		return false;
	std::vector<V2> pts(outer.begin(), outer.end());
	std::vector<uint32_t> poly(outer.size());
	for (size_t i = 0; i < outer.size(); ++i)
		poly[i] = uint32_t(i);
	if (SignedArea(outer) < 0.f)
		std::ranges::reverse(poly);

	// Trous : chacun dans le sens horaire, raccordés du plus à droite au plus à gauche.
	std::vector<std::vector<uint32_t>> loops;
	for (const auto &h : holes) {
		if (h.size() < 3)
			continue;
		std::vector<uint32_t> loop;
		for (const V2 &q : h) {
			loop.push_back(uint32_t(pts.size()));
			pts.push_back(q);
		}
		if (SignedArea(h) > 0.f)
			std::ranges::reverse(loop);
		loops.push_back(std::move(loop));
	}
	const auto rightmost = [&](const std::vector<uint32_t> &l) {
		float x = -1e30f;
		for (uint32_t i : l)
			x = std::max(x, pts[i].x);
		return x;
	};
	std::ranges::sort(loops, [&](const auto &a, const auto &b) { return rightmost(a) > rightmost(b); });
	for (const auto &loop : loops)
		BridgeHole(poly, loop, pts);

	// Découpe des oreilles.
	const size_t start = outIndices.size();
	size_t guard = poly.size() * poly.size() + 16;
	while (poly.size() > 3 && guard-- > 0) {
		bool clipped = false;
		const size_t n = poly.size();
		for (size_t i = 0; i < n && !clipped; ++i) {
			const uint32_t ia = poly[(i + n - 1) % n], ib = poly[i], ic = poly[(i + 1) % n];
			const V2 a = pts[ia], b = pts[ib], c = pts[ic];
			const float cross = Cross(a, b, c);
			if (std::fabs(cross) < 1e-12f) { // aligné (ou doublon de pont) : retiré sans triangle
				poly.erase(poly.begin() + std::ptrdiff_t(i));
				clipped = true;
				break;
			}
			if (cross < 0.f)
				continue; // réflexe
			// Seul un sommet réflexe peut se trouver dans une oreille d'un polygone simple.
			bool ear = true;
			for (size_t k = 0; k < n && ear; ++k) {
				const uint32_t ik = poly[k];
				if (ik == ia || ik == ib || ik == ic || Cross(pts[poly[(k + n - 1) % n]], pts[ik], pts[poly[(k + 1) % n]]) > 0.f)
					continue;
				const V2 q = pts[ik];
				if (q == a || q == b || q == c) // sommet dupliqué par un pont
					continue;
				ear = !InTriangle(q, a, b, c);
			}
			if (!ear)
				continue;
			outIndices.insert(outIndices.end(), {ia, ib, ic});
			poly.erase(poly.begin() + std::ptrdiff_t(i));
			clipped = true;
		}
		if (!clipped) { // auto-intersectant ou dégénéré : éventail de secours
			for (size_t i = 1; i + 1 < poly.size(); ++i)
				outIndices.insert(outIndices.end(), {poly[0], poly[i], poly[i + 1]});
			poly.clear();
		}
	}
	if (poly.size() == 3 && std::fabs(Cross(pts[poly[0]], pts[poly[1]], pts[poly[2]])) > 1e-12f)
		outIndices.insert(outIndices.end(), {poly[0], poly[1], poly[2]});
	return outIndices.size() > start;
}

} // namespace math
