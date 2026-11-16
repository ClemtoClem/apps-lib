#pragma once
#include "AxisSwapMesh.hpp"
#include "FlipMesh.hpp"
#include "PlaneMesh.hpp"
#include "MergeMesh.hpp"
#include "TranslateMesh.hpp"
#include "PlaneMesh.hpp"
#include "UvFlipMesh.hpp"
#include "UvSwapMesh.hpp"

namespace generator {

namespace detail {


class BoxFace
{
private:

	using Impl = TranslateMesh<PlaneMesh>;
	Impl m_translateMesh;

public:

	BoxFace(const math::FVector2& size, const math::IVec2& segments, float delta);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_translateMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_translateMesh.Vertices(); }

};


class BoxFaces
{
private:

	using Impl = MergeMesh<BoxFace, UvFlipMesh<FlipMesh<BoxFace>>>;
	Impl m_mergeMesh;

public:

	BoxFaces(const math::FVector2& size, const math::IVec2& segments, float delta);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }

};


}



/// Rectangular box centered at origin aligned along the x, y and z axis.
/// @image html BoxMesh.svg
class BoxMesh
{
private:

	using Impl = MergeMesh<
		AxisSwapMesh<detail::BoxFaces>,
		UvFlipMesh<AxisSwapMesh<detail::BoxFaces>>,
		detail::BoxFaces
	>;
	Impl m_mergeMesh;

public:

	/// @param size Half of the side length in x (0), y (1) and z (2) direction.
	/// @param segments The number of segments in x (0), y (1) and z (2)
	/// directions. All should be >= 1.f If any one is zero faces in that
	/// direction are not genereted. If more than one is zero the mesh is empty.
	explicit BoxMesh(
		const math::FVector3& size = {1.0f, 1.0f, 1.0f},
		const math::IVec3& segments = {8, 8, 8}
	) noexcept;

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }

};


}

