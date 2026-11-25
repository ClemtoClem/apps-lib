#pragma once
/**
 * @file octree.hpp
 * @brief Octree statique de triangles : phase large des requêtes de sélection
 *        (rayon, boîte, sphère, point). Construit une fois, non modifiable ; les
 *        requêtes rendent les indices de triangles dont la boîte englobante
 *        répond à la requête (phase exacte triangle/rayon à faire par l'appelant).
 *
 *   math::Octree tree(vertices, triangles);
 *   std::vector<uint32_t> hits;
 *   tree.QueryRay(ray, hits);   // candidats par boîte englobante
 */
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "math.hpp"

namespace math {

class Octree {
public:
	static constexpr uint32_t kInvalid = UINT32_MAX;

	Octree() = default;
	Octree(std::span<const FVector3> vertices, std::span<const std::array<uint32_t, 3>> triangles, size_t splitThreshold = 16) {
		Build(vertices, triangles, splitThreshold);
	}

	/// Construit depuis les sommets et les triangles (indices dans `vertices`).
	void Build(std::span<const FVector3> vertices, std::span<const std::array<uint32_t, 3>> triangles, size_t splitThreshold = 16);
	[[nodiscard]] bool Empty() const noexcept { return root_ == kInvalid; }

	/// Triangles dont la boîte englobante coupe le rayon.
	void QueryRay(const FRay &ray, std::vector<uint32_t> &out) const;
	/// Triangles dont la boîte englobante coupe `box`.
	void QueryAabb(const FAABB &box, std::vector<uint32_t> &out) const;
	/// Triangles dont la boîte englobante coupe la sphère (centre, rayon).
	void QuerySphere(const FVector3 &center, float radius, std::vector<uint32_t> &out) const;
	/// Triangles dont la boîte englobante contient `p`.
	void QueryPoint(const FVector3 &p, std::vector<uint32_t> &out) const;

private:
	struct Node {
		FAABB bounds;
		bool leaf = true;
		std::vector<uint32_t> indices; ///< feuille : indices de triangles
		std::vector<FAABB> triBounds;  ///< feuille : boîte de chaque triangle (parallèle à `indices`)
		std::array<uint32_t, 8> children{kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid};
	};

	[[nodiscard]] static FAABB TriangleBounds(std::span<const FVector3> vertices, const std::array<uint32_t, 3> &t) noexcept {
		const FVector3 &a = vertices[t[0]], &b = vertices[t[1]], &c = vertices[t[2]];
		return {Min(Min(a, b), c), Max(Max(a, b), c)};
	}

	/// Chevauchement inclusif (bords compris) — un triangle plan exactement sur un plan
	/// de coupe doit appartenir aux deux octants, sinon il serait perdu.
	[[nodiscard]] static bool Overlaps(const FAABB &a, const FAABB &b) noexcept {
		return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z &&
			   a.max.z >= b.min.z;
	}

	uint32_t BuildNode(std::span<const FVector3> vertices, std::span<const std::array<uint32_t, 3>> triangles,
					   std::vector<uint32_t> indices, const FAABB &bounds, size_t threshold);
	void MakeLeaf(uint32_t id, std::vector<uint32_t> indices, std::span<const FVector3> vertices,
				  std::span<const std::array<uint32_t, 3>> triangles);
	void QueryRayRec(const FRay &ray, uint32_t node, std::vector<uint32_t> &out) const;
	void QueryAabbRec(const FAABB &box, uint32_t node, std::vector<uint32_t> &out) const;
	void QuerySphereRec(const FVector3 &center, float radius, uint32_t node, std::vector<uint32_t> &out) const;
	void QueryPointRec(const FVector3 &p, uint32_t node, std::vector<uint32_t> &out) const;

