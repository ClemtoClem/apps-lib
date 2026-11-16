
#include "generator/BoxMesh.hpp"


using namespace generator;

using namespace generator::detail;




BoxFace::BoxFace(
	const math::FVector2& size, const math::IVec2& segments, float delta
) :
	m_translateMesh{{size, segments}, {0.0f, 0.0f, delta}}
{ }


BoxFaces::BoxFaces(
	const math::FVector2& size, const math::IVec2& segments, float delta
) :
	m_mergeMesh{
		{size, segments, delta},
		{{{size, segments, -delta}}, true, false}
	}
{ }



BoxMesh::BoxMesh(const math::FVector3& size, const math::IVec3& segments) noexcept :
	m_mergeMesh{
		{
			{{size[1], size[2]}, {segments[1], segments[2]}, size[0]},
			Axis::Z, Axis::X, Axis::Y
		},
		{
			{
				{{size[0], size[2]}, {segments[0], segments[2]}, size[1]},
				Axis::X, Axis::Z, Axis::Y
			},
			true, false
		},
		{{size[0], size[1]}, {segments[0], segments[1]}, size[2]}
	}
{ }

