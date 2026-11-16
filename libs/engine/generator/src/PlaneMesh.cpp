
#include "generator/PlaneMesh.hpp"


using namespace generator;


PlaneMesh::PlaneMesh(
	const math::FVector2& size,
	const math::IVec2& segments
) :
	m_parametricMesh{
		[size] (const math::FVector2& t) {
			MeshVertex vertex;
			vertex.position = math::FVector3{-size + 2.0f * t * size, 0.0f};
			vertex.normal = math::FVector3{0.0f, 0.0f, 1.0f};
			vertex.texCoord = t;
			return vertex;
		},
		segments
	}
{

}
