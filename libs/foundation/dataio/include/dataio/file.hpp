#pragma once
/**
 * @file file.hpp
 * @brief Écriture / lecture de fichiers texte avec fractionnement optionnel.
 *
 * GitHub refuse les fichiers de plus de 100 Mo : un fichier volumineux est fractionné
 * en `nom.1.ext`, `nom.2.ext`… (ex. `parts_workshop.1.scene`, `parts_workshop.2.scene`).
 * La lecture détecte ces fragments, les réassemble dans l'ordre et les traite comme un
 * seul fichier en mémoire.
 */
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace dataio {

/// Réglages d'écriture d'un fichier.
struct FileOptions {
	bool split = false;				   ///< fractionner les fichiers volumineux
	std::size_t maxSize = 100u << 20;  ///< taille maximale avant fractionnement (octets, 100 Mo)
};

/// Chemin du fragment `n` (1, 2, …) d'un fichier : `nom.n.ext`.
[[nodiscard]] std::filesystem::path FragmentPath(const std::filesystem::path &path, std::size_t n);

/// Écrit `text` dans `path`. Si `options.split` et `text.size() > options.maxSize`, le
/// contenu est fractionné en `nom.1.ext`, `nom.2.ext`… ; les fragments (ou le fichier de
/// base) devenus inutiles sont supprimés. Rend un message d'erreur, ou rien en cas de succès.
[[nodiscard]] std::optional<std::string> WriteFile(const std::filesystem::path &path, std::string_view text,
												   const FileOptions &options = {});

/// Lit `path` ; si `nom.1.ext` existe, réassemble les fragments dans l'ordre. Rien si illisible.
[[nodiscard]] std::optional<std::string> ReadFile(const std::filesystem::path &path);

} // namespace dataio
