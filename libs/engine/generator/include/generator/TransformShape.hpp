#pragma once
#include "ShapeVertex.hpp"
#include "utils.hpp"

namespace generator {



/// Apply a mutator function to each vertex.
template <typename Shape>
class TransformShape
{
private:

	using Impl = Shape;
	Impl m_shape;

public:

	class VertexGenerator {
	public:

		ShapeVertex Generate() const {
			auto temp = m_vertices.Generate();
			m_shape->m_mutate(temp);
			return temp;
		}

		bool Done() const noexcept { return m_vertices.Done(); }

		void Next() { m_vertices.Next(); }

	private:

		const TransformShape* m_shape;

		typename VertexGeneratorType<Shape>::Type  m_vertices;

		VertexGenerator(const TransformShape& shape) :
			m_shape{&shape},
			m_vertices{shape.m_shape.Vertices()}
		{ }

	friend class TransformShape;
	};

	/// @param shape Source data shape.
	/// @param mutate Callback function that gets called once per vertex.
	TransformShape(Shape shape, std::function<void(ShapeVertex&)> mutate) :
		m_shape{std::move(shape)},
		m_mutate{mutate}
	{

	}

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_shape.Edges(); }

	VertexGenerator Vertices() const noexcept { return *this; }

private:

	std::function<void(ShapeVertex&)> m_mutate;

};


template <typename Shape>
TransformShape<Shape> MakeTransformShape(
	Shape shape, std::function<void(ShapeVertex&)> mutate
) {
	return TransformShape<Shape>{std::move(shape), std::move(mutate)};
}


}


