#pragma once
#include "math.hpp"

namespace generator {

class Edge {
public:

	math::IVec2 vertices;

	Edge() noexcept :
		vertices{}
	{ }

	explicit Edge(const math::IVec2& indices) noexcept :
		vertices{indices}
	{ }

	Edge(int a, int b) noexcept : vertices{a, b} {}

};

}

