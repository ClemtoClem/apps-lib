#ifndef UUID_D7746018E901B4EE35CEDC635307152F
#define UUID_D7746018E901B4EE35CEDC635307152F

#include "Edge.hpp"
#include "ShapeVertex.hpp"
#include "MergeShape.hpp"
#include "RepeatShape.hpp"
#include "LineShape.hpp"


namespace generator
{


/**
 * A 2d regular grid.
 */
class GridShape
{
private:

	using Impl = MergeShape<RepeatShape<LineShape>, RepeatShape<LineShape>>;

	Impl m_mImpl;

public:

	using EdgeGenerator = Impl::EdgeGenerator;

	using VertexGenerator = Impl::VertexGenerator;

	/// @param size A half of the side length of the grid.
	/// @param segments The Number of cells in the grid.
	/// If <1 an empty shape results.
	/// @param subSegments The number of segment along each cell edge.
	/// If <1 an empty shape results.
	explicit GridShape(
		const math::FVector2& size = {1.0f, 1.0f},
		const math::IVec2& segments = {4, 4},
		const math::IVec2& subSegments = {2, 2}
	) noexcept;

	EdgeGenerator Edges() const noexcept;

	VertexGenerator Vertices() const noexcept;

};

}

#endif
