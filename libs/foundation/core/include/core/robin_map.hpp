#pragma once
/**
 * @file robin_map.hpp
 * @brief `core::RobinMap<K, V>` — table de hachage à adressage ouvert, hachage
 *        Robin Hood et suppression par décalage arrière (pas de pierres tombales).
 *
 * - Capacité puissance de 2, facteur de charge max 7/8 ;
 * - métadonnées séparées (1 octet de distance de sondage par case) : le sondage
 *   lit une ligne de cache de métadonnées avant de toucher aux clés ;
 * - la distance au slot idéal reste bornée et faible, donc `Find` est O(1)
 *   même en cas d'échec (arrêt dès que la distance du slot < distance courante) ;
 * - stockage contigu des entrées : itération linéaire sans pointeurs.
 *
 * Les pointeurs/itérateurs sont invalidés par toute insertion ou suppression.
 */
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#include "hash.hpp"

namespace core {

template <typename K, typename V, typename Hash = Hasher<K>, typename Eq = std::equal_to<>> class RobinMap {
public:
	struct Entry {
		K key;
		V value;
	};

private:
	// dist == 0 : case vide ; sinon distance au slot idéal + 1.
	std::unique_ptr<uint8_t[]> m_dist;
	Entry *m_slots = nullptr;
	size_t m_mask = 0; // capacité - 1 (capacité 0 => m_mask = 0 et m_slots null)
	size_t m_size = 0;
	[[no_unique_address]] Hash m_hash{};
	[[no_unique_address]] Eq m_eq{};

	[[nodiscard]] size_t SlotCount() const noexcept { return m_slots ? m_mask + 1 : 0; }

	void FreeSlots() noexcept {
		if (!m_slots)
			return;
		const size_t cap = SlotCount();
		for (size_t i = 0; i < cap; ++i)
			if (m_dist[i])
				std::destroy_at(&m_slots[i]);
		std::allocator<Entry>{}.deallocate(m_slots, cap);
		m_slots = nullptr;
		m_dist.reset();
		m_mask = 0;
	}

	void Rehash(size_t newCap) {
		Entry *old = m_slots;
		std::unique_ptr<uint8_t[]> oldDist = std::move(m_dist);
		const size_t oldCap = SlotCount();
		m_slots = std::allocator<Entry>{}.allocate(newCap);
		m_dist = std::make_unique<uint8_t[]>(newCap); // zéro-initialisé
		m_mask = newCap - 1;
		m_size = 0;
		for (size_t i = 0; i < oldCap; ++i) {
			if (oldDist[i]) {
				InsertNew(std::move(old[i].key), std::move(old[i].value));
				std::destroy_at(&old[i]);
			}
		}
		if (old)
			std::allocator<Entry>{}.deallocate(old, oldCap);
	}

	void Grow() { Rehash(m_slots ? SlotCount() * 2 : 16); }

	// Insère une clé absente ; retourne l'index final de l'entrée insérée.
	size_t InsertNew(K key, V value) {
		size_t idx = m_hash(key) & m_mask;
		uint8_t dist = 1;
		size_t result = SIZE_MAX;
		Entry carry{std::move(key), std::move(value)};
		for (;;) {
			if (m_dist[idx] == 0) {
				std::construct_at(&m_slots[idx], std::move(carry));
				m_dist[idx] = dist;
				++m_size;
				return result == SIZE_MAX ? idx : result;
			}
			if (m_dist[idx] < dist) { // Robin Hood : on vole la place du plus « riche »
				std::swap(carry, m_slots[idx]);
				std::swap(dist, m_dist[idx]);
				if (result == SIZE_MAX)
					result = idx;
			}
			idx = (idx + 1) & m_mask;
			if (++dist == 255) { // distance pathologique : agrandir et recommencer
				Grow();
				const size_t r = InsertNew(std::move(carry.key), std::move(carry.value));
				return result == SIZE_MAX ? r : SIZE_MAX; // l'index précédent n'est plus valide
			}
		}
	}

	template <typename Q> [[nodiscard]] size_t FindIndex(const Q &key) const noexcept {
		if (!m_slots)
			return SIZE_MAX;
		size_t idx = m_hash(key) & m_mask;
		for (uint8_t dist = 1;; ++dist) {
			const uint8_t d = m_dist[idx];
			if (d < dist)
				return SIZE_MAX;
			if (d == dist && m_eq(m_slots[idx].key, key))
				return idx;
			idx = (idx + 1) & m_mask;
		}
	}

	void EraseAt(size_t idx) noexcept {
		std::destroy_at(&m_slots[idx]);
		m_dist[idx] = 0;
		size_t next = (idx + 1) & m_mask;
		while (m_dist[next] > 1) { // décalage arrière
			std::construct_at(&m_slots[idx], std::move(m_slots[next]));
			std::destroy_at(&m_slots[next]);
			m_dist[idx] = static_cast<uint8_t>(m_dist[next] - 1);
			m_dist[next] = 0;
			idx = next;
			next = (next + 1) & m_mask;
		}
		--m_size;
	}

public:
	template <bool Const> class Iter {
		using Map = std::conditional_t<Const, const RobinMap, RobinMap>;
		Map *m_map;
		size_t m_i;
		void Skip() noexcept {
			const size_t cap = m_map->SlotCount();
			while (m_i < cap && !m_map->m_dist[m_i])
				++m_i;
		}

	public:
		Iter(Map *m, size_t i) noexcept : m_map(m), m_i(i) { Skip(); }
		auto &operator*() const noexcept { return m_map->m_slots[m_i]; }
		auto *operator->() const noexcept { return &m_map->m_slots[m_i]; }
		Iter &operator++() noexcept {
			++m_i;
			Skip();
			return *this;
		}
		bool operator==(const Iter &o) const noexcept { return m_i == o.m_i; }
	};
	using Iterator = Iter<false>;
	using ConstIterator = Iter<true>;

	RobinMap() = default;
	explicit RobinMap(size_t reserve) { Reserve(reserve); }
	RobinMap(const RobinMap &o) : m_hash(o.m_hash), m_eq(o.m_eq) {
		Reserve(o.m_size);
		for (const auto &e : o)
			InsertNew(e.key, e.value);
	}
	RobinMap &operator=(const RobinMap &o) {
		if (this != &o) {
			RobinMap tmp(o);
			Swap(tmp);
		}
		return *this;
	}
	RobinMap(RobinMap &&o) noexcept { Swap(o); }
	RobinMap &operator=(RobinMap &&o) noexcept {
		if (this != &o) {
			FreeSlots();
			m_size = 0;
			Swap(o);
		}
		return *this;
	}
	~RobinMap() { FreeSlots(); }

	void Swap(RobinMap &o) noexcept {
		std::swap(m_dist, o.m_dist);
		std::swap(m_slots, o.m_slots);
		std::swap(m_mask, o.m_mask);
		std::swap(m_size, o.m_size);
	}

	[[nodiscard]] size_t Size() const noexcept { return m_size; }
	[[nodiscard]] bool Empty() const noexcept { return m_size == 0; }
	[[nodiscard]] size_t Capacity() const noexcept { return SlotCount(); }

	void Reserve(size_t n) {
		size_t cap = 16;
		while (cap * 7 / 8 < n)
			cap <<= 1;
		if (cap > SlotCount())
			Rehash(cap);
	}
	void Clear() noexcept {
		if (!m_slots)
			return;
		const size_t cap = SlotCount();
		for (size_t i = 0; i < cap; ++i)
			if (m_dist[i]) {
				std::destroy_at(&m_slots[i]);
				m_dist[i] = 0;
			}
		m_size = 0;
	}

	template <typename Q> [[nodiscard]] V *Find(const Q &key) noexcept {
		const size_t i = FindIndex(key);
		return i == SIZE_MAX ? nullptr : &m_slots[i].value;
	}
	template <typename Q> [[nodiscard]] const V *Find(const Q &key) const noexcept {
		const size_t i = FindIndex(key);
		return i == SIZE_MAX ? nullptr : &m_slots[i].value;
	}
	template <typename Q> [[nodiscard]] bool Contains(const Q &key) const noexcept { return FindIndex(key) != SIZE_MAX; }

	/// Insère `key` si absente (valeur construite depuis args) ; retourne {valeur, inséré}.
	template <typename... Args> std::pair<V *, bool> TryEmplace(const K &key, Args &&...args) {
		if (const size_t i = FindIndex(key); i != SIZE_MAX)
			return {&m_slots[i].value, false};
		if ((m_size + 1) * 8 > SlotCount() * 7)
			Grow();
		// La clé est copiée AVANT de construire la valeur : `key` peut référencer un
		// membre de l'objet déplacé dans `args` (ex. InsertOrAssign(v.id, std::move(v))).
		K ownedKey(key);
		V value(std::forward<Args>(args)...);
		size_t i = InsertNew(ownedKey, std::move(value));
		if (i == SIZE_MAX)
			i = FindIndex(ownedKey);
		return {&m_slots[i].value, true};
	}
	template <typename U> V &InsertOrAssign(const K &key, U &&value) {
		auto [v, inserted] = TryEmplace(key, std::forward<U>(value));
		if (!inserted)
			*v = std::forward<U>(value);
		return *v;
	}
	V &operator[](const K &key) { return *TryEmplace(key).first; }

	template <typename Q> bool Erase(const Q &key) noexcept {
		const size_t i = FindIndex(key);
		if (i == SIZE_MAX)
			return false;
		EraseAt(i);
		return true;
	}
	/// Retire et retourne la valeur associée à `key` (si présente).
	template <typename Q> std::optional<V> Take(const Q &key) {
		const size_t i = FindIndex(key);
		if (i == SIZE_MAX)
			return std::nullopt;
		std::optional<V> out(std::move(m_slots[i].value));
		EraseAt(i);
		return out;
	}
	/// Supprime toutes les entrées pour lesquelles `pred(key, value)` est vrai.
	template <typename Pred> size_t EraseIf(Pred &&pred) {
		size_t removed = 0;
		for (size_t i = 0; i < SlotCount();) {
			if (m_dist[i] && pred(m_slots[i].key, m_slots[i].value)) {
				EraseAt(i); // le décalage arrière peut ramener une entrée en i : on ne l'avance pas
				++removed;
			} else {
				++i;
			}
		}
		return removed;
	}

	Iterator begin() noexcept { return Iterator(this, 0); }
	Iterator end() noexcept { return Iterator(this, SlotCount()); }
	ConstIterator begin() const noexcept { return ConstIterator(this, 0); }
	ConstIterator end() const noexcept { return ConstIterator(this, SlotCount()); }
};

} // namespace core
