#pragma once
#include "ParametricShape.hpp"

namespace generator {


/// A circle centered at origin.
/// @image html CircleShape.svg
class CircleShape
{
private:

	using Impl = ParametricShape;
	Impl m_parametricShape;

public:

	/// @param radius Radius of the circle
	/// @param segments Number of subdivisions around the circle.
	/// @param start Counterclockwise angle relative to x-axis.
	/// @param sweep Counterclockwise angle.
	CircleShape(
		float radius = 1.0f,
		int segments = 32,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_parametricShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_parametricShape.Vertices(); }

};

}

