#pragma once
/**
 * @file mods.hpp
 * @brief Découverte et chargement du contenu des mods.
 *
 * Un mod est un dossier contenant `mod.json` (identifiant, nom, version) et,
 * facultativement : `blocks.json` ou `blocks/` (définitions de blocs), `config.json`
 * (valeurs fusionnées : un mod chargé plus tard remplace une clé de premier
 * niveau), `textures/assets.json` (alias de textures), `init.script` puis
 * `scripts/` (scripts, dans l'ordre des noms).
 *
 * La configuration fusionnée et la liste des mods sont reportées dans la base du
 * monde (tables `config` et `mods`) : elles s'interrogent ensuite en SQL.
 */
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <core/robin_map.hpp>
#include <dataio/json.hpp>

#include "blocks.hpp"

namespace voxel {

class WorldStorage;

/// Paramètre déclaré par un mod dans `mod.json` (`settings`).
struct ModSetting {
	std::string id;
	std::string type; // int, float, bool, string
	std::string label, description, group, unit;
	dataio::NodePtr defaultValue;
	double min = 0, max = 1;
	bool advanced = false;
};

struct ModInfo {
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::filesystem::path directory;
	bool enabled = true;
	int priority = 0; // ordre de chargement
	std::vector<std::filesystem::path> scripts;
	size_t blockCount = 0;
	std::string author, category;
	std::vector<std::string> dependencies, tags;
	std::vector<ModSetting> settings;
	bool hasConfig = false; // config.json présent
};

struct TextureSource {
	std::string id;					// « core:stone »
	std::filesystem::path path;		// image principale
	std::string compose;			// expression de composition (`a.png^(b.png^tint(r,g,b))`)
	std::filesystem::path baseDir;	// dossier du mod (chemins relatifs de `compose`)
	std::array<uint8_t, 4> color{}; // couleur procédurale
	bool procedural = false;
	std::string biomeTint;			// « grass », « foliage »…
	float reflectivity = 0.f;		// 0 mat, 1 miroir (métaux, verre, glace)
};

/// Structure préfabriquée : blocs définis par des couches ASCII (world/structures.json).
struct Structure {
	std::string id;			// « core:house » (qualifié)
	int anchor[3] = {0, 0, 0}; // décalage de l'origine dans la structure
	bool replaceSolid = false;
	std::map<char, std::string> palette;	  // caractère → nom de bloc qualifié
	std::map<char, uint8_t> orient;		  // caractère → orientation (0 = identité)
	std::vector<std::vector<std::string>> layers; // couches (haut → bas) → lignes (z) → colonnes (x)
};

class ModManager {
public:
	/// Parcourt `modsDir` ; les mods de `disabled` sont listés mais désactivés.
	size_t Discover(const std::filesystem::path &modsDir, const std::set<std::string> &disabled = {});
	[[nodiscard]] const std::vector<ModInfo> &Mods() const noexcept { return m_mods; }
	[[nodiscard]] std::vector<ModInfo> &Mods() noexcept { return m_mods; }

	/// Charge blocs et configuration des mods activés. Retourne le nombre de blocs.
	size_t LoadContent(BlockRegistry &blocks);
	[[nodiscard]] const dataio::NodePtr &Config() const noexcept { return m_config; }

	/// Résout un nom de texture qualifié vers sa source (alias, puis `textures/blocks/<nom>.png`…).
	[[nodiscard]] std::optional<TextureSource> ResolveTexture(std::string_view qualified) const;

	/// Structures préfabriquées des mods (`world/structures.json`).
	[[nodiscard]] const std::vector<Structure> &Structures() const noexcept { return m_structures; }
	[[nodiscard]] const Structure *FindStructure(std::string_view id) const noexcept;

	/// Reporte la liste des mods et la configuration aplatie (`player.speed`) dans la base.
	void SyncToDatabase(WorldStorage &storage) const;

	[[nodiscard]] const ModInfo *Find(std::string_view id) const noexcept;

private:
	std::vector<ModInfo> m_mods;
	dataio::NodePtr m_config = dataio::Node::MakeObject();
	core::RobinMap<std::string, TextureSource, core::Hasher<std::string>> m_textureAliases;
	std::vector<Structure> m_structures;
};

} // namespace voxel
