#pragma once
#include <memory>

#include "AnyGenerator.hpp"
#include "MeshVertex.hpp"
#include "Triangle.hpp"


namespace generator {


/// A type erasing container that can store any mesh.
class AnyMesh 
{
public:

	template <typename Mesh>
	AnyMesh(Mesh mesh) :
		m_base{new Derived<Mesh>{std::move(mesh)}}
	{ }

	AnyMesh(const AnyMesh& that);

	AnyMesh(AnyMesh&&) = default;

	AnyMesh& operator=(const AnyMesh& that);

	AnyMesh& operator=(AnyMesh&&) = default;

	AnyGenerator<Triangle> Triangles() const noexcept;

	AnyGenerator<MeshVertex> Vertices() const noexcept;

private:

	class Base {
	public:
		virtual ~Base();
		virtual std::unique_ptr<Base> Clone() const = 0;
		virtual AnyGenerator<Triangle> Triangles() const = 0;
		virtual AnyGenerator<MeshVertex> Vertices() const = 0;
	};

	template <typename Mesh>
	class Derived : public Base {
	public:

		Derived(Mesh value) : mesh(std::move(value)) { }

		virtual std::unique_ptr<Base> Clone() const override {
			return std::unique_ptr<Base>{new Derived{mesh}};
		}

		virtual AnyGenerator<Triangle> Triangles() const override { 
			return mesh.Triangles();
		}

		virtual AnyGenerator<MeshVertex> Vertices() const override { 
			return mesh.Vertices();
		}

		Mesh mesh;

	};

	std::unique_ptr<Base> m_base;

};

}


