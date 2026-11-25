#include "generator/CircleShape.hpp"


using namespace generator;


CircleShape::CircleShape(
	float radius,
	int segments,
	float start,
	float sweep
) :
	m_parametricShape{
		[radius, start, sweep] (float t) {
			const float angle = t * sweep + start;
			const float sine = std::sin(angle);
			const float cosine = std::cos(angle);

			ShapeVertex vertex;
			vertex.position = math::FVector2{radius * cosine, radius * sine};
			vertex.tangent = math::FVector2{-sine, cosine};
			vertex.texCoord = t;

			return vertex;
		},
		segments
	}
{ }
