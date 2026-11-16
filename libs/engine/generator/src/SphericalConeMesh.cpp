#include "generator/SphericalConeMesh.hpp"


using namespace generator;


namespace {

float CalcSize(float radius, float size) {
	return 0.5f * std::sqrt(std::pow(2.0f * size, 2.0f) - radius * radius);
}

}



SphericalConeMesh::SphericalConeMesh(
	float radius,
	float size,
	int slices,
	int segments,
	int rings,
	float start,
	float sweep
) :
	m_translateMesh{
		{
			{radius, CalcSize(radius, size), slices, segments, start, sweep},
			{
				{
					{
						2.0f * size, slices, rings,
						start, sweep, 0.0f, std::asin(radius/(2.0f*size))
					},
					{0.0f, 0.0f, -CalcSize(radius, size)}
				},
				false, false, true
			}
		},
		{0.0f, 0.0f, size - CalcSize(radius, size)}
	}
{ }

