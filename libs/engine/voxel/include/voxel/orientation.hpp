#pragma once
/**
 * @file orientation.hpp
 * @brief Orientation d'un bloc (rotation quantifiée du cube) : modèle, mathématiques
 *        et conversion depuis des noms (points cardinaux) ou des angles d'Euler.
 *
 * Un bloc posé a une orientation parmi 24 (rotations propres du cube) : 6 choix de
 * « haut » (où va la face supérieure locale +Y) × 4 rotations autour de cet axe.
 * Encodage : `orient = up * 4 + yaw` (0..23). `orient = 0` est l'identité :
 * +Y vers le haut, −Z vers l'avant (convention du jeu : −Z = nord / avant).
 *
 * Noms acceptés :
 *  - points cardinaux : "north", "south", "east", "west" (face avant −Z orientée) ;
 *  - verticaux : "top" / "up", "bottom" / "down" ;
 *  - axe : "axis=x", "axis=y", "axis=z" (troncs, piliers).
 * Les angles d'Euler (degrés) sont arrondis à la rotation la plus proche.
 */
#include <cstdint>
#include <optional>
#include <string_view>

#include <math/math.hpp>

#include "types.hpp"

namespace voxel {

using BlockOrient = uint8_t;
inline constexpr uint8_t kOrientCount = 24;
inline constexpr BlockOrient kOrientDefault = 0; // identité

/// Les six directions « haut » (monde), dans l'ordre : +Y, −Y, +Z, −Z, +X, −X.
inline constexpr IVec3 kOrientUp[6] = {{0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}};

/// Rotation d'un point local (bloc centré, −0,5..0,5) vers le monde, selon `o`.
[[nodiscard]] math::FVector3 RotateOrient(math::FVector3 p, BlockOrient o) noexcept;
/// Rotation inverse (monde → local) : la face `f` affichée correspond à la face locale.
[[nodiscard]] int OrientFace(BlockOrient o, int face) noexcept;

/// Orientation à partir du cap (yaw de 0 à 3, « haut » = +Y) — 0 = nord, 1 = est, 2 = sud, 3 = ouest.
[[nodiscard]] constexpr BlockOrient OrientFromYaw(int yaw) noexcept { return BlockOrient(yaw & 3); }
/// Orientation verticale : `axis` ∈ {0 (X), 1 (Y), 2 (Z)} — le « haut » du bloc suit l'axe.
[[nodiscard]] BlockOrient OrientFromAxis(int axis) noexcept;

/// Cap (0..3) à partir d'une direction horizontale (dx, dz) — le bloc fait face à cette direction.
[[nodiscard]] int YawFromDirection(float dx, float dz) noexcept;

/// Analyse un nom (« north », « south », « east », « west », « top », « up », « bottom »,
/// « down », « axis=x|y|z »). `nullopt` si inconnu.
[[nodiscard]] std::optional<BlockOrient> OrientFromName(std::string_view name) noexcept;
/// Arrondit des angles d'Euler (degrés X, Y, Z) à l'orientation la plus proche.
[[nodiscard]] BlockOrient OrientFromAngles(math::FVector3 degrees) noexcept;
/// Nom de l'orientation pour l'affichage (« nord », « est », « haut », « axe x »…).
[[nodiscard]] const char *OrientName(BlockOrient o) noexcept;

} // namespace voxel
