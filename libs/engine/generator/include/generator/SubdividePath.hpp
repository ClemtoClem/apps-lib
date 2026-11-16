#pragma once
#include "Edge.hpp"
#include "PathVertex.hpp"
#include "utils.hpp"
#include "ShapeVertex.hpp"


namespace generator {


template <typename Path>
class SubdividePath {
public:

	class EdgeGenerator {
	public:

		bool Done() const noexcept { return m_edges.Done(); }

		Edge Generate() const {
			Edge edge = m_edges.Generate();

			if (m_i % 2 == 0) return Edge{{
				edge.vertices[0],
				static_cast<int>(m_path->m_vertexCache.size()) + m_i / 2
			}};

			return Edge{{
				static_cast<int>(m_path->m_vertexCache.size()) + m_i / 2,
				edge.vertices[1]
			}};
		}

		void Next() {
			++m_i;
			if (m_i % 2 == 0) m_edges.Next();
		}

	private:

		const SubdividePath* m_path;

		typename EdgeGeneratorType<Path>::Type m_edges;

		int m_i;

		EdgeGenerator(const SubdividePath& path) :
			m_path{&path},
			m_edges{path.m_path.Edges()},
			m_i{0}
		{ }

	friend class SubdividePath;
	};


	class VertexGenerator {
	public:

		bool Done() const noexcept {
			return m_vertexIndex == m_path->m_vertexCache.size() && m_edges.Done();
		}

		PathVertex Generate() const {
			if (m_vertexIndex < m_path->m_vertexCache.size())
				return m_path->m_vertexCache[m_vertexIndex];

			const Edge edge = m_edges.Generate();
			const PathVertex& v1 = m_path->m_vertexCache[edge.vertices[0]];
			const PathVertex& v2 = m_path->m_vertexCache[edge.vertices[1]];

			PathVertex vertex;
			vertex.position = math::Lerp(v1.position, v2.position, 0.5f);
			vertex.tangent = math::Normalize(math::Lerp(v1.tangent, v2.tangent, 0.5f));
			vertex.normal = math::Normalize(math::Lerp(v1.normal, v2.normal, 0.5f));
			vertex.texCoord = 0.5f * v1.texCoord + 0.5f * v2.texCoord;
			return vertex;
		}

		void Next() {
			if (m_vertexIndex < m_path->m_vertexCache.size()) ++m_vertexIndex;
			else m_edges.Next();
		}

	private:

		const SubdividePath* m_path;

		std::size_t m_vertexIndex;

		typename EdgeGeneratorType<Path>::Type m_edges;

		VertexGenerator(const SubdividePath& path) :
			m_path{&path},
			m_vertexIndex{0},
			m_edges{path.m_path.Edges()}
		{ }

	friend class SubdividePath;
	};


	SubdividePath(Path path) :
		m_path(std::move(path)),
		m_vertexCache{}
	{
		for (const PathVertex& vertex : m_path.Vertices()) {
			m_vertexCache.push_back(vertex);
		}
	}

	EdgeGenerator Edges() const { return *this; }

	VertexGenerator Vertices() const { return *this; }

private:

	Path m_path;

	std::vector<PathVertex> m_vertexCache;

};


template <typename Path>
SubdividePath<Path> MakeSubdividePath(Path path) {
	return SubdividePath<Path>{std::move(path)};
}


}

