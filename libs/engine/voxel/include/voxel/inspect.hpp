#pragma once
/**
 * @file inspect.hpp
 * @brief Lecture du monde vivant pour les outils (World Outliner du Studio) :
 *        entités de l'ECS classées par catégorie, filtrées (texte, catégorie,
 *        distance) et triées (catégorie, puis distance).
 *
 * Aucune copie du monde : la collecte lit directement les composants de l'ECS,
 * sur le thread qui possède le monde (thread principal), entre deux mises à jour.
 */
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ecs/ecs.hpp>
#include <math/math.hpp>

#include "types.hpp"

namespace voxel {

enum class EntityCategory : uint8_t { Player, Creature, Item, Projectile, Other, Count };

[[nodiscard]] const char *EntityCategoryLabel(EntityCategory category) noexcept; ///< « Joueurs », « Créatures »…
[[nodiscard]] const char *EntityCategoryId(EntityCategory category) noexcept;	///< "player", "creature"…

/// Catégorie d'une entité : joueur, objet (« …item… »), projectile (« …projectile… »,
/// « …arrow… »), créature (comportement `Wander`), sinon « autre ».
[[nodiscard]] EntityCategory CategorizeEntity(std::string_view kind, bool isPlayer, bool wanders) noexcept;

struct OutlinerEntry {
	ecs::Entity entity{};
	std::string kind; ///< `Kind::id` (« core:zombie »)
	EntityCategory category = EntityCategory::Other;
	math::FVector3 position{};
	float distance = 0.f; ///< à `from`
	ChunkKey chunk = 0;
};

struct OutlinerFilter {
	std::string text;						 ///< sous-chaîne de l'identifiant (casse ignorée)
	std::optional<EntityCategory> category; ///< nullopt : toutes
	float maxDistance = 0.f;				 ///< 0 : sans limite
};

struct OutlinerResult {
	std::vector<OutlinerEntry> entries; ///< entités retenues (triées)
	size_t total = 0;					///< entités du monde (avant filtre)
	std::array<size_t, size_t(EntityCategory::Count)> perCategory{}; ///< retenues, par catégorie
};

/// Entités (`Transform` + `Kind`) qui passent le filtre, triées par catégorie puis
/// distance. `limit` > 0 : seules les `limit` plus proches sont gardées (les compteurs
/// restent exacts).
[[nodiscard]] OutlinerResult CollectEntities(ecs::World &world, ecs::Entity player, const OutlinerFilter &filter, const math::FVector3 &from,
											 size_t limit = 0);

} // namespace voxel
