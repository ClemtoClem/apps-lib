#include "physics/world.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <diagnostics/trace.hpp>

namespace physics {

namespace {

constexpr float kSkin = 1e-4f; ///< marge laissée entre un corps et ce qu'il touche
constexpr int kPasses = 4;	   ///< passes de résolution par sous-pas
constexpr float kGround = 0.7f; ///< normale assez verticale pour être un sol

bool Overlap(const math::FAABB &a, const math::FAABB &b) {
	return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

math::FAABB TriangleBounds(const math::FVector3 &a, const math::FVector3 &b, const math::FVector3 &c) {
	return {math::Min(math::Min(a, b), c), math::Max(math::Max(a, b), c)};
}

/// Triangle inutilisable : aire nulle ou sommet non fini (écrit pour que NaN échoue au test).
bool Degenerate(const math::FVector3 &a, const math::FVector3 &b, const math::FVector3 &c) { return !((b - a).Cross(c - a).LengthSq() >= 1e-14f); }

bool Finite(const math::FVector3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/// Entrée du rayon dans la boîte [lo, hi] : distance et axe de la face touchée.
std::optional<std::pair<float, int>> RayBox(const math::FRay &ray, const math::FVector3 &lo, const math::FVector3 &hi, float maxDistance) {
	float tMin = 0.f, tMax = maxDistance;
	int axis = 0;
	for (int k = 0; k < 3; ++k) {
		const float o = ray.origin[k], dir = ray.dir[k];
		if (std::abs(dir) < 1e-9f) {
			if (o < lo[k] || o > hi[k])
				return std::nullopt;
			continue;
		}
		float t0 = (lo[k] - o) / dir, t1 = (hi[k] - o) / dir;
		if (t0 > t1)
			std::swap(t0, t1);
		if (t0 > tMin)
			tMin = t0, axis = k;
		tMax = std::min(tMax, t1);
		if (tMin > tMax)
			return std::nullopt;
	}
	return std::pair{tMin, axis};
}

std::optional<float> RaySphere(const math::FRay &ray, const math::FVector3 &center, float radius) {
	const math::FVector3 m = ray.origin - center;
	const float b = m.Dot(ray.dir), c = m.LengthSq() - radius * radius;
	if (c > 0.f && b > 0.f)
		return std::nullopt;
	const float disc = b * b - c;
	if (disc < 0.f)
		return std::nullopt;
	return std::max(0.f, -b - std::sqrt(disc));
}

} // namespace

// ── Tests de contact ─────────────────────────────────────────────────────────

std::optional<math::FVector3> BoxTriangle(const math::FVector3 &center, const math::FVector3 &half, const math::FVector3 &a, const math::FVector3 &b,
										  const math::FVector3 &c) {
	// Repère centré sur la boîte.
	const math::FVector3 v[3] = {a - center, b - center, c - center};
	const math::FVector3 e[3] = {v[1] - v[0], v[2] - v[1], v[0] - v[2]};
	const math::FVector3 normal = e[0].Cross(e[1]);
	if (normal.LengthSq() < 1e-14f)
		return std::nullopt; // triangle dégénéré
	math::FVector3 axes[13];
	int count = 0;
	axes[count++] = {1.f, 0.f, 0.f};
	axes[count++] = {0.f, 1.f, 0.f};
	axes[count++] = {0.f, 0.f, 1.f};
	axes[count++] = normal;
	for (const math::FVector3 &edge : e)
		for (const math::FVector3 &box : {math::FVector3{1.f, 0.f, 0.f}, math::FVector3{0.f, 1.f, 0.f}, math::FVector3{0.f, 0.f, 1.f}}) {
			const math::FVector3 axis = edge.Cross(box);
			if (axis.LengthSq() > 1e-10f)
				axes[count++] = axis;
		}
	float best = 1e30f;
	math::FVector3 push;
	for (int i = 0; i < count; ++i) {
		const math::FVector3 axis = axes[i].Normalized();
		const float p0 = v[0].Dot(axis), p1 = v[1].Dot(axis), p2 = v[2].Dot(axis);
		const float lo = std::min({p0, p1, p2}), hi = std::max({p0, p1, p2});
		const float r = half.Dot(Abs(axis));
		if (lo > r || hi < -r)
			return std::nullopt; // axe séparateur : aucun contact
		// Sortie dans le sens +axe (la boîte passe au-delà de hi) ou -axe (en deçà de lo).
		const float plus = hi + r, minus = r - lo;
		// Préférence nette pour la normale du triangle : les arêtes d'un sol plat ne font pas
		// trébucher une boîte qui glisse dessus.
		const float bias = i == 3 ? 0.98f : 1.f;
		if (plus * bias < best) {
			best = plus * bias;
			push = axis * plus;
		}
		if (minus * bias < best) {
			best = minus * bias;
			push = axis * -minus;
		}
	}
	return push;
}

math::FVector3 ClosestOnTriangle(const math::FVector3 &p, const math::FVector3 &a, const math::FVector3 &b, const math::FVector3 &c) {
	// Régions de Voronoï du triangle (Ericson, « Real-Time Collision Detection », 5.1.5).
	const math::FVector3 ab = b - a, ac = c - a, ap = p - a;
	const float d1 = ab.Dot(ap), d2 = ac.Dot(ap);
	if (d1 <= 0.f && d2 <= 0.f)
		return a;
	const math::FVector3 bp = p - b;
	const float d3 = ab.Dot(bp), d4 = ac.Dot(bp);
	if (d3 >= 0.f && d4 <= d3)
		return b;
	const float vc = d1 * d4 - d3 * d2;
	if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f)
		return a + ab * (d1 / (d1 - d3));
	const math::FVector3 cp = p - c;
	const float d5 = ab.Dot(cp), d6 = ac.Dot(cp);
	if (d6 >= 0.f && d5 <= d6)
		return c;
	const float vb = d5 * d2 - d1 * d6;
	if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f)
		return a + ac * (d2 / (d2 - d6));
	const float va = d3 * d6 - d5 * d4;
	if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f)
		return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	const float denom = 1.f / (va + vb + vc);
	return a + ab * (vb * denom) + ac * (vc * denom);
}

