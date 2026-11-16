#pragma once
#include <array>
#include <algorithm>
#include <sstream>
#include <memory>
#include <map>
#include <vector>

#include "math.hpp"

#include "Iterator.hpp"
#include "MeshVertex.hpp"
#include "PathVertex.hpp"
#include "ShapeVertex.hpp"
#include "Triangle.hpp"

namespace generator {


/// A simple svg writer class for generating preview and debug images.
class SvgWriter {
private:

	class BaseElem {
	public:

		float z;

		math::FVector3 color;

		BaseElem(float z, const math::FVector3& color);

		virtual ~BaseElem();

		// Writes this svg element to a stream.
		virtual void Stream(std::ostream&) const = 0;
	};


	class VertexElem : public BaseElem {
	public:

		math::FVector3 p;

		VertexElem(const math::FVector3& p, const math::FVector3& color);

		virtual void Stream(std::ostream& os) const override;

	};

	class LineElem : public BaseElem {
	public:

		math::FVector3 p1, p2;

		LineElem(
			const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& color
		);

		virtual void Stream(std::ostream& os) const override;

	};

	class TriangleElem : public BaseElem {
	public:

		std::array<math::FVector3, 3> p;

		TriangleElem(
			const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& p3,
			const math::FVector3& color
		);

		virtual void Stream(std::ostream& os) const override;

	};

	math::FVector3 Project(const math::FVector3& p) const;

	math::FVector3 NormalToColor(const math::FVector3& normal) const;

	math::IVec2 m_size;

	math::FMatrix4 m_viewMatrix;
	math::FMatrix4 m_projMatrix;
	math::FMatrix4 m_viewProjMatrix;
	math::IVec2 m_viewportOrigin;
	math::IVec2 m_viewportSize;

	math::FVector3 m_lightDir;

	bool m_cullface;

	mutable std::vector<std::unique_ptr<BaseElem>> m_elems;

public:

	/// @param width Width of the image in pixels
	/// @param height Height of the iamge in pixels
	SvgWriter(int width, int height);

	/// Sets the model view matrix. Default is the identity matrix.
	void ModelView(const math::FMatrix4& matrix);

	/// Sets the projection mode to perspective projection.
	/// Default is the orthographic.
	/// @param fovy Field of view along the y-axis.
	/// @param aspect aspect ratio (should usually match the vieport)
	void Perspective(float fovy, float aspect, float zNear, float zFar);

	/// Sets the projection mode to orthographic projection.
	/// This is the default.
	/// @param left Coordinate that maps to the left edge.
	/// @param right Coordinate that maps to the right edge.
	void Ortho(float left, float right, float bottom, float top);

	/// Sets the viewport. Default fills the whole image.
	void Viewport(int x, int y, int width, int height);

	/// Sets if backfacing triangles should be culled. Default is true.
	void Cullface(bool cullface);

	/// Write one point. Drawn as a circle.
	void WritePoint(
		const math::FVector3& p, const math::FVector3& color = {0.0f, 0.0f, 0.0f}
	);

	/// Write one line.
	void WriteLine(
		const math::FVector3& p1, const math::FVector3& p2,
		const math::FVector3& color = {0.0f, 0.0f, 0.0f}
	);

	/// Write one triangle.
	void WriteTriangle(
		const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& p3,
		const math::FVector3& color
	);

	/// Write one triangle with color automatically calculated from light.
	void WriteTriangle(
		const math::FVector3& p1, const math::FVector3& p2, const math::FVector3& p3
	);

	/// Write all shaped edges and optionally vertices, tangents and normals.
	template <typename Shape>
	void WriteShape(
		const Shape& shape, bool writeVertices = false, bool writeAxis = false
	) {

		std::vector<ShapeVertex> vertices{};
		for (const auto& vertex : shape.Vertices()) {
			vertices.push_back(vertex);
		}

		for (auto e : shape.Edges()) {
			auto p1 = math::FVector3{vertices[static_cast<std::size_t>(e.vertices[0])].position, 0.0f};
			auto p2 = math::FVector3{vertices[static_cast<std::size_t>(e.vertices[1])].position, 0.0f};

			WriteLine(p1, p2, {0.5f, 0.5f, 0.5f});
		}

		if (writeAxis) {
			for (auto v : vertices) {
				auto p1 = math::FVector3{v.position, 0.0f};
				auto p2 = math::FVector3{v.position + 0.1f * v.tangent, 0.0f};
				auto p3 = math::FVector3{v.position + 0.1f * v.Normal(), 0.0f};

				WriteLine(p1, p2, {0.0f, 1.0f, 0.0f});
				WriteLine(p1, p3, {1.0f, 0.0f, 0.0f});
			}
		}

		if (writeVertices) {
			for (auto v : shape.Vertices()) {
					WritePoint(math::FVector3{v.position, 0.0f});
			}
		}
	}

	/// Write all path edges as lines and optionally vertices, tangents, normals
	/// and binormals.
	template <typename Path>
	void WritePath(
		const Path& path, bool writeVertices = false, bool writeAxis = false
	) {

		std::vector<PathVertex> vertices{};
		for (const auto& temp : path.Vertices()) {
			vertices.push_back(temp);
		}

		if (writeAxis) {
			for (const auto & v : path.Vertices()) {
				WriteLine(v.position, v.position + 0.1f * v.tangent, {0.0f, 0.0f, 1.0f});
				WriteLine(v.position, v.position + 0.1f * v.normal, {1.0f, 0.0f, 0.0f});
				WriteLine(v.position, v.position + 0.1f * v.Binormal(), {0.0f, 1.0f, 0.0f});
			}
		}

		if (writeVertices) {
			for (const auto& v : path.Vertices()) {
				WritePoint(v.position + 0.001f * v.normal);
			}
		}

		for (const auto & e : path.Edges()) {
			WriteLine(
				vertices[static_cast<std::size_t>(e.vertices[0])].position, vertices[static_cast<std::size_t>(e.vertices[1])].position
			);
		}

	}

	/// Write all triangles from a mesh.
	template <typename Mesh>
	void WriteMesh(
		const Mesh& mesh, bool writeVertices = false, bool writeNormals = false
	) {

		std::vector<MeshVertex> vertices{};
		for (const MeshVertex& vertex : mesh.Vertices()) {
			vertices.push_back(vertex);
		}

		for (Triangle t : mesh.Triangles()) {
			WriteTriangle(
				vertices[static_cast<std::size_t>(t.vertices[0])].position,
				vertices[static_cast<std::size_t>(t.vertices[1])].position,
				vertices[static_cast<std::size_t>(t.vertices[2])].position
			);
		}

		if (writeVertices) {
			for (const auto & v : vertices) {
				WritePoint(v.position);
			}
		}

		// Normals
		if (writeNormals) {
			for (const auto & v : vertices) {
				WriteLine(v.position, v.position + 0.1f * v.normal, {0.0f, 0.0f, 1.0f});
			}
		}

	}

	/// Generates svg xml from the data written so far.
	std::string Str() const;

};

}

