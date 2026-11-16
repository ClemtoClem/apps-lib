#ifndef SPHERIFYMESH_HPP
#define SPHERIFYMESH_HPP

#include "TransformMesh.hpp"


namespace generator {


/// Projects vertices on a sphere centered at origin.
template <typename Mesh>
class SpherifyMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	SpherifyMesh(Mesh mesh, float radius, float factor) :
		m_transformMesh{
			std::move(mesh),
			[radius, factor] (MeshVertex& value) {
				 value.position = math::Lerp(
					value.position, 
					radius * math::Normalize(value.position), 
					factor
				);
				value.normal = math::Normalize(math::Lerp(
					value.normal, 
					math::Normalize(value.position), 
					factor
				));
			}
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_transformMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

};


template <typename Mesh>
SpherifyMesh<Mesh> MakeSpherifyMesh(Mesh mesh, float radius, float factor) {
	return SpherifyMesh<Mesh>{std::move(mesh), radius, factor};
}

}

#endif

