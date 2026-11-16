#ifndef UUID_580A4434BBE9A17A6302CC90C22DE898
#define UUID_580A4434BBE9A17A6302CC90C22DE898

#include "utils.hpp"
#include "PathVertex.hpp"
#include "Edge.hpp"

namespace generator
{


/**
 * Repeats the same path a given number of time at given intervals.
 */
template <typename Path>
class RepeatPath
{
public:

	class EdgeGenerator
	{
	public:

		bool Done() const noexcept
		{
			return m_mIndex >= m_mRepeatPath->m_mInstances;
		}

		Edge Generate() const
		{
			Edge temp = m_mEdges.Generate();
			temp.vertices += m_mDelta;
			return temp;
		}

		void Next() noexcept
		{
			m_mEdges.Next();

			if (m_mEdges.Done()) {
				++m_mIndex;
				m_mDelta += m_mRepeatPath->m_mVertexCount;
				m_mEdges = m_mRepeatPath->m_mPath.Edges();
			}
		}

	private:

		const RepeatPath* m_mRepeatPath;

		typename EdgeGeneratorType<Path>::Type m_mEdges;

		int m_mIndex;

		int m_mDelta;

		explicit EdgeGenerator(const RepeatPath* repeatPath) noexcept :
			m_mRepeatPath{repeatPath},
			m_mEdges{repeatPath->m_mPath.Edges()},
			m_mIndex{repeatPath->m_mVertexCount > 0 ? 0 : repeatPath->m_mInstances},
			m_mDelta{0}
		{

		}

	friend class RepeatPath;
	};


	class VertexGenerator
	{
	public:

		bool Done() const noexcept
		{
			return m_mIndex >= m_mRepeatPath->m_mInstances;
		}

		PathVertex Generate() const
		{
			PathVertex temp = m_mVertices.Generate();
			temp.position += m_mDelta;
			return temp;
		}

		void Next()
		{
			m_mVertices.Next();

			if (m_mVertices.Done()) {
				++m_mIndex;
				m_mDelta += m_mRepeatPath->m_mDelta;
				m_mVertices = m_mRepeatPath->m_mPath.Vertices();
			}
		}

	private:

		explicit VertexGenerator(const RepeatPath* repeatPath) :
			m_mRepeatPath{repeatPath},
			m_mVertices{repeatPath->m_mPath.Vertices()},
			m_mIndex{repeatPath->m_mVertexCount > 0 ? 0 : repeatPath->m_mInstances},
			m_mDelta{}
		{ }

		const RepeatPath* m_mRepeatPath;

		typename VertexGeneratorType<Path>::Type m_mVertices;

		int m_mIndex;

		math::FVector3 m_mDelta;

		int CountVertices() const noexcept
		{
			if (m_mRepeatPath->m_mInstances < 1) return 0;

			return
				m_mRepeatPath->m_mVertexCount *
				(m_mRepeatPath->m_mInstances - m_mIndex - 1) +
				Count(m_mVertices);
		}

	friend
		int Count(const VertexGenerator& generator) noexcept
		{
			return generator.CountVertices();
		}

	friend class RepeatPath;
	};


	/// @param path The path to repeat.
	/// @param instances Number of times to repeat. If <1 an empty path results.
	/// @param delta An offset aplied to each copy.
	explicit RepeatPath(Path path, int instances, const math::FVector3& delta) noexcept :
		m_mPath{std::move(path)},
		m_mInstances{instances},
		m_mDelta{delta},
		m_mVertexCount{Count(m_mPath.Vertices())}
	{ }

	EdgeGenerator Edges() const noexcept
	{
		return EdgeGenerator{this};
	}

	VertexGenerator Vertices() const noexcept
	{
		return VertexGenerator{this};
	}

private:

	Path m_mPath;

	int m_mInstances;

	math::FVector3 m_mDelta;

	int m_mVertexCount;

};


template <typename Path>
RepeatPath<Path> MakeRepeatPath(Path path, int instances, const math::FVector3& delta) noexcept
{
	return RepeatPath<Path>{std::move(path), instances, delta};
}


}

#endif
