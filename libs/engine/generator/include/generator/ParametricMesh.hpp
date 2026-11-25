#pragma once
#include <functional>


#include "Triangle.hpp"
#include "MeshVertex.hpp"


namespace generator {


/// A mesh with values evaluated using a callback function.
class ParametricMesh {
public:

	class TriangleGenerator {
	public:

		Triangle Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		TriangleGenerator(const ParametricMesh& mesh);

		const ParametricMesh* m_mesh;

		math::IVec2 m_i;

		bool m_even;

	friend class ParametricMesh;
	};

	class VertexGenerator {
	public:

		MeshVertex Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		VertexGenerator(const ParametricMesh& mesh);

		const ParametricMesh* m_mesh;

		math::IVec2 m_i;

	friend class ParametricMesh;
	};


	/// @param eval A callback that returns a MeshVertex for a given value.
	/// @param segments The number of segments along the surface.
	/// Both should be >= 1.f If either is zero an empty mesh is generated.
	explicit ParametricMesh(
		std::function<MeshVertex(const math::FVector2& t)> eval,
		const math::IVec2& segments = {16, 16}
	) noexcept;

	TriangleGenerator Triangles() const noexcept;

	VertexGenerator Vertices() const noexcept;

private:

	std::function<MeshVertex(const math::FVector2& t)> m_eval;

	math::IVec2 m_segments;

	math::FVector2 m_delta;

};


}

