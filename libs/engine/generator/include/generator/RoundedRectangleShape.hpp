#pragma once
#include "CircleShape.hpp"
#include "LineShape.hpp"
#include "MergeShape.hpp"
#include "TranslateShape.hpp"


namespace generator {



/// Rectangle with rounded corners centered at origin aligned along the x and y axis.
class RoundedRectangleShape
{
private:

	using Impl = MergeShape<
		LineShape, TranslateShape<CircleShape>,
		LineShape, TranslateShape<CircleShape>,
		LineShape, TranslateShape<CircleShape>,
		LineShape, TranslateShape<CircleShape>
	>;
	Impl m_mergeShape;

public:

	/// @param radius Radius of the rounded corners.
	/// @param size Half of a length of an edge.
	/// @param slices Number of subdivisions in each rounded corner.
	/// @param segments Number of subdivisions along each edge.
	RoundedRectangleShape(
		float radius = 0.25f,
		const math::FVector2& size = {0.75f, 0.75f},
		int slices = 4,
		const math::IVec2& segments = {8, 8}
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_mergeShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeShape.Vertices(); }

};


}

