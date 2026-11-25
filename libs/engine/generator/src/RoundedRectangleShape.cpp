
#include "generator/RoundedRectangleShape.hpp"


using namespace generator;



RoundedRectangleShape::RoundedRectangleShape(
	float radius,
	const math::FVector2& size,
	int slices,
	const math::IVec2& segments
) :
	m_mergeShape{
		{{size[0]+radius, -size[1]}, {size[0]+radius, size[1]}, segments[1]},
		{{radius, slices, 0.0f, math::Radians(90.0f)}, size},
		{{size[0], size[1]+radius}, {-size[0], size[1]+radius}, segments[0]},
		{{radius, slices, math::Radians(90.0f), math::Radians(90.0f)}, {-size[0], size[1]}},
		{{-size[0]-radius, size[1]}, {-size[0]-radius, -size[1]}, segments[1]},
		{{radius, slices, math::Radians(180.0f), math::Radians(90.0f)}, -size},
		{{-size[0], -size[1]-radius}, {size[0], -size[1]-radius}, segments[0]},
		{{radius, slices, math::Radians(270.0f), math::Radians(90.0f)}, {size[0], -size[1]}}
	}
{ }

