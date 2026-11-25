#pragma once
/**
 * @file heap.hpp
 * @brief Collecteur de cycles de l'interpréteur.
 *
 * Les valeurs de script sont comptées par référence (`std::shared_ptr`). Un cycle
 * (fermeture locale ↔ environnement de l'appel, `enfant.parent = this`, énumération ↔ ses
 * instances…) ne serait jamais libéré : une fonction appelée à chaque image qui déclare
 * une fermeture fuirait à chaque appel.
 *
 * `Heap` suit (par `weak_ptr`) les objets susceptibles de former un cycle, y ajoute tout ce
 * qu'ils atteignent (conteneurs créés ailleurs, environnements), et applique
 * l'algorithme de CPython : pour chaque objet suivi, on retranche de `use_count()` les
 * références venant d'autres objets suivis ; un reste positif signale une référence
 * extérieure (pile C++, globales, hôte, état natif opaque) : l'objet est une racine. Tout
 * ce qui n'est pas atteignable depuis une racine est vidé, ce qui casse les cycles.
 *
 * Conservateur : une référence invisible (capturée dans une native, un état C++, une liste
 * créée par l'hôte) compte comme extérieure, donc un objet n'est jamais libéré à tort.
 * Doit être appelé sous le verrou global, hors de toute évaluation sur le fil appelant.
 */
#include <cstddef>
#include <memory>
#include <vector>

#include "internal.hpp"

namespace script {

class Heap {
public:
	void Track(const std::shared_ptr<Closure> &c) { Add(m_closures, c); }
	void Track(const std::shared_ptr<Instance> &i) { Add(m_instances, i); }
	void Track(const std::shared_ptr<ClassDef> &c) { Add(m_classes, c); }
	void Track(const std::shared_ptr<List> &l) { Add(m_lists, l); }
	void Track(const std::shared_ptr<Map> &m) { Add(m_maps, m); }

	/// Assez d'allocations depuis la dernière collecte pour en justifier une nouvelle.
	[[nodiscard]] bool Due() const noexcept { return m_allocations >= m_threshold; }

	/// Libère les cycles inaccessibles ; rend le nombre d'objets vidés.
	std::size_t Collect();

private:
	template <typename T>
	void Add(std::vector<std::weak_ptr<T>> &list, const std::shared_ptr<T> &p) {
		list.emplace_back(p);
		++m_allocations;
	}

	std::vector<std::weak_ptr<Closure>> m_closures;
	std::vector<std::weak_ptr<Instance>> m_instances;
	std::vector<std::weak_ptr<ClassDef>> m_classes;
	std::vector<std::weak_ptr<List>> m_lists;
	std::vector<std::weak_ptr<Map>> m_maps;
	std::size_t m_allocations = 0;
	std::size_t m_threshold = 4096;
};

} // namespace script
