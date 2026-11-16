// Copyright 2016 Markus Ilmola
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1f of the License, or (at your option) any later version.

#ifndef UUID_8B1E88033CA8420EA606B29D704E10EC
#define UUID_8B1E88033CA8420EA606B29D704E10EC

#include <array>
#include <memory>


#include "Triangle.hpp"
#include "MeshVertex.hpp"
#include "ConvexPolygonMesh.hpp"
#include "utils.hpp"


namespace generator {


/// A regular dodecahedron centered at origin with given radius.
/// Each face optionally subdivided along edges and/or radius.
/// @image html DodecahedronMesh.svg
class DodecahedronMesh {
private:

	class TriangleGenerator {
	public:

		bool Done() const noexcept;
		Triangle Generate() const;
		void Next();

	private:

		const DodecahedronMesh* m_mMesh;

		int m_mFaceIndex;

		// Needs be a shared_ptr in order to make copy/move not to mess up the
		// internal pointer in mTriangles.
		std::shared_ptr<const ConvexPolygonMesh> m_mFaceMesh;

		typename TriangleGeneratorType<ConvexPolygonMesh>::Type m_mTriangles;

		explicit TriangleGenerator(const DodecahedronMesh& mesh) noexcept;

	friend class DodecahedronMesh;
	};


	class VertexGenerator {
	public:

		bool Done() const noexcept;
		MeshVertex Generate() const;
		void Next();

	private:

		const DodecahedronMesh* m_mMesh;

		int m_mFaceIndex;

		// Needs be a shared_ptr in order to make copy/move not to mess up the
		// internal pointer in mTriangles.
		std::shared_ptr<const ConvexPolygonMesh> m_mFaceMesh;

		typename VertexGeneratorType<ConvexPolygonMesh>::Type m_mVertices;

		explicit VertexGenerator(const DodecahedronMesh& mesh) noexcept;

	friend class DodecahedronMesh;
	};

	float m_mRadius;

	int m_mSegments;

	int m_mRings;

	int m_mFaceVertexCount;

public:

	/// @param radius The radius of the enclosing sphere.
	/// @param segments The number segments along each edge. Should be >= 1.f
	/// If <1 empty mesh is generated.
	/// @param rings The number of radial segments on each face. Should be >= 1.f
	/// If <1 an empty mesh is generated.
	explicit DodecahedronMesh(
		float radius = 1.0f, int segments = 1, int rings = 1
	) noexcept;

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

};

}

#endif

