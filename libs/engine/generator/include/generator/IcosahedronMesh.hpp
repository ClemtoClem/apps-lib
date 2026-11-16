#pragma once
#include <array>
#include <memory>


#include "Triangle.hpp"
#include "MeshVertex.hpp"
#include "TriangleMesh.hpp"
#include "utils.hpp"


namespace generator {


/// Regular icosahedron centered at origin with given radius.
/// @image html IcosahedronMesh.svg
class IcosahedronMesh {
public:

	class TriangleGenerator {
	public:

		bool Done() const noexcept;
		Triangle Generate() const;
		void Next();

	private:

		const IcosahedronMesh* m_mesh;

		int m_i;

		// Needs be a shared_ptr in order to make copy/move not to mess up the
		// internal pointer in triangles_.
		std::shared_ptr<const TriangleMesh> m_triangleMesh;

		typename TriangleGeneratorType<TriangleMesh>::Type m_triangles;

		TriangleGenerator(const IcosahedronMesh& mesh);

	friend class IcosahedronMesh;
	};


	class VertexGenerator {
	public:

		bool Done() const noexcept;
		MeshVertex Generate() const;
		void Next();

	private:

		const IcosahedronMesh* m_mesh;

		int m_i;

		// Needs be a shared_ptr in order to make copy/move not to mess up the
		// internal pointer in triangles_.
		std::shared_ptr<const TriangleMesh> m_triangleMesh;

		typename VertexGeneratorType<TriangleMesh>::Type m_vertices;

		VertexGenerator(const IcosahedronMesh& mesh);

	friend class IcosahedronMesh;
	};

private:

	float m_radius;

	int m_segments;

	int m_faceVertexCount;

public:

	/// @param radius The radius of the enclosing sphere.
	/// @param segments The number segments along each edge. Must be >= 1.f
	IcosahedronMesh(float radius = 1.0f, int segments = 1);

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

};

}