	std::vector<Node> nodes_;
	uint32_t root_ = kInvalid;
};

inline void Octree::Build(std::span<const FVector3> vertices, std::span<const std::array<uint32_t, 3>> triangles, size_t splitThreshold) {
	nodes_.clear();
	root_ = kInvalid;
	if (triangles.empty())
		return;
	std::vector<uint32_t> all(triangles.size());
	FAABB box;
	bool first = true;
	for (size_t i = 0; i < triangles.size(); ++i) {
		all[i] = uint32_t(i);
		const FAABB tb = TriangleBounds(vertices, triangles[i]);
		if (first) {
			box = tb;
			first = false;
		} else {
			box.min = Min(box.min, tb.min);
			box.max = Max(box.max, tb.max);
		}
	}
	const FVector3 eps{1e-4f}; // gonfle pour les cas limites (triangles plans)
	box.min = box.min - eps;
	box.max = box.max + eps;
	root_ = BuildNode(vertices, triangles, std::move(all), box, splitThreshold);
}

inline void Octree::MakeLeaf(uint32_t id, std::vector<uint32_t> indices, std::span<const FVector3> vertices,
							 std::span<const std::array<uint32_t, 3>> triangles) {
	nodes_[id].leaf = true;
	nodes_[id].indices = std::move(indices);
	nodes_[id].triBounds.reserve(nodes_[id].indices.size());
	for (uint32_t ti : nodes_[id].indices)
		nodes_[id].triBounds.push_back(TriangleBounds(vertices, triangles[ti]));
}

inline uint32_t Octree::BuildNode(std::span<const FVector3> vertices, std::span<const std::array<uint32_t, 3>> triangles,
								  std::vector<uint32_t> indices, const FAABB &bounds, size_t threshold) {
	nodes_.emplace_back();
	const uint32_t id = uint32_t(nodes_.size() - 1);
	nodes_[id].bounds = bounds;
	if (indices.size() <= threshold) {
		MakeLeaf(id, std::move(indices), vertices, triangles);
		return id;
	}
	const std::array<FAABB, 8> sub = bounds.Split();
	std::array<std::vector<uint32_t>, 8> lists;
	bool progressed = false;
	for (uint32_t c = 0; c < 8; ++c) {
		lists[c].reserve(indices.size() / 4 + 1);
		for (uint32_t ti : indices)
			if (Overlaps(TriangleBounds(vertices, triangles[ti]), sub[c]))
				lists[c].push_back(ti);
		if (!lists[c].empty() && lists[c].size() < indices.size())
			progressed = true;
	}
	if (!progressed) {
		// Aucun octant n'a strictement moins de triangles : la découpe n'aide pas
		// (géométrie plane ou dégénérée) — on arrête en feuille.
		MakeLeaf(id, std::move(indices), vertices, triangles);
		return id;
	}
	nodes_[id].leaf = false;
	for (uint32_t c = 0; c < 8; ++c)
		if (!lists[c].empty())
			nodes_[id].children[c] = BuildNode(vertices, triangles, std::move(lists[c]), sub[c], threshold);
	return id;
}

inline void Octree::QueryRay(const FRay &ray, std::vector<uint32_t> &out) const {
	if (root_ == kInvalid)
		return;
	float tmin = 0.f, tmax = 0.f;
	if (RayAabbIntersection(ray, nodes_[root_].bounds, tmin, tmax))
		QueryRayRec(ray, root_, out);
}

inline void Octree::QueryRayRec(const FRay &ray, uint32_t node, std::vector<uint32_t> &out) const {
	const Node &n = nodes_[node];
	if (n.leaf) {
		for (size_t i = 0; i < n.indices.size(); ++i) {
			float tmin = 0.f, tmax = 0.f;
			if (RayAabbIntersection(ray, n.triBounds[i], tmin, tmax))
				out.push_back(n.indices[i]);
		}
		return;
	}
	for (uint32_t c : n.children) {
		if (c == kInvalid)
			continue;
		float tmin = 0.f, tmax = 0.f;
		if (RayAabbIntersection(ray, nodes_[c].bounds, tmin, tmax))
			QueryRayRec(ray, c, out);
	}
}

inline void Octree::QueryAabb(const FAABB &box, std::vector<uint32_t> &out) const {
	if (root_ != kInvalid && nodes_[root_].bounds.Intersects(box))
		QueryAabbRec(box, root_, out);
}

inline void Octree::QueryAabbRec(const FAABB &box, uint32_t node, std::vector<uint32_t> &out) const {
	const Node &n = nodes_[node];
	if (n.leaf) {
		for (size_t i = 0; i < n.indices.size(); ++i)
			if (n.triBounds[i].Intersects(box))
				out.push_back(n.indices[i]);
		return;
	}
	for (uint32_t c : n.children)
		if (c != kInvalid && nodes_[c].bounds.Intersects(box))
			QueryAabbRec(box, c, out);
}

inline void Octree::QuerySphere(const FVector3 &center, float radius, std::vector<uint32_t> &out) const {
	if (root_ != kInvalid && nodes_[root_].bounds.IntersectsSphere(center, radius))
		QuerySphereRec(center, radius, root_, out);
}

inline void Octree::QuerySphereRec(const FVector3 &center, float radius, uint32_t node, std::vector<uint32_t> &out) const {
	const Node &n = nodes_[node];
	if (n.leaf) {
		for (size_t i = 0; i < n.indices.size(); ++i)
			if (n.triBounds[i].IntersectsSphere(center, radius))
				out.push_back(n.indices[i]);
		return;
	}
	for (uint32_t c : n.children)
		if (c != kInvalid && nodes_[c].bounds.IntersectsSphere(center, radius))
			QuerySphereRec(center, radius, c, out);
}

inline void Octree::QueryPoint(const FVector3 &p, std::vector<uint32_t> &out) const {
	if (root_ != kInvalid && nodes_[root_].bounds.Contains(p))
		QueryPointRec(p, root_, out);
}

inline void Octree::QueryPointRec(const FVector3 &p, uint32_t node, std::vector<uint32_t> &out) const {
	const Node &n = nodes_[node];
	if (n.leaf) {
		for (size_t i = 0; i < n.indices.size(); ++i)
			if (n.triBounds[i].Contains(p))
				out.push_back(n.indices[i]);
		return;
	}
	for (uint32_t c : n.children)
		if (c != kInvalid && nodes_[c].bounds.Contains(p))
			QueryPointRec(p, c, out);
}

} // namespace math
