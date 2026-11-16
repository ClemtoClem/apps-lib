#include "generator/AnyShape.hpp"


using namespace generator;


AnyShape::Base::~Base() { }


AnyShape::AnyShape(const AnyShape& that) :
	m_base{that.m_base->Clone()}
{ }


AnyShape& AnyShape::operator=(const AnyShape& that) {
	m_base = that.m_base->Clone();
	return *this;
}


AnyGenerator<Edge> AnyShape::Edges() const noexcept {
	return m_base->Edges();
}


AnyGenerator<ShapeVertex> AnyShape::Vertices() const noexcept {
	return m_base->Vertices();
}


