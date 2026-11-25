#pragma once
#include "TransformMesh.hpp"
#include "Triangle.hpp"

namespace generator {


/// Flips (mirrors) the mesh along one or more axis.
/// Texture coordinates are not flipped.
/// Also reverses triangle vertex order if needed.
template <typename Mesh>
class AxisFlipMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	class TriangleGenerator {
	public:

		Triangle Generate() const {
			Triangle triangle = m_triangles.Generate();
			if (m_mesh->m_flip)
				std::swap(triangle.vertices[0], triangle.vertices[2]);
			return triangle;
		}

		bool Done() const noexcept { return m_triangles.Done(); }

		void Next() { m_triangles.Next(); }

	private:

		const AxisFlipMesh* m_mesh;

		typename TriangleGeneratorType<TransformMesh<Mesh>>::Type m_triangles;

		TriangleGenerator(const AxisFlipMesh& mesh) :
			m_mesh{&mesh},
			m_triangles{mesh.m_transformMesh.Triangles()}
		{ }

	friend class AxisFlipMesh;
	};

	///@param mesh Source data mesh.
	///@param x Flip x
	///@param y Flip y
	///@param z Flip z
	AxisFlipMesh(Mesh mesh, bool x, bool y, bool z) :
		m_transformMesh{
			std::move(mesh),
			[x, y, z] (MeshVertex& vertex) {
				if (x) {
					vertex.position[0] *= -1.0f;
					vertex.normal[0] *= -1.0f;
				}

				if (y) {
					vertex.position[1] *= -1.0f;
					vertex.normal[1] *= -1.0f;
				}

				if (z) {
					vertex.position[2] *= -1.0f;
					vertex.normal[2] *= -1.0f;
				}
			}
		},
		m_flip{false}
	{
		if (x) m_flip = !m_flip;
		if (y) m_flip = !m_flip;
		if (z) m_flip = !m_flip;
	}

	TriangleGenerator Triangles() const noexcept { return {*this}; }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

private:

	bool m_flip;

};


template <typename Mesh>
AxisFlipMesh<Mesh> MakeAxisFlipMesh(Mesh mesh, bool x, bool y, bool z) {
	return AxisFlipMesh<Mesh>{std::move(mesh), x, y, z};
}



}



