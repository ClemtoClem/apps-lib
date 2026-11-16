#pragma once
#include "Axis.hpp"
#include "TransformMesh.hpp"
#include "Triangle.hpp"

namespace generator {


/// Swaps any number of axis in the mesh.
template <typename Mesh>
class AxisSwapMesh
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

		const AxisSwapMesh* m_mesh;

		typename TriangleGeneratorType<TransformMesh<Mesh>>::Type m_triangles;

		TriangleGenerator(const AxisSwapMesh& mesh) :
			m_mesh{&mesh},
			m_triangles{mesh.m_transformMesh.Triangles()}
		{ }

	friend class AxisSwapMesh;
	};

	///@param mesh Source data mesh.
	///@param x Axis to be used as the x-axis
	///@param y Axis to be used as the y-axis
	///@param z Axis to be used as the z-axis
	AxisSwapMesh(Mesh mesh, Axis x, Axis y, Axis z) :
		m_transformMesh{
			std::move(mesh),
			[x, y, z] (MeshVertex& vertex) {
				vertex.position	= math::FVector3{
					vertex.position[static_cast<int>(x)],
					vertex.position[static_cast<int>(y)],
					vertex.position[static_cast<int>(z)]
				};
				vertex.normal = math::FVector3{
					vertex.normal[static_cast<int>(x)],
					vertex.normal[static_cast<int>(y)],
					vertex.normal[static_cast<int>(z)]
				};
			}
		},
		m_flip{true}
	{
		if (x != Axis::X) m_flip = !m_flip;
		if (y != Axis::Y) m_flip = !m_flip;
		if (z != Axis::Z) m_flip = !m_flip;
	}

	TriangleGenerator Triangles() const noexcept { return {*this}; }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

private:

	bool m_flip;

};


template <typename Mesh>
AxisSwapMesh<Mesh> MakeAxisSwapMesh(Mesh mesh, Axis x, Axis y, Axis z) {
	return AxisSwapMesh<Mesh>{std::move(mesh), x, y, z};
}


}



