#pragma once
/**
 * @file ecs.hpp
 * @brief ECS à archétypes, orienté débit : des centaines de milliers d'entités
 *        traitées en quelques millisecondes.
 *
 * Mise en mémoire
 * - Un **archétype** regroupe les entités ayant exactement le même ensemble de
 *   composants. Chaque composant y est stocké dans sa propre **colonne contiguë**
 *   (SoA) alignée sur 64 octets : `Each` parcourt des tableaux denses, sans
 *   indirection, boucle vectorisable par le compilateur.
 * - Composants trivialement copiables relogés par `memcpy`.
 * - Suppression par « swap-remove » : les colonnes restent denses.
 * - Transitions d'archétype (ajout/retrait d'un composant) mises en cache dans un
 *   graphe d'arêtes : un `Add<T>` ne recherche jamais deux fois sa destination.
 *
 * Requêtes
 * - `Query<A, const B>` mémorise les archétypes correspondants et ne réexamine
 *   que ceux créés depuis son dernier passage (cache incrémental).
 * - `Each(fn)` : boucle serrée par archétype ; `ParEach(fn)` : lots de lignes
 *   répartis sur le pool de threads (`jobs::ParallelFor`).
 * - `Without<C>()` exclut un composant ; `fn` peut recevoir `Entity` en premier.
 *
 * Modifications structurelles pendant une itération : passer par un
 * `CommandBuffer` (un par thread) puis `World::Apply`.
 */
#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include <core/hash.hpp>
#include <core/robin_map.hpp>
#include <jobs/jobs.hpp>

namespace ecs {

// ── Entités ────────────────────────────────────────────────────────────────
struct Entity {
	uint32_t index = UINT32_MAX;
	uint32_t generation = 0;

	[[nodiscard]] constexpr bool IsNull() const noexcept { return index == UINT32_MAX; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept { return !IsNull(); }
	[[nodiscard]] constexpr uint64_t Bits() const noexcept { return (uint64_t(generation) << 32) | index; }
	[[nodiscard]] static constexpr Entity FromBits(uint64_t b) noexcept {
		return {static_cast<uint32_t>(b), static_cast<uint32_t>(b >> 32)};
	}
	constexpr bool operator==(const Entity &) const = default;
};
inline constexpr Entity kNull{};

// ── Types de composants ────────────────────────────────────────────────────
inline constexpr size_t kMaxComponents = 256;
using ComponentId = uint16_t;
using Signature = std::bitset<kMaxComponents>;

struct ComponentInfo {
	std::string name;
	size_t size = 0;
	size_t align = 1;
	bool trivial = false;
	void (*relocate)(void *dst, void *src) = nullptr; // construit dst par déplacement puis détruit src
	void (*destroy)(void *p) = nullptr;
	void (*copy)(void *dst, const void *src) = nullptr; // nullptr si non copiable
};

namespace detail {
ComponentId RegisterComponent(ComponentInfo info);
template <typename T> ComponentInfo MakeInfo() {
	ComponentInfo i;
	i.name = typeid(T).name();
	i.size = sizeof(T);
	i.align = alignof(T);
	i.trivial = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;
	i.relocate = [](void *d, void *s) {
		std::construct_at(static_cast<T *>(d), std::move(*static_cast<T *>(s)));
		std::destroy_at(static_cast<T *>(s));
	};
	i.destroy = [](void *p) { std::destroy_at(static_cast<T *>(p)); };
	if constexpr (std::is_copy_constructible_v<T>)
		i.copy = [](void *d, const void *s) { std::construct_at(static_cast<T *>(d), *static_cast<const T *>(s)); };
	return i;
}
template <typename T> inline const ComponentId kTypeId = RegisterComponent(MakeInfo<T>());
} // namespace detail

/// Identifiant stable (pour le processus) du type de composant `T`.
template <typename T> [[nodiscard]] inline ComponentId TypeId() noexcept { return detail::kTypeId<std::remove_cvref_t<T>>; }
[[nodiscard]] const ComponentInfo &GetComponentInfo(ComponentId id);
[[nodiscard]] size_t ComponentTypeCount();

template <typename... C> [[nodiscard]] Signature MakeSignature() {
	Signature s;
	(s.set(TypeId<C>()), ...);
	return s;
}

// ── Colonne : tableau contigu aligné d'un composant ───────────────────────
class Column {
	std::byte *m_data = nullptr;
	size_t m_capacity = 0;
	const ComponentInfo *m_info = nullptr;

public:
	static constexpr size_t kAlign = 64;

