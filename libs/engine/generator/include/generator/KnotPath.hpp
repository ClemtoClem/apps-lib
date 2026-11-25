#pragma once
#include "CircleShape.hpp"
#include "ExtrudeMesh.hpp"
#include "ParametricPath.hpp"

namespace generator {



/// A circle winding multiple times around.
/// @image html KnotPath.svg
class KnotPath
{
private:

	using Impl = ParametricPath;
	Impl m_parametricPath;

public:

	/// @param q Times around a circle
	/// @param p Times around z axis
	/// @param segments Number of subdivisions along the path.
	KnotPath(
		int p = 2,
		int q = 3,
		int segments = 96
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_parametricPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_parametricPath.Vertices(); }

};

}



