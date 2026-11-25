#pragma once
/**
 * @file base64.hpp
 * @brief Base64 (RFC 4648, alphabet standard, remplissage « = ») : URI `data:` (images
 *        embarquées dans du HTML, tampons glTF), pièces jointes texte.
 *
 *   const std::string text = dataio::Base64Encode(bytes);
 *   auto back = dataio::Base64Decode(text);              // std::nullopt si invalide
 */
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dataio {

[[nodiscard]] std::string Base64Encode(std::span<const uint8_t> bytes);
/// Décode ; les blancs (espace, tabulation, CR, LF) sont ignorés, le remplissage final est
/// facultatif. Caractère hors alphabet, « = » mal placé ou longueur impossible : std::nullopt.
[[nodiscard]] std::optional<std::vector<uint8_t>> Base64Decode(std::string_view text);

} // namespace dataio
