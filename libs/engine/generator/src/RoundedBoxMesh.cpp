
#include "generator/RoundedBoxMesh.hpp"


using namespace generator;
using namespace generator::detail;




BoxEdge::BoxEdge(
	const math::FVector2& position, float radius, float size,
	int slices, int segments
) :
	m_translateMesh{
		{radius, size, slices, segments, 0.0f, math::Radians(90.0f)},
		math::FVector3{position, 0.0f}
	}
{ }


BoxEdges::BoxEdges(
	const math::FVector3& size, float radius,
	int slices, int segments
) :
	m_mirrorMesh{
		{
			{size.Xy(), radius, size[2], slices, segments},
			Axis::Y
		},
		Axis::X
	}
{ }


BoxCorner::BoxCorner(
	const math::FVector3& position, float radius, int slices
) :
	m_translateMesh{
		{
			math::FVector3{radius, 0.0f, 0.0f},
			math::FVector3{0.0f, radius, 0.0f},
			math::FVector3{0.0f, 0.0f, radius},
			slices
		},
		position
	}
{ }


BoxCorners::BoxCorners(const math::FVector3& size, float radius, int slices) :
	m_mirrorMesh{{{{size, radius, slices}, Axis::X}, Axis::Y}, Axis::Z}
{ }



RoundedBoxMesh::RoundedBoxMesh(
	float radius,
	const math::FVector3& size,
	int slices,
	const math::IVec3& segments
) :
	m_mergeMesh{
		{
			{{size[1], size[2]}, {segments[1], segments[2]}, size[0] + radius},
			Axis::Z, Axis::X, Axis::Y
		},
		{
			{
				{{size[0], size[2]}, {segments[0], segments[2]}, size[1] + radius},
				Axis::X, Axis::Z, Axis::Y
			},
			true, false
		},
		{{size[0], size[1]}, {segments[0], segments[1]}, size[2] + radius},
		{
			{{size[2], size[1], size[0]}, radius, slices, segments[0]},
			Axis::Z, Axis::Y, Axis::X
		},
		{
			{{size[0], size[2], size[1]}, radius, slices, segments[1]},
			Axis::X, Axis::Z, Axis::Y
		},
		{size, radius, slices, segments[2]},
		{size, radius, slices}
	}
{ }

