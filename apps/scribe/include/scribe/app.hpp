#pragma once
#include <filesystem>
#include <string>
/**
 * @file app.hpp
 * @brief Application graphique Scribe (graphics.app + graphics.ui) au-dessus de `Editor`.
 */
#include <string>

#include <app/application.hpp>

namespace scribe {

struct LaunchOptions {
	std::string path;		 ///< document à ouvrir
	std::string templateName; ///< nouveau document depuis ce modèle (`--template=`) : identifiant d'un modèle intégré (« demo »…) ou nom d'un modèle personnel
	size_t page = 0;   ///< page affichée au démarrage (1…) ; 0 : début
	std::string language, theme; ///< langue / thème de cette session (non enregistrés) ; vide : réglages
};

/// Exporte le document (modèle intégré ou fichier) sans interface, selon l'extension de `output`
/// (.pdf, .html, .csv : premier tableau), polices installées comprises ; rend le code de sortie.
int ExportWithoutInterface(const LaunchOptions &options, const std::filesystem::path &output);
/// Lance l'interface ; rend le code de sortie.
int RunScribe(app::AppConfig config, const LaunchOptions &options);

} // namespace scribe
