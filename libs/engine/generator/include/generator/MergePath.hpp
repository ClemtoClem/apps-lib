#pragma once
#include "PathVertex.hpp"
#include "EmptyPath.hpp"
#include "utils.hpp"

namespace generator {


/// Merges (concatenates) multiple paths together.
template <typename... Path>
class MergePath; // undefined


template <>
class MergePath<> : public EmptyPath { };


template <typename Head, typename... Tail>
class MergePath<Head, Tail...> {
public:

	class EdgeGenerator {
	public:

		Edge Generate() const {
			if (!m_head.Done()) return m_head.Generate();
			return m_tail.Generate() + m_headVertexCount;
		}

		bool Done() const noexcept { return m_mAllDone; }

		void Next() {
			if (!m_head.Done()) m_head.Next();
			else m_tail.Next();

			m_mAllDone = m_tail.Done() && m_head.Done();
		}

	private:

		typename EdgeGeneratorType<Head>::Type  m_head;
		typename EdgeGeneratorType<MergePath<Tail...>>::Type m_tail;

		int m_headVertexCount;

		bool m_mAllDone;

		EdgeGenerator(const MergePath& path) :
			m_head{path.m_head.Triangles()},
			m_tail(path.m_tail.Triangles()),
			m_headVertexCount{Count(path.m_head.Vertices())},
			m_mAllDone{m_tail.Done() && m_head.Done()}
		{ }

	friend class MergePath<Head, Tail...>;
	};


	class VertexGenerator {
	public:

		PathVertex Generate() const {
			if (!m_head.Done()) return m_head.Generate();
			return m_tail.Generate();
		}

		bool Done() const noexcept { return m_mAllDone; }

		void Next() {
			if (!m_head.Done()) m_head.Next();
			else m_tail.Next();

			m_mAllDone = m_tail.Done() && m_head.Done();
		}

	private:

		typename VertexGeneratorType<Head>::Type m_head;
		typename VertexGeneratorType<MergePath<Tail...>>::Type m_tail;
		bool m_mAllDone;

		VertexGenerator(const MergePath& path) :
			m_head{path.m_head.Vertices()},
			m_tail(path.m_tail.Vertices()),
			m_mAllDone{m_tail.Done() && m_head.Done()}
		{ }

	friend class MergePath<Head, Tail...>;
	};



	MergePath(Head head, Tail... tail) :
		m_head{head},
		m_tail{tail...}
	{ }

	EdgeGenerator Edges() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }


private:

	Head m_head;
	MergePath<Tail...> m_tail;

};



template <typename... Path>
MergePath<Path...> MakeMergePath(Path... paths) {
	return MergePath<Path...>{std::move(paths)...};
}

}

