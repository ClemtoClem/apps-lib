
#include <cassert>
#include <cstdlib>
#include "generator/EmptyMesh.hpp"


using namespace generator;


EmptyMesh::TriangleGenerator::TriangleGenerator() { }

Triangle EmptyMesh::TriangleGenerator::Generate() const {
	assert(!"Called generate on an EmptyMesh!");
	std::abort(); // précondition violée : générateur vide
}

bool EmptyMesh::TriangleGenerator::Done() const noexcept {
	return true;
}

void EmptyMesh::TriangleGenerator::Next() {
	assert(!"Called next on an EmptyMesh!");
	std::abort(); // précondition violée : générateur vide
}




EmptyMesh::VertexGenerator::VertexGenerator() { }

MeshVertex EmptyMesh::VertexGenerator::Generate() const {
	assert(!"Called generate on an EmptyMesh!");
	std::abort(); // précondition violée : générateur vide
}

bool EmptyMesh::VertexGenerator::Done() const noexcept {
	return true;
}

void EmptyMesh::VertexGenerator::Next() {
	assert(!"Called next on an EmptyMesh!");
	std::abort(); // précondition violée : générateur vide
}



EmptyMesh::EmptyMesh() {}

EmptyMesh::TriangleGenerator EmptyMesh::Triangles() const noexcept {
	return {};
}

EmptyMesh::VertexGenerator EmptyMesh::Vertices() const noexcept {
	return {};
}

