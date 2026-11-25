#pragma once
/**
 * @file json.hpp
 * @brief Fonctions JSON de commodité sur l'arbre `dataio::Node` : parsage tolérant
 *        (commentaires `//`, `#`, commentaires bloc du C et virgules finales), écriture compacte ou
 *        indentée, lecture/écriture de fichiers, et édition en place de tableaux.
 *
 * Le parsage et l'écriture du texte JSON sont délégués au codec commun
 * `dataio::JsonDocument` (sur l'arbre `dataio::Node` partagé avec les autres formats).
 */
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <core/result.hpp>

#include "dataio/documents/json.hpp" // JsonDocument, WriteJsonNode (apporte ParseError)

namespace dataio {

/// Parse `text` en arbre `Node` (tolérant : commentaires, virgules finales).
[[nodiscard]] Result<NodePtr, ParseError> ParseJson(StringView text);
/// Lit un fichier JSON (tolérant).
[[nodiscard]] Result<NodePtr, ParseError> LoadJsonFile(const std::string &path);
/// `indent < 0` : sortie compacte (une ligne).
[[nodiscard]] String WriteJson(const NodePtr &value, int indent = 2);
bool SaveJsonFile(const std::string &path, const NodePtr &value, int indent = 2);

/// Positions [début, fin) des éléments du tableau racine d'un texte JSON
/// (commentaires `//`, `#` et `/* *\/` tolérés) ; vide si la racine n'est pas un tableau.
[[nodiscard]] std::vector<std::pair<size_t, size_t>> JsonArrayElementSpans(StringView text);
/// Remplace l'élément `index` du tableau racine par `value` (JSON), sans toucher au
/// reste du texte (commentaires, mise en forme des autres éléments) ; indentation
/// reprise de l'élément remplacé. `nullopt` si l'élément n'existe pas.
[[nodiscard]] std::optional<String> ReplaceJsonArrayElement(StringView text, size_t index, const NodePtr &value);
/// Ajoute `value` à la fin du tableau racine (même indentation que le dernier élément).
[[nodiscard]] std::optional<String> AppendJsonArrayElement(StringView text, const NodePtr &value);

} // namespace dataio
