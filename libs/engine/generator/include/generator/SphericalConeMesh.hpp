#pragma once
#include "AxisFlipMesh.hpp"
#include "ConeMesh.hpp"
#include "MergeMesh.hpp"
#include "SphereMesh.hpp"
#include "TranslateMesh.hpp"


namespace generator {


/// A cone with a spherical cap centered at origin tip pointing towards z-axis.
/// Each point on the cap has equal distance from the tip.
/// @image html SphericalConeMesh.svg
class SphericalConeMesh
{
private:

	using Impl = TranslateMesh<MergeMesh<ConeMesh, AxisFlipMesh<TranslateMesh<SphereMesh>>>>;
	Impl m_translateMesh;

public:

	/// @param radius Radius of the negative z end on the xy-plane.
	/// @param size Half of the distance between cap and tip along the z-axis.
	/// @param slices Number of subdivisions around the z-axis.
	/// @param segments Number subdivisions along the z-axis.
	/// @param rings Number subdivisions in the cap.
	/// @param start Counterclockwise angle around the z-axis relative to the positive x-axis.
	/// @param sweep Counterclockwise angle around the z-axis.
	SphericalConeMesh(
		float radius = 1.0f,
		float size = 1.0f,
		int slices = 32,
		int segments = 8,
		int rings = 4,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_translateMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_translateMesh.Vertices(); }

};


}