std::optional<math::FVector3> SphereTriangle(const math::FVector3 &center, float radius, const math::FVector3 &a, const math::FVector3 &b,
											 const math::FVector3 &c) {
	if (Degenerate(a, b, c))
		return std::nullopt;
	const math::FVector3 d = center - ClosestOnTriangle(center, a, b, c);
	const float dist = d.Length();
	if (dist >= radius)
		return std::nullopt;
	math::FVector3 n = dist > 1e-6f ? d / dist : (b - a).Cross(c - a).Normalized(); // centre sur le triangle : sa normale
	return n * (radius - dist);
}

std::optional<std::pair<math::FVector3, float>> BodyOverlap(const Body &a, const Body &b) {
	const math::FVector3 d = b.center - a.center;
	if (a.shape == Shape::Sphere && b.shape == Shape::Sphere) {
		const float dist = d.Length(), depth = a.radius + b.radius - dist;
		if (depth <= 0.f)
			return std::nullopt;
		return std::pair{dist > 1e-6f ? d / dist : math::FVector3{0.f, 1.f, 0.f}, depth};
	}
	if (a.shape == Shape::Sphere) { // sphère / boîte : symétrique de boîte / sphère
		auto r = BodyOverlap(b, a);
		if (r)
			r->first = -r->first;
		return r;
	}
	if (b.shape == Shape::Sphere) {
		const math::FVector3 q = math::Max(a.center - a.half, math::Min(b.center, a.center + a.half));
		const math::FVector3 out = b.center - q;
		const float dist = out.Length();
		if (dist >= b.radius)
			return std::nullopt;
		if (dist > 1e-6f)
			return std::pair{out / dist, b.radius - dist};
		// Centre de la sphère dans la boîte : axe de moindre recouvrement.
	}
	const math::FVector3 overlap = a.half + b.Extent() - Abs(d);
	if (overlap.x <= 0.f || overlap.y <= 0.f || overlap.z <= 0.f)
		return std::nullopt;
	int axis = 0;
	if (overlap.y < overlap[axis])
		axis = 1;
	if (overlap.z < overlap[axis])
		axis = 2;
	math::FVector3 n{};
	n[axis] = d[axis] >= 0.f ? 1.f : -1.f;
	return std::pair{n, overlap[axis]};
}

