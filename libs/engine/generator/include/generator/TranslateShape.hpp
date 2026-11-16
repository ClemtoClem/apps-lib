#pragma once
#include "math.hpp"

#include "TransformShape.hpp"

namespace generator {


/// Translates the position of each vertex by given amount.
template <typename Shape>
class TranslateShape
{
private:

	using Impl = TransformShape<Shape>;
	Impl m_transformShape;

public:

	/// @param shape Source data shape.
	/// @param delta Amount to increment vertex positions.
	TranslateShape(Shape shape, const math::FVector2& delta) :
		m_transformShape{
			std::move(shape),
			[delta] (ShapeVertex& value) { value.position += delta;	}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformShape.Vertices(); }

};


template <typename Shape>
TranslateShape<Shape> MakeTranslateShape(Shape shape, const math::FVector2& delta) {
	return TranslateShape<Shape>{std::move(shape), delta};
}

}

