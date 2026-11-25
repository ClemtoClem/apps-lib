#pragma once
#include <memory>
#include <vector>

#include "Axis.hpp"
#include "MeshVertex.hpp"
#include "ShapeVertex.hpp"
#include "Triangle.hpp"


namespace generator {



/// Spins a shape around an axis to create a mesh.
/// u-texture coordinate is taken from the shape and v runs from 0 to 1 around
/// the axis counterclockwise.
template <typename Shape>
class LatheMesh {
public:

	class TriangleGenerator {
	public:

		Triangle Generate() const {
			Triangle triangle;

			const auto& shapeEdge = m_shapeEdges.Generate();

			int slice = m_i / 2;

			const int delta = m_mesh->m_slices + 1 ;

			if (m_i % 2 == 0) {
				triangle.vertices[0] = shapeEdge.vertices[0] * delta + slice;
				triangle.vertices[1] = shapeEdge.vertices[1] * delta + slice;
				triangle.vertices[2] = shapeEdge.vertices[1] * delta + slice + 1;
			}
			else {
				triangle.vertices[0] = shapeEdge.vertices[0] * delta + slice;
				triangle.vertices[1] = shapeEdge.vertices[1] * delta + slice + 1;
				triangle.vertices[2] = shapeEdge.vertices[0] * delta + slice + 1;
			}

			return triangle;
		}

		bool Done() const noexcept { return m_shapeEdges.Done(); }

		void Next() {
			++m_i;
			if (m_i == 2 * m_mesh->m_slices) {
				m_i = 0;
				m_shapeEdges.Next();
			}
		}

	private:

		const LatheMesh* m_mesh;

		decltype(std::declval<const Shape &>().Edges()) m_shapeEdges;

		int m_i;

		TriangleGenerator(const LatheMesh& mesh) :
			m_mesh{&mesh},
			m_shapeEdges{mesh.m_shape.Edges()},
			m_i{0}
		{ }

	friend class LatheMesh;
	};

	class VertexGenerator {
	public:

		MeshVertex Generate() const {
			MeshVertex vertex;

			const auto shapeVertex = m_shapeVertices.Generate();
			const math::FVector2 normal = shapeVertex.Normal();

			const float deltaAngle = m_mesh->m_sweep / static_cast<float>(m_mesh->m_slices);
			const float angle = static_cast<float>(m_i) * deltaAngle + m_mesh->m_start;

			const math::FQuaternion q = math::FQuaternion::AxisAngle(m_mesh->m_axis, angle);

			vertex.position = math::Rotate(q, math::FVector3{shapeVertex.position[0], shapeVertex.position[1], 0.0f});

			vertex.normal = math::Rotate(q, math::FVector3{normal[0], normal[1], 0.0f});


			vertex.texCoord[0] = shapeVertex.texCoord;
			vertex.texCoord[1] = angle / m_mesh->m_sweep;

			return vertex;
		}

		bool Done() const noexcept { return m_shapeVertices.Done(); }

		void Next() {
			++m_i;

			if (m_i == m_mesh->m_slices  + 1) {
				m_i = 0;
				m_shapeVertices.Next();
			}

		}

	private:

		const LatheMesh* m_mesh;

		decltype(std::declval<const Shape &>().Vertices()) m_shapeVertices;

		int m_i;

		VertexGenerator(const LatheMesh& mesh) :
			m_mesh{&mesh},
			m_shapeVertices{mesh.m_shape.Vertices()},
			m_i{0}
		{ }

	friend class LatheMesh;
	};

	/// @param shape The shape to spin.
	/// @param axis Unit length axis to spin around.
	/// @param slices Number of subdivisions around the axis.
	/// @param start Counterclockwise angle around the given axis relative to the xy-plane.
	/// relative to the xy-plane.
	/// @param sweep Counterclockwise angle around the given axis.
	LatheMesh(
		Shape shape,
		const math::FVector2& axis,
		int slices = 32,
		float start = 0.0f,
		float sweep = math::Radians(360.0f)
	) :
		m_axis{axis, 0.0f},
		m_shape{std::move(shape)},
		m_slices{slices},
		m_start{start},
		m_sweep{sweep}
	{ }

	TriangleGenerator Triangles() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }

private:

	math::FVector3 m_axis;

	Shape m_shape;

	int m_slices;

	float m_start;

	float m_sweep;

};


template <typename Shape>
LatheMesh<Shape> Lathe(
	Shape shape,
	Axis axis = Axis::X,
	int slices = 32,
	float start = 0.0f,
	float sweep = math::Radians(360.0f)
) {
	return LatheMesh<Shape>{std::move(shape), axis, slices, start, sweep};
}


}


