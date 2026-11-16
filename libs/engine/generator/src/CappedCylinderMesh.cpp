
#include "generator/CappedCylinderMesh.hpp"


using namespace generator;
using namespace generator::detail;


Cap::Cap(
	float radius,
	float distance,
	int slices,
	int rings,
	float start,
	float sweep
) :
	m_translateMesh{
		{radius, 0.0f, slices, rings, start, sweep},
		{0.0f, 0.0f, distance}
	}
{ }


CappedCylinderMesh::CappedCylinderMesh(
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
		{radius, size, slices, rings, start, sweep},
		{{{radius, -size, slices, rings, start, sweep}}, true, false}
	}
{ }