std::optional<float> RayTriangle(const math::FRay &ray, const math::FVector3 &a, const math::FVector3 &b, const math::FVector3 &c) {
	// Möller–Trumbore.
	const math::FVector3 e1 = b - a, e2 = c - a, p = ray.dir.Cross(e2);
	const float det = e1.Dot(p);
	if (std::abs(det) < 1e-9f)
		return std::nullopt;
	const float inv = 1.f / det;
	const math::FVector3 s = ray.origin - a;
	const float u = s.Dot(p) * inv;
	if (u < 0.f || u > 1.f)
		return std::nullopt;
	const math::FVector3 q = s.Cross(e1);
	const float v = ray.dir.Dot(q) * inv;
	if (v < 0.f || u + v > 1.f)
		return std::nullopt;
	const float t = e2.Dot(q) * inv;
	return t > 0.f ? std::optional<float>(t) : std::nullopt;
}

// ── Monde ────────────────────────────────────────────────────────────────────

int64_t World::CellCoord(float v) const noexcept { return int64_t(std::floor(v / m_cell)); }

uint64_t World::CellKey(int64_t x, int64_t y, int64_t z) noexcept {
	const auto h = [](int64_t v) { return uint64_t(v) * 0x9E3779B97F4A7C15ull; };
	return h(x) ^ (h(y) << 21 | h(y) >> 43) ^ (h(z) << 42 | h(z) >> 22);
}

void World::AddTriangles(std::span<const math::FVector3> vertices, UserId user) {
	for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
		const math::FVector3 &a = vertices[i], &b = vertices[i + 1], &c = vertices[i + 2];
		if (Degenerate(a, b, c))
			continue;
		const Triangle t{a, b, c, TriangleBounds(a, b, c), user};
		m_topY = std::max(m_topY, t.bounds.max.y);
		uint32_t index;
		if (!m_freeTriangles.empty()) {
			index = m_freeTriangles.back();
			m_freeTriangles.pop_back();
			m_triangles[index] = t;
		} else {
			index = uint32_t(m_triangles.size());
			m_triangles.push_back(t);
		}
		for (int64_t x = CellCoord(t.bounds.min.x); x <= CellCoord(t.bounds.max.x); ++x)
			for (int64_t y = CellCoord(t.bounds.min.y); y <= CellCoord(t.bounds.max.y); ++y)
				for (int64_t z = CellCoord(t.bounds.min.z); z <= CellCoord(t.bounds.max.z); ++z)
					m_grid[CellKey(x, y, z)].push_back(index);
	}
	m_stamp.resize(m_triangles.size(), 0);
}

