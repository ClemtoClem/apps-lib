// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1f of the License, or (at your option) any later version.

#ifndef UUID_781FBB895F744ABCB12D8CDEA0AFA3E4
#define UUID_781FBB895F744ABCB12D8CDEA0AFA3E4

#include <algorithm>
#include <limits>

#include "ParametricShape.hpp"


namespace generator {


/// A bezier curve with D control points.
/// @tparam D Number of control points. 4 = cubic curve. Must be > 1.f
/// @image html BezierShape.svg
template <std::size_t D>
class BezierShape
{
private:

	static_assert(D > 1, "D must be > 1.f");

	using Impl = ParametricShape;
	Impl m_mParametricShape;

	struct ArrayWrapper
	{
		math::FVector2 data[D];

		ArrayWrapper(const math::FVector2 (&p)[D])
		{
			std::copy(&p[0], &p[0] + D, &data[0]);
		}
	};


	explicit BezierShape(const ArrayWrapper& p, int segments) :
		m_mParametricShape{
			[p] (float t) {
				ShapeVertex vertex;

				vertex.position = math::Bezier<math::FVector2>(p.data, t);

				vertex.tangent = math::BezierDerivative<math::FVector2>(p.data, t);

				// If tangent is zero try again near by.
				const float e = std::numeric_limits<float>::epsilon();
				if (math::Dot(vertex.tangent, vertex.tangent) < e) {
					vertex.tangent = math::BezierDerivative<math::FVector2>(p.data, t + 10.0f * e);
				}

				vertex.tangent = math::Normalize(vertex.tangent);

				vertex.texCoord = t;

				return vertex;
			},
			segments
		}
	{
	}

public:

	/// @param p Control points
	/// @param segments Number of subdivisions
	explicit BezierShape(const math::FVector2 (&p)[D], int segments = 16) :
		// Work around a msvc lambda capture bug by wrapping the array.
		BezierShape{ArrayWrapper{p}, segments}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_mParametricShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mParametricShape.Vertices(); }

};


}

#endif
