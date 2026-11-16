#pragma once
#include "LineShape.hpp"
#include "MergeShape.hpp"


namespace generator {



/// Rectangle centered at origin aligned along the x and y axis.
/// @image html RectangleShape.svg
class RectangleShape
{
private:

	using Impl = MergeShape<LineShape, LineShape, LineShape, LineShape>;
	Impl m_mergeShape;

public:

	/// @param size Half of the length of an edge.
	/// @param segments Number of subdivisions per edge.
	RectangleShape(
		const math::FVector2& size = math::FVector2{1.0f, 1.0f},
		const math::IVec2& segments = math::IVec2{8, 8}
	);

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_mergeShape.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_mergeShape.Vertices(); }

};


}