void World::RemoveTriangles(UserId user) {
	bool any = false;
	for (size_t i = 0; i < m_triangles.size(); ++i) {
		Triangle &t = m_triangles[i];
		if (!t.dead && t.user == user) {
			t.dead = true;
			m_freeTriangles.push_back(uint32_t(i));
			any = true;
		}
	}
	if (!any)
		return;
	// Grille reconstruite à partir des triangles vivants (les entrées d'un slot réutilisé
	// repartiraient sinon vers un triangle déplacé).
	m_grid.clear();
	m_topY = -1e30f;
	for (size_t i = 0; i < m_triangles.size(); ++i) {
		const Triangle &t = m_triangles[i];
		if (t.dead)
			continue;
		m_topY = std::max(m_topY, t.bounds.max.y);
		for (int64_t x = CellCoord(t.bounds.min.x); x <= CellCoord(t.bounds.max.x); ++x)
			for (int64_t y = CellCoord(t.bounds.min.y); y <= CellCoord(t.bounds.max.y); ++y)
				for (int64_t z = CellCoord(t.bounds.min.z); z <= CellCoord(t.bounds.max.z); ++z)
					m_grid[CellKey(x, y, z)].push_back(uint32_t(i));
	}
}

ColliderId World::AddMesh(std::span<const math::FVector3> localVertices, const math::FMatrix4 &transform, UserId user) {
	Mesh mesh;
	mesh.user = user;
	for (size_t i = 0; i + 2 < localVertices.size(); i += 3)
		if (!Degenerate(localVertices[i], localVertices[i + 1], localVertices[i + 2]))
			mesh.local.insert(mesh.local.end(), localVertices.begin() + std::ptrdiff_t(i), localVertices.begin() + std::ptrdiff_t(i + 3));
	if (!m_freeMeshes.empty()) {
		const ColliderId id = m_freeMeshes.back();
		m_freeMeshes.pop_back();
		m_meshes[size_t(id)] = std::move(mesh);
		SetTransform(id, transform);
		return id;
	}
	m_meshes.push_back(std::move(mesh));
	const ColliderId id = ColliderId(m_meshes.size() - 1);
	SetTransform(id, transform);
	return id;
}

void World::RemoveMesh(ColliderId id) {
	if (id < 0 || size_t(id) >= m_meshes.size() || !m_meshes[size_t(id)].enabled)
		return;
	m_meshes[size_t(id)].enabled = false;
	m_freeMeshes.push_back(id);
}

void World::SetTransform(ColliderId id, const math::FMatrix4 &transform) {
	Mesh &m = m_meshes[size_t(id)];
	if (!m.placed.empty() && std::equal(transform.Data(), transform.Data() + 16, m.transform.Data()))
		return; // inchangée (pont ECS : recopiée à chaque pas)
	m.transform = transform;
	m.placed.clear();
	m.bounds = {math::FVector3(1e30f), math::FVector3(-1e30f)};
	for (size_t i = 0; i + 2 < m.local.size(); i += 3) {
		const math::FVector3 a = transform.TransformPoint(m.local[i]), b = transform.TransformPoint(m.local[i + 1]),
							 c = transform.TransformPoint(m.local[i + 2]);
		const math::FAABB box = TriangleBounds(a, b, c);
		m.placed.push_back({a, b, c, box, m.user});
		m.bounds = {math::Min(m.bounds.min, box.min), math::Max(m.bounds.max, box.max)};
	}
}

BodyId World::AddBody(const Body &body) {
	if (!m_freeBodies.empty()) {
		const BodyId id = m_freeBodies.back();
		m_freeBodies.pop_back();
		m_bodies[size_t(id)] = body;
		return id;
	}
	m_bodies.push_back(body);
	return BodyId(m_bodies.size() - 1);
}

void World::RemoveBody(BodyId id) {
	if (id < 0 || size_t(id) >= m_bodies.size() || !m_bodies[size_t(id)].enabled)
		return;
	m_bodies[size_t(id)].enabled = false;
	m_freeBodies.push_back(id);
}

