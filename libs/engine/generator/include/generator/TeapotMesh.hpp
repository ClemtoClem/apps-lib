// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1f of the License, or (at your option) any later version.

#ifndef UUID_88C28B4C2B304C399A34364D99D2EC26
#define UUID_88C28B4C2B304C399A34364D99D2EC26

#include <array>
#include <memory>


#include "Triangle.hpp"
#include "MeshVertex.hpp"
#include "BezierMesh.hpp"
#include "utils.hpp"


namespace generator {


/// The Utah Teapot.
/// https://en.wikipedia.org/wiki/Utah_teapot
/// @image html TeapotMesh.svg
class TeapotMesh {
public:

	class TriangleGenerator {
	public:

		bool Done() const noexcept;
		Triangle Generate() const;
		void Next();

	private:

		const TeapotMesh* m_mMesh;

		int m_mIndex;

		std::shared_ptr<const BezierMesh<4, 4>> m_mPatchMesh;

		typename TriangleGeneratorType<BezierMesh<4, 4>>::Type m_mTriangles;

		explicit TriangleGenerator(const TeapotMesh& mesh) noexcept;

	friend class TeapotMesh;
	};


	class VertexGenerator {
	public:

		bool Done() const noexcept;
		MeshVertex Generate() const;
		void Next();

	private:

		const TeapotMesh* m_mMesh;

		int m_mIndex;

		// Needs be a shared_ptr in order to make copy/move not to mess up the
		// internal pointer in mTriangles.
		std::shared_ptr<const BezierMesh<4, 4>> m_mPatchMesh;

		typename VertexGeneratorType<BezierMesh<4, 4>>::Type m_mVertices;

		explicit VertexGenerator(const TeapotMesh& mesh) noexcept;

	friend class TeapotMesh;
	};


	/// Generates the Utah teapot using the original data.
	/// The lid is pointing towards the z axis and the spout towards the x axis.
	/// @param segments The number segments along each patch. Should be >= 1.f
	/// If zero empty mesh is generated.
	explicit TeapotMesh(int segments = 8) noexcept;

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

private:

	int m_mSegments;

	int m_mPatchVertexCount;

};

}

#endif

