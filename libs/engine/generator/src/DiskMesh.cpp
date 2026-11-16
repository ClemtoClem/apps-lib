#include "generator/DiskMesh.hpp"


using namespace generator;


DiskMesh::DiskMesh(
	float radius,
	float innerRadius,
	int slices,
	int rings,
	float start,
	float sweep
) :
	m_axisSwapMesh{
		{
			{{0.0f, innerRadius}, {0.0f, radius}, rings},
			{1.0f, 0.0f}, slices, start, sweep
		},
		Axis::Y, Axis::Z, Axis::X
	}
{ }
