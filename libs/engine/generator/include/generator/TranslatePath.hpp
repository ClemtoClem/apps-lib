#pragma once
#include "math.hpp"

#include "TransformPath.hpp"

namespace generator {


/// Translates the position of each vertex by given amount.
template <typename Path>
class TranslatePath
{
private:

	using Impl = TransformPath<Path>;
	Impl m_transformPath;

public:

	/// @param path Source data path.
	/// @param delta Amount to increment each vertex position.
	TranslatePath(Path path, const math::FVector3& delta) :
		m_transformPath{
			std::move(path),
			[delta] (PathVertex& value) { value.position += delta;	}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformPath.Vertices(); }

};


template <typename Path>
TranslatePath<Path> MakeTranslatePath(Path path, const math::FVector3& delta) {
	return TranslatePath<Path>{std::move(path), delta};
}

}

