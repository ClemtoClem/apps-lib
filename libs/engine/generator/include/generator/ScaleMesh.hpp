#pragma once
#include "TransformMesh.hpp"


namespace generator {


/// Scales a mesh (keeps normals unit length)
template <typename Mesh>
class ScaleMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	/// @param mesh Source data mesh.
	/// @param scale Scale factors. Must not be zero!
	ScaleMesh(Mesh mesh, const math::FVector3& scale) :
		m_transformMesh{
			std::move(mesh),
			[scale] (MeshVertex& value) {
				value.position *= scale;
				value.normal = math::Normalize(scale * value.normal);
			}
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_transformMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

};


template <typename Mesh>
ScaleMesh<Mesh> MakeScaleMesh(Mesh mesh, const math::FVector3& delta) {
	return ScaleMesh<Mesh>{std::move(mesh), delta};
}

}

