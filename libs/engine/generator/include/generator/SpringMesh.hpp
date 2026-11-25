#pragma once
#include "CircleShape.hpp"
#include "ExtrudeMesh.hpp"
#include "HelixPath.hpp"

namespace generator {


/// A spring aligned along the z-axis winding counterclockwise
/// @image html SpringMesh.svg
class SpringMesh
{
private:

	using Impl = ExtrudeMesh<CircleShape, HelixPath>;
	Impl m_extrudeMesh;

public:

	/// @param minor Radius of the spring it self.
	/// @param major Radius from the z-axis
	/// @param size Half of the length along the z-axis.
	/// @param slices Subdivisions around the spring.
	/// @param segments Subdivisions along the path.
	/// @param majorStart Counterclockwise angle around the z-axis relative to the x-axis.
	/// @param majorSweep Counterclockwise angle arounf the z-axis.
	SpringMesh(
		float minor = 0.25f,
		float major = 1.0f,
		float size = 1.0f,
		int slices = 8,
		int segments = 32,
		float minorStart = 0.0f,
		float minorSweep = math::Radians(360.0f),
		float majorStart = 0.0f,
		float majorSweep = math::Radians(720.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_extrudeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_extrudeMesh.Vertices(); }

};

}



