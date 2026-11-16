
#include <cassert>
#include "generator/DodecahedronMesh.hpp"
#include "generator/Iterator.hpp"

#include <array>
#include <memory>

using namespace generator;


static const float a = (1.0f + std::sqrt(5.0f)) / 2.0f;

static const std::array<math::FVector3, 20> vertices {{
	{ 1.0f,  1.0f, -1.0f},
	{-1.0f,  1.0f, -1.0f},
	{ 1.0f, -1.0f, -1.0f},
	{-1.0f, -1.0f, -1.0f},
	{ 1.0f,  1.0f,  1.0f},
	{-1.0f,  1.0f,  1.0f},
	{ 1.0f, -1.0f,  1.0f},
	{-1.0f, -1.0f,  1.0f},

	{0.0f,  1.0f/a, -a},
	{0.0f, -1.0f/a, -a},
	{0.0f,  1.0f/a,  a},
	{0.0f, -1.0f/a,  a},

	{ 1.0f/a,  a, 0.0f},
	{-1.0f/a,  a, 0.0f},
	{ 1.0f/a, -a, 0.0f},
	{-1.0f/a, -a, 0.0f},

	{ a, 0.0f, -1.0f/a},
	{-a, 0.0f, -1.0f/a},
	{ a, 0.0f,  1.0f/a},
	{-a, 0.0f,  1.0f/a}
}};


static const std::array<std::array<int, 5>, 12> polygons{{
	{{ 0, 12,  4, 18, 16}}, // 0
	{{18,  6, 14,  2, 16}}, // 1
	{{ 4, 10, 11,  6, 18}}, // 2
	{{11,  7, 15, 14,  6}}, // 3
	{{ 7, 11, 10,  5, 19}}, // 4
	{{17,  3, 15,  7, 19}}, // 5
	{{ 9,  2, 14, 15,  3}}, // 6
	{{ 1,  8,  9,  3, 17}}, // 7
	{{10,  4, 12, 13,  5}}, // 8
	{{ 5, 13,  1, 17, 19}}, // 9
	{{ 0,  8,  1, 13, 12}}, // 10
	{{ 0, 16,  2,  9,  8}}  // 11
}};


static std::vector<math::FVector3> MakeVertices(int faceIndex) noexcept {
	std::vector<math::FVector3> result(5);
	for (int i = 0; i < 5; ++i) {
		result.at(static_cast<size_t>(i)) = math::Normalize(vertices.at(static_cast<size_t>(polygons.at(static_cast<size_t>(faceIndex))[static_cast<size_t>(i)])));
	}
	return result;
}


DodecahedronMesh::TriangleGenerator::TriangleGenerator(const DodecahedronMesh& mesh) noexcept :
	m_mMesh{&mesh},
	m_mFaceIndex{0},
	m_mFaceMesh{std::make_shared<ConvexPolygonMesh>(MakeVertices(0), m_mMesh->m_mSegments, m_mMesh->m_mRings)},
	m_mTriangles{m_mFaceMesh->Triangles()}
{ }



bool DodecahedronMesh::TriangleGenerator::Done() const noexcept {
	return static_cast<size_t>(m_mFaceIndex) == ::polygons.size();
}


Triangle DodecahedronMesh::TriangleGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	Triangle triangle = m_mTriangles.Generate();

	const int base = m_mFaceIndex * m_mMesh->m_mFaceVertexCount;

	triangle.vertices[0] += base;
	triangle.vertices[1] += base;
	triangle.vertices[2] += base;

	return triangle;
}


void DodecahedronMesh::TriangleGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	m_mTriangles.Next();

	if (m_mTriangles.Done()) {
		++m_mFaceIndex;

		if (!Done()) {
			m_mFaceMesh = std::make_shared<ConvexPolygonMesh>(
				MakeVertices(m_mFaceIndex),
				m_mMesh->m_mSegments
			);

			m_mTriangles = m_mFaceMesh->Triangles();
		}
	}
}



DodecahedronMesh::VertexGenerator::VertexGenerator(const DodecahedronMesh& mesh) noexcept :
	m_mMesh{&mesh},
	m_mFaceIndex{0},
	m_mFaceMesh{std::make_shared<ConvexPolygonMesh>(
		MakeVertices(0),
		m_mMesh->m_mSegments,
		m_mMesh->m_mRings
	)},
	m_mVertices{m_mFaceMesh->Vertices()}
{

}


bool DodecahedronMesh::VertexGenerator::Done() const noexcept {
	return static_cast<size_t>(m_mFaceIndex) == ::polygons.size();
}


MeshVertex DodecahedronMesh::VertexGenerator::Generate() const {

	MeshVertex vertex = m_mVertices.Generate();

	vertex.position *= m_mMesh->m_mRadius;

	return vertex;
}


void DodecahedronMesh::VertexGenerator::Next() {
	m_mVertices.Next();

	if (m_mVertices.Done()) {
		++m_mFaceIndex;

		if (!Done()) {
			m_mFaceMesh = std::make_shared<ConvexPolygonMesh>(
				MakeVertices(m_mFaceIndex),
				m_mMesh->m_mSegments,
				m_mMesh->m_mRings
			);

			m_mVertices = m_mFaceMesh->Vertices();
		}
	}
}



DodecahedronMesh::DodecahedronMesh(float radius, int segments, int rings) noexcept :
	m_mRadius{radius},
	m_mSegments{segments},
	m_mRings{rings},
	m_mFaceVertexCount{Count(ConvexPolygonMesh{1.0f, 5u, segments, rings}.Vertices())}
{

}


DodecahedronMesh::TriangleGenerator DodecahedronMesh::Triangles() const noexcept {
	return TriangleGenerator{*this};
}


DodecahedronMesh::VertexGenerator DodecahedronMesh::Vertices() const noexcept {
	return VertexGenerator{*this};
}



