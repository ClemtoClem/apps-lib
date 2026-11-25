#pragma once
/**
 * @file catalog.hpp
 * @brief Catalogue de fonctionnalités : description structurée des outils, panneaux,
 *        notions et API de l'application, avec recherche insensible à la casse.
 *        Alimente le panneau « Aide → Catalogue » (voir Phase 2).
 *
 *   ui::Catalog cat;
 *   cat.Add({"ligne", "Outils", "Ligne", icon, "Dessine une ligne", "…"});
 *   for (size_t i : cat.Search("ligne")) … cat.entries[i] …;
 */
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

/// Entrée du catalogue : un outil, un panneau, une notion ou une API de script.
struct CatalogEntry {
	std::string id;		  ///< identifiant stable
	std::string category; ///< regroupement (« Outils », « Panneaux », « Scripts »…)
	std::string name;	  ///< titre court
	char32_t icon = 0;	  ///< icône Material (codepoint), 0 = aucune
	std::string summary;  ///< résumé d'une ligne
	std::string description; ///< détail et mode d'emploi
};

/// Collection d'entrées de catalogue, avec recherche plein-texte.
struct Catalog {
	std::vector<CatalogEntry> entries;

	void Add(CatalogEntry entry) { entries.push_back(std::move(entry)); }
	void Clear() noexcept { entries.clear(); }
	[[nodiscard]] bool Empty() const noexcept { return entries.empty(); }
	[[nodiscard]] size_t Size() const noexcept { return entries.size(); }

	/// Indices des entrées dont l'identifiant, la catégorie, le nom, le résumé ou la
	/// description contiennent `query` (insensible à la casse). Requête vide : tout.
	[[nodiscard]] std::vector<size_t> Search(std::string_view query) const;
};

} // namespace ui
