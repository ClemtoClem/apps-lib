#pragma once
#include <memory>


#include "AnyGenerator.hpp"
#include "Edge.hpp"
#include "ShapeVertex.hpp"


namespace generator {


/// A type erasing container that can store any shape.
class AnyShape
{
public:

	template <typename Shape>
	AnyShape(Shape shape) :
		m_base{new Derived<Shape>{std::move(shape)}}
	{ }

	AnyShape(const AnyShape& that);

	AnyShape(AnyShape&&) = default;

	AnyShape& operator=(const AnyShape& that);

	AnyShape& operator=(AnyShape&&) = default;

	AnyGenerator<Edge> Edges() const noexcept;

	AnyGenerator<ShapeVertex> Vertices() const noexcept;

private:

	class Base {
	public:
		virtual ~Base();
		virtual std::unique_ptr<Base> Clone() const = 0;
		virtual AnyGenerator<Edge> Edges() const = 0;
		virtual AnyGenerator<ShapeVertex> Vertices() const = 0;
	};

	template <typename Shape>
	class Derived : public Base {
	public:

		Derived(Shape value) : shape(std::move(value)) { }

		virtual std::unique_ptr<Base> Clone() const override {
			return std::unique_ptr<Base>{new Derived{shape}};
		}

		virtual AnyGenerator<Edge> Edges() const override {
			return shape.Edges();
		}

		virtual AnyGenerator<ShapeVertex> Vertices() const override {
			return shape.Vertices();
		}

		Shape shape;
	};

	std::unique_ptr<Base> m_base;

};

}


