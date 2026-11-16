#pragma once
#include <functional>

#include "MeshVertex.hpp"
#include "utils.hpp"


namespace generator {


/// Apply a mutator function to each vertex.
template <typename Mesh>
class TransformMesh
{
private:

	using Impl = Mesh;
	Impl m_mesh;

public:

	class VertexGenerator {
	public:

		MeshVertex Generate() const {
			auto vertex = m_vertices.Generate();
			m_mesh->m_mutate(vertex);
			return vertex;
		}

		bool Done() const noexcept { return m_vertices.Done(); }

		void Next() { m_vertices.Next(); }

	private:

		const TransformMesh* m_mesh;

		typename VertexGeneratorType<Mesh>::Type m_vertices;

		explicit VertexGenerator(const TransformMesh& mesh) :
			m_mesh{&mesh},
			m_vertices{mesh.m_mesh.Vertices()}
		{ }

	friend class TransformMesh;
	};

	/// @param mesh Source data mesh.
	/// @param mutate Callback function that gets called once per vertex.
	explicit TransformMesh(Mesh mesh, std::function<void (MeshVertex&)> mutate) :
		m_mesh{std::move(mesh)},
		m_mutate{std::move(mutate)}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mesh.Triangles(); }

	VertexGenerator Vertices() const noexcept { return VertexGenerator{*this}; }

private:

	std::function<void(MeshVertex&)> m_mutate;

};


template <typename Mesh>
TransformMesh<Mesh> MakeTransformMesh(
	Mesh mesh, std::function<void(MeshVertex&)> mutate
) {
	return TransformMesh<Mesh>{std::move(mesh), std::move(mutate)};
}

}

