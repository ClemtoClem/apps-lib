#pragma once
/**
 * @file project.hpp
 * @brief Projet REA : un binaire analysé, l'architecture choisie et les
 *        annotations de l'utilisateur, sérialisés en JSON (réouvert plus tard).
 *
 * Le modèle s'inspire du projet Ghidra : un projet référence un programme
 * (binaire) et conserve le travail d'analyse de l'utilisateur (commentaires,
 * renommages, signets). L'analyse elle-même est recalculée à l'ouverture.
 */
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <core/core.hpp>

#include "arch.hpp"

namespace rea {

/// Annotation utilisateur sur une adresse : commentaire et/ou renommage.
struct Annotation {
	uint64_t address = 0;	///< adresse annotée
	std::string comment;	///< commentaire (vide si aucun)
	std::string name;		///< renommage (vide si aucun)
};

/// Un projet REA (équivalent du « program » Ghidra + ses annotations).
struct Project {
	std::string binaryPath;			   ///< chemin du binaire analysé
	Arch arch = Arch::X86_64;		   ///< architecture choisie
	std::vector<Annotation> annotations; ///< travail de l'utilisateur

	/// Charge un projet depuis un fichier JSON.
	static Result<Project, String> Load(const std::filesystem::path &path);
	/// Enregistre le projet dans `path` (JSON indenté). Rend faux en cas d'échec.
	bool Save(const std::filesystem::path &path) const;

	/// Annotation à `address` (ou `nullptr`).
	[[nodiscard]] const Annotation *Find(uint64_t address) const noexcept;
	/// Pose/remplace un commentaire à `address`.
	void Comment(uint64_t address, std::string text);
	/// Pose/remplace un renommage à `address`.
	void Rename(uint64_t address, std::string name);
};

} // namespace rea
