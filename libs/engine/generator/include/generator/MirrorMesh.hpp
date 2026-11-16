#pragma once
#include "Axis.hpp"
#include "AxisFlipMesh.hpp"
#include "MergeMesh.hpp"


namespace generator {


/// Duplicates the mesh by mirrorring it along an axis.
template <typename Mesh>
class MirrorMesh
{
private:

	using Impl = MergeMesh<Mesh, AxisFlipMesh<Mesh>>;
	Impl m_mergeMesh;

public:

	/// @param mesh Source data mesh.
	/// @param axis The axis to mirror along.
	MirrorMesh(Mesh mesh, Axis axis) :
		m_mergeMesh{
			mesh, {mesh, axis == Axis::X, axis == Axis::Y, axis == Axis::Z}
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mergeMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeMesh.Vertices(); }

};


template <typename Mesh>
MirrorMesh<Mesh> MakeMirrorMesh(Mesh mesh, Axis axis = Axis::X) {
	return MirrorMesh<Mesh>{std::move(mesh), axis};
}

}

