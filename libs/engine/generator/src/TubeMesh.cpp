
#include "generator/TubeMesh.hpp"


using namespace generator;


TubeMesh::TubeMesh(
	float radius,
	float innerRadius,
	float size,
	int slices,
	int segments,
	float start,
	float sweep
) :
	m_mergeMesh{
		{radius, size, slices, segments, start, sweep},
		{{{innerRadius, size, slices, segments, start, sweep}, true, false}}
	}
{ }

