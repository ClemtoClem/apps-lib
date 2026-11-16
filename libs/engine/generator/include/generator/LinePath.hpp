#pragma once
#include "ParametricPath.hpp"

namespace generator {


/// A path from point to point.
/// @image html LinePath.svg
class LinePath
{
private:

	using Impl = ParametricPath;
	Impl m_parametricPath;

public:

	/// @param start Start point of the line.
	/// @param end End point of the line.
	/// @param normal Line normal. Should be parallel to the line.
	/// @param segments Number of subdivisions along the line.
	LinePath(
		const math::FVector3& start = {0.0f, 0.0f, -1.0f},
		const math::FVector3& end = {0.0f, 0.0f, 1.0f},
		const math::FVector3& normal = {1.0f, 0.0f, 0.0f},
		int segments = 8
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_parametricPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_parametricPath.Vertices(); }

};


}


