#pragma once
/**
 * @file storage.hpp
 * @brief Stockage de fichiers abstrait et sandboxé (SDL_storage.h) : titre, utilisateur, disque.
 */
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

#include <core/core.hpp>
#include "sdl3/core/error.hpp"

namespace sdl3 {

// ============================================================================
// PathInfo / PathType
// ============================================================================

/// Nature d'un chemin de stockage.
enum class PathType: Uint8 {
	NONE	  = SDL_PATHTYPE_NONE,		///< n'existe pas
	FILE	  = SDL_PATHTYPE_FILE,		///< fichier
	DIRECTORY = SDL_PATHTYPE_DIRECTORY, ///< répertoire
	OTHER	  = SDL_PATHTYPE_OTHER,		///< autre (lien, périphérique…)
};
namespace detail {
	constexpr SDL_PathType ToSDL(PathType e) {
		return static_cast<SDL_PathType>(e);
	}
}

/// Métadonnées d'un chemin : type, taille, dates.
struct PathInfo {
	PathType type = PathType::NONE; ///< nature du chemin
	uint64_t size = 0;				///< taille en octets (fichiers)
	Sint64 createTime = 0;			///< date de création (ns, époque)
	Sint64 modifyTime = 0;			///< date de dernière modification (ns)
	Sint64 accessTime = 0;			///< date de dernier accès (ns)

	PathInfo() = default;
	/// Construit depuis un `SDL_PathInfo` brut.
	explicit PathInfo(const SDL_PathInfo &info) noexcept
		: type(PathType(info.type)), size(info.size), createTime(info.create_time), modifyTime(info.modify_time),
		  accessTime(info.access_time) {}
};

// ============================================================================
// Storage — RAII SDL_Storage (accès fichiers abstrait : titre, user, disque)
// ============================================================================

/// Stockage de fichiers abstrait (RAII sur SDL_Storage).
class Storage : public Wrapper<SDL_Storage, SDL_CloseStorage> {
public:
	using Wrapper::Wrapper;

	/// Stockage en lecture seule des assets embarqués avec l'application.
	/// `overridePath` remplace le répertoire de base (tests/outils) ; `props` peut paramétrer l'ouverture.
	[[nodiscard]] static Result<Storage, StringView> OpenTitle(const String &overridePath = "",
															   SDL_PropertiesID props = 0);

	/// Stockage persistant (sauvegardes, config) propre à l'utilisateur, isolé par org/app.
	[[nodiscard]] static Result<Storage, StringView> OpenUser(const String &org, const String &app,
															  SDL_PropertiesID props = 0);

	/// Accès direct à un chemin du disque local (pas de sandboxing).
	[[nodiscard]] static Result<Storage, StringView> OpenFile(const String &path);

	/// Backend de stockage PERSONNALISÉ (SDL_StorageInterface) : `iface` et
	/// `userdata` doivent rester valides tant que le Storage est ouvert.
	[[nodiscard]] static Result<Storage, StringView> OpenCustom(const SDL_StorageInterface *iface, void *userdata);

	/// Vrai si le stockage est prêt. Le stockage peut être asynchrone (ex: montage réseau).
	[[nodiscard]] bool Ready() const noexcept { return mHandle && SDL_StorageReady(mHandle); }

	/// Taille d'un fichier en octets ; erreur s'il n'existe pas.
	[[nodiscard]] Result<uint64_t, StringView> FileSize(const String &path) const;

	/// Lit un fichier entier en mémoire.
	[[nodiscard]] Result<std::vector<uint8_t>, StringView> ReadFile(const String &path) const;

	/// Écrit un tampon brut dans un fichier (écrase le contenu).
	[[nodiscard]] bool WriteFile(const String &path, const void *data, size_t len);
	/// Écrit un vecteur d'octets dans un fichier.
	[[nodiscard]] bool WriteFile(const String &path, const std::vector<uint8_t> &data);

	/// Crée un répertoire (parents compris).
	[[nodiscard]] bool CreateDirectory(const String &path);

	/// Énumère les entrées d'un dossier : appelle `fn(dirname, filename)` pour chacune.
	/// Retourner `false` depuis `fn` arrête l'énumération anticipativement.
	[[nodiscard]] bool EnumerateDirectory(const String &path,
										  std::function<bool(const char *, const char *)> fn) const;

	/// Supprime un fichier ou un dossier vide.
	bool Remove(const String &path) { return mHandle && SDL_RemoveStoragePath(mHandle, path.c_str()); }

	/// Renomme / déplace un fichier ou dossier.
	bool Rename(const String &oldPath, const String &newPath);
	/// Copie un fichier.
	bool Copy(const String &oldPath, const String &newPath);

	/// Métadonnées d'un chemin ; NONE s'il n'existe pas.
	[[nodiscard]] Option<sdl3::PathInfo> PathInfo(const String &path) const;

	/// Espace libre restant (octets) sur le stockage.
	[[nodiscard]] uint64_t SpaceRemaining() const noexcept;

	/// Énumère les chemins correspondant à un motif glob (ex: "*.png").
	/// `path` peut être vide pour la racine du storage.
	[[nodiscard]] std::vector<String> Glob(const String &path, const String &pattern,
										   bool caseInsensitive = false) const;
};

} // namespace sdl3
