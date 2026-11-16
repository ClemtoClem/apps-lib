#include "generator/SphereMesh.hpp"

using namespace generator;


SphereMesh::SphereMesh(
	float radius,
	int slices,
	int segments,
	float sliceStart,
	float sliceSweep,
	float segmentStart,
	float segmentSweep
) :
	m_axisSwapMesh{
		{
			{radius, segments, segmentStart, segmentSweep},
			{1.0f, 0.0f}, slices, sliceStart, sliceSweep
		},
		Axis::Y, Axis::Z, Axis::X
	}
{ }


