#pragma once
#include <memory>

#include "AnyGenerator.hpp"
#include "Edge.hpp"
#include "PathVertex.hpp"


namespace generator {


/// A type erasing container that can store any path.
class AnyPath
{
public:

	template <typename Path>
	AnyPath(Path path) :
		m_base{new Derived<Path>{std::move(path)}}
	{ }

	AnyPath(const AnyPath& that);

	AnyPath(AnyPath&&) = default;

	AnyPath& operator=(const AnyPath& that);

	AnyPath& operator=(AnyPath&&) = default;

	AnyGenerator<Edge> Edges() const noexcept;

	AnyGenerator<PathVertex> Vertices() const noexcept;

private:

	class Base {
	public:
		virtual ~Base();
		virtual std::unique_ptr<Base> Clone() const = 0;
		virtual AnyGenerator<Edge> Edges() const = 0;
		virtual AnyGenerator<PathVertex> Vertices() const = 0;
	};

	template <typename Path>
	class Derived : public Base {
	public:
		Derived(Path value) : path(std::move(value)) { }

		virtual std::unique_ptr<Base> Clone() const override {
			return std::unique_ptr<Base>{new Derived{path}};
		}

		virtual AnyGenerator<Edge> Edges() const override {
			return path.Edges();
		}

		virtual AnyGenerator<PathVertex> Vertices() const override {
			return path.Vertices();
		}

		Path path;
	};

	std::unique_ptr<Base> m_base;

};

}


