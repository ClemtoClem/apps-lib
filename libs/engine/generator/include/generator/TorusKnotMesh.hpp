#pragma once
#include "CircleShape.hpp"
#include "ExtrudeMesh.hpp"
#include "KnotPath.hpp"

namespace generator {


/// A Circle extruded along a knot path.
/// @image html TorusKnotMesh.svg
class TorusKnotMesh
{
private:

	using Impl = ExtrudeMesh<CircleShape, KnotPath>;
	Impl m_extrudeMesh;

public:

	/// @param slices Number subdivisions around the circle.
	/// @param segments Number of subdivisions around the path.
	TorusKnotMesh(
		int p = 2,
		int q = 3,
		int slices = 8,
		int segments = 96
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_extrudeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_extrudeMesh.Vertices(); }

};

}



