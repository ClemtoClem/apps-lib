#pragma once
#include "ShapeVertex.hpp"
#include "EmptyShape.hpp"
#include "utils.hpp"

namespace generator {


/// Merges (concatenates) multiple shapes together.
template <typename... Shape>
class MergeShape; // undefined


template <>
class MergeShape<> : public EmptyShape { };


template <typename Head, typename... Tail>
class MergeShape<Head, Tail...> {
public:

	class EdgeGenerator {
	public:

		Edge Generate() const {
			if (!m_head.Done()) return m_head.Generate();

			Edge edge = m_tail.Generate();
			edge.vertices += m_headVertexCount;
			return edge;
		}

		bool Done() const noexcept { return m_mAllDone; }

		void Next() {
			if (!m_head.Done()) m_head.Next();
			else m_tail.Next();

			m_mAllDone = m_tail.Done() && m_head.Done();
		}

	private:

		typename EdgeGeneratorType<Head>::Type  m_head;
		typename EdgeGeneratorType<MergeShape<Tail...>>::Type m_tail;

		int m_headVertexCount;

		bool m_mAllDone;

		EdgeGenerator(const MergeShape& shape) :
			m_head{shape.m_head.Edges()},
			m_tail(shape.m_tail.Edges()),
			m_headVertexCount{Count(shape.m_head.Vertices())},
			m_mAllDone{m_tail.Done() && m_head.Done()}
		{ }

	friend class MergeShape<Head, Tail...>;
	};


	class VertexGenerator {
	public:

		ShapeVertex Generate() const {
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
		typename VertexGeneratorType<MergeShape<Tail...>>::Type m_tail;
		bool m_mAllDone;

		VertexGenerator(const MergeShape& shape) :
			m_head{shape.m_head.Vertices()},
			m_tail(shape.m_tail.Vertices()),
			m_mAllDone{m_tail.Done() && m_head.Done()}
		{ }

	friend class MergeShape<Head, Tail...>;
	};



	MergeShape(Head head, Tail... tail) :
		m_head{head},
		m_tail{tail...}
	{ }

	EdgeGenerator Edges() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }


private:

	Head m_head;
	MergeShape<Tail...> m_tail;

};



template <typename... Shape>
MergeShape<Shape...> MakeMergeShape(Shape... shapes) {
	return MergeShape<Shape...>{std::move(shapes)...};
}

}

