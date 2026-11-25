#pragma once
/**
 * @file ecs.hpp
 * @brief Pont entre `physics::World` et `foundation.ecs` : composants, ressources et système.
 *
 * Le monde physique est une ressource du monde ECS (`physics::World`, créée au premier
 * `AttachBody` / `AttachMesh`). Chaque entité physique porte :
 *  - `RigidBody` + `Position` : corps (centre en `Position`) ; les corps dynamiques écrivent
 *    leur position après le pas, les autres (immobiles, placés par le jeu) la lisent avant ;
 *  - `KinematicMesh` : maillage cinématique placé par `transform`, recopié avant le pas.
 * Propriétaire des corps et maillages : `UserOf(entité)` ; évènements de contact et rayons
 * renvoient à l'entité par `EntityOf`. Gravité : ressource `Gravity` (défaut −9,81 m/s² en Y).
 *
 * @code
 *   physics::AttachBody(world, e, physics::Body::MakeSphere({0, 5, 0}, 0.5f));
 *   schedule.Add("physique", physics::StepSystem);
 * @endcode
 */
#include <span>

#include <ecs/ecs.hpp>

#include "world.hpp"

namespace physics {

struct RigidBody {
	BodyId id = -1;
};
struct Position {
	math::FVector3 value;
};
struct KinematicMesh {
	ColliderId id = -1;
	math::FMatrix4 transform = math::FMatrix4::Identity();
};
/// Ressource facultative.
struct Gravity {
	math::FVector3 value{0.f, -9.81f, 0.f};
};

/// Propriétaire physique d'une entité (jamais `kWorld`).
[[nodiscard]] constexpr UserId UserOf(ecs::Entity e) noexcept { return e.Bits() + 1; }
/// Entité d'un propriétaire (`kNull` pour `kWorld`).
[[nodiscard]] constexpr ecs::Entity EntityOf(UserId user) noexcept { return user == kWorld ? ecs::kNull : ecs::Entity::FromBits(user - 1); }

/// Monde physique du monde ECS (créé s'il manque).
physics::World &PhysicsOf(ecs::World &world);
/// Crée le corps de `e` et lui ajoute `RigidBody` et `Position`.
BodyId AttachBody(ecs::World &world, ecs::Entity e, Body body);
/// Crée le maillage cinématique de `e` (triangles locaux) et lui ajoute `KinematicMesh`.
ColliderId AttachMesh(ecs::World &world, ecs::Entity e, std::span<const math::FVector3> localVertices, const math::FMatrix4 &transform);
/// Système : place corps immobiles et maillages, avance le monde, recopie les corps dynamiques.
void StepSystem(ecs::World &world, float dt);

} // namespace physics
