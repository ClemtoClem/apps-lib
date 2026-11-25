
#include "generator/IcoSphereMesh.hpp"


using namespace generator;


IcoSphereMesh::IcoSphereMesh(float radius, int segments) :
	m_spherifyMesh{{1.0f, segments}, radius, 1.0f}
{ }


