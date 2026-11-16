
#include <cassert>
#include "generator/IcosahedronMesh.hpp"
#include "generator/Iterator.hpp"

#include <array>

using namespace generator;


static const float a = (1.0f + std::sqrt(5.0f)) / 2.0f;
static const float d = std::sqrt(1.0f + a * a);
static const float x = 1.0f / d;
static const float y = a / d;

static const std::array<math::FVector3, 12> vertices {{
	{-x, y, 0.0f},
	{x, y, 0.0f},
	{-x, -y, 0.0f},
	{x, -y, 0.0f},
	{0.0f, -x, y},
	{0.0f, x, y},
	{0.0f, -x, -y},
	{0.0f, x, -y},
	{y, 0.0f, -x},
	{y, 0.0f, x},
	{-y, 0.0f, -x},
	{-y, 0.0f, x}
}};


static const std::array<math::IVec3, 20> triangles{{
	{0,  11, 5},
	{0,  5,  1},
	{0,  1,  7},
	{0,  7,  10},
	{0,  10, 11},
	{1,  5,  9},
	{5,  11, 4},
	{11, 10, 2},
	{10, 7,  6},
	{7,  1,  8},
	{3,  9,  4},
	{3,  4,  2},
	{3,  2,  6},
	{3,  6,  8},
	{3,  8,  9},
	{4,  9,  5},
	{2,  4,  11},
	{6,  2,  10},
	{8,  6,  7},
	{9,  8,  1}
}};


IcosahedronMesh::TriangleGenerator::TriangleGenerator(const IcosahedronMesh& mesh) :
	m_mesh{&mesh},
	m_i{0},
	m_triangleMesh{std::make_shared<TriangleMesh>(
		::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(0)][0])],
		::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(0)][1])],
		::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(0)][2])],
		mesh.m_segments
	)},
	m_triangles{m_triangleMesh->Triangles()}
{ }



bool IcosahedronMesh::TriangleGenerator::Done() const noexcept {
	return m_i == static_cast<int>(::triangles.size());
}


Triangle IcosahedronMesh::TriangleGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	Triangle triangle = m_triangles.Generate();

	const int base = m_i * m_mesh->m_faceVertexCount;

	triangle.vertices[0] +=  base;
	triangle.vertices[1] +=  base;
	triangle.vertices[2] +=  base;

	return triangle;
}


void IcosahedronMesh::TriangleGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	m_triangles.Next();

	if (m_triangles.Done()) {
		++m_i;

		if (!Done()) {
			m_triangleMesh = std::make_shared<TriangleMesh>(
				::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(m_i)][0])],
				::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(m_i)][1])],
				::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(m_i)][2])],
				m_mesh->m_segments
			);

			m_triangles =  m_triangleMesh->Triangles();
		}
	}
}




IcosahedronMesh::VertexGenerator::VertexGenerator(const IcosahedronMesh& mesh) :
	m_mesh{&mesh},
	m_i{0},
	m_triangleMesh{std::make_shared<TriangleMesh>(
		::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(0)][0])],
		::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(0)][1])],
		::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(0)][2])],
		m_mesh->m_segments
	)},
	m_vertices{m_triangleMesh->Vertices()}
{

}


bool IcosahedronMesh::VertexGenerator::Done() const noexcept {
	return m_i == static_cast<int>(::triangles.size());
}

MeshVertex IcosahedronMesh::VertexGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	MeshVertex vertex = m_vertices.Generate();

	vertex.position *= m_mesh->m_radius;

	return vertex;
}

void IcosahedronMesh::VertexGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	m_vertices.Next();

	if (m_vertices.Done()) {
		++m_i;

		if (!Done()) {
			m_triangleMesh = std::make_shared<TriangleMesh>(
				::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(m_i)][0])],
				::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(m_i)][1])],
				::vertices[static_cast<size_t>(::triangles[static_cast<size_t>(m_i)][2])],
				m_mesh->m_segments
			);

			m_vertices = m_triangleMesh->Vertices();
		}
	}
}




IcosahedronMesh::IcosahedronMesh(float radius, int segments) :
	m_radius{radius},
	m_segments{segments},
	m_faceVertexCount{Count(TriangleMesh{1.0f, segments}.Vertices())}
{ }

IcosahedronMesh::TriangleGenerator IcosahedronMesh::Triangles() const noexcept {
	return {*this};
}

IcosahedronMesh::VertexGenerator IcosahedronMesh::Vertices() const noexcept {
	return {*this};
}



