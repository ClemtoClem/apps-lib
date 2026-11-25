#pragma once
#include "TransformPath.hpp"


namespace generator {


/// Scales a path. Keeps tangents and normals unit length.
template <typename Path>
class ScalePath
{
private:

	using Impl = TransformPath<Path>;
	Impl m_transformPath;

public:

	/// @param path Source data path.
	/// @param scale Scale Factor. Must not have zero components!
	ScalePath(Path path, const math::FVector3& scale) :
		m_transformPath{
			std::move(path),
			[scale] (PathVertex& value) { 
				value.position *= scale;
				value.tangent = math::Normalize(scale * value.tangent);
				value.normal = math::Normalize(scale * value.normal);  
			}
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformPath.Vertices(); }

};


template <typename Path>
ScalePath<Path> MakeScalePath(Path path, const math::FVector3& scale) {
	return ScalePath<Path>{std::move(path), scale};
}

}

