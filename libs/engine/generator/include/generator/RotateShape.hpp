#pragma once
#include "TransformShape.hpp"


namespace generator {


/// Rotates a shape around the origin on the xy-plane.
template <typename Shape>
class RotateShape
{
private:

	using Impl = TransformShape<Shape>;
	Impl m_transformShape;

public:

	/// @param shape Source data shape.
	/// @param angle Counterclockwise angle.
	RotateShape(Shape shape, float angle) :
		m_transformShape{
			std::move(shape),
			[angle] (ShapeVertex& value) {
				value.position = math::Rotate2D(value.position, angle);
				value.tangent = math::Rotate2D(value.tangent, angle);
			}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformShape.Vertices(); }

};


template <typename Shape>
RotateShape<Shape> MakeRotateShape(Shape shape, float angle) {
	return RotateShape<Shape>{std::move(shape), angle};
}

}


