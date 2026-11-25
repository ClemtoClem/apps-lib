#pragma once
#include "MeshVertex.hpp"
#include "EmptyMesh.hpp"
#include "utils.hpp"

namespace generator {


/// Merges (concatenates) multiple meshes to to together.
template <typename... Mesh>
class MergeMesh; // undefined


template <>
class MergeMesh<> : public EmptyMesh { };


template <typename Head, typename... Tail>
class MergeMesh<Head, Tail...> {
public:

	class TriangleGenerator {
	public:

		Triangle Generate() const {
			if (!m_head.Done()) return m_head.Generate();

			Triangle triangle = m_tail.Generate();
			triangle.vertices += m_headVertexCount;
			return triangle;
		}

		bool Done() const noexcept { return m_mAllDone; }

		void Next() {
			if (!m_head.Done()) m_head.Next();
			else m_tail.Next();

			m_mAllDone = m_tail.Done() && m_head.Done();
		}

	private:

		typename TriangleGeneratorType<Head>::Type  m_head;
		typename TriangleGeneratorType<MergeMesh<Tail...>>::Type m_tail;

		int m_headVertexCount;

		bool m_mAllDone;

		TriangleGenerator(const MergeMesh& mesh) :
			m_head{mesh.m_head.Triangles()},
			m_tail(mesh.m_tail.Triangles()),
			m_headVertexCount{Count(mesh.m_head.Vertices())},
			m_mAllDone{m_tail.Done() && m_head.Done()}
		{ }

	friend class MergeMesh<Head, Tail...>;
	};


	class VertexGenerator {
	public:

		MeshVertex Generate() const {
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
		typename VertexGeneratorType<MergeMesh<Tail...>>::Type m_tail;

		bool m_mAllDone;

		VertexGenerator(const MergeMesh& mesh) :
			m_head{mesh.m_head.Vertices()},
			m_tail(mesh.m_tail.Vertices()),
			m_mAllDone{m_tail.Done() && m_head.Done()}
		{ }

	friend class MergeMesh<Head, Tail...>;
	};



	MergeMesh(Head head, Tail... tail) :
		m_head{std::move(head)},
		m_tail{std::move(tail)...}
	{ }

	TriangleGenerator Triangles() const noexcept { return *this; }

	VertexGenerator Vertices() const noexcept { return *this; }


private:

	Head m_head;
	MergeMesh<Tail...> m_tail;

};



template <typename... Mesh>
MergeMesh<Mesh...> MakeMergeMesh(Mesh... meshes) {
	return MergeMesh<Mesh...>{std::move(meshes)...};
}

}

