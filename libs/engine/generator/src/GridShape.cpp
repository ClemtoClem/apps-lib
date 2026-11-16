#include "generator/GridShape.hpp"

using namespace generator;

GridShape::GridShape(
	const math::FVector2& size,
	const math::IVec2& segments, const math::IVec2& subSegments
) noexcept :
	m_mImpl{
		// Horizontal lines
		RepeatShape<LineShape>{
			LineShape{
				math::FVector2{-size[0], -size[1]}, math::FVector2{size[0], -size[1]},
				segments[0] * subSegments[0]
			},
			segments[1] < 1 ? 0 : segments[1] + 1,
			math::FVector2{0.0f, 2.0f * size[1] / static_cast<float>(std::max(segments[1], 1))}
		},
		// Vertical lines
		RepeatShape<LineShape>{
			LineShape{
				math::FVector2{-size[0], -size[1]}, math::FVector2{-size[0], size[1]},
				segments[1] * subSegments[1]
			},
			segments[0] < 1 ? 0 : segments[0] + 1,
			math::FVector2{2.0f * size[0] / static_cast<float>(std::max(segments[0], 1)), 0.0f}
		},
	}
{
	//
}


GridShape::EdgeGenerator GridShape::Edges() const noexcept
{
	return m_mImpl.Edges();
}


GridShape::VertexGenerator GridShape::Vertices() const noexcept
{
	return m_mImpl.Vertices();
}

