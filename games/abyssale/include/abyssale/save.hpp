#pragma once
/**
 * @file save.hpp
 * @brief Métadonnées d'une partie (graine, position du plongeur, qualité) persistées en
 *        JSON (`foundation.dataio`) dans `worlds/<nom>/world.json`, et énumération des
 *        sauvegardes.
 */
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <math/math.hpp>

namespace abyssale {

/// Métadonnées d'une partie sauvegardée (le décor et la faune sont régénérés depuis la
/// graine ; la faune mobile est, elle, sérialisée par chunk dans `foundation.sql`).
struct WorldMeta {
	std::string name = "Nouvelle plongée";
	uint64_t seed = 0;
	int quality = 0;
	math::FVector3 position{0.f, -30.f, 0.f};
	float yaw = 0.f, pitch = 0.25f;
	std::string created, lastPlayed;
	double playSeconds = 0;

	static std::optional<WorldMeta> Load(const std::filesystem::path &worldDir);
	bool Save(const std::filesystem::path &worldDir) const;
};

/// Liste des parties (nom de dossier → métadonnées), les plus récentes d'abord.
[[nodiscard]] std::vector<std::pair<std::string, WorldMeta>> ListWorlds(const std::filesystem::path &worldsDir);
/// Supprime le dossier d'une partie.
bool DeleteWorld(const std::filesystem::path &worldsDir, const std::string &folder);

/// Nom de dossier sûr dérivé d'un nom de partie.
[[nodiscard]] std::string WorldFolderName(std::string_view name);
/// Date/heure locale « jj/mm/aaaa hh:mm ».
[[nodiscard]] std::string NowText();

} // namespace abyssale
