#pragma once
#include "Iterator.hpp"
#include "PathVertex.hpp"
#include "Edge.hpp"

namespace generator {


/// Empty path with zero vertices and edges.
class EmptyPath {
public:

	class EdgeGenerator {
	public:

		Edge Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		EdgeGenerator();

	friend class EmptyPath;
	};

	class VertexGenerator {
	public:

		PathVertex Generate() const;
		bool Done() const noexcept;
		void Next();

	private:

		VertexGenerator();

	friend class EmptyPath;
	};

	EmptyPath();

	EdgeGenerator Edges() const noexcept;

	VertexGenerator Vertices() const noexcept;

};


}

