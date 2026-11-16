#pragma once
#include "math.hpp"


namespace generator {


/// A point on a path.
/// Position and assosiated coordinate system.
class ShapeVertex {
public:

	math::FVector2 position;

	/// Unit length vector parallel to the shape at this point.
	/// Also the y-axis of the shape's local coordinate system.
	math::FVector2 tangent;

	float texCoord;

	ShapeVertex() :
		position{},
		tangent{},
		texCoord{}
	{ }

	/// Returns the tangent rotated 90 dec clockwise.
	/// Also the x-axis of the shape's local coordinate system.
	math::FVector2 Normal() const noexcept {
		return -tangent.Perp();
	}

};


}

