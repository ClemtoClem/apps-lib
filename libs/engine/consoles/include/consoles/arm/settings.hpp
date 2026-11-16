#pragma once
/**
 * @file settings.hpp
 * @brief Réglages d'une instance du cœur (aucun état global : chaque `Core` a les siens).
 */
#include <string>

namespace consoles::arm {

struct Settings {
	bool directBoot = true;	  ///< démarrer sur la cartouche sans passer par le micrologiciel
	bool highRes3D = false;	  ///< rendu 3D en double résolution
	bool arm7Hle = false;	  ///< EXPÉRIMENTAL : ARM7 arrêté, protocole IPC simulé
	int threaded2D = 0;		  ///< rendu 2D sur un fil dédié (0 : non)
	int threaded3D = 0;		  ///< fils de rendu 3D (0 à 3)
	int fpsLimiter = 0;		  ///< limiteur d'images (0 : non, 1 : souple, 2 : strict)
	std::string ndsBios9Path, ndsBios7Path, firmwarePath, gbaBiosPath, sdImagePath;
	/// Chemin des fichiers associés à la ROM, sans extension : `<base>.sav`, `<base>.cht`.
	std::string saveBasePath;
};

} // namespace consoles::arm
