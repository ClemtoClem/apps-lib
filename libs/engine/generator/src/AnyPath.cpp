#include "generator/AnyPath.hpp"


using namespace generator;


AnyPath::Base::~Base() { }


AnyPath::AnyPath(const AnyPath& that) :
	m_base{that.m_base->Clone()}
{ }


AnyPath& AnyPath::operator=(const AnyPath& that) {
	m_base = that.m_base->Clone();
	return *this;
}


AnyGenerator<Edge> AnyPath::Edges() const noexcept {
	return m_base->Edges();
}


AnyGenerator<PathVertex> AnyPath::Vertices() const noexcept {
	return m_base->Vertices();
}
