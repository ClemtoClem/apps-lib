#pragma once
#include "PathVertex.hpp"
#include "ShapeVertex.hpp"
#include "utils.hpp"


namespace generator {



/// Converts a Shape to a Path.
/// The shape position coordinates are used as the x and y and 0 is used as z.
/// Shape tangent becomes the path tangent and z-axis becomes normal vector and
/// thus the shape normal becomes the path binormal.
template <typename Shape>
class ShapeToPath
{
private:

	using Impl = Shape;
	Impl m_shape;

public:

	class VertexGenerator {
	public:

		PathVertex Generate() const {

			ShapeVertex shapeVertex = m_vertices.Generate();

			PathVertex vertex;

			vertex.position = math::FVector3(shapeVertex.position, 0.0f);

			vertex.tangent = math::FVector3(shapeVertex.tangent, 0.0f);
			vertex.normal = math::FVector3{0.0f, 0.0f, 1.0f};

			vertex.texCoord = shapeVertex.texCoord;

			return vertex;
		}

		bool Done() const noexcept { return m_vertices.Done(); }

		void Next() { m_vertices.Next(); }

	private:

		typename VertexGeneratorType<Shape>::Type m_vertices;

		VertexGenerator(const Shape& shape) :
			m_vertices{shape.Vertices()}
		{ }

	friend class ShapeToPath;
	};

	ShapeToPath(Shape shape) :
		m_shape{std::move(shape)}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_shape.Edges(); }

	VertexGenerator Vertices() const { return VertexGenerator{m_shape}; }

};



template <typename Shape>
ShapeToPath<Shape> MakeShapeToPath(Shape shape) {
	return ShapeToPath<Shape>{std::move(shape)};
}


}



