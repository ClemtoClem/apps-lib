#pragma once
#include "Edge.hpp"
#include "TransformPath.hpp"


namespace generator {


/// Flips mesh inside out. Reverses triangles and normals.
template <typename Path>
class FlipPath
{
private:

	using Impl = TransformPath<Path>;
	Impl m_transformPath;

public:

	class EdgeGenerator {
	public:

		Edge Generate() const {
			Edge edge = m_edges.Generate();
			std::swap(edge.vertices[0], edge.vertices[1]);
			return edge;
		}

		bool Done() const noexcept { return m_edges.Done(); }

		void Next() { m_edges.Next(); }

	private:

		typename EdgeGeneratorType<TransformPath<Path>>::Type m_edges;

		EdgeGenerator(const TransformPath<Path>& path) :
			m_edges{path.Edges()}
		{ }

	friend class FlipPath;
	};

	/// @param path Source data path.
	FlipPath(Path path) :
		m_transformPath{
			std::move(path),
			[] (PathVertex& vertex) {
				vertex.tangent *= -1.0f;
				vertex.normal *= -1.0f;
			}
		}
	{ }

	EdgeGenerator Edges() const noexcept { return EdgeGenerator{m_transformPath}; }

	using VertexGenerator = typename Impl::VertexGenerator;

	VertexGenerator Vertices() const noexcept { return m_transformPath.Vertices(); }


};


template <typename Path>
FlipPath<Path> MakeFlipPath(Path path) {
	return FlipPath<Path>{std::move(path)};
}


}



