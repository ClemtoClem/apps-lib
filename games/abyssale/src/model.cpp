#include "abyssale/model.hpp"

#include <algorithm>
#include <cmath>

namespace abyssale {

SwayParams PlantSway(Model model) noexcept {
	const size_t m = size_t(model);
	if (m >= size_t(Model::Kelp0) && m <= size_t(Model::Kelp2))
		return {1.3f, 0.38f, kKelpHeights[m - size_t(Model::Kelp0)]}; // houle lente
	if (m >= size_t(Model::Frond0) && m <= size_t(Model::Frond1))
		return {0.45f, 0.8f, kFrondHeights[m - size_t(Model::Frond0)]};
	if (m >= size_t(Model::Seagrass0) && m <= size_t(Model::Seagrass2))
		return {0.12f, 1.1f, kSeagrassHeights[m - size_t(Model::Seagrass0)]};
	switch (model) {
	case Model::Tuft: return {0.07f, 1.4f, 0.45f};
	case Model::SeaWhip: return {0.15f, 0.9f, 1.2f};
	case Model::SeaPen: return {0.06f, 1.2f, 1.1f};
	case Model::Anemone0:
	case Model::Anemone1: return {0.06f, 1.6f, 0.4f};
	case Model::FeatherDuster: return {0.05f, 2.f, 0.6f};
	default: return {};
	}
}

math::FVector3 PlantSwayOffset(const math::FMatrix4 &transform, const math::FVector3 &local, float time, const SwayParams &sway) noexcept {
	if (!sway.Active())
		return {};
	// Mêmes formules que SwayWorld (sway.glsl) : phase tirée de la position de l'instance.
	const math::FVector3 origin{transform.m[3][0], transform.m[3][1], transform.m[3][2]};
	const float phase = (origin.x * 0.21f + origin.y * 0.07f + origin.z * 0.17f) * 3.f;
	const float h = std::clamp(local.y / std::max(sway.height, 1e-3f), 0.f, 1.f);
	const float bend = h * h, f = sway.frequency;
	return math::FVector3{std::sin(time * f + phase + local.y * 0.35f) + 0.12f * std::sin(time * f * 2.3f + phase * 1.7f), 0.f,
						  std::cos(time * f * 0.83f + phase * 1.31f + local.y * 0.3f)} *
		   (sway.amplitude * bend);
}

} // namespace abyssale
