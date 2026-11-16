
#include <cassert>
#include <cstdlib>
#include "generator/EmptyPath.hpp"


using namespace generator;



Edge EmptyPath::EdgeGenerator::Generate() const {
	assert(!"Called generate on an EmptyPath!");
	std::abort(); // précondition violée : générateur vide
}

bool EmptyPath::EdgeGenerator::Done() const noexcept {
	return true;
}

void EmptyPath::EdgeGenerator::Next() {
	assert(!"Called next on an EmptyPath!");
	std::abort(); // précondition violée : générateur vide
}



EmptyPath::EdgeGenerator::EdgeGenerator() {}




PathVertex EmptyPath::VertexGenerator::Generate() const {
	assert(!"Called generate on an EmptyPath!");
	std::abort(); // précondition violée : générateur vide
}

bool EmptyPath::VertexGenerator::Done() const noexcept {
	return true;
}

void EmptyPath::VertexGenerator::Next() {
	assert(!"Called next on an EmptyPath!");
	std::abort(); // précondition violée : générateur vide
}


EmptyPath::VertexGenerator::VertexGenerator() { }


EmptyPath::EmptyPath() {}

EmptyPath::EdgeGenerator EmptyPath::Edges() const noexcept {
	return {};
}

EmptyPath::VertexGenerator EmptyPath::Vertices() const noexcept {
	return {};
}



