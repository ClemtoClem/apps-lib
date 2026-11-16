#pragma once
/**
 * @file signature.hpp
 * @brief Recherche de motifs : signatures d'algorithmes et de bibliothèques
 *        (constantes connues, tables) retrouvées dans les octets d'un binaire.
 */
#include <cstdint>
#include <string>
#include <vector>

#include <core/core.hpp>

#include "elf.hpp"

namespace rea {

/// Une signature : un motif d'octets à rechercher, son nom et sa description.
struct ByteSignature {
	std::string name;			 ///< « AES », « SHA-256 », « MD5 »…
	std::vector<uint8_t> bytes;	 ///< octets exacts à retrouver
	std::string description;	 ///< « boîte S du chiffrement AES »…
};

/// Une occurrence trouvée.
struct ByteSignatureMatch {
	std::string name;
	uint64_t offset = 0;	///< décalage dans le fichier
	uint64_t address = 0;	///< adresse virtuelle (0 si non mappée)
	std::string description;
};

/// Signatures intégrées : AES (boîte S), SHA-256 (vecteurs d'initialisation),
/// MD5 (vecteurs d'initialisation).
[[nodiscard]] std::vector<ByteSignature> BuiltinSignatures();

/// Cherche `signatures` dans un tampon brut (décalages seuls, adresse = 0).
[[nodiscard]] std::vector<ByteSignatureMatch> SearchSignatures(const uint8_t *data, size_t size, const std::vector<ByteSignature> &signatures);

/// Cherche `signatures` dans les octets du binaire. Rend les occurrences
/// (adresse virtuelle résolue via les sections allouées).
[[nodiscard]] std::vector<ByteSignatureMatch> SearchSignatures(const Elf &elf, const std::vector<ByteSignature> &signatures);

} // namespace rea
