
#include "generator/SpringMesh.hpp"

using namespace generator;


SpringMesh::SpringMesh(
	float minor,
	float major,
	float size,
	int slices,
	int segments,
	float minorStart,
	float minorSweep,
	float majorStart,
	float majorSweep
) :
	m_extrudeMesh{
		{minor, slices, minorStart, minorSweep},
		{major, size, segments, majorStart, majorSweep}
	}
{ }


