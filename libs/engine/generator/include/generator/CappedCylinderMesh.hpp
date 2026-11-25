#pragma once
#include "FlipMesh.hpp"
#include "LatheMesh.hpp"
#include "LineShape.hpp"
#include "MergeMesh.hpp"
#include "CylinderMesh.hpp"
#include "TranslateMesh.hpp"
#include "DiskMesh.hpp"
#include "RotateMesh.hpp"
#include "UvFlipMesh.hpp"


namespace generator {

namespace detail {

class Cap
{
private:

	using Impl = TranslateMesh<DiskMesh>;
	Impl m_translateMesh;

public:

	Cap(
		float radius,
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



/// Like CylinderMesh but with end caps.
/// @image html CappedCylinderMesh.svg
class CappedCylinderMesh
{
private:

	using Impl = MergeMesh<
		CylinderMesh,
		detail::Cap,
		UvFlipMesh<FlipMesh<detail::Cap>>
	>;
	Impl m_mergeMesh;

public:

	/// @param radius Radius of the cylinder along the xy-plane.
	/// @param size Half of the length cylinder along the z-axis.
	/// @param slices Number of subdivisions around the z-axis.
	/// @param segments Number of subdivisions along the z-axis.
	/// @param rings Number of subdivisions on the caps.
	/// @param start Counterclockwise angle around the z-axis relative to x-axis.
	/// @param sweep Counterclockwise angle around the z-axis.
	CappedCylinderMesh(
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

