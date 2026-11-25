#pragma once
/**
 * @file i18n.hpp
 * @brief Langues de l'interface de Scribe : traducteur global de `graphics.ui` (`ui::I18n`),
 *        clés = textes français des sources (repli sur le français si une traduction manque),
 *        fichiers `apps/scribe/i18n/<code>.json`, langue du système par défaut.
 *
 *   Tr("Gras (Ctrl+B)")                       // « Bold (Ctrl+B) » en anglais
 *   TrF("Insérer {} ligne(s) au-dessus", n)   // les traductions gardent les mêmes {}
 */
#include <filesystem>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace scribe {

struct LanguageInfo {
	std::string_view code; ///< « fr », « en », « de », « it », « zh », « ja »
	std::string_view name; ///< nom dans la langue elle-même
};

/// Langues proposées (le français est la langue des sources).
[[nodiscard]] std::span<const LanguageInfo> Languages() noexcept;
/// Indice de la langue `code` (0, le français, si inconnue).
[[nodiscard]] size_t LanguageIndex(std::string_view code) noexcept;
/// Langue proposée la plus proche des langues préférées du système (codes ISO « fr », « zh »…,
/// éventuellement suivis d'un pays) ; anglais si aucune ne correspond.
[[nodiscard]] std::string_view PickLanguage(std::span<const std::string> preferred) noexcept;
/// Langues préférées du système (wrapper sdl3).
[[nodiscard]] std::vector<std::string> SystemLanguages();

/// Charge la langue `code` (français : table vidée) depuis `directory` ; faux si le fichier manque
/// ou est invalide (la langue précédente reste).
bool LoadLanguage(std::string_view code, const std::filesystem::path &directory = "apps/scribe/i18n");
/// Langue chargée (« fr » par défaut).
[[nodiscard]] std::string_view CurrentLanguage() noexcept;

/// Texte traduit (repli : le texte français).
[[nodiscard]] std::string Tr(std::string_view french);
/// Mêmes accolades (« {} », « {:g} »…, dans le même ordre) : une traduction formatable comme
/// son texte source (le projet est compilé sans exceptions : un motif faux arrêterait tout).
[[nodiscard]] bool SamePlaceholders(std::string_view french, std::string_view translated);

/// Texte traduit puis formaté (même nombre de « {} » dans chaque traduction : vérifié par les
/// tests ; une traduction incompatible retombe sur le texte français).
template <typename... Args> [[nodiscard]] std::string TrF(std::format_string<Args...> french, Args &&...args) {
	std::string pattern = Tr(french.get());
	if (!SamePlaceholders(french.get(), pattern))
		pattern = french.get();
	std::string out;
	std::vformat_to(std::back_inserter(out), pattern, std::make_format_args(args...));
	return out;
}

/// Motif choisi à l'exécution avec un seul « {} » : traduit puis complété par `argument`.
[[nodiscard]] std::string TrArg(std::string_view french, std::string_view argument);

/// Traductions d'une liste de libellés (vues valides tant que l'objet vit).
struct TrList {
	explicit TrList(std::span<const std::string_view> french);
	std::vector<std::string> storage;
	std::vector<std::string_view> views;
};

} // namespace scribe
