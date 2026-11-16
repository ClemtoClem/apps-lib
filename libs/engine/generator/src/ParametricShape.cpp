
#include <cassert>
#include "generator/ParametricShape.hpp"


using namespace generator;


ParametricShape::EdgeGenerator::EdgeGenerator(const ParametricShape& shape) :
	m_shape{&shape},
	m_i{0}
{ }


Edge ParametricShape::EdgeGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	return Edge{{m_i, m_i + 1}};
}

bool ParametricShape::EdgeGenerator::Done() const noexcept {
	return m_i ==  m_shape->m_segments;
}

void ParametricShape::EdgeGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");
	++m_i;
}




ShapeVertex ParametricShape::VertexGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	return m_shape->m_eval(static_cast<float>(m_i) * m_shape->m_delta);
}



ParametricShape::VertexGenerator::VertexGenerator(const ParametricShape& shape) :
	m_shape{&shape}, m_i{0}
{ }

bool ParametricShape::VertexGenerator::Done() const noexcept {
	if (m_shape->m_segments == 0) return true;
	return m_i == m_shape->m_segments + 1;
}

void ParametricShape::VertexGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");
	++m_i;
}



ParametricShape::ParametricShape(
	std::function<ShapeVertex(float)> eval,
	int segments
) noexcept :
	m_eval{std::move(eval)},
	m_segments{segments},
	m_delta{1.0f / static_cast<float>(segments)}
{ }



ParametricShape::EdgeGenerator ParametricShape::Edges() const noexcept {
	return *this;
}

ParametricShape::VertexGenerator ParametricShape::Vertices() const noexcept {
	return *this;
}

