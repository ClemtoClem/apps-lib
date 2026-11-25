#pragma once
#include "ShapeVertex.hpp"
#include "Edge.hpp"


namespace generator {


/// Empty shape with zero vertices and edges.
class EmptyShape {
public:

	class EdgeGenerator {
	public:
		Edge Generate() const;
		bool Done() const noexcept;
		void Next();
	private:
		EdgeGenerator();
	friend class EmptyShape;
	};

	class VertexGenerator {
	public:
		ShapeVertex Generate() const;
		bool Done() const noexcept;
		void Next();
	private:
		VertexGenerator();
	friend class EmptyShape;
	};

	EdgeGenerator Edges() const noexcept;

	VertexGenerator Vertices() const noexcept;

};


}