void World::Query(const math::FAABB &box, std::vector<uint32_t> &out) const {
	out.clear();
	const auto finite = [](const math::FVector3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
	if (!finite(box.min) || !finite(box.max))
		return; // boîte invalide (corps aux coordonnées non finies) : rien à toucher
	++m_query;
	// Boîte démesurée : parcourir tous les triangles coûte moins que des millions de cellules.
	const double cells = double(CellCoord(box.max.x) - CellCoord(box.min.x) + 1) * double(CellCoord(box.max.y) - CellCoord(box.min.y) + 1) *
						 double(CellCoord(box.max.z) - CellCoord(box.min.z) + 1);
	if (cells > double(1 << 22)) {
		for (uint32_t t = 0; t < uint32_t(m_triangles.size()); ++t)
			if (!m_triangles[t].dead && Overlap(box, m_triangles[t].bounds))
				out.push_back(t);
		return;
	}
	for (int64_t x = CellCoord(box.min.x); x <= CellCoord(box.max.x); ++x)
		for (int64_t y = CellCoord(box.min.y); y <= CellCoord(box.max.y); ++y)
			for (int64_t z = CellCoord(box.min.z); z <= CellCoord(box.max.z); ++z) {
				const auto it = m_grid.find(CellKey(x, y, z));
				if (it == m_grid.end())
					continue;
				for (const uint32_t t : it->second)
					if (m_stamp[t] != m_query && Overlap(box, m_triangles[t].bounds)) {
						m_stamp[t] = m_query;
						out.push_back(t);
					}
			}
}

std::optional<World::Contact> World::Deepest(const Body &body) const {
	std::optional<Contact> deepest;
	const auto test = [&](const Triangle &t) {
		const auto push = body.shape == Shape::Sphere ? SphereTriangle(body.center, body.radius, t.a, t.b, t.c) : BoxTriangle(body.center, body.half, t.a, t.b, t.c);
		// La plus forte pénétration d'abord : un mur n'est pas confondu avec le sol.
		if (push && (!deepest || push->LengthSq() > deepest->push.LengthSq()))
			deepest = Contact{*push, t.user};
	};
	const math::FAABB box = body.Bounds();
	Query(box, m_scratch);
	for (const uint32_t ti : m_scratch)
		test(m_triangles[ti]);
	for (const Mesh &m : m_meshes)
		if (m.enabled && Overlap(box, m.bounds))
			for (const Triangle &t : m.placed)
				if (Overlap(box, t.bounds))
					test(t);
	return deepest;
}

bool World::ResolveWorld(Body &body, std::vector<math::FVector3> &normals) {
	bool touched = false;
	for (int pass = 0; pass < kPasses; ++pass) {
		const auto contact = Deepest(body);
		if (!contact || !Finite(contact->push))
			break; // contact invalide : ignoré plutôt que de propager des NaN
		const float length = contact->push.Length();
		const math::FVector3 n = contact->push / std::max(length, 1e-9f);
		body.center += n * (length + kSkin);
		normals.push_back(n);
		if (std::ranges::find(body.touching, contact->user) == body.touching.end())
			body.touching.push_back(contact->user);
		touched = true;
	}
	return touched;
}

void World::Move(Body &body, const math::FVector3 &delta, std::vector<math::FVector3> &normals, float maxStep) {
	// Sous-pas : jamais plus de la moitié de la plus petite demi-dimension (pas de tunnel).
	if (maxStep <= 0.f) {
		const math::FVector3 e = body.Extent();
		maxStep = std::max(0.02f, std::min({e.x, e.y, e.z}) * 0.5f);
	}
	const int steps = std::clamp(int(std::ceil(delta.Length() / maxStep)), 1, 64);
	for (int i = 0; i < steps; ++i) {
		body.center += delta / float(steps);
		ResolveWorld(body, normals);
	}
}

void World::StepUp(Body &body, const math::FVector3 &start, const math::FVector3 &delta, std::vector<math::FVector3> &normals) {
	// Un corps posé, arrêté à l'horizontale par un obstacle bas, monte dessus.
	const math::FVector3 wanted{delta.x, 0.f, delta.z};
	const math::FVector3 done{body.center.x - start.x, 0.f, body.center.z - start.z};
	if (wanted.Length() <= 1e-4f || done.Length() >= wanted.Length() * 0.6f)
		return;
	Body trial = body;
	trial.center = start;
	std::vector<math::FVector3> trialNormals;
	// Pas fins : en redescendant, la marche doit être touchée par-dessus (pénétration verticale
	// plus faible que le recouvrement horizontal), pas par le côté.
	constexpr float kFine = 0.004f;
	Move(trial, {0.f, body.stepHeight, 0.f}, trialNormals, kFine * 4.f);
	Move(trial, wanted, trialNormals);
	trialNormals.clear();
	Move(trial, {0.f, -body.stepHeight - 0.05f, 0.f}, trialNormals, kFine);
	const math::FVector3 progress{trial.center.x - start.x, 0.f, trial.center.z - start.z};
	const bool landed = std::ranges::any_of(trialNormals, [](const math::FVector3 &n) { return n.y > kGround; });
	if (landed && progress.Length() > done.Length() + 1e-3f && trial.center.y - start.y <= body.stepHeight + 0.01f) {
		body.center = trial.center;
		body.touching = trial.touching;
		normals = std::move(trialNormals);
	}
}

void World::ResolveBodies() {
	// Balayage trié sur X : seules les paires dont les boîtes se recouvrent sont examinées
	// (quasi linéaire au lieu de n² : des milliers de corps — bancs de poissons, végétation).
	m_sweep.clear();
	for (size_t i = 0; i < m_bodies.size(); ++i)
		if (m_bodies[i].enabled)
			m_sweep.push_back(uint32_t(i));
	std::ranges::sort(m_sweep, [this](uint32_t a, uint32_t b) { return m_bodies[a].Bounds().min.x < m_bodies[b].Bounds().min.x; });
	for (size_t s = 0; s < m_sweep.size(); ++s) {
		const math::FAABB boxS = m_bodies[m_sweep[s]].Bounds();
		for (size_t t = s + 1; t < m_sweep.size(); ++t) {
			const math::FAABB boxT = m_bodies[m_sweep[t]].Bounds();
			if (boxT.min.x > boxS.max.x)
				break; // les suivants sont plus loin encore sur X
			if (boxT.min.y > boxS.max.y || boxT.max.y < boxS.min.y || boxT.min.z > boxS.max.z || boxT.max.z < boxS.min.z)
				continue;
			// Rôles dans l'ordre des rangs (normale de a vers b), comme sans balayage.
			const uint32_t i = std::min(m_sweep[s], m_sweep[t]), j = std::max(m_sweep[s], m_sweep[t]);
			Body &a = m_bodies[i], &b = m_bodies[j];
			if (!a.dynamic && !b.dynamic)
				continue;
			const auto overlap = BodyOverlap(a, b);
			if (!overlap)
				continue;
			if (std::ranges::find(a.touching, b.user) == a.touching.end())
				a.touching.push_back(b.user);
			if (std::ranges::find(b.touching, a.user) == b.touching.end())
				b.touching.push_back(a.user);
			if (a.ghost || b.ghost)
				continue; // zone traversable : contact signalé seulement
			const auto [n, depth] = *overlap; // de a vers b
			if (!Finite(n) || !std::isfinite(depth))
				continue; // corps aux coordonnées invalides : ne contamine pas son voisin
			const float ia = a.dynamic ? 1.f / std::max(a.mass, 1e-3f) : 0.f, ib = b.dynamic ? 1.f / std::max(b.mass, 1e-3f) : 0.f;
			const float total = ia + ib;
			a.center -= n * (depth * ia / total);
			b.center += n * (depth * ib / total);
			// Impulsion : la vitesse relative le long de n s'annule (avec rebond).
			const float closing = (b.velocity - a.velocity).Dot(n);
			if (closing < 0.f) {
				const float e = std::min(a.bounce, b.bounce);
				const float impulse = -(1.f + e) * closing / total;
				a.velocity -= n * (impulse * ia);
				b.velocity += n * (impulse * ib);
			}
			// Posé sur l'autre.
			if (n.y > kGround && b.dynamic)
				b.grounded = true;
			if (n.y < -kGround && a.dynamic)
				a.grounded = true;
		}
	}
}

void World::CollectEvents() {
	m_events.clear();
	// Contacts courants (triés, sans doublon) : bien plus rapide qu'un std::set
	// (pas d'allocation par nœud) quand des bancs denses produisent des milliers de contacts.
	m_nowScratch.clear();
	for (size_t i = 0; i < m_bodies.size(); ++i)
		if (m_bodies[i].enabled)
			for (const UserId u : m_bodies[i].touching)
				m_nowScratch.emplace_back(BodyId(i), u);
	std::ranges::sort(m_nowScratch);
	m_nowScratch.erase(std::unique(m_nowScratch.begin(), m_nowScratch.end()), m_nowScratch.end());

	// Contacts apparus : dans `now`, absents de `m_contacts`.
	m_began.clear();
	std::set_difference(m_nowScratch.begin(), m_nowScratch.end(), m_contacts.begin(), m_contacts.end(), std::back_inserter(m_began));
	for (const auto &[body, other] : m_began)
		m_events.push_back({m_bodies[size_t(body)].user, other, body, true});
	// Contacts terminés : dans `m_contacts`, absents de `now`.
	m_ended.clear();
	std::set_difference(m_contacts.begin(), m_contacts.end(), m_nowScratch.begin(), m_nowScratch.end(), std::back_inserter(m_ended));
	for (const auto &[body, other] : m_ended)
		m_events.push_back({m_bodies[size_t(body)].user, other, body, false});
	m_contacts = std::move(m_nowScratch);
}

void World::Step(float dt, const math::FVector3 &gravity) {
	std::vector<math::FVector3> normals;
	{
		DIAG_ZONE("physics::Step::bodies");
		for (Body &body : m_bodies) {
			body.touching.clear();
			if (!body.dynamic || !body.enabled)
				continue;
			const bool wasGrounded = body.grounded;
			body.grounded = false;
			body.velocity += gravity * (body.gravityScale * dt);
			if (body.drag > 0.f)
				body.velocity *= std::exp(-body.drag * dt);
			const math::FVector3 start = body.center, delta = body.velocity * dt;
			normals.clear();
			if (body.ghost) {
				body.center += delta;
				continue;
			}
			Move(body, delta, normals);
			if (body.stepHeight > 0.f && wasGrounded)
				StepUp(body, start, delta, normals);
			// Réponse : composante entrant dans chaque contact annulée (ou renvoyée), frottement au sol.
			for (const math::FVector3 &n : normals) {
				const float into = body.velocity.Dot(n);
				if (into < 0.f)
					body.velocity -= n * (into * (1.f + (n.y > kGround && std::abs(into) < 1.5f ? 0.f : body.bounce)));
				if (n.y > kGround)
					body.grounded = true;
			}
			if (body.grounded) {
				const float k = std::clamp(1.f - body.friction * 10.f * dt, 0.f, 1.f);
				body.velocity.x *= k;
				body.velocity.z *= k;
			}
		}
	}
	{
		DIAG_ZONE("physics::Step::pairs");
		ResolveBodies();
	}
	{
		DIAG_ZONE("physics::Step::events");
		CollectEvents();
	}
}

bool World::InsideSolid(const math::FVector3 &point) const {
	// Rayon presque vertical (légèrement incliné : il ne passe pas exactement par une arête
	// partagée, qui compterait double).
	const math::FRay ray{point, math::FVector3{0.0013f, 1.f, 0.0007f}.Normalized()};
	float top = m_topY;
	for (const Mesh &mesh : m_meshes)
		top = std::max(top, mesh.bounds.max.y);
	const float reach = top - point.y + 1.f; // jusqu'au sommet de la scène seulement
	if (reach <= 1.f || !std::isfinite(reach))
		return false; // au-dessus de tout : dehors
	const math::FVector3 end = ray.At(reach);
	std::vector<uint32_t> candidates;
	Query({math::Min(point, end), math::Max(point, end)}, candidates);
	int crossings = 0;
	for (const uint32_t ti : candidates) {
		const Triangle &t = m_triangles[ti];
		crossings += RayTriangle(ray, t.a, t.b, t.c).has_value() ? 1 : 0;
	}
	for (const Mesh &mesh : m_meshes)
		if (mesh.enabled)
			for (const Triangle &t : mesh.placed)
				crossings += RayTriangle(ray, t.a, t.b, t.c).has_value() ? 1 : 0;
	return crossings % 2 == 1;
}

bool World::Blocked(const Body &body, BodyId ignoreBody, bool dynamicBodies) const {
	if (Deepest(body) || InsideSolid(body.center))
		return true;
	for (size_t i = 0; i < m_bodies.size(); ++i) {
		const Body &other = m_bodies[i];
		if (BodyId(i) == ignoreBody || !other.enabled || other.ghost || (other.dynamic && !dynamicBodies))
			continue;
		if (BodyOverlap(body, other))
			return true;
	}
	return false;
}

std::optional<Hit> World::Raycast(const math::FRay &ray, float maxDistance, BodyId ignoreBody) const {
	RayFilter filter;
	filter.ignoreBody = ignoreBody;
	return Raycast(ray, maxDistance, filter);
}

std::optional<Hit> World::Raycast(const math::FRay &ray, float maxDistance, const RayFilter &filter) const {
	std::optional<Hit> best;
	const auto testTriangle = [&](const Triangle &t, ColliderId collider) {
		if (filter.ignoreUser != kWorld && t.user == filter.ignoreUser)
			return;
		if (const auto d = RayTriangle(ray, t.a, t.b, t.c); d && *d <= maxDistance && (!best || *d < best->distance)) {
			math::FVector3 n = (t.b - t.a).Cross(t.c - t.a).Normalized();
			if (n.Dot(ray.dir) > 0.f)
				n = -n;
			best = Hit{*d, ray.At(*d), n, t.user, -1, collider};
		}
	};
	const math::FVector3 end = ray.At(maxDistance);
	const math::FAABB segment{math::Min(ray.origin, end), math::Max(ray.origin, end)};
	std::vector<uint32_t> candidates;
	Query(segment, candidates);
	for (const uint32_t ti : candidates)
		testTriangle(m_triangles[ti], -1);
	for (size_t i = 0; i < m_meshes.size(); ++i)
		if (m_meshes[i].enabled && Overlap(segment, m_meshes[i].bounds) && RayBox(ray, m_meshes[i].bounds.min, m_meshes[i].bounds.max, maxDistance))
			for (const Triangle &t : m_meshes[i].placed)
				testTriangle(t, ColliderId(i));
	for (size_t i = 0; i < m_bodies.size() && filter.bodies; ++i) {
		const Body &b = m_bodies[i];
		if (BodyId(i) == filter.ignoreBody || b.ghost || !b.enabled || (b.dynamic && !filter.dynamicBodies) ||
			(filter.ignoreUser != kWorld && b.user == filter.ignoreUser))
			continue;
		if (b.shape == Shape::Sphere) {
			if (const auto d = RaySphere(ray, b.center, b.radius); d && *d <= maxDistance && (!best || *d < best->distance))
				best = Hit{*d, ray.At(*d), (ray.At(*d) - b.center).Normalized(), b.user, BodyId(i), -1};
		} else if (const auto box = RayBox(ray, b.center - b.half, b.center + b.half, maxDistance); box && (!best || box->first < best->distance)) {
			math::FVector3 n{};
			n[box->second] = ray.dir[box->second] > 0.f ? -1.f : 1.f;
			best = Hit{box->first, ray.At(box->first), n, b.user, BodyId(i), -1};
		}
	}
	return best;
}

} // namespace physics
