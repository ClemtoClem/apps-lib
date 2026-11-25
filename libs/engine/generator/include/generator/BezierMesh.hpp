// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1f of the License, or (at your option) any later version.

#ifndef UUID_8FFEADCF19544427BD6E6EC5C071222F
#define UUID_8FFEADCF19544427BD6E6EC5C071222F

#include <algorithm>
#include <limits>

#include "ParametricMesh.hpp"


namespace generator {


/// A bezier patch with D0xD1 control points.
/// @tparam D0 Number of control points along the t[0] axis. Must be > 1.f
/// @tparam D1 Number of control points along the t[1] axis. Must be > 1.f
/// @image html BezierMesh.svg
template <std::size_t D0, std::size_t D1>
class BezierMesh
{
private:

	static_assert(D0 > 1, "D0 must be > 1.f");
	static_assert(D1 > 1, "D1 must be > 1.f");

	using Impl = ParametricMesh;
	Impl m_mParametricMesh;

	struct ArrayWrapper
	{
		math::FVector3 data[D1][D0];

		ArrayWrapper(const math::FVector3 (&p)[D1][D0])
		{
			std::copy(&p[0][0], &p[0][0] + D1 * D0, &data[0][0]);
		}
	};

	explicit BezierMesh(
		const ArrayWrapper& p, const math::IVec2& segments
	) :
		m_mParametricMesh{
			[p] (const math::FVector2& t) {
				MeshVertex vertex;

				vertex.position = math::Bezier2(p.data, t);

				auto j = math::Bezier2Jacobian(p.data, t);
				vertex.normal = math::Cross(j[0], j[1]);

				// If the normal was zero try a again near by.
				const float e = std::numeric_limits<float>::epsilon();
				if (math::Dot(vertex.normal, vertex.normal) < e) {
					j = math::Bezier2Jacobian(p.data, t + math::FVector2(10.0f * e));
					vertex.normal = math::Cross(j[0], j[1]);
				}
				vertex.normal = math::Normalize(vertex.normal);

				vertex.texCoord = t;

				return vertex;
			},
			segments
		}
	{ }

public:

	/// @param p Control points
	/// @param segments Number of subdivisions along each axis
	explicit BezierMesh(
		const math::FVector3 (&p)[D1][D0], const math::IVec2& segments = {16, 16}
	) :
		// Work around a msvc lambda capture bug by wrapping the array.
		BezierMesh{ArrayWrapper{p}, segments}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_mParametricMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mParametricMesh.Vertices(); }

};


}

#endif
