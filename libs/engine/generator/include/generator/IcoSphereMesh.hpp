#pragma once
#include "IcosahedronMesh.hpp"
#include "SpherifyMesh.hpp"


namespace generator {

/// Icosphere aka spherical subdivided icosahedron
/// @image html IcoSphereMesh.svg
class IcoSphereMesh
{
private:

	using Impl = SpherifyMesh<IcosahedronMesh>;
	Impl m_spherifyMesh;

public:

	/// @param radius The radius of the containing sphere.
	/// @param segments The number of segments per icosahedron edge. Must be >= 1.f
	IcoSphereMesh(float radius = 1.0f, int segments = 4);

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_spherifyMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_spherifyMesh.Vertices(); }

};


}