	Column() = default;
	explicit Column(const ComponentInfo &info) : m_info(&info) {}
	Column(Column &&o) noexcept
		: m_data(std::exchange(o.m_data, nullptr)), m_capacity(std::exchange(o.m_capacity, 0)), m_info(o.m_info) {}
	Column &operator=(Column &&o) noexcept;
	Column(const Column &) = delete;
	Column &operator=(const Column &) = delete;
	~Column();

	/// Agrandit à `cap` éléments en relogeant les `size` premiers.
	void Reserve(size_t cap, size_t size);
	void DestroyRange(size_t first, size_t last) noexcept;

	[[nodiscard]] void *At(size_t row) noexcept { return m_data + row * m_info->size; }
	[[nodiscard]] const void *At(size_t row) const noexcept { return m_data + row * m_info->size; }
	template <typename T> [[nodiscard]] T *As() noexcept { return std::launder(reinterpret_cast<T *>(m_data)); }
	[[nodiscard]] const ComponentInfo &Info() const noexcept { return *m_info; }
};

class World;

// ── Archétype ──────────────────────────────────────────────────────────────
class Archetype {
public:
	Archetype(uint32_t id, std::vector<ComponentId> ids);
	~Archetype();
	Archetype(const Archetype &) = delete;
	Archetype &operator=(const Archetype &) = delete;

	[[nodiscard]] uint32_t Id() const noexcept { return m_id; }
	[[nodiscard]] const Signature &Sig() const noexcept { return m_signature; }
	[[nodiscard]] std::span<const ComponentId> Components() const noexcept { return m_ids; }
	[[nodiscard]] size_t Size() const noexcept { return m_entities.size(); }
	[[nodiscard]] std::span<const Entity> Entities() const noexcept { return m_entities; }
	[[nodiscard]] bool Has(ComponentId id) const noexcept { return m_signature.test(id); }
	[[nodiscard]] int ColumnOf(ComponentId id) const noexcept { return m_columnOf[id]; }
	[[nodiscard]] Column &ColumnAt(int c) noexcept { return m_columns[static_cast<size_t>(c)]; }
	template <typename T> [[nodiscard]] std::remove_cvref_t<T> *Data() noexcept {
		const int c = m_columnOf[TypeId<T>()];
		return c < 0 ? nullptr : m_columns[static_cast<size_t>(c)].template As<std::remove_cvref_t<T>>();
	}

	void Reserve(size_t n);
	/// Ajoute une ligne aux colonnes non initialisées (à construire par l'appelant).
	uint32_t PushRow(Entity e);
	/// Retire la ligne `row` ; détruit les composants sauf ceux de `movedOut` (déjà relogés).
	/// Retourne l'entité déplacée dans `row` (ou kNull).
	Entity SwapRemove(uint32_t row, const Signature &movedOut = {});

private:
	friend class World;
	uint32_t m_id;
	Signature m_signature;
	std::vector<ComponentId> m_ids;
	std::vector<Column> m_columns;
	std::vector<Entity> m_entities;
	size_t m_capacity = 0;
	int16_t m_columnOf[kMaxComponents];
	core::RobinMap<ComponentId, Archetype *> m_addEdges;
	core::RobinMap<ComponentId, Archetype *> m_removeEdges;
};

// ── Tampon de commandes différées (un par thread) ─────────────────────────
class CommandBuffer {
public:
	void Despawn(Entity e) { m_despawn.push_back(e); }
	template <typename T> void Add(Entity e, T value);
	template <typename T> void Remove(Entity e);
	template <typename... C> void Spawn(C... comps);
	void Run(std::move_only_function<void(World &)> fn) { m_ops.push_back(std::move(fn)); }
	[[nodiscard]] bool Empty() const noexcept { return m_despawn.empty() && m_ops.empty(); }
	void Clear() noexcept {
		m_despawn.clear();
		m_ops.clear();
	}

private:
	friend class World;
	std::vector<Entity> m_despawn;
	std::vector<std::move_only_function<void(World &)>> m_ops;
};

template <typename... C> class Query;

// ── Monde ──────────────────────────────────────────────────────────────────
class World {
public:
	World();
	~World();
	World(const World &) = delete;
	World &operator=(const World &) = delete;

