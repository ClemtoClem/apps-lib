#pragma once
#include "Axis.hpp"
#include "TransformPath.hpp"


namespace generator {


/// Rotates vertices, tangents and normals.
template <typename Path>
class RotatePath
{
private:

	using Impl = TransformPath<Path>;
	Impl m_transformPath;

public:

	RotatePath(Path path, const math::FQuaternion& rotation) :
		m_transformPath{
			std::move(path),
			[rotation] (PathVertex& value) {
				value.position = math::Rotate(rotation, value.position);
				value.normal = math::Rotate(rotation, value.normal);
			}
		}
	{ }

	RotatePath(Path path, float angle, const math::FVector3& axis) :
		RotatePath{std::move(path), math::FQuaternion::AxisAngle(axis, angle)}
	{ }

	RotatePath(Path path, float angle, Axis axis) :
		RotatePath{
			std::move(path),
			math::FQuaternion::AxisAngle(axis == Axis::X ?
					math::FVector3{1.0f, 0.0f, 0.0f} :
					(axis == Axis::Y ? math::FVector3{0.0f, 1.0f, 0.0f} : math::FVector3{0.0f, 0.0f, 1.0f}), angle)
		}
	{ }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_transformPath.Edges(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformPath.Vertices(); }

};


template <typename Path>
RotatePath<Path> MakeRotatePath(Path path, const math::FQuaternion& rotation) {
	return RotatePath<Path>{std::move(path), rotation};
}


template <typename Path>
RotatePath<Path> MakeRotatePath(Path path, float angle, const math::FVector3& axis) {
	return RotatePath<Path>{std::move(path), angle, axis};
}


template <typename Path>
RotatePath<Path> MakeRotatePath(Path path, float angle, Axis axis) {
	return RotatePath<Path>{std::move(path), angle, axis};
}


}


