#pragma once
#include <functional>


#include "Edge.hpp"
#include "ShapeVertex.hpp"

namespace generator {


/// A shape with values evaluated using a callback function.
class ParametricShape {
public:

	class EdgeGenerator {
	public:

		Edge Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		EdgeGenerator(const ParametricShape& shape);

		const ParametricShape* m_shape;

		int m_i;

	friend class ParametricShape;
	};

	class VertexGenerator {
	public:

		ShapeVertex Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		VertexGenerator(const ParametricShape& shape);

		const ParametricShape* m_shape;

		int m_i;

	friend class ParametricShape;
	};


	/// @param eval A callback that returns a ShapeVertex for a given value.
	/// @param segments The number of segments along the shape.
	/// Should be >= 1.f Zero yields an empty shape.
	explicit ParametricShape(
		std::function<ShapeVertex(float)> eval,
		int segments = 16
	) noexcept;

	EdgeGenerator Edges() const noexcept;

	VertexGenerator Vertices() const noexcept;

private:

	std::function<ShapeVertex(float)> m_eval;

	int m_segments;

	float m_delta;

};


}

