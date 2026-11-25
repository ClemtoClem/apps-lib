#pragma once
#include "TransformMesh.hpp"


namespace generator {


/// Flips texture coordinate axis directions.
template <typename Mesh>
class UvFlipMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	/// @param mesh Source data mesh.
	/// @param u Flip u
	/// @param v Flip v
	UvFlipMesh(Mesh mesh, bool u, bool v) :
		m_transformMesh{
			std::move(mesh),
			[u, v] (MeshVertex& vertex) {
				if (u) vertex.texCoord[0] = 1.0f - vertex.texCoord[0];
				if (v) vertex.texCoord[1] = 1.0f - vertex.texCoord[1];
			}
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_transformMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

};


template <typename Mesh>
UvFlipMesh<Mesh> MakeUvFlipMesh(Mesh mesh, bool u = true, bool v = false) {
	return UvFlipMesh<Mesh>{std::move(mesh), u, v};
}


}


