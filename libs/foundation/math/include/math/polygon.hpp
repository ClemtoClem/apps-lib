#pragma once
/**
 * @file polygon.hpp
 * @brief Polygones 2D : aire signée, appartenance d'un point, triangulation avec trous
 *        (découpe d'oreilles, trous raccordés par des ponts visibles — Eberly).
 */
#include <cstdint>
#include <span>
#include <vector>

#include "math.hpp"

namespace math {

/// Aire signée (positive pour un contour dans le sens trigonométrique).
[[nodiscard]] float SignedArea(std::span<const FVector2> polygon) noexcept;
/// Le point est-il à moins de `tolerance` d'un côté du polygone ?
[[nodiscard]] bool OnPolygonBorder(std::span<const FVector2> polygon, FVector2 p, float tolerance) noexcept;
/// Le point est-il strictement à l'intérieur (règle pair-impair) ?
[[nodiscard]] bool PointInPolygon(std::span<const FVector2> polygon, FVector2 p) noexcept;

/// Triangule un polygone simple avec trous. Les indices ajoutés à `outIndices` se rapportent à
/// la concaténation [outer, holes[0], holes[1]…] ; triangles dans le sens trigonométrique ;
/// le sens des contours fournis est indifférent. Faux si rien n'a pu être triangulé.
bool TriangulatePolygon(std::span<const FVector2> outer, const std::vector<std::vector<FVector2>> &holes, std::vector<uint32_t> &outIndices);

} // namespace math
