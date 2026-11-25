#include "generator/LinePath.hpp"


using namespace generator;


LinePath::LinePath(
	const math::FVector3& start,
	const math::FVector3& end,
	const math::FVector3& normal,
	int segments
):
	m_parametricPath{
		[start, end, normal] (float t) {
			PathVertex vertex;

			vertex.position = start + t * (end - start);
			vertex.tangent = math::Normalize(end - start);
			vertex.normal = normal;
			vertex.texCoord = t;

			return vertex;
		},
		segments
	}
{ }

