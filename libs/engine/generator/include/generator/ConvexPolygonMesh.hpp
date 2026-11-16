// Copyright 2016 Markus Ilmola
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1f of the License, or (at your option) any later version.

#ifndef UUID_418D292D0BAB4AB4832E1CFEB92F0589
#define UUID_418D292D0BAB4AB4832E1CFEB92F0589

#include <vector>

#include "math.hpp"

#include "MeshVertex.hpp"
#include "Triangle.hpp"


namespace generator {



/// A polygonal disk with arbitrary number of corners.
/// Subdivided along each side to segments and radially to rings.
/// @image html ConvexPolygonMesh.svg
class ConvexPolygonMesh {
private:

	class TriangleGenerator {
	public:
		bool Done() const noexcept;
		Triangle Generate() const;
		void Next();

	private:

		const ConvexPolygonMesh* m_mMesh;

		bool m_mOdd;

		int m_mSegmentIndex;

		int m_mSideIndex;

		int m_mRingIndex;

		explicit TriangleGenerator(const ConvexPolygonMesh&) noexcept;

	friend class ConvexPolygonMesh;
	};

	class VertexGenerator {
	public:

		bool Done() const noexcept;
		MeshVertex Generate() const;
		void Next();

	private:

		const ConvexPolygonMesh* m_mMesh;

		bool m_mCenterDone;

		int m_mSegmentIndex;

		int m_mSideIndex;

		int m_mRingIndex;

		explicit VertexGenerator(const ConvexPolygonMesh&) noexcept;

	friend class ConvexPolygonMesh;
	};

	std::vector<math::FVector3> m_mVertices;

	int m_mSegments;

	int m_mRings;

	math::FVector3 m_mCenter;

	math::FVector3 m_mNormal;

	math::FVector3 m_mTangent;

	math::FVector3 m_mBitangent;

	math::FVector2 m_mTexDelta;

public:

	/// @param radius The radius the enclosing circle.
	/// @param sides The number of sides. Should be >= 3.f If <3 an empty mesh
	/// is generated.
	/// @param segments The number of segments per side. Should be >= 1.f If zero
	/// an empty mesh is generated.
	/// @param rings The number of radial segments. Should be >= 1.f = yelds an empty mesh.
	explicit ConvexPolygonMesh(
		float radius = 1.0f, int sides = 5, int segments = 4, int rings = 4
	) noexcept;

	//// @param vertices The corner vertex coordinates. Should form a convex polygon.
	explicit ConvexPolygonMesh(
		const std::vector<math::FVector2>& vertices, int segments = 1, int rings = 1
	) noexcept;

	/// @param vertices The corner vertex coordinates. Should be coplanar and
	/// form a convex polygon.
	/// calculated as an avarage.
	explicit ConvexPolygonMesh(
		std::vector<math::FVector3> vertices, int segments = 1, int rings = 1
	) noexcept;

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

};

}

#endif
