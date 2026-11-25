#pragma once
/**
 * @file i18n.hpp
 * @brief Internationalisation : traducteur de chaînes (table clé → traduction), chargé
 *        depuis des fichiers JSON et appliqué automatiquement à l'interface retenue.
 *
 * Fichier de traduction (une langue par fichier) :
 * @code{.json}
 * {
 *   "locale": "en",
 *   "language": "English",
 *   "strings": {
 *     "title": "Complete catalogue",          // texte du widget d'Id « title »
 *     "name.placeholder": "Your name…",       // texte grisé d'un champ
 *     "save.tooltip": "Save the document",    // infobulle
 *     "tabs.0": "Controls",                   // onglet / option / menu n° 0
 *     "menu.0.2": "Quit"                      // item n° 2 du menu n° 0
 *   }
 * }
 * @endcode
 * Les clés sont les identifiants des widgets (`.Id("title")`) : `ui::Ui::SetTranslator`
 * remplace leur texte, et le rétablit quand une clé disparaît (retour à la langue des
 * sources). Le code peut aussi traduire lui-même avec `Tr("clé")` (repli sur la clé).
 */
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

#include <core/result.hpp>

namespace ui {

/// Traducteur de chaînes (I18n) : table clé → traduction, avec repli sur la clé.
class Translator {
public:
	/// Ajoute (ou remplace) une traduction.
	void Add(std::string key, std::string translation) {
		m_table[std::move(key)] = std::move(translation);
		++m_version;
	}

	/// Traduction de `key`, ou `key` elle-même si absente.
	[[nodiscard]] std::string_view Tr(std::string_view key) const {
		const std::string *found = Find(key);
		return found ? std::string_view(*found) : key;
	}
	/// Traduction de `key`, ou nullptr si absente.
	[[nodiscard]] const std::string *Find(std::string_view key) const {
		const auto it = m_table.find(std::string(key));
		return it == m_table.end() ? nullptr : &it->second;
	}

	[[nodiscard]] bool Contains(std::string_view key) const { return Find(key) != nullptr; }
	[[nodiscard]] size_t Size() const noexcept { return m_table.size(); }
	void Clear() noexcept {
		m_table.clear();
		m_locale.clear();
		m_language.clear();
		++m_version;
	}

	/// Remplace la table par le contenu d'un document JSON (voir l'en-tête du fichier) :
	/// objet `strings` (sinon l'objet racine entier), `locale` et `language` facultatifs.
	/// Rend le nombre de chaînes chargées ; la table est inchangée en cas d'erreur.
	Result<size_t, std::string> LoadJson(std::string_view json);
	/// Comme LoadJson, depuis un fichier.
	Result<size_t, std::string> LoadFile(const std::filesystem::path &path);

	/// Code de la langue chargée (« fr », « en »…), vide si inconnu.
	[[nodiscard]] const std::string &Locale() const noexcept { return m_locale; }
	/// Nom de la langue, dans cette langue (« Français », « English »…).
	[[nodiscard]] const std::string &Language() const noexcept { return m_language; }
	/// Augmente à chaque modification : l'interface sait quand réappliquer les textes.
	[[nodiscard]] uint64_t Version() const noexcept { return m_version; }

private:
	std::unordered_map<std::string, std::string> m_table;
	std::string m_locale, m_language;
	uint64_t m_version = 1;
};

/// Traducteur global (partagé par les applications).
[[nodiscard]] Translator &I18n() noexcept;

/// Traduction globale de `key` (repli sur la clé).
[[nodiscard]] std::string_view Tr(std::string_view key);

} // namespace ui
