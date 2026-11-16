#pragma once
#include "AxisSwapMesh.hpp"
#include "LatheMesh.hpp"
#include "LineShape.hpp"
#include "UvFlipMesh.hpp"


namespace generator {



/// A cone centered at origin tip pointing towards z-axis.
/// @image html ConeMesh.svg
class ConeMesh
{
private:

	using Impl = AxisSwapMesh<LatheMesh<LineShape>>;
	Impl m_axisSwapMesh;

public:

	///@param radius Radius of the negative z end on the xy-plane.
	///@param size Half of the length of the cylinder along the z-axis.
	///@param slices Number of subdivisions around the z-axis.
	///@param segments Number subdivisions along the z-axis.
	///@param start Counterclockwise angle around the z-axis relative to the x-axis.
	///@param sweep Counterclockwise angle around the z-axis.
	ConeMesh(
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