	// Entités
	Entity Spawn();
	template <typename... C> Entity Spawn(C &&...comps);
	/// Crée `count` entités identiques (copies de `comps`) en une seule passe :
	/// une réservation, des boucles de construction contiguës. Retourne la première.
	template <typename... C> Entity SpawnBatch(size_t count, const C &...comps);
	/// Variante qui écrit les entités créées dans `out`.
	template <typename... C> void SpawnBatch(std::vector<Entity> &out, size_t count, const C &...comps);
	bool Despawn(Entity e);
	[[nodiscard]] bool IsAlive(Entity e) const noexcept {
		return e.index < m_records.size() && m_records[e.index].generation == e.generation && m_records[e.index].archetype;
	}
	[[nodiscard]] size_t EntityCount() const noexcept { return m_alive; }
	/// Entité vivante associée à un index (génération courante), ou kNull.
	[[nodiscard]] Entity EntityAt(uint32_t index) const noexcept;

	// Composants
	template <typename T> T &Add(Entity e, T value = T{});
	template <typename T> bool Remove(Entity e);
	template <typename T> [[nodiscard]] T *Get(Entity e) noexcept;
	template <typename T> [[nodiscard]] const T *Get(Entity e) const noexcept;
	template <typename T> [[nodiscard]] bool Has(Entity e) const noexcept;
	[[nodiscard]] Archetype *ArchetypeOf(Entity e) const noexcept { return IsAlive(e) ? m_records[e.index].archetype : nullptr; }

	// Requêtes
	template <typename... C> [[nodiscard]] Query<C...> MakeQuery();
	[[nodiscard]] std::span<const std::unique_ptr<Archetype>> Archetypes() const noexcept { return m_archetypes; }

	// Commandes différées
	void Apply(CommandBuffer &cb);
	/// Tampon partagé, protégé par un verrou (pratique, mais préférer un tampon par thread).
	void Defer(std::move_only_function<void(World &)> fn);
	void FlushDeferred();

	// Ressources (singletons typés)
	template <typename R, typename... Args> R &SetResource(Args &&...args);
	template <typename R> [[nodiscard]] R *GetResource() noexcept;
	template <typename R> bool RemoveResource();

	void Clear();

	jobs::ThreadPool &Pool() noexcept { return *m_pool; }
	void SetPool(jobs::ThreadPool &pool) noexcept { m_pool = &pool; }

private:
	template <typename... C> friend class Query;
	struct Record {
		Archetype *archetype = nullptr;
		uint32_t row = 0;
		uint32_t generation = 0;
	};

	Entity AllocateEntity();
	Archetype *FindOrCreateArchetype(std::vector<ComponentId> ids);
	Archetype *AddEdge(Archetype *from, ComponentId id);
	Archetype *RemoveEdge(Archetype *from, ComponentId id);
	/// Déplace l'entité vers `to` ; les colonnes communes sont relogées, `destroyed`
	/// (déjà détruit par l'appelant) est ignoré. Retourne la nouvelle ligne.
	uint32_t MoveEntity(Entity e, Archetype *to, ComponentId destroyed = UINT16_MAX);

