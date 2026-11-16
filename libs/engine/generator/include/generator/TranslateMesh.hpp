#pragma once
#include "TransformMesh.hpp"


namespace generator {


/// Translates the position of each vertex by given amount.
template <typename Mesh>
class TranslateMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	/// @param mesh Source data mesh.
	/// @param delta Amount to increment vertex positions.
	TranslateMesh(Mesh mesh, const math::FVector3& delta) :
		m_transformMesh{
			std::move(mesh),
			[delta] (MeshVertex& value) { value.position += delta; }
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_transformMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

};


template <typename Mesh>
TranslateMesh<Mesh> MakeTranslateMesh(Mesh mesh, const math::FVector3& delta) {
	return TranslateMesh<Mesh>{std::move(mesh), delta};
}

}

