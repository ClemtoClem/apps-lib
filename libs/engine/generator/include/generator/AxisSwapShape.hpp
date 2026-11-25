#pragma once
#include "TransformShape.hpp"

namespace generator {


/// Swaps the x and y axis.
template <typename Shape>
class AxisSwapShape
{
private:

	using Impl = TransformShape<Shape>;
	Impl m_transformShape;

public:

	/// @param shape Source data shape.
	AxisSwapShape(Shape shape) :
		m_transformShape{
			std::move(shape),
			[] (ShapeVertex& vertex) {
				std::swap(vertex.position[0u], vertex.position[1u]);
				std::swap(vertex.tangent[0u], vertex.tangent[1u]);
			}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformShape.Vertices(); }

};


template <typename Shape>
AxisSwapShape<Shape> MakeAxisSwapShape(Shape shape) {
	return AxisSwapShape<Shape>{std::move(shape)};
}


}


