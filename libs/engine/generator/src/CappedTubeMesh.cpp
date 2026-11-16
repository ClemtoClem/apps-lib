
#include "generator/CappedTubeMesh.hpp"

using namespace generator;

using namespace generator::detail;


TubeCap::TubeCap(
	float radius,
	float innerRadius,
	float distance,
	int slices,
	int rings,
	float start,
	float sweep
) :
	m_translateMesh{
		{radius, innerRadius, slices, rings, start, sweep},
		{0.0f, 0.0f, distance}
	}
{ }


CappedTubeMesh::CappedTubeMesh(
	float radius,
	float innerRadius,
	float size,
	int slices,
	int segments,
	int rings,
	float start,
	float sweep
) :
	m_mergeMesh{
		{radius, innerRadius, size, slices, segments, start, sweep},
		{radius, innerRadius, size, slices, rings, start, sweep},
		{{radius, innerRadius, -size, slices, rings, start, sweep}},
	}
{ }

