#pragma once
/**
 * @file system.hpp
 * @brief Registre des systèmes émulés (la liste `GAME()`/`CONS()` de MAME) : chaque pilote
 *        déclare son nom, ses extensions de fichiers et la fabrique de sa machine.
 *
 * @code
 * emu::SystemRegistry registry;
 * consoles::RegisterAll(registry);
 * if (const emu::SystemInfo *system = registry.FindByExtension(".gbc")) {
 *     emu::Media media;
 *     media.rom = LireFichier(path);
 *     media.romPath = path;
 *     auto machine = system->create(media);       // Result<unique_ptr<Machine>, string>
 * }
 * @endcode
 */
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <core/result.hpp>

#include "machine.hpp"

namespace emu {

/// Supports fournis à la fabrique d'une machine.
struct Media {
	std::vector<uint8_t> rom;				 ///< image de la cartouche
	std::filesystem::path romPath;			 ///< chemin d'origine (nom des sauvegardes)
	std::filesystem::path saveDirectory;	 ///< dossier des sauvegardes sur pile (vide : à côté de la ROM)
	std::filesystem::path biosDirectory;	 ///< dossier des BIOS / micrologiciels
	std::map<std::string, std::string, std::less<>> options; ///< options propres au pilote

	/// Fichier de sauvegarde sur pile : `<dossier>/<nom de la ROM><extension>`.
	[[nodiscard]] std::filesystem::path SavePath(std::string_view extension = ".sav") const;
	[[nodiscard]] std::string_view Option(std::string_view key, std::string_view fallback = {}) const;
};

using MachineResult = Result<std::unique_ptr<Machine>, std::string>;

struct SystemInfo {
	std::string name;		 ///< identifiant court : « gbc »
	std::string description; ///< « Nintendo Game Boy Color »
	std::string manufacturer;
	int year = 0;
	std::vector<std::string> extensions; ///< « .gb », « .gbc » (minuscules, avec le point)
	std::function<MachineResult(const Media &)> create;
	/// Vrai si la fabrique lit elle-même la ROM depuis `Media::romPath` (archives comprises) :
	/// l'appelant peut alors laisser `Media::rom` vide et éviter une copie de l'image.
	bool readsRomFromPath = false;
};

/// Image de ROM chargée par le registre, avec le système qui la prend en charge.
struct LoadedRom {
	const SystemInfo *system = nullptr;
	std::vector<uint8_t> data;
	std::string name; ///< nom du fichier (ou de l'entrée de l'archive) d'origine
};

class SystemRegistry {
public:
	/// Charge une ROM : fichier simple ou archive (ZIP, 7z, RAR, TAR… : première entrée dont
	/// l'extension est prise en charge). `system` vide : système déduit de l'extension.
	[[nodiscard]] Result<LoadedRom, std::string> LoadRom(const std::filesystem::path &path, std::string_view system = {}) const;
	/// Système d'un fichier de ROM (extension, ou extension de l'entrée retenue d'une archive)
	/// sans extraire son contenu.
	[[nodiscard]] Result<const SystemInfo *, std::string> Identify(const std::filesystem::path &path, std::string_view system = {}) const;
	/// Vrai si le fichier peut contenir une ROM d'un système connu (extension ou archive).
	[[nodiscard]] bool IsRomFile(const std::filesystem::path &path) const;
	/// Faux si un système de même nom existe déjà.
	bool Register(SystemInfo info);
	[[nodiscard]] const SystemInfo *Find(std::string_view name) const noexcept;
	/// Extension avec ou sans point, casse indifférente.
	[[nodiscard]] const SystemInfo *FindByExtension(std::string_view extension) const noexcept;
	[[nodiscard]] const std::vector<SystemInfo> &All() const noexcept { return m_systems; }

private:
	std::vector<SystemInfo> m_systems;
};

/// Lit une ROM (fichier simple ou première entrée d'une archive — ZIP, 7z, RAR… — dont
/// l'extension figure dans `extensions`). Vide en cas d'échec.
[[nodiscard]] std::vector<uint8_t> ReadRomImage(const std::filesystem::path &path, std::span<const std::string> extensions);

/// Lit un fichier entier ; vide s'il est illisible.
[[nodiscard]] std::vector<uint8_t> ReadFile(const std::filesystem::path &path);
[[nodiscard]] bool WriteFile(const std::filesystem::path &path, std::span<const uint8_t> data);

} // namespace emu
