#pragma once
#include "ParametricMesh.hpp"

namespace generator {



/// A plane (rectangular grid) on the xy -plane normal pointing towards z-axis.
/// @image html PlaneMesh.svg
class PlaneMesh
{
private:

	using Impl = ParametricMesh;
	Impl m_parametricMesh;

public:

	/// @param size Half of the side length in x (0) and y (1) direction.
	/// @param segments Number of subdivisions in the x (0) and y (1) direction.
	PlaneMesh(
		const math::FVector2& size = {1.0f, 1.0f},
		const math::IVec2& segments = {8, 8}
	);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_parametricMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_parametricMesh.Vertices(); }

};


}

