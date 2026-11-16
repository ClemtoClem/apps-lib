
#include "generator/AnyMesh.hpp"


using namespace generator;



AnyMesh::Base::~Base() { }


AnyMesh::AnyMesh(const AnyMesh& that) :
	m_base{that.m_base->Clone()}
{ }


AnyMesh& AnyMesh::operator=(const AnyMesh& that) {
	m_base = that.m_base->Clone();
	return *this;
}


AnyGenerator<Triangle> AnyMesh::Triangles() const noexcept {
	return m_base->Triangles();
}


AnyGenerator<MeshVertex> AnyMesh::Vertices() const noexcept {
	return m_base->Vertices();
}

