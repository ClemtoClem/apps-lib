#ifndef UUID_768F8730AFFB92752C688F5439A49976
#define UUID_768F8730AFFB92752C688F5439A49976

#include "utils.hpp"
#include "ShapeVertex.hpp"
#include "Edge.hpp"

namespace generator
{


/**
 * Repeats the same shape a given number of time at given intervals.
 */
template <typename Shape>
class RepeatShape
{
public:

	class EdgeGenerator
	{
	public:

		bool Done() const noexcept
		{
			return m_mIndex >= m_mRepeatShape->m_mInstances;
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
				m_mDelta += m_mRepeatShape->m_mVertexCount;
				m_mEdges = m_mRepeatShape->m_mShape.Edges();
			}
		}

	private:

		const RepeatShape* m_mRepeatShape;

		typename EdgeGeneratorType<Shape>::Type m_mEdges;

		int m_mIndex;

		int m_mDelta;

		explicit EdgeGenerator(const RepeatShape* repeatShape) noexcept :
			m_mRepeatShape{repeatShape},
			m_mEdges{repeatShape->m_mShape.Edges()},
			m_mIndex{repeatShape->m_mVertexCount > 0 ? 0 : repeatShape->m_mInstances},
			m_mDelta{0}
		{ }

		int CountEdges() const noexcept
		{
			if (m_mRepeatShape->m_mInstances < 1) return 0;

			return
				Count(m_mRepeatShape->m_mShape.Edges()) *
				(m_mRepeatShape->m_mInstances - m_mIndex - 1) +
				Count(m_mEdges);
		}

	friend
		int Count(const EdgeGenerator& generator) noexcept
		{
			return generator.CountEdges();
		}

	friend class RepeatShape;
	};


	class VertexGenerator
	{
	public:

		bool Done() const noexcept
		{
			return m_mIndex >= m_mRepeatShape->m_mInstances;
		}

		ShapeVertex Generate() const
		{
			ShapeVertex temp = m_mVertices.Generate();
			temp.position += m_mDelta;
			return temp;
		}

		void Next()
		{
			m_mVertices.Next();

			if (m_mVertices.Done()) {
				++m_mIndex;
				m_mDelta += m_mRepeatShape->m_mDelta;
				m_mVertices = m_mRepeatShape->m_mShape.Vertices();
			}
		}

	private:

		explicit VertexGenerator(const RepeatShape* repeatShape) :
			m_mRepeatShape{repeatShape},
			m_mVertices{repeatShape->m_mShape.Vertices()},
			m_mIndex{repeatShape->m_mVertexCount > 0 ? 0 : repeatShape->m_mInstances},
			m_mDelta{}
		{ }

		const RepeatShape* m_mRepeatShape;

		typename VertexGeneratorType<Shape>::Type m_mVertices;

		int m_mIndex;

		math::FVector2 m_mDelta;

		int CountVertices() const noexcept
		{
			if (m_mRepeatShape->m_mInstances < 1) return 0;

			return
				m_mRepeatShape->m_mVertexCount *
				(m_mRepeatShape->m_mInstances - m_mIndex - 1) +
				Count(m_mVertices);
		}

	friend
		int Count(const VertexGenerator& generator) noexcept
		{
			return generator.CountVertices();
		}

	friend class RepeatShape;
	};


	/// @param shape The shape to repeat.
	/// @param instances Number of times to repeat. If <1 an empty shape results.
	/// @param delta An offset aplied to each copy.
	explicit RepeatShape(Shape shape, int instances, const math::FVector2& delta) noexcept :
		m_mShape{std::move(shape)},
		m_mInstances{instances},
		m_mDelta{delta},
		m_mVertexCount{Count(m_mShape.Vertices())}
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

	Shape m_mShape;

	int m_mInstances;

	math::FVector2 m_mDelta;

	int m_mVertexCount;

};


template <typename Shape>
RepeatShape<Shape> MakeRepeatShape(Shape shape, int instances, const math::FVector2& delta) noexcept
{
	return RepeatShape<Shape>{std::move(shape), instances, delta};
}


}

#endif
