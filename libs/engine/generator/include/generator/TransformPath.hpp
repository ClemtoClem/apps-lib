#pragma once
#include <functional>

#include "PathVertex.hpp"
#include "utils.hpp"


namespace generator {


/// Apply a mutator function to each vertex.
template <typename Path>
class TransformPath
{
private:

	using Impl = Path;
	Impl m_path;

public:

	class VertexGenerator {
	public:

		PathVertex Generate() const {
			auto vertex = m_vertices.Generate();
			m_path->m_mutate(vertex);
			return vertex;
		}

		bool Done() const noexcept { return m_vertices.Done(); }

		void Next() { m_vertices.Next(); }

	private:

		VertexGenerator(const TransformPath& path) :
			m_path{&path},
			m_vertices{path.m_path.Vertices()}
		{ }

		const TransformPath* m_path;

		typename VertexGeneratorType<Path>::Type m_vertices;

	friend class TransformPath;
	};

	/// @param path Source data path.
	/// @param mutate Callback function that gets called once per vertex.
	TransformPath(Path path, std::function<void(PathVertex&)> mutate) :
		m_path{std::move(path)},
		m_mutate{mutate}
	 { }

	VertexGenerator Vertices() const noexcept { return*this; }

	using EdgeGenerator = typename Impl::EdgeGenerator;

	EdgeGenerator Edges() const noexcept { return m_path.Edges(); }

private:

	std::function<void(PathVertex&)> m_mutate;

};


template <typename Path>
TransformPath<Path> MakeTransformPath(
	Path path, std::function<void(PathVertex&)> mutate
) {
	return TransformPath<Path>{std::move(path), std::move(mutate)};
}


}

