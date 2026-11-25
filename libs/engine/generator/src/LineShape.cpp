
#include "generator/LineShape.hpp"


using namespace generator;


LineShape::LineShape(
	const math::FVector2& start,
	const math::FVector2& end,
	int segments
) :
	m_parametricShape{
		[start, end] (float t) {
			ShapeVertex vertex;

			vertex.position = start + t * (end - start);
			vertex.tangent = math::Normalize(end - start);
			vertex.texCoord = t;

			return vertex;
		},
		segments
	}
{

}
