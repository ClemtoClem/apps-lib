
#include <cassert>
#include <cstdlib>
#include "generator/EmptyShape.hpp"

#include <stdexcept>

using namespace generator;


EmptyShape::EdgeGenerator::EdgeGenerator() { }


Edge EmptyShape::EdgeGenerator::Generate() const {
	assert(!"Called generate on an EmptyShape!");
	std::abort(); // précondition violée : générateur vide
}

bool EmptyShape::EdgeGenerator::Done() const noexcept {
	return true;
}

void EmptyShape::EdgeGenerator::Next() {
	assert(!"Called next on an EmptyShape!");
	std::abort(); // précondition violée : générateur vide
}




EmptyShape::VertexGenerator::VertexGenerator() { }


ShapeVertex EmptyShape::VertexGenerator::Generate() const {
	assert(!"Called generate on an EmptyShape!");
	std::abort(); // précondition violée : générateur vide
}

bool EmptyShape::VertexGenerator::Done() const noexcept {
	return true;
}

void EmptyShape::VertexGenerator::Next() {
	assert(!"Called next on an EmptyShape!");
	std::abort(); // précondition violée : générateur vide
}



EmptyShape::EdgeGenerator EmptyShape::Edges() const noexcept {
	return {};
}

EmptyShape::VertexGenerator EmptyShape::Vertices() const noexcept {
	return {};
}


