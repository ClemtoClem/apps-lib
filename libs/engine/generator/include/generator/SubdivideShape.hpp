#pragma once
#include "Edge.hpp"
#include "ShapeVertex.hpp"
#include "utils.hpp"


namespace generator {


/// Cuts each edge in half
template <typename Shape>
class SubdivideShape {
public:

	class EdgeGenerator {
	public:

		bool Done() const noexcept { return m_edges.Done(); }

		Edge Generate() const {
			Edge edge = m_edges.Generate();

			if (m_i % 2 == 0) return Edge{
				edge.vertices[0],
				static_cast<int>(m_shape->m_vertexCache.size()) + m_i / 2
			};

			return Edge{
				static_cast<int>(m_shape->m_vertexCache.size()) + m_i / 2,
				edge.vertices[1]
			};
		}

		void Next() {
			++m_i;
			if (m_i % 2 == 0) m_edges.Next();
		}

	private:

		const SubdivideShape* m_shape;

		typename EdgeGeneratorType<Shape>::Type m_edges;

		int m_i;

		EdgeGenerator(const SubdivideShape& shape) :
			m_shape{&shape},
			m_edges{shape.m_shape.Edges()},
			m_i{0}
		{ }

	friend class SubdivideShape;
	};


	class VertexGenerator {
	public:

		bool Done() const noexcept {
			return m_vertexIndex == m_shape->m_vertexCache.size() && m_edges.Done();
		}

		ShapeVertex Generate() const {
			if (m_vertexIndex < m_shape->m_vertexCache.size())
				return m_shape->m_vertexCache[m_vertexIndex];

			const Edge edge = m_edges.Generate();
			const ShapeVertex& v1 = m_shape->m_vertexCache[edge.vertices[0]];
			const ShapeVertex& v2 = m_shape->m_vertexCache[edge.vertices[1]];

			ShapeVertex vertex;
			vertex.position = math::Lerp(v1.position, v2.position, 0.5f);
			vertex.tangent = math::Normalize(math::Lerp(v1.tangent, v2.tangent, 0.5f));
			vertex.texCoord = 0.5f * v1.texCoord + 0.5f * v2.texCoord;
			return vertex;
		}

		void Next() {
			if (m_vertexIndex < m_shape->m_vertexCache.size()) ++m_vertexIndex;
			else m_edges.Next();
		}

	private:

		const SubdivideShape* m_shape;

		std::size_t m_vertexIndex;

		typename EdgeGeneratorType<Shape>::Type m_edges;

		VertexGenerator(const SubdivideShape& shape) :
			m_shape{&shape},
			m_vertexIndex{0},
			m_edges{shape.m_shape.Edges()}
		{ }

	friend class SubdivideShape;
	};


	SubdivideShape(Shape shape) :
		m_shape(std::move(shape)),
		m_vertexCache{}
	{
		for (const ShapeVertex& vertex : m_shape.Vertices()) {
			m_vertexCache.push_back(vertex);
		}
	}

	EdgeGenerator Edges() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }

private:

	Shape m_shape;

	std::vector<ShapeVertex> m_vertexCache;

};


}

