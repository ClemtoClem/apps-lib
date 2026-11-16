#include "voxel/inspect.hpp"

#include <algorithm>
#include <cctype>

#include "voxel/entities.hpp"

namespace voxel {

namespace {
constexpr const char *kLabels[] = {"Joueurs", "Créatures", "Objets", "Projectiles", "Autres"};
constexpr const char *kIds[] = {"player", "creature", "item", "projectile", "other"};

bool ContainsNoCase(std::string_view haystack, std::string_view needle) noexcept {
	if (needle.empty())
		return true;
	return std::ranges::search(haystack, needle, [](char a, char b) {
			   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		   }).begin() != haystack.end();
}
} // namespace

const char *EntityCategoryLabel(EntityCategory category) noexcept {
	return category < EntityCategory::Count ? kLabels[size_t(category)] : "?";
}
const char *EntityCategoryId(EntityCategory category) noexcept { return category < EntityCategory::Count ? kIds[size_t(category)] : "?"; }

EntityCategory CategorizeEntity(std::string_view kind, bool isPlayer, bool wanders) noexcept {
	if (isPlayer || kind.ends_with(":player"))
		return EntityCategory::Player;
	if (ContainsNoCase(kind, "projectile") || ContainsNoCase(kind, "arrow"))
		return EntityCategory::Projectile;
	if (ContainsNoCase(kind, "item"))
		return EntityCategory::Item;
	if (wanders)
		return EntityCategory::Creature;
	return EntityCategory::Other;
}

OutlinerResult CollectEntities(ecs::World &world, ecs::Entity player, const OutlinerFilter &filter, const math::FVector3 &from, size_t limit) {
	OutlinerResult out;
	const float maxSq = filter.maxDistance > 0 ? filter.maxDistance * filter.maxDistance : 0.f;
	world.MakeQuery<const Transform, const Kind>().Each([&](ecs::Entity e, const Transform &t, const Kind &k) {
		++out.total;
		const float dSq = (t.position - from).LengthSq();
		if (maxSq > 0 && dSq > maxSq)
			return;
		if (!ContainsNoCase(k.id, filter.text))
			return;
		const EntityCategory cat = CategorizeEntity(k.id, e == player, world.Has<Wander>(e));
		if (filter.category && *filter.category != cat)
			return;
		++out.perCategory[size_t(cat)];
		out.entries.push_back({e, k.id, cat, t.position, std::sqrt(dSq), ChunkKeyOfPosition(t.position)});
	});
	if (limit > 0 && out.entries.size() > limit) { // les plus proches seulement
		std::ranges::nth_element(out.entries, out.entries.begin() + std::ptrdiff_t(limit), {}, &OutlinerEntry::distance);
		out.entries.resize(limit);
	}
	std::ranges::sort(out.entries, [](const OutlinerEntry &a, const OutlinerEntry &b) {
		return a.category != b.category ? a.category < b.category : a.distance < b.distance;
	});
	return out;
}

} // namespace voxel
