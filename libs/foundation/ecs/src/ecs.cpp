#include "ecs/ecs.hpp"

#include <cassert>
#include <chrono>
#include <deque>

namespace ecs {

// ── Registre des types de composants ──────────────────────────────────────
namespace {
struct Registry {
	std::mutex mutex;
	std::deque<ComponentInfo> infos; // deque : adresses stables
};
Registry &GetRegistry() {
	static Registry r;
	return r;
}
} // namespace

ComponentId detail::RegisterComponent(ComponentInfo info) {
	Registry &r = GetRegistry();
	std::scoped_lock lock(r.mutex);
	if (r.infos.size() >= kMaxComponents) {
		std::fprintf(stderr, "ecs: trop de types de composants (max %zu)\n", kMaxComponents);
		std::abort();
	}
	r.infos.push_back(std::move(info));
	return static_cast<ComponentId>(r.infos.size() - 1);
}

const ComponentInfo &GetComponentInfo(ComponentId id) {
	Registry &r = GetRegistry();
	std::scoped_lock lock(r.mutex);
	return r.infos[id];
}

size_t ComponentTypeCount() {
	Registry &r = GetRegistry();
	std::scoped_lock lock(r.mutex);
	return r.infos.size();
}

// ── Column ────────────────────────────────────────────────────────────────
Column &Column::operator=(Column &&o) noexcept {
	if (this != &o) {
		if (m_data)
			::operator delete(m_data, std::align_val_t{kAlign});
		m_data = std::exchange(o.m_data, nullptr);
		m_capacity = std::exchange(o.m_capacity, 0);
		m_info = o.m_info;
	}
	return *this;
}

Column::~Column() {
	if (m_data)
		::operator delete(m_data, std::align_val_t{kAlign});
}

void Column::Reserve(size_t cap, size_t size) {
	if (cap <= m_capacity)
		return;
	const size_t elem = m_info->size;
	auto *fresh = static_cast<std::byte *>(::operator new(std::max<size_t>(cap * elem, 1), std::align_val_t{kAlign}));
	if (m_data) {
		if (m_info->trivial) {
			if (size)
				std::memcpy(fresh, m_data, size * elem);
		} else {
			for (size_t i = 0; i < size; ++i)
				m_info->relocate(fresh + i * elem, m_data + i * elem);
		}
		::operator delete(m_data, std::align_val_t{kAlign});
	}
	m_data = fresh;
	m_capacity = cap;
}

void Column::DestroyRange(size_t first, size_t last) noexcept {
	if (m_info->trivial)
		return;
	for (size_t i = first; i < last; ++i)
		m_info->destroy(At(i));
}

// ── Archetype ─────────────────────────────────────────────────────────────
Archetype::Archetype(uint32_t id, std::vector<ComponentId> ids) : m_id(id), m_ids(std::move(ids)) {
	std::fill(std::begin(m_columnOf), std::end(m_columnOf), int16_t{-1});
	m_columns.reserve(m_ids.size());
	for (size_t i = 0; i < m_ids.size(); ++i) {
		m_signature.set(m_ids[i]);
		m_columnOf[m_ids[i]] = static_cast<int16_t>(i);
		m_columns.emplace_back(GetComponentInfo(m_ids[i]));
	}
}

Archetype::~Archetype() {
	for (Column &c : m_columns)
		c.DestroyRange(0, m_entities.size());
}

void Archetype::Reserve(size_t n) {
	if (n <= m_capacity)
		return;
	size_t cap = std::max<size_t>(m_capacity * 2, 64);
	while (cap < n)
		cap *= 2;
	for (Column &c : m_columns)
		c.Reserve(cap, m_entities.size());
	m_entities.reserve(cap);
	m_capacity = cap;
}

uint32_t Archetype::PushRow(Entity e) {
	if (m_entities.size() == m_capacity)
		Reserve(m_capacity + 1);
	m_entities.push_back(e);
	return static_cast<uint32_t>(m_entities.size() - 1);
}

Entity Archetype::SwapRemove(uint32_t row, const Signature &movedOut) {
	const size_t last = m_entities.size() - 1;
	for (size_t c = 0; c < m_columns.size(); ++c) {
		Column &col = m_columns[c];
		const ComponentInfo &info = col.Info();
		const bool gone = movedOut.test(m_ids[c]);
		if (row != last) {
			if (!gone && !info.trivial)
				info.destroy(col.At(row));
			if (info.trivial)
				std::memcpy(col.At(row), col.At(last), info.size);
			else
				info.relocate(col.At(row), col.At(last));
		} else if (!gone && !info.trivial) {
			info.destroy(col.At(row));
		}
	}
	Entity moved = kNull;
	if (row != last) {
		moved = m_entities[last];
		m_entities[row] = moved;
	}
	m_entities.pop_back();
	return moved;
}

// ── World ─────────────────────────────────────────────────────────────────
World::World() : m_pool(&jobs::ThreadPool::Global()) { m_empty = FindOrCreateArchetype({}); }

World::~World() {
	m_archetypes.clear();
	for (auto &e : m_resources)
		e.value.deleter(e.value.ptr);
}

void World::Clear() {
	m_archetypes.clear();
	m_archetypeIndex.Clear();
	m_records.clear();
	m_freeList.clear();
	m_alive = 0;
	m_empty = FindOrCreateArchetype({});
}

Entity World::AllocateEntity() {
	uint32_t index;
	if (!m_freeList.empty()) {
		index = m_freeList.back();
		m_freeList.pop_back();
	} else {
		index = static_cast<uint32_t>(m_records.size());
		m_records.emplace_back();
	}
	++m_alive;
	return {index, m_records[index].generation};
}

Entity World::EntityAt(uint32_t index) const noexcept {
	if (index >= m_records.size() || !m_records[index].archetype)
		return kNull;
	return {index, m_records[index].generation};
}

Entity World::Spawn() {
	const Entity e = AllocateEntity();
	m_records[e.index].archetype = m_empty;
	m_records[e.index].row = m_empty->PushRow(e);
	return e;
}

bool World::Despawn(Entity e) {
	if (!IsAlive(e))
		return false;
	Record &r = m_records[e.index];
	const Entity moved = r.archetype->SwapRemove(r.row);
	if (!moved.IsNull())
		m_records[moved.index].row = r.row;
	r.archetype = nullptr;
	++r.generation;
	m_freeList.push_back(e.index);
	--m_alive;
	return true;
}

Archetype *World::FindOrCreateArchetype(std::vector<ComponentId> ids) {
	uint64_t h = 0x51ed2701f3a5c7b9ULL;
	for (ComponentId id : ids)
		h = core::HashCombine(h, id);
	auto &bucket = m_archetypeIndex[h];
	for (Archetype *a : bucket)
		if (std::ranges::equal(a->Components(), ids))
			return a;
	auto arch = std::make_unique<Archetype>(static_cast<uint32_t>(m_archetypes.size()), std::move(ids));
	Archetype *raw = arch.get();
	m_archetypes.push_back(std::move(arch));
	bucket.push_back(raw);
	return raw;
}

Archetype *World::AddEdge(Archetype *from, ComponentId id) {
	if (Archetype **cached = from->m_addEdges.Find(id))
		return *cached;
	std::vector<ComponentId> ids(from->m_ids);
	ids.insert(std::ranges::upper_bound(ids, id), id);
	Archetype *to = FindOrCreateArchetype(std::move(ids));
	from->m_addEdges.InsertOrAssign(id, to);
	to->m_removeEdges.InsertOrAssign(id, from);
	return to;
}

Archetype *World::RemoveEdge(Archetype *from, ComponentId id) {
	if (Archetype **cached = from->m_removeEdges.Find(id))
		return *cached;
	std::vector<ComponentId> ids(from->m_ids);
	std::erase(ids, id);
	Archetype *to = FindOrCreateArchetype(std::move(ids));
	from->m_removeEdges.InsertOrAssign(id, to);
	to->m_addEdges.InsertOrAssign(id, from);
	return to;
}

uint32_t World::MoveEntity(Entity e, Archetype *to, ComponentId destroyed) {
	Record &r = m_records[e.index];
	Archetype *from = r.archetype;
	const uint32_t oldRow = r.row;
	const uint32_t newRow = to->PushRow(e);
	Signature moved;
	for (size_t c = 0; c < from->m_ids.size(); ++c) {
		const ComponentId id = from->m_ids[c];
		const int dst = to->ColumnOf(id);
		if (dst < 0 || id == destroyed)
			continue;
		Column &src = from->m_columns[c];
		Column &dcol = to->ColumnAt(dst);
		const ComponentInfo &info = src.Info();
		if (info.trivial)
			std::memcpy(dcol.At(newRow), src.At(oldRow), info.size);
		else
			info.relocate(dcol.At(newRow), src.At(oldRow));
		moved.set(id);
	}
	if (destroyed != UINT16_MAX)
		moved.set(destroyed);
	const Entity swapped = from->SwapRemove(oldRow, moved);
	if (!swapped.IsNull())
		m_records[swapped.index].row = oldRow;
	r.archetype = to;
	r.row = newRow;
	return newRow;
}

void World::Apply(CommandBuffer &cb) {
	for (Entity e : cb.m_despawn)
		Despawn(e);
	for (auto &op : cb.m_ops)
		op(*this);
	cb.Clear();
}

void World::Defer(std::move_only_function<void(World &)> fn) {
	std::scoped_lock lock(m_deferMutex);
	m_deferred.Run(std::move(fn));
}

void World::FlushDeferred() {
	CommandBuffer local;
	{
		std::scoped_lock lock(m_deferMutex);
		std::swap(local.m_despawn, m_deferred.m_despawn);
		std::swap(local.m_ops, m_deferred.m_ops);
	}
	Apply(local);
}

// ── Schedule ──────────────────────────────────────────────────────────────
Schedule &Schedule::Add(std::string name, SystemFn fn) {
	m_systems.push_back(std::move(fn));
	m_stats.push_back({std::move(name)});
	return *this;
}

void Schedule::SetEnabled(std::string_view name, bool enabled) {
	for (auto &s : m_stats)
		if (s.name == name)
			s.enabled = enabled;
}

void Schedule::Run(World &world, float dt) {
	using Clock = std::chrono::steady_clock;
	for (size_t i = 0; i < m_systems.size(); ++i) {
		SystemStats &st = m_stats[i];
		if (!st.enabled)
			continue;
		const auto t0 = Clock::now();
		m_systems[i](world, dt);
		world.FlushDeferred();
		st.lastMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
		st.avgMs += (st.lastMs - st.avgMs) * 0.05;
	}
}

} // namespace ecs
