#pragma once
#include "math.hpp"

#include "MeshVertex.hpp"
#include "Triangle.hpp"


namespace generator {


/// A triangular region on a surface of a sphere.
/// @image html SphericalTriangleMesh.svg
class SphericalTriangleMesh
{
public:

	class TriangleGenerator {
	public:

		bool Done() const noexcept;

		Triangle Generate() const;

		void Next();

	private:

		const SphericalTriangleMesh* m_mesh;
		int m_row;
		int m_col;
		int m_i;

		TriangleGenerator(const SphericalTriangleMesh& mesh);

	friend class SphericalTriangleMesh;
	};

	class VertexGenerator {
	public:

		bool Done() const noexcept;

		MeshVertex Generate() const;

		void Next();

	private:

		const SphericalTriangleMesh* m_mesh;
		int m_row;
		int m_col;

		VertexGenerator(const SphericalTriangleMesh& mesh);

	friend class SphericalTriangleMesh;
	};

	/// @param radius Radius of the containing sphere.
	/// @param segments Number of subdivisions along each edge.
	SphericalTriangleMesh(
		float radius = 1.0f,
		int segments = 4
	);

	/// @param segments Number of subdivisions along each edge.
	SphericalTriangleMesh(
		const math::FVector3& v0, const math::FVector3& v1, const math::FVector3& v2,
		int segments = 4
	);

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

private:

	math::FVector3 m_v0, m_v1, m_v2;

	math::FVector3 m_normal;

	int m_segments;

};


}


