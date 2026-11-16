#pragma once
#include "AxisSwapMesh.hpp"
#include "CircleShape.hpp"
#include "LatheMesh.hpp"
#include "UvFlipMesh.hpp"


namespace generator {



/// A sphere of the given radius centered around the origin.
/// Subdivided around the z-axis in slices and along the z-axis in segments.
/// @image html SphereMesh.svg
class SphereMesh
{
private:

	using Impl = AxisSwapMesh<LatheMesh<CircleShape>>;
	Impl m_axisSwapMesh;

public:

	/// @param radius The radius of the sphere
	/// @param slices Subdivisions around the z-azis (longitudes).
	/// @param segments Subdivisions along the z-azis (latitudes).
	/// @param sliceStart Counterclockwise angle around the z-axis relative to x-axis.
	/// @param sliceSweep Counterclockwise angle.
	/// @param segmentStart Counterclockwise angle relative to the z-axis.
	/// @param segmentSweep Counterclockwise angle.
	SphereMesh(
		float radius = 1.0f,
		int slices = 32,
		int segments = 16,
		float sliceStart = 0.0f,
		float sliceSweep = math::Radians(360.0f),
		float segmentStart = 0.0f,
		float segmentSweep = math::Radians(180.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_axisSwapMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_axisSwapMesh.Vertices(); }

};

}

