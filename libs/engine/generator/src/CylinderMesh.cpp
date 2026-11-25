
#include "generator/CylinderMesh.hpp"

using namespace generator;

CylinderMesh::CylinderMesh(
	float radius,
	float size,
	int slices,
	int segments,
	float start,
	float sweep
) :
	m_axisSwapMesh{
		{
			{{size, radius}, {-size, radius}, segments},
			{1.0f, 0.0f}, slices, start, sweep
		},
		Axis::Y, Axis::Z, Axis::X
	}
{ }


