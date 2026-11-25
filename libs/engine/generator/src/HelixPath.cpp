
#include "generator/HelixPath.hpp"


using namespace generator;


HelixPath::HelixPath(
	float radius,
	float size,
	int segments,
	float start,
	float sweep
) :
	m_parametricPath{
		[radius, size, start, sweep] (float t) {
			PathVertex vertex;
			const float angle = start + t * sweep;
			const float sine = std::sin(angle);
			const float cosine = std::cos(angle);

			vertex.position = math::FVector3{
				radius * cosine,
				radius * sine,
				2.0f * t * size - size
			};

			vertex.tangent = math::Normalize(math::FVector3{
				-radius * sine,
				radius * cosine,
				2.0f * size / sweep,
			});

			vertex.normal = math::FVector3{cosine, sine, 0.0f};

			vertex.texCoord = t;

			return vertex;
		},
		segments
	}
{ }


