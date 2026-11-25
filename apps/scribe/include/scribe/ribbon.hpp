#pragma once
/**
 * @file ribbon.hpp
 * @brief Onglets du ruban et choix de l'onglet contextuel (fonction pure, testable).
 */
#include <string_view>

#include <document/selection.hpp>

namespace scribe {

enum class RibbonTab : int { File, Home, Insert, References, PageLayout, Table, Arrange, View, Count };
inline constexpr int kRibbonTabCount = int(RibbonTab::Count);

[[nodiscard]] std::string_view TabName(RibbonTab tab) noexcept;

/// Onglet à afficher après une *nouvelle* sélection (README §1.7) :
///  - sélection vide : onglet courant conservé ;
///  - contenu d'un tableau (tableau, ligne, cellule ou texte d'une cellule) : Tableau ;
///  - groupe, ou uniquement des images / formes : Disposition ;
///  - page : Mise en page ;
///  - texte (paragraphe, segment, en-tête, pied) : Accueil ;
///  - sinon : onglet courant.
[[nodiscard]] RibbonTab SuggestTab(const doc::SelectionContext &context, RibbonTab current) noexcept;

} // namespace scribe
