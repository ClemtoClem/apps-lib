#pragma once
#include "AxisSwapMesh.hpp"
#include "CircleShape.hpp"
#include "LatheMesh.hpp"
#include "TranslateShape.hpp"


namespace generator {


/// Torus centered at origin on the xy-plane.
/// @image html TorusMesh.svg
class TorusMesh
{
private:

	using Impl = AxisSwapMesh<LatheMesh<TranslateShape<CircleShape>>>;
	Impl m_axisSwapMesh;

public:

	/// @param minor Radius of the minor (inner) ring
	/// @param major Radius of the major (outer) ring
	/// @param slices Subdivisions around the minor ring
	/// @param segments Subdivisions around the major ring
	/// @param minorStart Counterclockwise angle relative to the xy-plane.
	/// @param minorSweep Counterclockwise angle around the circle.
	/// @param majorStart Counterclockwise angle around the z-axis relative to the x-axis.
	/// @param majorSweep Counterclockwise angle around the z-axis.
	TorusMesh(
		float minor = 0.25f,
		float major = 1.0f,
		int slices = 16,
		int segments = 32,
		float minorStart = 0.0f,
		float minorSweep = math::Radians(360.0f),
		float majorStart = 0.0f,
		float majorSweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_axisSwapMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_axisSwapMesh.Vertices(); }

};


}


