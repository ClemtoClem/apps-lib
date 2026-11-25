#pragma once
#include "AxisFlipMesh.hpp"
#include "BoxMesh.hpp"
#include "CylinderMesh.hpp"
#include "MergeMesh.hpp"
#include "MirrorMesh.hpp"
#include "TranslateMesh.hpp"
#include "RotateMesh.hpp"
#include "PlaneMesh.hpp"
#include "SphericalTriangleMesh.hpp"


namespace generator {

namespace detail {


class BoxEdge
{
private:

	using Impl = TranslateMesh<CylinderMesh>;
	Impl m_translateMesh;

public:

	BoxEdge(
		const math::FVector2& position, float radius, float size,
		int slices, int segments
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_translateMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_translateMesh.Vertices(); }

};


class BoxEdges
{
private:

	using Impl = MirrorMesh<MirrorMesh<BoxEdge>>;
	Impl m_mirrorMesh;

public:

	BoxEdges(
		const math::FVector3& size, float radius,
		int slices, int segments
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mirrorMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mirrorMesh.Vertices(); }

};


class BoxCorner
{
private:

	using Impl = TranslateMesh<SphericalTriangleMesh>;
	Impl m_translateMesh;

public:

	BoxCorner(const math::FVector3& position, float radius, int slices);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_translateMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_translateMesh.Vertices(); }

};


class BoxCorners
{
private:

	using Impl = MirrorMesh<MirrorMesh<MirrorMesh<BoxCorner>>>;
	Impl m_mirrorMesh;

public:
	BoxCorners(const math::FVector3& size, float radius, int slices);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mirrorMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mirrorMesh.Vertices(); }
};



}

/// Rectangular box with rounded edges centered at origin aligned along the x, y
/// and z axis.
/// @image html RoundedBoxMesh.svg
class RoundedBoxMesh
{
private:

	using Impl = MergeMesh<
		AxisSwapMesh<detail::BoxFaces>,
		UvFlipMesh<AxisSwapMesh<detail::BoxFaces>>,
		detail::BoxFaces,
		AxisSwapMesh<detail::BoxEdges>,
		AxisSwapMesh<detail::BoxEdges>,
		detail::BoxEdges,
		detail::BoxCorners
	>;
	Impl m_mergeMesh;

public:

	/// @param radius Radius of the rounded edges.
	/// @param size Half of the side length in x (0), y (1) and z (2) direction.
	/// @param slices Number subdivions around in the rounded edges.
	/// @param segments Number of subdivisons in x (0), y (1) and z (2)
	/// direction for the flat faces.
	RoundedBoxMesh(
		float radius = 0.25f,
		const math::FVector3& size = {0.75f, 0.75f, 0.75f},
		int slices = 4,
		const math::IVec3& segments = {8, 8, 8}
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }

};


}

