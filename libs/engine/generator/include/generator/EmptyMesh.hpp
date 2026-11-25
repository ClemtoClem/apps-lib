#pragma once
#include "Iterator.hpp"
#include "MeshVertex.hpp"
#include "Triangle.hpp"


namespace generator {


/// Empty Mesh with zero vertices and triangles.
class EmptyMesh {
public:

	class TriangleGenerator {
	public:
		Triangle Generate() const;
		bool Done() const noexcept;
		void Next();
	private:

		TriangleGenerator();

	friend class EmptyMesh;
	};

	class VertexGenerator {
	public:

		MeshVertex Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		VertexGenerator();

	friend class EmptyMesh;
	};

	EmptyMesh();

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

};


}