	std::vector<Record> m_records;
	std::vector<uint32_t> m_freeList;
	size_t m_alive = 0;
	std::vector<std::unique_ptr<Archetype>> m_archetypes;
	core::RobinMap<uint64_t, std::vector<Archetype *>> m_archetypeIndex; // hachage de signature → archétypes
	Archetype *m_empty = nullptr;
	struct ResourceSlot {
		void *ptr = nullptr;
		void (*deleter)(void *) = nullptr;
	};
	core::RobinMap<ComponentId, ResourceSlot> m_resources;
	std::mutex m_deferMutex;
	CommandBuffer m_deferred;
	jobs::ThreadPool *m_pool;
};

// ── Requête typée ──────────────────────────────────────────────────────────
template <typename... C> class Query {
	static_assert(sizeof...(C) > 0, "Query<> sans composant");

public:
	explicit Query(World &w) : m_world(&w), m_include(MakeSignature<C...>()) {}

	template <typename... X> Query &Without() {
		(m_exclude.set(TypeId<X>()), ...);
		m_matched.clear();
		m_seen = 0;
		return *this;
	}

	/// Appelle `fn(C&...)` ou `fn(Entity, C&...)` pour chaque entité correspondante.
	template <typename Fn> void Each(Fn &&fn) {
		Refresh();
		for (Archetype *a : m_matched) {
			const size_t n = a->Size();
			if (n == 0)
				continue;
			RunRange(a, 0, n, fn);
		}
	}

	/// Version parallèle : lots de `grain` lignes répartis sur le pool. `fn` doit être
	/// thread-safe (n'écrire que dans les composants de l'entité courante).
	template <typename Fn> void ParEach(Fn &&fn, size_t grain = 8192) {
		Refresh();
		m_ranges.clear();
		for (Archetype *a : m_matched)
			for (size_t first = 0, n = a->Size(); first < n; first += grain)
				m_ranges.push_back({a, first, std::min(n, first + grain)});
		jobs::ParallelFor(m_world->Pool(), 0, m_ranges.size(), 1, [&](size_t i) {
			const RangeJob &r = m_ranges[i];
			RunRange(r.archetype, r.first, r.last, fn);
		});
	}

	/// Appelle `fn(std::span<Entity const>, C*...)` une fois par archétype (accès en bloc).
	template <typename Fn> void EachChunk(Fn &&fn) {
		Refresh();
		for (Archetype *a : m_matched)
			if (a->Size())
				fn(a->Entities(), a->template Data<C>()...);
	}

	[[nodiscard]] size_t Count() {
		Refresh();
		size_t n = 0;
		for (Archetype *a : m_matched)
			n += a->Size();
		return n;
	}
	[[nodiscard]] std::span<Archetype *const> MatchedArchetypes() {
		Refresh();
		return m_matched;
	}

private:
	struct RangeJob {
		Archetype *archetype;
		size_t first, last;
	};

	void Refresh() {
		const auto &all = m_world->m_archetypes;
		for (; m_seen < all.size(); ++m_seen) {
			Archetype *a = all[m_seen].get();
			if ((a->Sig() & m_include) == m_include && (a->Sig() & m_exclude).none())
				m_matched.push_back(a);
		}
	}

	template <typename T> static T *Column(Archetype *a) noexcept {
		using Raw = std::remove_cvref_t<T>;
		return a->template Data<Raw>();
	}

	template <typename Fn> static void RunRange(Archetype *a, size_t first, size_t last, Fn &fn) {
		auto cols = std::make_tuple(Column<C>(a)...);
		if constexpr (std::is_invocable_v<Fn &, Entity, C &...>) {
			const Entity *ents = a->Entities().data();
			for (size_t i = first; i < last; ++i)
				std::apply([&](auto *...p) { fn(ents[i], p[i]...); }, cols);
		} else {
			for (size_t i = first; i < last; ++i)
				std::apply([&](auto *...p) { fn(p[i]...); }, cols);
		}
	}

	World *m_world;
	Signature m_include;
	Signature m_exclude;
	std::vector<Archetype *> m_matched;
	size_t m_seen = 0;
	std::vector<RangeJob> m_ranges;
};

// ── Ordonnanceur de systèmes ───────────────────────────────────────────────
class Schedule {
public:
	using SystemFn = std::move_only_function<void(World &, float dt)>;
	struct SystemStats {
		std::string name;
		double lastMs = 0.0;
		double avgMs = 0.0;
		bool enabled = true;
	};

