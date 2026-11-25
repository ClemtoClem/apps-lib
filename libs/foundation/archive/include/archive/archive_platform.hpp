#pragma once
/**
 * archive — opérations de système de fichiers absentes de SDL3.
 *
 * SDL3 ne sait ni créer ni lire un lien symbolique (`SDL_GetPathInfo` les
 * suit) ; ces quelques fonctions passent par `std::filesystem`, toujours dans
 * ses surcharges à `std::error_code` : aucune exception n'est levée (cf.
 * memory/feedback_no_exceptions.md). Les chemins sont convertis en
 * `std::u8string` pour que l'UTF-8 survive aussi sous Windows.
 *
 * Sous Windows, créer un lien exige le mode développeur ou des droits
 * d'administrateur : l'échec est alors signalé, pas masqué.
 */
#include <core/core.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>

namespace archive::platform {

namespace detail {

[[nodiscard]] std::filesystem::path ToPath(const String& path);

[[nodiscard]] String FromPath(const std::filesystem::path& path);

} // namespace detail

/// Vrai si `path` est lui-même un lien symbolique (sans le suivre).
[[nodiscard]] bool IsSymlink(const String& path);

/// Cible d'un lien symbolique, telle qu'écrite dans le lien.
[[nodiscard]] Option<String> ReadSymlink(const String& path);

/// Crée `linkPath` → `target`. `directoryHint` : la cible est un dossier
/// (seul Windows distingue les deux sortes de liens).
[[nodiscard]] Result<bool, String>
CreateSymlink(const String& target, const String& linkPath, bool directoryHint = false);

enum class PathType : uint8_t { NONE, FILE, DIRECTORY, OTHER };

/// Informations d'un chemin (liens suivis) ; `modifyTime` en nanosecondes depuis 1970.
struct PathInfo {
	PathType type = PathType::NONE;
	uint64_t size = 0;
	int64_t modifyTime = 0;
};

[[nodiscard]] Option<PathInfo> GetPathInfo(const String &path);
/// Supprime un fichier ou un dossier vide.
bool Remove(const String &path);
bool Rename(const String &from, const String &to);
/// Crée le dossier et ses parents ; vrai s'il existe déjà.
bool CreateDirectory(const String &path);
/// Appelle `visit(dossier, nom)` pour chaque entrée (sans ordre garanti) ; s'arrête si elle rend faux.
bool EnumerateDirectory(const String &path, const std::function<bool(const char *, const char *)> &visit);
/// Heure courante en nanosecondes depuis 1970.
[[nodiscard]] int64_t CurrentTimeNs();

} // namespace archive::platform
