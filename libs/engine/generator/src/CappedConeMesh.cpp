
#include "generator/CappedConeMesh.hpp"


using namespace generator;


CappedConeMesh::CappedConeMesh(
	float radius,
	float size,
	int slices,
	int segments,
	int rings,
	float start,
	float sweep
) :
	m_mergeMesh{
		{radius, size, slices, segments, start, sweep},
		{{{
			DiskMesh{radius, 0.0f, slices, rings},
			math::FVector3{0.0f, 0.0f, -size}
		}}, true, false}
	}
{ }



