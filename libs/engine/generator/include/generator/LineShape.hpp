#pragma once
#include "ParametricShape.hpp"


namespace generator {


/// A line from a point to a point.
/// @image html LineShape.svg
class LineShape
{
private:

	using Impl = ParametricShape;
	Impl m_parametricShape;

public:

	/// @param start Start position
	/// @param end End position
	/// @param segments Number of subdivisions
	LineShape(
		const math::FVector2& start = {0.0f, -1.0f},
		const math::FVector2& end = {0.0f, 1.0f},
		int segments = 8
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_parametricShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_parametricShape.Vertices(); }

};


}

