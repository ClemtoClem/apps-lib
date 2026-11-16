
#include "generator/ConeMesh.hpp"


using namespace generator;


ConeMesh::ConeMesh(
	float radius,
	float size,
	int slices,
	int segments,
	float start,
	float sweep
) :
	m_axisSwapMesh{
		{
			{{size, 0.0f}, {-size, radius}, segments},
			{1.0f, 0.0f}, slices, start, sweep
		},
		Axis::Y, Axis::Z, Axis::X
	}
{ }
