#pragma once
#include "TransformMesh.hpp"


namespace generator {


/// Swaps the texture coordinates axis u and v.
template <typename Mesh>
class UvSwapMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	/// @param mesh Source data mesh
	UvSwapMesh(Mesh mesh) :
		m_transformMesh{
			std::move(mesh),
			[] (MeshVertex& vertex) {
				std::swap(vertex.texCoord[0], vertex.texCoord[1]);
			}
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_transformMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

};

template <typename Mesh>
UvSwapMesh<Mesh> MakeUvSwapMesh(Mesh mesh) {
	return UvSwapMesh<Mesh>{std::move(mesh)};
}


}


