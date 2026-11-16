
#include "generator/TorusKnotMesh.hpp"


using namespace generator;


TorusKnotMesh::TorusKnotMesh(
	int p,
	int q,
	int slices,
	int segments
) :
	m_extrudeMesh{
		{0.25f, slices, 0.0f, math::Radians(360.0f)},
		{p, q, segments}
	}
{ }

