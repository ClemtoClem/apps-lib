#pragma once
/**
 * @file world.hpp
 * @brief Composition du décor d'un chunk : végétation, roches, pièces maîtresses, obstacles.
 *
 * Chaque `World` décore un unique chunk (repère du monde) selon son template
 * (`ChunkKind`). La carte d'entrée conserve ses six lieux classiques (varech, grotte,
 * épave, jardin d'éponges, nageoire de baleine, prairie) ; les autres templates placent
 * un décor distinctif (cheminées hydrothermales, statues, épave de paquebot, …).
 */
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <math/math.hpp>
#include <sdl3/render/renderer3d.hpp>

#include "abyssale/chunk.hpp"
#include "abyssale/config.hpp"
#include "abyssale/model.hpp"
#include "abyssale/terrain.hpp"

namespace abyssale {


struct Collider {
	math::FVector3 center;
	float radius;
};

/// Caméra fixe : position, lacet et tangage (radians), nom.
struct Viewpoint {
	math::FVector3 position;
	float yaw = 0.f, pitch = 0.f;
	std::string_view name;
	math::FVector3 target; ///< sujet cadré
};

/// Point de passage du survol : position et cible du regard.
struct TourKey {
	math::FVector3 pos, look;
};

class World {
public:
	World(const Terrain &terrain, const Quality &quality, uint64_t seed = 0xAB155A1E, int32_t cx = 0, int32_t cz = 0,
		  ChunkKind kind = ChunkKind::Entry);

	std::array<std::vector<sdl3::MeshInstance>, 3> kelp;   ///< par variante de hauteur
	std::array<std::vector<sdl3::MeshInstance>, 2> fronds; ///< algues violettes, dorées, vertes
	std::vector<sdl3::MeshInstance> tufts;				   ///< tapis magenta
	std::array<std::vector<sdl3::MeshInstance>, 3> rocks;  ///< blocs posés et surplombs
	std::vector<sdl3::MeshInstance> arches;
	std::vector<sdl3::MeshInstance> barrels, tubes, corals, fans;
	std::array<std::vector<sdl3::MeshInstance>, 3> seagrass; ///< herbiers (court, moyen, long)
	std::vector<sdl3::MeshInstance> seaWhips, mushroomCorals, seaPens;
	std::vector<sdl3::MeshInstance> tubeWorms; ///< serpules (fond et roches)
	std::vector<sdl3::MeshInstance> barnacles; ///< amas collés sur les blocs
	std::vector<sdl3::MeshInstance> chimneys;  ///< cheminées hydrothermales
	std::vector<sdl3::MeshInstance> statues;   ///< statues antiques
	math::FMatrix4 wreck = math::FMatrix4::Identity();
	math::FMatrix4 fluke = math::FMatrix4::Identity();

	std::vector<math::FVector3> glintAnchors; ///< points des reflets scintillants (dans le varech)
	std::vector<math::FVector3> vents;		  ///< sources de bulles au fond
	std::vector<Collider> colliders;
	std::array<Viewpoint, 6> views{};
	std::vector<TourKey> tour; ///< survol (la végétation s'écarte de sa trajectoire)

	[[nodiscard]] ChunkKind Kind() const noexcept { return m_kind; }
	[[nodiscard]] std::span<const Landmark> Landmarks() const noexcept { return m_landmarks; }

	/// Place deux colonnes de bulles dans le champ de chaque vue.
	void DressViews(const Terrain &terrain);
	/// Repousse `pos` hors des obstacles et du fond (nageur de rayon `radius`).
	void Collide(const Terrain &terrain, math::FVector3 &pos, float radius) const noexcept;
	/// Le segment [a, b] ne traverse ni obstacle ni relief.
	[[nodiscard]] bool LineOfSight(const Terrain &terrain, const math::FVector3 &a, const math::FVector3 &b) const noexcept;
	/// Cadre `target` : position dégagée à `distance` et `height` au-dessus du fond.
	[[nodiscard]] Viewpoint Frame(const Terrain &terrain, const math::FVector3 &target, float distance, float height, float angle,
								  std::string_view name) const;

private:
	ChunkKind m_kind;
	std::vector<Landmark> m_landmarks; ///< lieux en coordonnées monde
};

/// Lacet orientant le regard de `from` vers `to` (convention du jeu : avant = (sin, ·, −cos)).
[[nodiscard]] float YawTowards(const math::FVector3 &from, const math::FVector3 &to) noexcept;

} // namespace abyssale
