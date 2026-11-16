#pragma once
#include "TransformMesh.hpp"
#include "Triangle.hpp"


namespace generator {



/// Flips mesh inside out. Reverses triangles and normals.
template <typename Mesh>
class FlipMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	class TriangleGenerator {
	public:

		Triangle Generate() const {
			Triangle triangle = m_triangles.Generate();
			std::swap(triangle.vertices[0], triangle.vertices[2]);
			return triangle;
		}

		bool Done() const noexcept { return m_triangles.Done(); }

		void Next() { m_triangles.Next(); }

	private:

		typename TriangleGeneratorType<TransformMesh<Mesh>>::Type m_triangles;

		TriangleGenerator(const TransformMesh<Mesh>& mesh) :
			m_triangles{mesh.Triangles()}
		{ }

	friend class FlipMesh;
	};

	/// @param mesh Source data mesh.
	FlipMesh(Mesh mesh) :
		m_transformMesh{std::move(mesh), [] (MeshVertex& vertex) {
			vertex.normal *= -1.0f;
		}}
	{ }

	TriangleGenerator Triangles() const noexcept { return this->m_transformMesh; }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }


};


template <typename Mesh>
FlipMesh<Mesh> MakeFlipMesh(Mesh mesh) {
	return FlipMesh<Mesh>{std::move(mesh)};
}


}



