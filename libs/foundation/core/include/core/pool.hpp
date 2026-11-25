#pragma once
/**
 * @file pool.hpp
 * @brief Arène générationnelle : stockage contigu d'objets accédés par des handles
 *        validables (indice + génération), réutilisés sans invalider les anciens handles.
 *
 *   core::Pool<T> p;
 *   const core::Handle h = p.Emplace(1, 2, 3);   // construction en place, O(1)
 *   if (T *v = p.Get(h)) …                        // accès validé
 *   p.Erase(h);                                   // l'emplacement est réutilisé
 *   p.ForEach([](core::Handle h, T &v) { … });    // itération des objets vivants
 *
 * L'index 0 n'est jamais valide : un `Handle{}` (génération 0) est le handle « nul ».
 */
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace core {

/// Handle générationnel : identifie un emplacement d'une arène. Un handle reste
/// « mort » (génération obsolète) après l'effacement de son objet — `Pool::Contains`
/// le détecte. L'index 0 n'est jamais attribué.
struct Handle {
	uint32_t index = 0;
	uint32_t generation = 0;
	constexpr bool operator==(const Handle &) const = default;
	constexpr explicit operator bool() const noexcept { return generation != 0; }
};

/// Arène générationnelle de `T`. Les objets vivent dans un `std::vector` contigu ;
/// les emplacements libérés sont réutilisés (liste libre). `T` doit être au minimum
/// constructible/mobile (porté par un `std::optional`).
template <typename T>
class Pool {
public:
	Pool() = default;
	Pool(const Pool &) = delete;
	Pool &operator=(const Pool &) = delete;
	Pool(Pool &&) noexcept = default;
	Pool &operator=(Pool &&) noexcept = default;

	/// Construit un objet en place et rend son handle.
	template <typename... Args> Handle Emplace(Args &&...args) {
		const uint32_t index = TakeSlot();
		slots_[index].value.emplace(std::forward<Args>(args)...);
		++size_;
		return {index, slots_[index].generation};
	}

	/// Insère une valeur déplacée et rend son handle.
	Handle Insert(T value) { return Emplace(std::move(value)); }

	/// Pointeur vers l'objet du handle, ou nullptr si le handle est mort.
	[[nodiscard]] T *Get(Handle h) noexcept { return Contains(h) ? &*slots_[h.index].value : nullptr; }
	[[nodiscard]] const T *Get(Handle h) const noexcept { return Contains(h) ? &*slots_[h.index].value : nullptr; }

	/// Efface l'objet (l'emplacement est réutilisable). Faux si le handle est déjà mort.
	bool Erase(Handle h) noexcept {
		if (!Contains(h))
			return false;
		slots_[h.index].value.reset();
		BumpGeneration(h.index);
		free_.push_back(h.index);
		--size_;
		return true;
	}

	/// Le handle désigne-t-il un objet vivant ?
	[[nodiscard]] bool Contains(Handle h) const noexcept {
		return h.index < slots_.size() && slots_[h.index].generation == h.generation && slots_[h.index].value.has_value();
	}

	[[nodiscard]] size_t Size() const noexcept { return size_; }
	[[nodiscard]] bool Empty() const noexcept { return size_ == 0; }
	[[nodiscard]] size_t Capacity() const noexcept { return slots_.size(); }
	void Reserve(size_t n) { slots_.reserve(n); }

	/// Parcourt les objets vivants : `fn(Handle, T &)`.
	template <typename F> void ForEach(F &&fn) {
		for (size_t i = 0; i < slots_.size(); ++i)
			if (slots_[i].value)
				fn(Handle{uint32_t(i), slots_[i].generation}, *slots_[i].value);
	}
	template <typename F> void ForEach(F &&fn) const {
		for (size_t i = 0; i < slots_.size(); ++i)
			if (slots_[i].value)
				fn(Handle{uint32_t(i), slots_[i].generation}, *slots_[i].value);
	}

	/// Efface tout (les handles existants deviennent morts).
	void Clear() noexcept {
		slots_.clear();
		free_.clear();
		size_ = 0;
	}

private:
	struct Slot {
		std::optional<T> value;
		uint32_t generation = 0;
	};

	uint32_t TakeSlot() {
		if (!free_.empty()) {
			const uint32_t i = free_.back();
			free_.pop_back();
			return i;
		}
		if (slots_.empty()) // réserve l'index 0 pour le handle nul (génération 0)
			slots_.emplace_back();
		slots_.emplace_back();
		slots_.back().generation = 1; // première génération attribuée
		return uint32_t(slots_.size() - 1);
	}

	void BumpGeneration(uint32_t index) {
		uint32_t &g = slots_[index].generation;
		g = (g + 1) & 0x7FFFFFFFu;
		if (g == 0) // 0 reste réservé à « jamais valide »
			g = 1;
	}

	std::vector<Slot> slots_;
	std::vector<uint32_t> free_;
	size_t size_ = 0;
};

} // namespace core
