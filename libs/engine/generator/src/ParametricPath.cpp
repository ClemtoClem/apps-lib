
#include <cassert>
#include "generator/ParametricPath.hpp"


using namespace generator;


ParametricPath::EdgeGenerator::EdgeGenerator(const ParametricPath& path) :
	m_path{&path},
	m_i{0}
{

}


Edge ParametricPath::EdgeGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");
	return Edge{{m_i, m_i + 1}};
}


bool ParametricPath::EdgeGenerator::Done() const noexcept {
	return m_i == m_path->m_segments;
}


void ParametricPath::EdgeGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");
	++m_i;
}




ParametricPath::VertexGenerator::VertexGenerator(const ParametricPath& path) :
	m_path{&path},
	m_i{0}
{ }



PathVertex ParametricPath::VertexGenerator::Generate() const {
	assert(!(Done()) && "générateur épuisé");

	return m_path->m_eval(static_cast<float>(m_i) * m_path->m_delta);
}

bool ParametricPath::VertexGenerator::Done() const noexcept {
	if (m_path->m_segments == 0) return true;
	return m_i == m_path->m_segments + 1;
}

void ParametricPath::VertexGenerator::Next() {
	assert(!(Done()) && "générateur épuisé");
	++m_i;
}





ParametricPath::ParametricPath(
	std::function<PathVertex(float)> eval,
	int segments
) noexcept :
	m_eval{std::move(eval)},
	m_segments{segments},
	m_delta{1.0f / static_cast<float>(segments)}
{ }


ParametricPath::EdgeGenerator ParametricPath::Edges() const noexcept {
	return *this;
}


ParametricPath::VertexGenerator ParametricPath::Vertices() const noexcept {
	return *this;
}


