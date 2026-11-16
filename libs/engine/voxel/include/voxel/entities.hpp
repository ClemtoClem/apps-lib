#pragma once
/**
 * @file entities.hpp
 * @brief Composants ECS standard des entités du monde voxel et leur sérialisation
 *        dans les blobs de chunks.
 */
#include <cstdint>
#include <string>

#include <dataio/binary.hpp>
#include <ecs/ecs.hpp>
#include <math/math.hpp>

#include "types.hpp"

namespace voxel {

struct Transform {
	math::FVector3 position;
	float yaw = 0.f;
};
struct Velocity {
	math::FVector3 value;
};
/// Boîte de collision (demi-étendues) et état du sol.
struct Body {
	math::FVector3 halfExtents{0.3f, 0.9f, 0.3f};
	bool onGround = false;
	bool gravity = true;
	bool collide = true;
};
struct Health {
	float current = 20.f;
	float max = 20.f;
};
/// Type d'entité (« core:zombie », « core:player »…).
struct Kind {
	std::string id;
};
/// Skin du joueur (identifiant qualifié, ex. « core:prince »).
struct Skin {
	std::string id;
};
/// Propriétés libres des mods (JSON compact).
struct Props {
	std::string json;
};
/// Comportement d'errance simple (créatures).
struct Wander {
	float timer = 0.f;
	float speed = 1.5f;
	uint32_t seed = 0;
};
/// Entité non sauvegardée avec le chunk (joueur, projectiles…).
struct Transient {};
/// Appartenance à la grille spatiale : chunk courant et rang dans `Chunk::entities`.
struct GridCell {
	ChunkKey chunk = 0;
	uint32_t slot = UINT32_MAX;
};

/// Données sérialisables d'une entité (indépendantes de l'ECS : produites/consommées
/// sur les threads du pool, instanciées sur le thread principal).
struct EntityRecord {
	std::string kind;
	math::FVector3 position;
	math::FVector3 velocity;
	float yaw = 0.f;
	float health = 20.f, maxHealth = 20.f;
	math::FVector3 halfExtents{0.3f, 0.9f, 0.3f};
	std::string props;
	bool wander = false;
	uint64_t uid = 0; // identifiant stable (v2), 0 : aucun
	std::string ext;  // mécanique (v2) : masse, lumière, son, joints (JSON compact)
};

/// Version d'enregistrement courante (1 : sans uid ni mécanique).
inline constexpr uint32_t kEntityRecordVersion = 2;

void WriteEntityRecord(dataio::ByteWriter &w, const EntityRecord &r);
[[nodiscard]] bool ReadEntityRecord(dataio::ByteReader &r, EntityRecord &out, uint32_t version = kEntityRecordVersion);

/// Capture l'état d'une entité de l'ECS.
[[nodiscard]] EntityRecord CaptureEntity(ecs::World &world, ecs::Entity e);
/// Instancie une entité dans l'ECS à partir d'un enregistrement.
ecs::Entity InstantiateEntity(ecs::World &world, const EntityRecord &r);

} // namespace voxel
