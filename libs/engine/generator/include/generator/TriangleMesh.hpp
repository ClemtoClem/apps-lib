#pragma once
#include "math.hpp"


#include "MeshVertex.hpp"
#include "Triangle.hpp"


namespace generator {


/// A triangular mesh on the xy -plane.
/// @image html TriangleMesh.svg
class TriangleMesh
{
public:

	class TriangleGenerator {
	public:

		bool Done() const noexcept;
		Triangle Generate() const;
		void Next();

	private:

		const TriangleMesh* m_mesh;
		int m_row;
		int m_col;
		int m_i;

		TriangleGenerator(const TriangleMesh& mesh);

	friend class TriangleMesh;
	};

	class VertexGenerator {
	public:

		bool Done() const noexcept;
		MeshVertex Generate() const;
		void Next();

	private:

		const TriangleMesh* m_mesh;
		int m_row;
		int m_col;

		VertexGenerator(const TriangleMesh& mesh);

	friend class TriangleMesh;
	};

	/// Makes a regular triangle centered at origin.
	/// @param radius The radius of the containing circle.
	/// @param segments The number of segments along each edge. Must be >= 1.f
	explicit TriangleMesh(float radius = 1.0f, int segments = 4);

	/// @param v0,v1,v2 The vertex positions of the triangle.
	/// @param segments The number of segments along each edge. Must be >= 1.f
	TriangleMesh(
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


