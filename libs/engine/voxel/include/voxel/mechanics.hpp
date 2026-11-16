#pragma once
/**
 * @file mechanics.hpp
 * @brief Mécanique des entités : identifiant stable, masse, émetteurs de lumière et de son,
 *        joints entre entités (attache, charnière, glissière, poulie à contrepoids, corde)
 *        avec moteurs et limites, et leur persistance.
 *
 * Un joint appartient à une entité (composant `Joints`, le « porteur » A) et vise une autre
 * entité (B) par son `Uid` : les références survivent à la sauvegarde et au rechargement des
 * chunks. Les points d'attache sont exprimés dans le repère local du porteur (tournant avec
 * son cap `Transform::yaw`). `SolveJoints` s'exécute après l'intégration physique et impose
 * la position (et la vitesse) de B — ou de A et B pour une poulie, système à un degré de
 * liberté entraîné par la gravité et le moteur :
 *
 * | Type      | Contrainte                                                   | `position`        |
 * |-----------|--------------------------------------------------------------|-------------------|
 * | Attach    | B suit A rigidement                                          | —                 |
 * | Hinge     | B tourne autour d'un axe vertical passant par l'ancre de A   | angle (°)         |
 * | Slider    | B glisse le long de `axis` (repère de A)                     | course (m)        |
 * | Pulley    | y(A) + ratio·y(B) = longueur (câble sur poulie, contrepoids) | hauteur de A (m)  |
 * | Rope      | distance(ancre de A, B) ≤ longueur                           | —                 |
 *
 * Moteur : vitesse visée (°/s ou m/s) et force maximale (N, poulie) ; limites [min, max]
 * actives si min < max.
 */
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dataio/json.hpp>
#include <ecs/ecs.hpp>
#include <math/math.hpp>

namespace voxel {

/// Identifiant stable (sauvegardé) : cible des joints.
struct Uid {
	uint64_t value = 0;
};

/// Masse en kilogrammes (poulie à contrepoids).
struct Mass {
	float kg = 70.f;
};

/// Lumière dynamique portée par l'entité (ponctuelle, spot ou directionnelle).
struct LightEmitter {
	math::FVector3 color{1.f, 0.82f, 0.55f};
	float intensity = 1.f;
	float range = 8.f;		   // blocs
	math::FVector3 offset{};   // repère local de l'entité
	bool enabled = true;
	// Type de source : 0 ponctuelle, 1 spot (cône), 2 directionnelle (faisceau).
	uint8_t type = 0;
	// Direction d'émission (repère local, tournée avec l'entité) — spot/directionnelle.
	math::FVector3 direction{0.f, -1.f, 0.f};
	// Demi-angle du spot, en degrés.
	float coneAngle = 30.f;
};

/// Son contextuel émis par l'entité (spatialisé autour de l'auditeur).
struct SoundEmitter {
	std::string sound; // « core:metal_clang » ou chemin relatif au mod
	float volume = 1.f;
	float range = 16.f;
	bool loop = true;
	bool enabled = true;
};

enum class JointType : uint8_t { Attach, Hinge, Slider, Pulley, Rope };
[[nodiscard]] const char *JointTypeId(JointType t) noexcept;	// "attach"…
[[nodiscard]] const char *JointTypeLabel(JointType t) noexcept; // « Attache »…
[[nodiscard]] std::optional<JointType> ParseJointType(std::string_view id) noexcept;

struct Joint {
	JointType type = JointType::Attach;
	uint64_t target = 0; // Uid de B
	std::string name;
	math::FVector3 anchor{};	   // ancre sur A (repère local de A)
	math::FVector3 axis{1, 0, 0}; // glissière : direction (repère local de A)
	float ratio = 1.f;			   // poulie : déplacement de B = −déplacement de A / ratio
	float length = 0.f;			   // poulie : y(A) + ratio·y(B) ; corde : longueur max (0 : à la création)
	float minLimit = 0.f, maxLimit = 0.f;
	bool motor = false;
	float motorSpeed = 0.f; // °/s (charnière), m/s (glissière, poulie : vitesse de A)
	float motorForce = 0.f; // N (poulie) ; 0 : moteur idéal
	// État (sauvegardé) :
	bool initialized = false;
	math::FVector3 offset{}; // position de B relative à l'ancre à position 0 (repère de A)
	float restYaw = 0.f;	 // cap de B relatif à A à la création
	float position = 0.f;
	float velocity = 0.f;
};

struct Joints {
	std::vector<Joint> list;
};

/// Donne un `Uid` à l'entité si elle n'en a pas ; retourne sa valeur.
uint64_t EnsureUid(ecs::World &world, ecs::Entity e);
/// Entité portant `uid` (recherche linéaire ; le solveur construit une table).
[[nodiscard]] ecs::Entity FindByUid(ecs::World &world, uint64_t uid);

/// Rotation d'un vecteur local par un cap (radians, convention de `Transform::yaw`).
[[nodiscard]] math::FVector3 RotateYaw(const math::FVector3 &v, float yaw) noexcept;

struct MechanicsStats {
	size_t joints = 0;
	size_t active = 0; // cibles chargées
};

/// Résout tous les joints (après l'intégration physique) ; `gravity` en m/s² (vers le bas).
MechanicsStats SolveJoints(ecs::World &world, float dt, float gravity = 24.f);

// ── Persistance (JSON compact dans l'enregistrement de l'entité) ─────────────

[[nodiscard]] dataio::Json JointToJson(const Joint &j);
[[nodiscard]] std::optional<Joint> JointFromJson(const dataio::Json &j);
/// Masse, lumière, son et joints de l'entité (vide si aucun).
[[nodiscard]] std::string CaptureMechanics(ecs::World &world, ecs::Entity e);
void ApplyMechanics(ecs::World &world, ecs::Entity e, std::string_view json);

} // namespace voxel
