
#include "generator/TorusMesh.hpp"


using namespace generator;



TorusMesh::TorusMesh(
	float minor,
	float major,
	int slices,
	int segments,
	float minorStart,
	float minorSweep,
	float majorStart,
	float majorSweep
) :
	m_axisSwapMesh{
		{
			{
				{minor, slices, minorStart+math::Radians(90.0f), minorSweep},
				{0.0f, major}
			},
			{1.0f, 0.0f}, segments, majorStart, majorSweep
		},
		Axis::Y, Axis::Z, Axis::X
	}
{ }

