#pragma once
#include "AxisSwapMesh.hpp"
#include "LatheMesh.hpp"
#include "LineShape.hpp"
#include "UvFlipMesh.hpp"

namespace generator {



/// Cylinder centered at origin aligned along the z-axis.
/// @image html CylinderMesh.svg
class CylinderMesh
{
private:

	using Impl = AxisSwapMesh<LatheMesh<LineShape>>;
	Impl m_axisSwapMesh;

public:

	/// @param radius Radius of the cylinder along the xy-plane.
	/// @param size Half of the length of the cylinder along the z-axis.
	/// @param slices Subdivisions around the z-axis.
	/// @param segments Subdivisions along the z-axis.
	/// @param start Counterclockwise angle around the z-axis relative to the x-axis.
	/// @param sweep Counterclockwise angle around the z-axis.
	CylinderMesh(
		float radius = 1.0f,
		float size = 1.0f,
		int slices = 32,
		int segments = 8,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_axisSwapMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_axisSwapMesh.Vertices(); }

};


}