	/// Ajoute un système à la fin ; les systèmes s'exécutent dans l'ordre d'ajout.
	Schedule &Add(std::string name, SystemFn fn);
	void SetEnabled(std::string_view name, bool enabled);
	/// Exécute chaque système puis vide les commandes différées du monde.
	void Run(World &world, float dt);
	[[nodiscard]] std::span<const SystemStats> Stats() const noexcept { return m_stats; }

private:
	std::vector<SystemFn> m_systems;
	std::vector<SystemStats> m_stats;
};

// ══════════════════════════════════════════════════════════════════════════
// Implémentations des templates
// ══════════════════════════════════════════════════════════════════════════

namespace detail {
template <typename... C> std::vector<ComponentId> SortedIds() {
	std::vector<ComponentId> ids{TypeId<C>()...};
	std::ranges::sort(ids);
	ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
	return ids;
}
} // namespace detail

template <typename... C> Entity World::Spawn(C &&...comps) {
	if constexpr (sizeof...(C) == 0) {
		return Spawn();
	} else {
		Archetype *a = FindOrCreateArchetype(detail::SortedIds<C...>());
		const Entity e = AllocateEntity();
		const uint32_t row = a->PushRow(e);
		(std::construct_at(static_cast<std::remove_cvref_t<C> *>(a->ColumnAt(a->ColumnOf(TypeId<C>())).At(row)),
						   std::forward<C>(comps)),
		 ...);
		m_records[e.index].archetype = a;
		m_records[e.index].row = row;
		return e;
	}
}

template <typename... C> void World::SpawnBatch(std::vector<Entity> &out, size_t count, const C &...comps) {
	Archetype *a = FindOrCreateArchetype(detail::SortedIds<C...>());
	a->Reserve(a->Size() + count);
	m_records.reserve(m_records.size() + count);
	const size_t base = a->Size();
	out.reserve(out.size() + count);
	for (size_t i = 0; i < count; ++i) {
		const Entity e = AllocateEntity();
		const uint32_t row = a->PushRow(e);
		m_records[e.index].archetype = a;
		m_records[e.index].row = row;
		out.push_back(e);
	}
	// Construction colonne par colonne : écritures séquentielles, une colonne à la fois.
	(
		[&] {
			using T = std::remove_cvref_t<C>;
			T *col = a->template Data<T>();
			if constexpr (std::is_trivially_copyable_v<T>) {
				for (size_t i = 0; i < count; ++i)
					std::memcpy(static_cast<void *>(col + base + i), &comps, sizeof(T));
			} else {
				for (size_t i = 0; i < count; ++i)
					std::construct_at(col + base + i, comps);
			}
		}(),
		...);
}

template <typename... C> Entity World::SpawnBatch(size_t count, const C &...comps) {
	std::vector<Entity> tmp;
	SpawnBatch(tmp, count, comps...);
	return tmp.empty() ? kNull : tmp.front();
}

template <typename T> T &World::Add(Entity e, T value) {
	Record &r = m_records[e.index];
	const ComponentId id = TypeId<T>();
	if (r.archetype->Has(id)) {
		T *p = r.archetype->template Data<T>() + r.row;
		*p = std::move(value);
		return *p;
	}
	Archetype *to = AddEdge(r.archetype, id);
	const uint32_t row = MoveEntity(e, to);
	T *p = to->template Data<T>() + row;
	std::construct_at(p, std::move(value));
	return *p;
}

template <typename T> bool World::Remove(Entity e) {
	if (!IsAlive(e))
		return false;
	Record &r = m_records[e.index];
	const ComponentId id = TypeId<T>();
	if (!r.archetype->Has(id))
		return false;
	Archetype *from = r.archetype;
	std::destroy_at(from->template Data<T>() + r.row);
	MoveEntity(e, RemoveEdge(from, id), id);
	return true;
}

template <typename T> T *World::Get(Entity e) noexcept {
	if (!IsAlive(e))
		return nullptr;
	const Record &r = m_records[e.index];
	T *col = r.archetype->template Data<T>();
	return col ? col + r.row : nullptr;
}
template <typename T> const T *World::Get(Entity e) const noexcept { return const_cast<World *>(this)->Get<T>(e); }
template <typename T> bool World::Has(Entity e) const noexcept { return IsAlive(e) && m_records[e.index].archetype->Has(TypeId<T>()); }

template <typename... C> Query<C...> World::MakeQuery() { return Query<C...>(*this); }

template <typename R, typename... Args> R &World::SetResource(Args &&...args) {
	RemoveResource<R>();
	R *p = new R(std::forward<Args>(args)...);
	m_resources.InsertOrAssign(TypeId<R>(), ResourceSlot{p, [](void *q) { delete static_cast<R *>(q); }});
	return *p;
}
template <typename R> R *World::GetResource() noexcept {
	ResourceSlot *s = m_resources.Find(TypeId<R>());
	return s ? static_cast<R *>(s->ptr) : nullptr;
}
template <typename R> bool World::RemoveResource() {
	if (auto s = m_resources.Take(TypeId<R>())) {
		s->deleter(s->ptr);
		return true;
	}
	return false;
}

template <typename T> void CommandBuffer::Add(Entity e, T value) {
	m_ops.push_back([e, v = std::move(value)](World &w) mutable {
		if (w.IsAlive(e))
			w.Add<T>(e, std::move(v));
	});
}
template <typename T> void CommandBuffer::Remove(Entity e) {
	m_ops.push_back([e](World &w) { w.Remove<T>(e); });
}
template <typename... C> void CommandBuffer::Spawn(C... comps) {
	m_ops.push_back([... c = std::move(comps)](World &w) mutable { w.Spawn(std::move(c)...); });
}

} // namespace ecs
