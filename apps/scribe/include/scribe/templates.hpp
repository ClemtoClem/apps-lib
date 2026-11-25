#pragma once
/**
 * @file templates.hpp
 * @brief Modèles de documents : modèles intégrés (construits avec les actions de l'éditeur :
 *        lettre, compte rendu, rapport, CV, démonstration…) et modèles personnels, fichiers
 *        `.docdb` d'un dossier (créer à partir du document ouvert, renommer, supprimer).
 */
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "scribe/editor.hpp"

namespace scribe {

struct BuiltinTemplate {
	std::string_view key;  ///< identifiant stable
	std::string_view name; ///< nom affiché (langue des sources, traduit par l'interface)
	void (*build)(Editor &editor);
};

/// Modèles intégrés, le document vierge en premier.
[[nodiscard]] std::span<const BuiltinTemplate> BuiltinTemplates() noexcept;
/// Modèle intégré d'identifiant `key` (« demo », « letter »…), nullptr s'il n'existe pas.
[[nodiscard]] const BuiltinTemplate *FindBuiltinTemplate(std::string_view key) noexcept;
/// Identifiants des modèles intégrés : « blank, letter, … » (messages d'aide).
[[nodiscard]] std::string BuiltinTemplateKeys();

/// Modèles personnels : un fichier `<nom>.docdb` par modèle dans `directory`.
class TemplateStore {
public:
	explicit TemplateStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}

	[[nodiscard]] const std::filesystem::path &Directory() const noexcept { return m_directory; }
	/// Noms des modèles, triés.
	[[nodiscard]] std::vector<std::string> List() const;
	[[nodiscard]] std::filesystem::path PathOf(std::string_view name) const;
	[[nodiscard]] bool Contains(std::string_view name) const;
	/// Enregistre le document comme modèle (refusé si le nom est invalide ou déjà pris).
	doc::Status Create(std::string_view name, const doc::DocumentView &document);
	doc::Status Rename(std::string_view from, std::string_view to);
	doc::Status Remove(std::string_view name);
	/// Nom utilisable comme modèle : non vide, 80 caractères au plus, sans / \ : * ? " < > |,
	/// ni espace en tête ou en fin, ni point en tête.
	[[nodiscard]] static bool ValidName(std::string_view name) noexcept;

private:
	std::filesystem::path m_directory;
};

} // namespace scribe
