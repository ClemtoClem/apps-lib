
#include "generator/RectangleShape.hpp"


using namespace generator;


RectangleShape::RectangleShape(
	const math::FVector2& size,
	const math::IVec2& segments
) :
	m_mergeShape{
		{{size[0], -size[1]}, size, segments[1]},
		{size, {-size[0], size[1]}, segments[0]},
		{{-size[0], size[1]}, -size, segments[1]},
		{-size, {size[0], -size[1]}, segments[0]}
	}
{ }

