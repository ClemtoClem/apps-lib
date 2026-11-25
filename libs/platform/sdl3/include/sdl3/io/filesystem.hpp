#pragma once
/**
 * @file filesystem.hpp
 * @brief Accès direct au système de fichiers (SDL_filesystem.h) : chemins standard, énumération, glob.
 *
 * Contrairement à `Storage` (storage.hpp, sandboxée), ces fonctions accèdent
 * directement au disque local, sans restriction.
 */
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include <core/core.hpp>
#include "sdl3/io/storage.hpp" // PathInfo / PathType (partagés avec l'API Storage sandboxée)
#include <SDL3/SDL.h>

namespace sdl3 {

// ============================================================================
// Folder — dossiers utilisateur standard (Documents, Downloads, ...)
// ============================================================================

/// Dossiers utilisateur standard connus du système.
enum class Folder {
	HOME = SDL_FOLDER_HOME,						///< répertoire personnel
	DESKTOP = SDL_FOLDER_DESKTOP,				///< bureau
	DOCUMENTS = SDL_FOLDER_DOCUMENTS,			///< documents
	DOWNLOADS = SDL_FOLDER_DOWNLOADS,			///< téléchargements
	MUSIC = SDL_FOLDER_MUSIC,					///< musique
	PICTURES = SDL_FOLDER_PICTURES,				///< images
	PUBLIC_SHARE = SDL_FOLDER_PUBLICSHARE,		///< partage public
	SAVED_GAMES = SDL_FOLDER_SAVEDGAMES,		///< sauvegardes de jeu
	SCREENSHOTS = SDL_FOLDER_SCREENSHOTS,		///< captures d'écran
	TEMPLATES = SDL_FOLDER_TEMPLATES,			///< modèles
	VIDEOS = SDL_FOLDER_VIDEOS,					///< vidéos
};

// ============================================================================
// filesystem — accès direct au système de fichiers (sans sandbox, cf.
// `Storage` dans storage.hpp pour l'API sandboxée titre/utilisateur)
// ============================================================================

namespace filesystem {

/// Répertoire contenant l'exécutable (se termine par un séparateur de chemin).
[[nodiscard]] String BasePath();

/// Répertoire d'écriture recommandé pour « org/app » (créé si besoin par l'OS).
/// Retourne NONE si aucun répertoire approprié n'existe.
[[nodiscard]] Option<String> PrefPath(const String &org, const String &app);

/// Dossier de données utilisateur d'une application du projet « apps-lib » :
/// « ~/.local/share/apps-lib/<app>/ » (XDG, via PrefPath), sinon « data/<app>/ »
/// en dernier recours (aucun répertoire personnel inscriptible).
[[nodiscard]] String AppDataDir(StringView app);

/// Chemin absolu d'un dossier utilisateur standard.
[[nodiscard]] inline const char *UserFolder(Folder f) noexcept { return SDL_GetUserFolder(SDL_Folder(f)); }

/// Répertoire de travail courant du processus.
[[nodiscard]] String CurrentDirectory();

/// Crée un répertoire (parents compris). Vrai si créé ou déjà existant.
inline bool CreateDirectory(const String &path) { return SDL_CreateDirectory(path.c_str()); }

/// Énumère les entrées d'un dossier : appelle `fn(dirname, filename)` pour chacune.
/// Retourner `false` depuis `fn` arrête l'énumération anticipativement.
bool EnumerateDirectory(const String &path, std::function<bool(const char *, const char *)> fn);

/// Supprime un fichier ou un dossier vide.
inline bool Remove(const String &path) { return SDL_RemovePath(path.c_str()); }

/// Renomme / déplace un fichier ou dossier.
bool Rename(const String &oldPath, const String &newPath);

/// Copie un fichier.
bool CopyFile(const String &oldPath, const String &newPath);

/// Métadonnées (type, taille, dates) d'un chemin ; NONE s'il n'existe pas.
[[nodiscard]] Option<sdl3::PathInfo> PathInfo(const String &path);

/// Énumère les chemins correspondant à un motif glob (ex: "*.png").
/// `path` est le dossier de base, `pattern` le motif ; `caseInsensitive` ignore la casse.
[[nodiscard]] std::vector<String> Glob(const String &path, const String &pattern, bool caseInsensitive = false);

} // namespace filesystem

} // namespace sdl3
