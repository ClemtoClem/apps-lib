#pragma once
/**
 * @file ambience.hpp
 * @brief Effets d'ambiance autour de la caméra (ce ne sont pas des objets du monde : ni
 *        entités, ni collisions), recalculés à chaque image en instances.
 *
 * - particules en suspension : boîte enroulée autour de la caméra ;
 * - reflets du varech : scintillement vert et or près du fond ;
 * - faisceaux de lumière : ancrés dans le monde par cellules, orientés selon le soleil.
 */
#include <cstdint>
#include <vector>

#include <math/math.hpp>
#include <math/noise.hpp>
#include <sdl3/render/renderer3d.hpp>

#include "abyssale/config.hpp"
#include "abyssale/terrain.hpp"

namespace abyssale {

class Ambience {
public:
	Ambience(const Terrain &terrain, const Quality &quality, uint64_t seed = 0xA3B1);

	void Update(float dt, float time, const math::FVector3 &camera, const math::FVector3 &sunDirection);

	std::vector<sdl3::MeshInstance> motes, glints, shafts;

private:
	const Terrain &m_terrain;
	std::vector<math::FVector3> m_motes, m_glints;
	std::vector<float> m_moteSize;
	int m_shafts = 0;
};

} // namespace abyssale
