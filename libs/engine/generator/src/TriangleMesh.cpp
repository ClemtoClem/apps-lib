
#include <cassert>
#include "generator/TriangleMesh.hpp"

#include <stdexcept>

using namespace generator;



TriangleMesh::TriangleGenerator::TriangleGenerator(const TriangleMesh& mesh) :
	m_mesh{&mesh},
	m_row{0},
	m_col{0},
	m_i{0}
{ }

bool TriangleMesh::TriangleGenerator::Done() const noexcept {
	return m_row == m_mesh->m_segments;
}

Triangle TriangleMesh::TriangleGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	Triangle triangle;

	if (m_col % 2 == 0) {
		triangle.vertices[0] = m_i;
		triangle.vertices[1] = m_i + 1;
		triangle.vertices[2] = m_i + 1 + m_mesh->m_segments- m_row;
	}
	else {
		triangle.vertices[0] = m_i;
		triangle.vertices[1] = m_i + 1 + m_mesh->m_segments- m_row;
		triangle.vertices[2] = m_i + m_mesh->m_segments- m_row;
	}

	return triangle;
}

void TriangleMesh::TriangleGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	if (m_col % 2 == 0) ++m_i;

	++m_col;
	if (m_col == 2 * (m_mesh->m_segments - m_row) - 1) {
		++m_i;
		m_col = 0;
		++m_row;
	}
}




TriangleMesh::VertexGenerator::VertexGenerator(const TriangleMesh& mesh) :
	m_mesh{&mesh},
	m_row{0},
	m_col{0}
{ }

bool TriangleMesh::VertexGenerator::Done() const noexcept {
	return m_row > m_mesh->m_segments;
}

MeshVertex TriangleMesh::VertexGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	MeshVertex vertex;

	if (m_row == m_mesh->m_segments) {
		vertex.position = m_mesh->m_v2;
		vertex.texCoord = math::FVector2{0.5f, 1.0f};
	}
	else {
		const float t = 1.0f / static_cast<float>(m_mesh->m_segments) * static_cast<float>(m_row);
		const float t2 = 1.0f / static_cast<float>(m_mesh->m_segments - m_row) * static_cast<float>(m_col);

		const auto e1 = math::Lerp(m_mesh->m_v0, m_mesh->m_v2, t);
		const auto e2 = math::Lerp(m_mesh->m_v1, m_mesh->m_v2, t);

		vertex.position = math::Lerp(e1, e2, t2);

		vertex.texCoord[0] = t2;
		vertex.texCoord[1] = t;
	}

	vertex.normal = m_mesh->m_normal;
	return vertex;
}

void TriangleMesh::VertexGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	++m_col;
	if (m_col > m_mesh->m_segments - m_row) {
		m_col = 0;
		++m_row;
	}
}




TriangleMesh::TriangleMesh(float radius, int segments) :
	TriangleMesh{
		radius * math::Normalize(math::FVector3{-1.0f, -1.0f, 0.0f}),
		radius * math::Normalize(math::FVector3{1.0f, -1.0f, 0.0f}),
		math::FVector3{0.0f, radius, 0.0f},
		segments
	}
{ }



TriangleMesh::TriangleMesh(
	const math::FVector3& v0, const math::FVector3& v1, const math::FVector3& v2,
	int segments
) :
	m_v0{v0},
	m_v1{v1},
	m_v2{v2},
	m_normal{math::TriangleNormal(v0, v1, v2)},
	m_segments{segments}
{ }

TriangleMesh::TriangleGenerator TriangleMesh::Triangles() const noexcept {
	return {*this};
}

TriangleMesh::VertexGenerator TriangleMesh::Vertices() const noexcept {
	return {*this};
}

