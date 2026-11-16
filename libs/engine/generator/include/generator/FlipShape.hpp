#pragma once
#include "Edge.hpp"
#include "TransformShape.hpp"


namespace generator {


/// Flips shape direction. Reverses edges and tangents.
template <typename Shape>
class FlipShape
{
private:

	using Impl = TransformShape<Shape>;
	Impl m_transformShape;

public:

	class EdgeGenerator {
	public:

		Edge Generate() const {
			Edge edge = m_edges.Generate();
			std::swap(edge.vertices[0], edge.vertices[1]);
			return edge;
		}

		bool Done() const noexcept { return m_edges.Done(); }

		void Next() { m_edges.Next(); }

	private:

		typename EdgeGeneratorType<TransformShape<Shape>>::Type m_edges;

		EdgeGenerator(const TransformShape<Shape>& shape) :
			m_edges{shape.Edges()}
		{ }

	friend class FlipShape;
	};

	/// @param shape Source data shape.
	FlipShape(Shape shape) :
		m_transformShape{
			std::move(shape),
			[] (ShapeVertex& vertex) {
				vertex.tangent *= -1.0f;
			}
		}
	{ }

	EdgeGenerator Edges() const noexcept { return EdgeGenerator{m_transformShape}; }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformShape.Vertices(); }


};


template <typename Shape>
FlipShape<Shape> MakeFlipShape(Shape shape) {
	return FlipShape<Shape>{std::move(shape)};
}


}



