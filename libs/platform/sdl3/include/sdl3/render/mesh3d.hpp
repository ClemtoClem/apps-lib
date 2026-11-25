#pragma once
/**
 * @file mesh3d.hpp
 * @brief Maillages indexés côté CPU et géométries procédurales (boîte, sphère, plan,
 *        cylindre, tore, nœud torique). Triangles dans le sens direct (faces avant
 *        vues de l'extérieur), normales unitaires, coordonnées de texture 0..1.
 */
#include <cstdint>
#include <span>
#include <vector>

#include <math/math.hpp>

namespace sdl3 {

/// Sommet d'un maillage 3D (position, normale, coordonnée de texture).
/// Distinct de `sdl3::Vertex` (le sommet 2D de `Renderer`).
struct MeshVertex {
	math::FVector3 position;
	math::FVector3 normal;
	math::FVector2 uv;
};
static_assert(sizeof(MeshVertex) == 32);

struct MeshData {
	std::vector<MeshVertex> vertices;
	std::vector<uint32_t> indices; // trois par triangle

	[[nodiscard]] math::FAABB Bounds() const noexcept;
	[[nodiscard]] size_t TriangleCount() const noexcept { return indices.size() / 3; }

	/// Boîte centrée de côtés `w`×`h`×`d` (faces séparées : normales franches).
	[[nodiscard]] static MeshData Box(float w, float h, float d);
	[[nodiscard]] static MeshData Cube(float size = 1.f) { return Box(size, size, size); }
	/// Sphère UV centrée.
	[[nodiscard]] static MeshData Sphere(float radius = 0.5f, int segments = 32, int rings = 16);
	/// Plan horizontal (normale +Y) de `w`×`d`, centré.
	[[nodiscard]] static MeshData Plane(float w, float d);
	/// Cylindre (cône si `radiusTop` = 0) d'axe Y, centré, avec ses disques.
	[[nodiscard]] static MeshData Cylinder(float radiusTop, float radiusBottom, float height, int segments = 32);
	/// Tore d'axe Y : rayon du cercle central `radius`, rayon du tube `tube`.
	[[nodiscard]] static MeshData Torus(float radius = 0.8f, float tube = 0.3f, int radial = 16, int tubular = 48);
	/// Nœud torique (p, q) : le tube s'enroule `p` fois autour de l'axe et `q` fois autour du tore.
	[[nodiscard]] static MeshData TorusKnot(float radius = 0.6f, float tube = 0.2f, int tubular = 128, int radial = 16, int p = 2,
											int q = 3);
};

} // namespace sdl3
