#pragma once
#include "CylinderMesh.hpp"
#include "MergeMesh.hpp"
#include "SphereMesh.hpp"
#include "TranslateMesh.hpp"


namespace generator {


/// Capsule (cylinder with spherical caps) centered at origin aligned along z-axis.
/// @image html CapsuleMesh.svg
class CapsuleMesh
{
private:

	using Impl = MergeMesh<
		CylinderMesh,
		TranslateMesh<SphereMesh>,
		TranslateMesh<SphereMesh>
	>;
	Impl m_mergeMesh;

public:

	/// @param radius Radius of the capsule on the xy-plane.
	/// @param size Half of the length between centers of the caps along the z-axis.
	/// @param slices Number of subdivisions around the z-axis.
	/// @param rings Number of radial subdivisions in the caps.
	/// @param start Counterclockwise angle relative to the x-axis.
	/// @param sweep Counterclockwise angle.
	CapsuleMesh(
		float radius = 1.0f,
		float size = 0.5f,
		int slices = 32,
		int segments = 4,
		int rings = 8,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }


};


}

