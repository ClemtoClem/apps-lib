
#include "generator/KnotPath.hpp"


using namespace generator;


namespace {

math::FVector3 Knot(int p, int q, float t) {
	t *= math::Radians(360.0f);

	const float pt = static_cast<float>(p) * t;
	const float qt = static_cast<float>(q) * t;

	const float sinpt = std::sin(pt);
	const float cospt = std::cos(pt);
	const float sinqt = std::sin(qt);
	const float cosqt = std::cos(qt);

	const float r = 0.5f * (2.0f + sinqt);

	return math::FVector3{r * cospt, r * sinpt, r * cosqt};
}

}


KnotPath::KnotPath(
	int p,
	int q,
	int segments
) :
	m_parametricPath{
		[p, q] (float t) {
			PathVertex vertex;

			vertex.position = Knot(p, q, t);

			const math::FVector3 prev = Knot(p, q, t - 0.01f);
			const math::FVector3 next = Knot(p, q, t + 0.01f);

			vertex.tangent = math::Normalize(next - prev);

			vertex.normal = math::Normalize(math::Cross(next - prev, next + prev));

			vertex.texCoord = t;

			return vertex;
		},
		segments
	}
{ }


