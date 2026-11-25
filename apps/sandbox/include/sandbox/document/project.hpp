#pragma once
/**
 * @file project.hpp
 * @brief Projet du sandbox : un dossier dont le fichier d'entrée `nom.project`
 *        (`"format": "sandbox.project"`) recense ses fichiers :
 *
 *   nom.project        index : nom, scène active, listes ci-dessous
 *   scenes/<s>.scene   scènes (`"format": "sandbox.scene"`, cf. Document)
 *   objects/<o>.object objets réutilisables (`"format": "sandbox.object"`) : un groupe avec sa
 *                      géométrie, son comportement et son script, à insérer dans une scène
 *   models/<m>.model   modèles éditables (`"format": "sandbox.model"`)
 *   scripts/<x>.script scripts (classes du jeu, modules importés par `import "nom"`)
 *
 * Les chemins sont relatifs au dossier du projet ; ceux d'une scène (scripts de ses groupes)
 * sont relatifs à la scène.
 */
#include <filesystem>
#include <optional>
#include <vector>

#include <core/core.hpp>

namespace sandbox {

struct ProjectScript {
	String name;		///< nom d'import (`import "monstres"`)
	String description;
	String file;		///< relatif au projet
};

class Project {
public:
	String name;
	String activeScene; ///< nom (sans extension) de la scène ouverte au chargement
	std::vector<String> scenes, objects, models;
	std::vector<ProjectScript> scripts;

	/// Lit `path` (un `.project`) ; erreur si le format n'est pas « sandbox.project ».
	[[nodiscard]] static Result<Project, String> Load(const std::filesystem::path &path);
	/// Réécrit l'index (`File()`).
	[[nodiscard]] Option<String> Save() const;
	/// Nouveau projet (dossier et index) ; rien n'est écrit avant `Save`.
	[[nodiscard]] static Project Create(const std::filesystem::path &file, String name);

	[[nodiscard]] const std::filesystem::path &File() const noexcept { return m_file; }
	[[nodiscard]] std::filesystem::path Folder() const { return m_file.parent_path(); }
	[[nodiscard]] std::filesystem::path Resolve(const String &relative) const { return Folder() / relative.c_str(); }
	/// Chemin relatif au projet d'un fichier de son dossier.
	[[nodiscard]] String Relative(const std::filesystem::path &file) const;
	/// Scène active (ou la première) ; vide si le projet n'en a pas.
	[[nodiscard]] std::optional<std::filesystem::path> ActiveScene() const;
	/// Fichier d'un module de script : par son nom dans `scripts`, sinon `scripts/nom.script`.
	[[nodiscard]] std::optional<std::filesystem::path> ScriptFile(std::string_view module) const;
	/// Ajoute un fichier à une liste s'il n'y est pas (chemin relatif au projet).
	static void AddUnique(std::vector<String> &list, const String &relative);

private:
	std::filesystem::path m_file;
};

} // namespace sandbox
