#pragma once
#include "FlipMesh.hpp"
#include "MergeMesh.hpp"
#include "TubeMesh.hpp"
#include "TranslateMesh.hpp"
#include "DiskMesh.hpp"


namespace generator {

namespace detail {

class TubeCap
{
private:

	using Impl = TranslateMesh<DiskMesh>;
	Impl m_translateMesh;

public:

	TubeCap(
		float radius,
		float innerRadius,
		float distance,
		int slices,
		int rings,
		float start,
		float sweep
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_translateMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_translateMesh.Vertices(); }

};

}



/// Like TubeMesh but with end caps.
/// @image html CappedTubeMesh.svg
class CappedTubeMesh
{
private:

	using Impl = MergeMesh<TubeMesh, detail::TubeCap, FlipMesh<detail::TubeCap>>;
	Impl m_mergeMesh;

public:

	/// @param radius The outer radius of the cylinder on the xy-plane.
	/// @param innerRadius The inner radius of the cylinder on the xy-plane.
	/// @param size Half of the length of the cylinder along the z-axis.
	/// @param slices Number nubdivisions around the z-axis.
	/// @param segments Number of subdivisions along the z-axis.
	/// @param rings Number radial subdivisions in the cap.
	/// @param start Counterclockwise angle around the z-axis relative to the x-axis.
	/// @param sweep Counterclockwise angle around the z-axis.
	CappedTubeMesh(
		float radius = 1.0f,
		float innerRadius = 0.75f,
		float size = 1.0f,
		int slices = 32,
		int segments = 8,
		int rings = 1,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }

};


}

