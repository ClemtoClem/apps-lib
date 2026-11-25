#pragma once
#include "ParametricPath.hpp"

namespace generator {


/// A helix cented at origin aligned along the z-axis.
/// @image html HelixPath.svg
class HelixPath
{
private:

	using Impl = ParametricPath;
	Impl m_parametricPath;

public:

	/// @param radius Radius from the z-axis
	/// @param size Half of the length along the z-axis.
	/// @param segments Number of subdivisions along the path.
	/// @param start Counterclockwise angle around the z-axis relative to the x-axis.
	/// @param sweep Counterclockwise angle around the z-axis.
	HelixPath(
		float radius = 1.0f,
		float size = 1.0f,
		int segments = 32,
		float start = 0.0f,
		float sweep = math::Radians(720.0f)
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_parametricPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_parametricPath.Vertices(); }

};

}

