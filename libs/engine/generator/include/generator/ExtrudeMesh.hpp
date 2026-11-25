#pragma once
#include "MeshVertex.hpp"
#include "PathVertex.hpp"
#include "ShapeVertex.hpp"
#include "Triangle.hpp"
#include "Edge.hpp"
#include "Iterator.hpp"


namespace generator {



/// Extrude a shape along a path.
/// The shape normal becomes the mesh normal.
/// u-texture coordinate is taken from the shape and v from the path.
template <typename Shape, typename Path>
class ExtrudeMesh {
public:

	class TriangleGenerator {
	public:

		Triangle Generate() const {
			Triangle triangle;

			const auto& shapeEdge = m_shapeEdges.Generate();
			const auto& pathEdge = m_pathEdges.Generate();

			if (m_odd == 0) {
				triangle.vertices[0] =
					shapeEdge.vertices[0] + pathEdge.vertices[0] * m_mesh->m_shapeVertexCount;
				triangle.vertices[1] =
					shapeEdge.vertices[1] + pathEdge.vertices[1] * m_mesh->m_shapeVertexCount;
				triangle.vertices[2] =
					shapeEdge.vertices[0] + pathEdge.vertices[1] * m_mesh->m_shapeVertexCount;
			}
			else {
				triangle.vertices[0] =
					shapeEdge.vertices[0] + pathEdge.vertices[0] * m_mesh->m_shapeVertexCount;
				triangle.vertices[1] =
					shapeEdge.vertices[1] + pathEdge.vertices[0] * m_mesh->m_shapeVertexCount;
				triangle.vertices[2] =
					shapeEdge.vertices[1] + pathEdge.vertices[1] * m_mesh->m_shapeVertexCount;
			}

			return triangle;
		}

		bool Done() const noexcept { return m_pathEdges.Done(); }

		void Next() {
			m_odd = !m_odd;

			if (m_odd) {
				m_shapeEdges.Next();
				if (m_shapeEdges.Done()) {
					m_pathEdges.Next();
					m_shapeEdges = m_mesh->m_shape.Edges();
				}
			}
		}

	private:

		const ExtrudeMesh* m_mesh;

		decltype(std::declval<const Shape &>().Edges()) m_shapeEdges;

		decltype(std::declval<const Path &>().Edges()) m_pathEdges;

		bool m_odd;

		TriangleGenerator(const ExtrudeMesh& mesh) :
			m_mesh{&mesh},
			m_shapeEdges{mesh.m_shape.Edges()},
			m_pathEdges{mesh.m_path.Edges()},
			m_odd{true}
		{
		}

	friend class ExtrudeMesh;
	};

	class VertexGenerator {
	public:

		MeshVertex Generate() const {
			MeshVertex vertex;

			const auto& shapeVertex = m_shapeVertices.Generate();
			const auto& pathVertex = m_pathVertices.Generate();

			math::FVector3 pathBinormal = pathVertex.Binormal();
			vertex.position =
				pathVertex.position +
				shapeVertex.position[0] * pathVertex.normal +
				shapeVertex.position[1] * pathBinormal;

			math::FVector2 shapeNormal = shapeVertex.Normal();
			vertex.normal =
				shapeNormal[0] * pathVertex.normal +
				shapeNormal[1] * pathBinormal;

			vertex.texCoord[0] = shapeVertex.texCoord;
			vertex.texCoord[1] = pathVertex.texCoord;

			return vertex;
		}

		bool Done() const noexcept { return m_pathVertices.Done(); }

		void Next() {
			m_shapeVertices.Next();
			if (m_shapeVertices.Done()) {
				m_pathVertices.Next();
				m_shapeVertices = m_mesh->m_shape.Vertices();
			}
		}

	private:

		const ExtrudeMesh* m_mesh;

		decltype(std::declval<const Shape &>().Vertices()) m_shapeVertices;

		decltype(std::declval<const Path &>().Vertices()) m_pathVertices;

		VertexGenerator(const ExtrudeMesh& mesh) :
			m_mesh{&mesh},
			m_shapeVertices{mesh.m_shape.Vertices()},
			m_pathVertices{mesh.m_path.Vertices()}
		{ }

	friend class ExtrudeMesh;
	};

	TriangleGenerator Triangles() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }

	/// @param shape Shape to be extruded.
	/// @param path Path to extrude along.
	ExtrudeMesh(Shape shape, Path path) :
		m_shape{std::move(shape)},
		m_path{std::move(path)},
		m_shapeVertexCount{Count(m_shape.Vertices())}
	{ }

private:

	Shape m_shape;

	Path m_path;

	int m_shapeVertexCount;

};



template <typename Shape, typename Path>
ExtrudeMesh<Shape, Path> MakeExtrudeMesh(Shape shape, Path path) {
	return ExtrudeMesh<Shape, Path>{std::move(shape), std::move(path)};
}


}

