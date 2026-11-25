#pragma once
/**
 * @file settings.hpp
 * @brief Réglages de Scribe conservés d'une session à l'autre : langue et thème de l'interface,
 *        documents récents, grille d'aimantation. Fichier texte « clé=valeur » dans le dossier de données de
 *        l'application (`sdl3::filesystem::AppDataDir("scribe")`).
 */
#include <filesystem>
#include <string>
#include <vector>

namespace scribe {

struct Settings {
	std::string language;			   ///< vide : langue du système
	std::string theme = "night";
	std::vector<std::string> recent;   ///< chemins, le plus récent d'abord
	static constexpr size_t kMaxRecent = 12;
	bool grid = false;				   ///< grille affichée (formes aimantées)
	double gridStep = 72.0 / 2.54 / 2; ///< pas de la grille (points) : 0,5 cm
	static constexpr double kMinGridStep = 72.0 / 25.4; ///< 1 mm

	/// Ajoute (ou remonte) un document récent.
	void AddRecent(const std::filesystem::path &path);
	void RemoveRecent(const std::filesystem::path &path);

	/// Lecture ; fichier absent ou illisible : réglages par défaut (lignes inconnues ignorées).
	[[nodiscard]] static Settings Load(const std::filesystem::path &file);
	/// Écriture atomique (fichier temporaire puis renommage) ; faux en cas d'échec.
	bool Save(const std::filesystem::path &file) const;
};

/// Dossier des données de Scribe (réglages, modèles personnels).
[[nodiscard]] std::filesystem::path DataDirectory();

} // namespace scribe
