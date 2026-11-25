#pragma once
/**
 * @file demo.hpp
 * @brief Document de démonstration construit avec les actions de l'éditeur (option `--demo`,
 *        captures d'écran de vérification, tests).
 */
#include "scribe/editor.hpp"

namespace scribe {

/// Remplace le document de l'éditeur par un document d'exemple (contenu fictif, de type fiche
/// technique) : page de titre, tables des matières, des tableaux et des figures, titres
/// numérotés (styles), encadré avec lien, colonnes (tableau sans bordure) de listes à puces,
/// tableaux de données (en-tête répété, filets, bandes, exposants, légendes), figure légendée,
/// renvois (numéro et page), listes numérotées imbriquées, bloc de code, citation, en-tête
/// courant (titre du chapitre) et numéro de page en pied.
void BuildDemo(Editor &editor);

} // namespace scribe
