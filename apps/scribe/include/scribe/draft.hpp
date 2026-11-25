#pragma once
/**
 * @file draft.hpp
 * @brief Brouillon du dernier document modifié : copie enregistrée automatiquement (fichier
 *        `draft.docdb` du dossier des données) avec son nom et le chemin du document d'origine
 *        (`draft.ini`), pour reprendre un document non enregistré depuis l'accueil, y compris
 *        après la fermeture de l'application.
 */
#include <filesystem>
#include <optional>
#include <string>

#include <document/document.hpp>

namespace scribe {

struct DraftInfo {
	std::string name;			  ///< nom affiché (fichier d'origine, modèle ou « Sans titre »)
	std::filesystem::path source; ///< document d'origine ; vide s'il n'a jamais été enregistré
};

/// Un seul brouillon par dossier : chaque enregistrement remplace le précédent.
class DraftStore {
public:
	DraftStore() = default;
	explicit DraftStore(std::filesystem::path directory) : m_directory(std::move(directory)) {}

	[[nodiscard]] const std::filesystem::path &Directory() const noexcept { return m_directory; }
	[[nodiscard]] std::filesystem::path DocumentFile() const { return m_directory / "draft.docdb"; }
	[[nodiscard]] std::filesystem::path InfoFile() const { return m_directory / "draft.ini"; }
	/// Brouillon présent (son document existe) ; nom vide si `draft.ini` manque.
	[[nodiscard]] std::optional<DraftInfo> Load() const;
	/// Remplace le brouillon. Écrit dans des fichiers temporaires puis les renomme : un
	/// enregistrement interrompu laisse le brouillon précédent intact. Utilisable depuis un
	/// thread de fond tant qu'un seul enregistrement a lieu à la fois.
	doc::Status Save(const doc::DocumentView &document, const DraftInfo &info) const;
	/// Supprime le brouillon (sans effet s'il n'y en a pas).
	void Remove() const;

private:
	std::filesystem::path m_directory;
};

} // namespace scribe
