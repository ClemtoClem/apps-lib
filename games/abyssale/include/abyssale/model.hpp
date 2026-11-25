#pragma once
/**
 * @file model.hpp
 * @brief Catégories de rendu d'Abyssale (un maillage et un matériau par catégorie) et
 *        ondulation des plantes partagée entre le shader et les collisions.
 *
 * Chaque entité dessinée porte une catégorie (`Render::model`). Les plantes ondulent au GPU
 * (`sdl3::Material::Sway::Plant`) ; `PlantSwayOffset` reproduit exactement ce calcul
 * (shaders/sway.glsl) pour que leurs volumes de collision suivent la forme affichée.
 */
#include <array>
#include <cstddef>
#include <cstdint>

#include <math/math.hpp>

namespace abyssale {

/// Hauteurs locales des variantes de maillage de varech, d'algues et d'herbiers (m).
inline constexpr std::array<float, 3> kKelpHeights{20.f, 26.f, 32.f};
inline constexpr std::array<float, 2> kFrondHeights{5.f, 8.f};
inline constexpr std::array<float, 3> kSeagrassHeights{0.6f, 1.1f, 1.8f};

enum class Model : uint8_t {
	// Décor
	Terrain,
	Rock0,
	Rock1,
	Rock2, ///< surplombs (non aplatis)
	Arch,
	Wreck,
	Fluke,
	VentChimney, ///< cheminée hydrothermale
	Statue,      ///< statue antique (colonne + buste)
	// Végétation
	Kelp0,
	Kelp1,
	Kelp2,
	Frond0,
	Frond1,
	Tuft,
	Seagrass0,
	Seagrass1,
	Seagrass2,
	// Éponges, coraux, petite faune fixée
	Barrel0,
	Barrel1,
	Tubes,
	Coral,
	Fan,
	SeaWhip,
	Mushroom,
	SeaPen,
	TubeWorm,
	Barnacles,
	Anemone0, ///< petite
	Anemone1, ///< grande
	FeatherDuster,
	// Faune mobile
	Fish,
	Manta,
	Crab,
	Lobster,
	Shrimp,
	// Effets
	Bubble,
	Count
};
inline constexpr size_t kModelCount = size_t(Model::Count);

/// Variante `i` d'une suite de catégories consécutives (`Kelp0 + 2` → `Kelp2`).
[[nodiscard]] constexpr Model Variant(Model first, size_t i) noexcept { return Model(size_t(first) + i); }

/// Ondulation de plante : amplitude (m), pulsation (rad/s), hauteur de référence (m).
struct SwayParams {
	float amplitude = 0.f, frequency = 0.f, height = 1.f;
	[[nodiscard]] bool Active() const noexcept { return amplitude > 0.f; }
};

/// Ondulation de la catégorie (amplitude nulle : rigide ou animée autrement).
[[nodiscard]] SwayParams PlantSway(Model model) noexcept;

/// Décalage dans le monde du point `local` d'une instance `transform` à l'instant `time`
/// (réplique du mode plante de sway.glsl : courbure croissante vers le sommet).
[[nodiscard]] math::FVector3 PlantSwayOffset(const math::FMatrix4 &transform, const math::FVector3 &local, float time, const SwayParams &sway) noexcept;

} // namespace abyssale
