#pragma once
#include "TransformShape.hpp"


namespace generator {


/// Scales a shape keeping tangents unit length.
template <typename Shape>
class ScaleShape
{
private:

	using Impl = TransformShape<Shape>;
	Impl m_transformShape;

public:

	/// @param shape Source data shape.
	/// @param scale Scale factor.
	ScaleShape(Shape shape, const math::FVector2& scale) :
		m_transformShape{
			std::move(shape),
			[scale] (ShapeVertex& value) { 
				value.position *= scale; 
				value.tangent = math::Normalize(scale * value.tangent);
			}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformShape.Vertices(); }

};


template <typename Shape>
ScaleShape<Shape> MakeScaleShape(Shape shape, const math::FVector2& scale) {
	return ScaleShape<Shape>{std::move(shape), scale};
}

}

