#pragma once
#include <vector>
#include <map>

#include "Edge.hpp"
#include "MeshVertex.hpp"
#include "Triangle.hpp"
#include "utils.hpp"


namespace generator {


template <typename Mesh, int Iterations>
class SubdivideMesh
{
	static_assert(Iterations > 0, "Iterations must be greater than zero!");

private:

	using Impl = SubdivideMesh<SubdivideMesh<Mesh, Iterations-1>, 1>;
	Impl m_subdivideMesh;

public:

	SubdivideMesh(Mesh mesh) :
		m_subdivideMesh{
			SubdivideMesh<Mesh, Iterations-1>{std::move(mesh)}
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_subdivideMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_subdivideMesh.Vertices(); }

};


template <typename Mesh>
class SubdivideMesh<Mesh, 0>
{
private:

	using Impl = Mesh;
	Impl m_mesh;

public:

	SubdivideMesh(Mesh mesh) :
		m_mesh{std::move(mesh)}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mesh.Vertices(); }
};



/// Subdivides each triangle to 4 parts by splitting edges.
template <typename Mesh>
class SubdivideMesh<Mesh, 1> {
public:

	class TriangleGenerator {
	public:

		bool Done() const noexcept { return m_triangles.Done(); }

		Triangle Generate() const {
			if (m_i == 0) m_triangle = m_triangles.Generate();

			if (m_i == 3) {
				return Triangle{{
					VertexFromEdge(m_triangle.vertices[0], m_triangle.vertices[1]),
					VertexFromEdge(m_triangle.vertices[1], m_triangle.vertices[2]),
					VertexFromEdge(m_triangle.vertices[2], m_triangle.vertices[0])
				}};
			}

			int j = (m_i + 1) % 3;
			int k = (m_i + 2) % 3;
			return Triangle{{
				m_triangle.vertices[m_i],
				VertexFromEdge(m_triangle.vertices[m_i], m_triangle.vertices[j]),
				VertexFromEdge(m_triangle.vertices[k], m_triangle.vertices[m_i])
			}};
		}

		void Next() {
			++m_i;
			if (m_i == 4) {
				m_i = 0;
				m_triangles.Next();
			}
		}

	private:

		const SubdivideMesh* m_mesh;

		int m_i;

		typename TriangleGeneratorType<Mesh>::Type m_triangles;

		mutable Triangle m_triangle;

		TriangleGenerator(const SubdivideMesh& mesh) :
			m_mesh{&mesh},
			m_i{0},
			m_triangles{mesh.m_mesh.Triangles()},
			m_triangle{}
		{ }

		int VertexFromEdge(int a, int b) const {
			if (a > b) std::swap(a, b);
			return static_cast<int>(m_mesh->m_vertexCache.size()) + m_mesh->m_edgeMap.at({a, b});
		}

	friend class SubdivideMesh;
	};


	class VertexGenerator {
	public:

		bool Done() const noexcept {
			return
				m_vertexIndex == m_mesh->m_vertexCache.size() &&
				m_edgeIndex == m_mesh->m_edgeCache.size();
		}

		MeshVertex Generate() const {
			if (m_vertexIndex < m_mesh->m_vertexCache.size())
				return m_mesh->m_vertexCache[m_vertexIndex];

			const MeshVertex& v1 = m_mesh->m_vertexCache[static_cast<std::size_t>(m_mesh->m_edgeCache[m_edgeIndex].vertices[0])];
			const MeshVertex& v2 = m_mesh->m_vertexCache[static_cast<std::size_t>(m_mesh->m_edgeCache[m_edgeIndex].vertices[1])];

			MeshVertex vertex;
			vertex.position = math::Lerp(v1.position, v2.position, 0.5f);
			vertex.texCoord = math::Lerp(v1.texCoord, v2.texCoord, 0.5f);
			vertex.normal = math::Normalize(math::Lerp(v1.normal, v2.normal, 0.5f));
			return vertex;
		}

		void Next() {
			if (m_vertexIndex < m_mesh->m_vertexCache.size()) ++m_vertexIndex;
			else ++m_edgeIndex;
		}

	private:

		const SubdivideMesh* m_mesh;

		std::size_t m_edgeIndex;
		std::size_t m_vertexIndex;

		VertexGenerator(const SubdivideMesh& mesh) :
			m_mesh{&mesh},
			m_edgeIndex{0},
			m_vertexIndex{0}
		{ }

	friend class SubdivideMesh;
	};


	SubdivideMesh(Mesh mesh) :
		m_mesh{std::move(mesh)}
	{
		for (const MeshVertex& vertex : m_mesh.Vertices()) {
			m_vertexCache.push_back(vertex);
		}

		for (const Triangle& triangle : m_mesh.Triangles()) {
			for (int i = 0; i < 3; ++i) {
				int j = (i + 1) % 3;

				Edge e{{triangle.vertices[i], triangle.vertices[j]}};
				if (e.vertices[0] > e.vertices[1])
					std::swap(e.vertices[0], e.vertices[1]);

				if (m_edgeMap.find(e.vertices) == m_edgeMap.end()) {
					m_edgeMap[e.vertices] = static_cast<int>(m_edgeCache.size());
					m_edgeCache.push_back(e);
				}

			}
		}
	}


	TriangleGenerator Triangles() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }

private:

	Mesh m_mesh;

	std::vector<Edge> m_edgeCache;

	std::map<math::IVec2, int> m_edgeMap;

	std::vector<MeshVertex> m_vertexCache;

};


}

