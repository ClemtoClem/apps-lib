#pragma once
#include "AxisSwapMesh.hpp"
#include "LatheMesh.hpp"
#include "LineShape.hpp"


namespace generator {


/// A circular disk centered at origin on the xy-plane.
/// @image html DiskMesh.svg
class DiskMesh
{
private:

	using Impl = AxisSwapMesh<LatheMesh<LineShape>>;
	Impl m_axisSwapMesh;

public:

	/// @param radius Outer radius of the disk on the xy-plane.
	/// @param innerRadius radius of the inner circle on the xy-plane.
	/// @param slices Number of subdivisions around the z-axis.
	/// @param rings Number of subdivisions along the radius.
	/// @param start Counterclockwise angle relative to the x-axis
	/// @param sweep Counterclockwise angle.
	DiskMesh(
		float radius = 1.0f,
		float innerRadius = 0.0f,
		int slices = 32,
		int rings = 4,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_axisSwapMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_axisSwapMesh.Vertices(); }

};


}

