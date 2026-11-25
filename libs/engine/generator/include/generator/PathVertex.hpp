#pragma once
#include "math.hpp"

namespace generator {

class PathVertex {
public:

	/// Unit length vector perpendicular to the path at this point.
	/// Also the x-axis of the path coordinate system at this point.
	math::FVector3 normal;

	math::FVector3 position;

	/// Unit length vector parallel to the path at this point.
	/// Also the z-axis of the path at this point.
	math::FVector3 tangent;

	float texCoord;

	PathVertex() :
		normal{},
		position{},
		tangent{},
		texCoord{}
	{ }

	/// Returns tangent x normal.
	/// Also the y-axis of the path coordinate system.
	/// See: http://mathworld.wolfram.com/BinormalVector.html
	math::FVector3 Binormal() const noexcept {
		return math::Cross(tangent, normal);
	}

};

}

