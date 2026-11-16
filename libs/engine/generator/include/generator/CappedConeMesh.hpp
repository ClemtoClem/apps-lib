#pragma once
#include "ConeMesh.hpp"
#include "DiskMesh.hpp"
#include "MergeMesh.hpp"
#include "FlipMesh.hpp"
#include "TranslateMesh.hpp"
#include "UvFlipMesh.hpp"


namespace generator {



/// A cone with a cap centered at origin pointing towards positive z-axis.
/// @image html CappedConeMesh.svg
class CappedConeMesh
{
private:

	using Impl = MergeMesh<
		ConeMesh,
		UvFlipMesh<FlipMesh<TranslateMesh<DiskMesh>>>
	>;
	Impl m_mergeMesh;

public:

	/// @param radius Radius of the flat (negative z) end along the xy-plane.
	/// @param size Half of the length of the cylinder along the z-axis.
	/// @param slices Number of subdivisions around the z-axis.
	/// @param segments Number of subdivisions along the z-axis.
	/// @param rings Number of subdivisions of the cap.
	/// @param start Counterclockwise angle around the z-axis relative to the
	/// positive x-axis.
	/// @param sweep Counterclockwise angle around the z-axis.
	CappedConeMesh(
		float radius = 1.0f,
		float size = 1.0f,
		int slices = 32,
		int segments = 8,
		int rings = 4,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }

};


}

