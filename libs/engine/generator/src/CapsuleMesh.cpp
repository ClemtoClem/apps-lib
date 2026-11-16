
#include "generator/CapsuleMesh.hpp"


using namespace generator;


CapsuleMesh::CapsuleMesh(
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
		{
			{radius, slices, rings, start, sweep, 0.0f, math::Radians(90.0f)},
			{0.0f, 0.0f, size}
		},
		{
			{
				radius, slices, rings, start, sweep,
				math::Radians(90.0f), math::Radians(90.0f)
			},
			{0.0f, 0.0f, -size}
		}
	}
{

}


