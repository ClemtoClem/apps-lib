
#include <cassert>
#include "generator/ParametricMesh.hpp"


using namespace generator;


ParametricMesh::TriangleGenerator::TriangleGenerator(const ParametricMesh& mesh) :
	m_mesh{&mesh},
	m_i{0, 0},
	m_even{false}
{ }


Triangle ParametricMesh::TriangleGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	Triangle triangle;

	const int base = m_i[1] * (m_mesh->m_segments[0] + 1) + m_i[0];

	if (!m_even) {
		triangle.vertices = {
			base,
			base + 1,
			base + m_mesh->m_segments[0] + 1
		};
	}
	else {
		triangle.vertices = {
			base + 1,
			base + m_mesh->m_segments[0] + 2,
			base + m_mesh->m_segments[0] + 1
		};
	}

	return triangle;
}

bool ParametricMesh::TriangleGenerator::Done() const noexcept {
	if (m_mesh->m_segments[0] == 0 || m_mesh->m_segments[1] == 0) return true;
	return m_i[1] == m_mesh->m_segments[1];
}

void ParametricMesh::TriangleGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	m_even = !m_even;

	if (!m_even) {
		++m_i[0];
		if (m_i[0] == m_mesh->m_segments[0]) {
			m_i[0] = 0;
			++m_i[1];
		}
	}
}




ParametricMesh::VertexGenerator::VertexGenerator(const ParametricMesh& mesh) :
	m_mesh{&mesh}, m_i{0, 0}
{ }



MeshVertex ParametricMesh::VertexGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	return m_mesh->m_eval({static_cast<float>(m_i[0]) * m_mesh->m_delta[0], static_cast<float>(m_i[1]) * m_mesh->m_delta[1]});
}


bool ParametricMesh::VertexGenerator::Done() const noexcept {
	if (m_mesh->m_segments[0] == 0 || m_mesh->m_segments[1] == 0) return true;
	return m_i[1] > m_mesh->m_segments[1];
}


void ParametricMesh::VertexGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");

	++m_i[0];
	if (m_i[0] > m_mesh->m_segments[0]) {
		m_i[0] = 0;
		++m_i[1];
	}
}




ParametricMesh::ParametricMesh(
	std::function<MeshVertex(const math::FVector2& t)> eval,
	const math::IVec2& segments
) noexcept :
	m_eval{std::move(eval)},
	m_segments{segments},
	m_delta{1.0f / static_cast<float>(segments[0]), 1.0f / static_cast<float>(segments[1])}
{ }


ParametricMesh::TriangleGenerator ParametricMesh::Triangles() const noexcept {
	return *this;
}


ParametricMesh::VertexGenerator ParametricMesh::Vertices() const noexcept {
	return *this;
}

