#pragma once
#include "math.hpp"

namespace generator {



class Triangle {
public:

	/// Zero based indices of the triangle vertices in counterclockwise order.
	math::IVec3 vertices;

	Triangle() noexcept :
		vertices{}
	{ }

	Triangle(int a, int b, int c) noexcept : vertices{a, b, c} {}

	explicit Triangle(const math::IVec3& indices) noexcept :
		vertices{indices}
	{ }

};

}

