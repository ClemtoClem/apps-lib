// Définitions de ui/i18n.hpp — traducteur global, chargement des fichiers JSON.
#include "ui/i18n.hpp"

#include <fstream>
#include <sstream>

#include <dataio/json.hpp>

namespace ui {

Result<size_t, std::string> Translator::LoadJson(std::string_view json) {
	auto parsed = dataio::ParseJson(json);
	if (parsed.IsError())
		return Err(parsed.Error().Format());
	const dataio::Json &root = parsed.Value();
	if (!root.IsObject())
		return Err(std::string("traduction : un objet JSON est attendu"));
	const dataio::Json *strings = root.Find("strings");
	const dataio::Json &table = strings ? *strings : root;
	if (!table.IsObject())
		return Err(std::string("traduction : « strings » doit être un objet"));
	std::unordered_map<std::string, std::string> loaded;
	for (const auto &[key, value] : table.AsObject())
		if (value.IsString())
			loaded[key] = std::string(value.AsString());
	m_table = std::move(loaded);
	const dataio::Json *locale = root.Find("locale");
	const dataio::Json *language = root.Find("language");
	m_locale = locale && locale->IsString() ? std::string(locale->AsString()) : std::string();
	m_language = language && language->IsString() ? std::string(language->AsString()) : m_locale;
	++m_version;
	return Ok(m_table.size());
}

Result<size_t, std::string> Translator::LoadFile(const std::filesystem::path &path) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return Err("traduction introuvable : " + path.string());
	std::ostringstream text;
	text << in.rdbuf();
	auto result = LoadJson(text.str());
	if (result.IsError())
		return Err(path.string() + " : " + result.Error());
	return result;
}

Translator &I18n() noexcept {
	static Translator translator;
	return translator;
}

std::string_view Tr(std::string_view key) { return I18n().Tr(key); }

} // namespace ui
