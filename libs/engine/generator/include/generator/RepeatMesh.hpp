#ifndef UUID_34107193C252FF8CC2A084B47CFB15D4
#define UUID_34107193C252FF8CC2A084B47CFB15D4

#include "utils.hpp"
#include "MeshVertex.hpp"
#include "Triangle.hpp"

namespace generator
{


/**
 * Repeats the same mesh a given number of time at given intervals.
 */
template <typename Mesh>
class RepeatMesh
{
public:

	class TriangleGenerator
	{
	public:

		bool Done() const noexcept
		{
			return m_mIndex >= m_mRepeatMesh->m_mInstances;
		}

		Triangle Generate() const
		{
			Triangle temp = m_mTriangles.Generate();
			temp.vertices += m_mDelta;
			return temp;
		}

		void Next() noexcept
		{
			m_mTriangles.Next();

			if (m_mTriangles.Done()) {
				++m_mIndex;
				m_mDelta += m_mRepeatMesh->m_mVertexCount;
				m_mTriangles = m_mRepeatMesh->m_mMesh.Triangles();
			}
		}

	private:

		const RepeatMesh* m_mRepeatMesh;

		typename TriangleGeneratorType<Mesh>::Type m_mTriangles;

		int m_mIndex;

		int m_mDelta;

		explicit TriangleGenerator(const RepeatMesh* repeatMesh) noexcept :
			m_mRepeatMesh{repeatMesh},
			m_mTriangles{repeatMesh->m_mMesh.Triangles()},
			m_mIndex{repeatMesh->m_mVertexCount > 0 ? 0 : repeatMesh->m_mInstances},
			m_mDelta{0}
		{ }

		int CountTriangles() const noexcept
		{
			if (m_mRepeatMesh->m_mInstances < 1) return 0;

			return
				Count(m_mRepeatMesh->m_mMesh.Triangles()) *
				(m_mRepeatMesh->m_mInstances - m_mIndex - 1) +
				Count(m_mTriangles);
		}

	friend
		int Count(const TriangleGenerator& generator) noexcept
		{
			return generator.CountTriangles();
		}

	friend class RepeatMesh;
	};


	class VertexGenerator
	{
	public:

		bool Done() const noexcept
		{
			return m_mIndex >= m_mRepeatMesh->m_mInstances;
		}

		MeshVertex Generate() const
		{
			MeshVertex temp = m_mVertices.Generate();
			temp.position += m_mDelta;
			return temp;
		}

		void Next()
		{
			m_mVertices.Next();

			if (m_mVertices.Done()) {
				++m_mIndex;
				m_mDelta += m_mRepeatMesh->m_mDelta;
				m_mVertices = m_mRepeatMesh->m_mMesh.Vertices();
			}
		}

	private:

		explicit VertexGenerator(const RepeatMesh* repeatMesh) :
			m_mRepeatMesh{repeatMesh},
			m_mVertices{repeatMesh->m_mMesh.Vertices()},
			m_mIndex{repeatMesh->m_mVertexCount > 0 ? 0 : repeatMesh->m_mInstances},
			m_mDelta{}
		{ }

		const RepeatMesh* m_mRepeatMesh;

		typename VertexGeneratorType<Mesh>::Type m_mVertices;

		int m_mIndex;

		math::FVector3 m_mDelta;

		int CountVertices() const noexcept
		{
			if (m_mRepeatMesh->m_mInstances < 1) return 0;

			return
				m_mRepeatMesh->m_mVertexCount *
				(m_mRepeatMesh->m_mInstances - m_mIndex - 1) +
				Count(m_mVertices);
		}

	friend
		int Count(const VertexGenerator& generator) noexcept
		{
			return generator.CountVertices();
		}

	friend class RepeatMesh;
	};


	/// @param mesh The mesh to repeat.
	/// @param instances Number of times to repeat. If <1 an empty mesh results.
	/// @param delta An offset aplied to each copy.
	explicit RepeatMesh(Mesh mesh, int instances, const math::FVector3& delta) noexcept :
		m_mMesh{std::move(mesh)},
		m_mInstances{instances},
		m_mDelta{delta},
		m_mVertexCount{Count(m_mMesh.Vertices())}
	{ }

	TriangleGenerator Triangles() const noexcept
	{
		return TriangleGenerator{this};
	}

	VertexGenerator Vertices() const noexcept
	{
		return VertexGenerator{this};
	}

private:

	Mesh m_mMesh;

	int m_mInstances;

	math::FVector3 m_mDelta;

	int m_mVertexCount;

};





template <typename Mesh>
RepeatMesh<Mesh> MakeRepeatMesh(Mesh mesh, int instances, const math::FVector3& delta) noexcept
{
	return RepeatMesh<Mesh>{std::move(mesh), instances, delta};
}


}

#endif
