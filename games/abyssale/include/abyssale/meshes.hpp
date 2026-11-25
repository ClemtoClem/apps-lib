#pragma once
/**
 * @file meshes.hpp
 * @brief Maillages procéduraux d'Abyssale (`sdl3::MeshData`), tous en mètres.
 *
 * Conventions : plantes enracinées en (0, 0, 0) et dressées selon +Y ; poisson et raie le
 * nez vers +X, la queue vers −X, les ailes de la raie selon ±Z (modes d'ondulation
 * `sdl3::Material::Sway::Swim` / `Flap`).
 */
#include <array>
#include <cstdint>
#include <vector>

#include <math/math.hpp>
#include <sdl3/render/mesh3d.hpp>

#include "abyssale/model.hpp"

namespace abyssale {
class Terrain;
}

namespace abyssale::meshes {

/// Plant de varech complet (tige ondulante + feuilles alternées), haut de `height`.
/// Un seul maillage : tige et feuilles ondulent ensemble.
[[nodiscard]] sdl3::MeshData Kelp(float height, int leaves, float leafLength, float leafWidth, uint64_t seed);
/// Touffe de brins fins (tapis magenta).
[[nodiscard]] sdl3::MeshData Tuft(int blades, float height, uint64_t seed);
/// Rocher : sphère déformée par un fBm 3D, aplatie à la base.
[[nodiscard]] sdl3::MeshData Rock(int segments, float roughness, float flatten, uint64_t seed);
/// Rayon du bloc `Rock(…, roughness, …, seed)` dans la direction unitaire `dir` du repère
/// local (moitié haute : l'aplatissement du dessous n'y joue pas). Sert à poser des
/// organismes exactement sur la roche.
[[nodiscard]] float RockRadius(const math::FVector3 &dir, float roughness, uint64_t seed);

/// Variantes de blocs du jeu (indice = variante de `World::rocks`).
struct RockShape {
	float roughness, flatten;
	uint64_t seed;
};
inline constexpr std::array<RockShape, 3> kRockShapes{{
	{0.5f, 0.55f, 300}, // éboulis aplatis
	{0.6f, 0.55f, 301},
	{0.34f, 3.f, 302},	// non aplati : surplombs
}};
/// Arche rocheuse : section bosselée balayée le long d'un demi-cercle de rayon `span`.
[[nodiscard]] sdl3::MeshData Arch(float span, float thickness, uint64_t seed);
/// Éponge tonneau creuse (paroi extérieure, lèvre, paroi intérieure).
[[nodiscard]] sdl3::MeshData BarrelSponge(float radius, float height, uint64_t seed);
/// Bouquet d'éponges tubulaires creuses.
[[nodiscard]] sdl3::MeshData TubeSponges(int tubes, uint64_t seed);
/// Corail branchu (tubes ramifiés).
[[nodiscard]] sdl3::MeshData BranchCoral(int depth, uint64_t seed);
/// Corail en éventail (gorgone) : réseau plan de branches fines.
[[nodiscard]] sdl3::MeshData FanCoral(uint64_t seed);
/// Épave : coque de bordés interrompus, membrures apparentes, quille, mâts et vergues brisés.
[[nodiscard]] sdl3::MeshData Shipwreck();
/// Nageoire caudale de baleine en pierre : pédoncule et deux lobes au profil d'aile.
[[nodiscard]] sdl3::MeshData WhaleFluke();
/// Cheminée hydrothermale : empilement de cônes tronqués, gueule évasée.
[[nodiscard]] sdl3::MeshData VentChimney(uint64_t seed);
/// Statue antique : piédestal, colonne cannelée et buste érodé.
[[nodiscard]] sdl3::MeshData Statue(uint64_t seed);
/// Poisson fuselé (nez +X) avec caudale et dorsale.
[[nodiscard]] sdl3::MeshData Fish();
/// Raie manta : ailes au profil épais (envergure ±1 selon Z), céphaliques, queue.
[[nodiscard]] sdl3::MeshData Manta();
/// Poisson simplifié (même silhouette, ~150 triangles) pour les bancs lointains.
[[nodiscard]] sdl3::MeshData FishFar();
/// Particule : octaèdre (vu de partout, très peu de sommets).
[[nodiscard]] sdl3::MeshData Mote();
/// Faisceau de lumière : deux plans croisés, v = 0 au sommet (surface), 1 en bas.
[[nodiscard]] sdl3::MeshData Shaft();
/// Surface de l'eau : grand plan subdivisé (vu de dessous).
[[nodiscard]] sdl3::MeshData Surface(float size);

// ── Espèces végétales complémentaires ─────────────────────────────────────────

/// Herbier dense (posidonie) : longues lames souples serrées, ondulant ensemble.
[[nodiscard]] sdl3::MeshData Seagrass(int blades, float height, uint64_t seed);
/// Anémone de mer : colonne charnue et couronne de tentacules fins qui se recourbent.
[[nodiscard]] sdl3::MeshData Anemone(int tentacles, float height, uint64_t seed);
/// Corail-fouet : longue tige souple couverte de polypes latéraux (bouquets denses).
[[nodiscard]] sdl3::MeshData SeaWhip(float length, uint64_t seed);
/// Corail champignon : disque festonné et plissé sur pied court.
[[nodiscard]] sdl3::MeshData MushroomCoral(float radius, uint64_t seed);
/// Plume de mer (pennatule) : axe dressé portant des barbules plumeuses latérales.
[[nodiscard]] sdl3::MeshData SeaPen(uint64_t seed);
/// Ver tubicole spirographe : tube coriace et couronne de radioles plumeuses.
[[nodiscard]] sdl3::MeshData FeatherDuster(uint64_t seed);
/// Ver tubicole calcifié (serpule) : tube spiralé, ouverture frangée.
[[nodiscard]] sdl3::MeshData SpiralTubeWorm(uint64_t seed);

// ── Crustacés ─────────────────────────────────────────────────────────────────

/// Crabe : carapace aplatie et bombée, 8 pattes articulées, 2 pinces, yeux pédonculés.
[[nodiscard]] sdl3::MeshData Crab(float size, uint64_t seed);
/// Homard : céphalothorax lisse, abdomen annelé, queue en éventail, grandes pinces et
/// longues antennes. Nez vers +X.
[[nodiscard]] sdl3::MeshData Lobster(uint64_t seed);
/// Crevette : corps arqué, segments annelés, fines antennes, pattes grêles.
[[nodiscard]] sdl3::MeshData Shrimp(uint64_t seed);
/// Amas de balanes : petits cônes calcaires irréguliers (à coller sur roches ou épaves).
[[nodiscard]] sdl3::MeshData Barnacles(int count, uint64_t seed);

/// Pointes des pattes (repère local du maillage, au repos), dans l'ordre : avant → arrière,
/// côté −z puis +z. Servent à poser le corps sur le relief (inclinaison selon les appuis).
[[nodiscard]] std::vector<math::FVector3> CrabFeet(float size);
[[nodiscard]] std::vector<math::FVector3> LobsterFeet();

/// Maillage de la catégorie `model` (catalogue du jeu : paramètres et graines fixés).
[[nodiscard]] sdl3::MeshData ModelMesh(Model model, const Terrain &terrain);

} // namespace abyssale::meshes
