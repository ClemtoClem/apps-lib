#pragma once
#include "math.hpp"

namespace generator {


class MeshVertex {
public:

	math::FVector3 position;

	/// Unit vector perpendicular to the surface.
	math::FVector3 normal;

	/// UV texture coordinates
	math::FVector2 texCoord;

	MeshVertex() noexcept :
		position{},
		normal{},
		texCoord{}
	{ }

};



}

