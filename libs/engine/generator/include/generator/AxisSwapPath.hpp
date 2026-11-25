#pragma once
#include "Axis.hpp"
#include "TransformPath.hpp"

namespace generator {



/// Swaps axis in path.
template <typename Path>
class AxisSwapPath
{
private:

	using Impl = TransformPath<Path>;
	Impl m_transformPath;

public:

	/// @param path Source data path
	/// @param x Axis to use as the X-axis
	/// @param y Axis to use as the Y-axis
	/// @param z Axis to use as the Z-axis
	AxisSwapPath(Path path, Axis x, Axis y, Axis z) :
		m_transformPath{
			std::move(path),
			[x, y, z] (PathVertex& vertex) {
				vertex.position	= math::FVector3{
					vertex.position[static_cast<int>(x)],
					vertex.position[static_cast<int>(y)],
					vertex.position[static_cast<int>(z)]
				};
				vertex.tangent = math::FVector3{
					vertex.tangent[static_cast<int>(x)],
					vertex.tangent[static_cast<int>(y)],
					vertex.tangent[static_cast<int>(z)]
				};
				vertex.normal = math::FVector3{
					vertex.normal[static_cast<int>(x)],
					vertex.normal[static_cast<int>(y)],
					vertex.normal[static_cast<int>(z)]
				};
			}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformPath.Vertices(); }

};


template <typename Path>
AxisSwapPath<Path> MakeAxisSwapPath(Path path, Axis x, Axis y, Axis z) {
	return AxisSwapPath<Path>{std::move(path), x, y, z};
}


}


