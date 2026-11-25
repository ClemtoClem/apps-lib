#pragma once
#include "Axis.hpp"
#include "TransformMesh.hpp"


namespace generator {


/// Rotates vertices and normals.
template <typename Mesh>
class RotateMesh
{
private:

	using Impl = TransformMesh<Mesh>;
	Impl m_transformMesh;

public:

	/// @param mesh Source mesh data.
	/// @param rotation Quaternion presenting the rotation.
	RotateMesh(Mesh mesh, const math::FQuaternion& rotation) :
		m_transformMesh{
			std::move(mesh),
			[rotation] (MeshVertex& value) {
				value.position = math::Rotate(rotation, value.position);
				value.normal = math::Rotate(rotation, value.normal);
			}
		}
	{ }

	/// @param mesh Source mesh data.
	/// @param angle Counterclockwise angle around the given axis.
	/// @param axis Unit length axis to rotate around.
	RotateMesh(Mesh mesh, float angle, const math::FVector3& axis) :
		RotateMesh{std::move(mesh), math::FQuaternion::AxisAngle(axis, angle)}
	{ }

	RotateMesh(Mesh mesh, float angle, Axis axis) :
		RotateMesh{
			std::move(mesh),
			math::FQuaternion::AxisAngle(axis == Axis::X ?
					math::FVector3{1.0f, 0.0f, 0.0f} :
					(axis == Axis::Y ? math::FVector3{0.0f, 1.0f, 0.0f} : math::FVector3{0.0f, 0.0f, 1.0f}), angle)
		}
	{ }

	using TriangleGenerator = typename Impl::TriangleGenerator;

	TriangleGenerator Triangles() const noexcept { return m_transformMesh.Triangles(); }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformMesh.Vertices(); }

};


template <typename Mesh>
RotateMesh<Mesh> MakeRotateMesh(Mesh mesh, const math::FQuaternion& rotation) {
	return RotateMesh<Mesh>{std::move(mesh), rotation};
}


template <typename Mesh>
RotateMesh<Mesh> MakeRotateMesh(Mesh mesh, float angle, const math::FVector3& axis) {
	return RotateMesh<Mesh>{std::move(mesh), angle, axis};
}


template <typename Mesh>
RotateMesh<Mesh> MakeRotateMesh(Mesh mesh, float angle, Axis axis) {
	return RotateMesh<Mesh>{std::move(mesh), angle, axis};
}

}


