#include "scribe/i18n.hpp"

#include <algorithm>

#include <sdl3/core/locale.hpp>
#include <ui/i18n.hpp>

namespace scribe {

namespace {

constexpr LanguageInfo kLanguages[] = {{"fr", "Français"}, {"en", "English"},	{"de", "Deutsch"},
									   {"it", "Italiano"}, {"zh", "简体中文"}, {"ja", "日本語"}};

std::string &Current() {
	static std::string code = "fr";
	return code;
}

/// Accolades du motif, dans l'ordre (« {} », « {:g} »…).
std::vector<std::string_view> Fields(std::string_view s) {
	std::vector<std::string_view> out;
	for (size_t i = 0; i < s.size(); ++i)
		if (s[i] == '{') {
			const size_t end = s.find('}', i);
			if (end == std::string_view::npos)
				return {"<invalide>"};
			out.push_back(s.substr(i, end - i + 1));
			i = end;
		} else if (s[i] == '}') {
			return {"<invalide>"};
		}
	return out;
}

} // namespace

std::span<const LanguageInfo> Languages() noexcept { return kLanguages; }

size_t LanguageIndex(std::string_view code) noexcept {
	for (size_t i = 0; i < std::size(kLanguages); ++i)
		if (kLanguages[i].code == code)
			return i;
	return 0;
}

std::string_view PickLanguage(std::span<const std::string> preferred) noexcept {
	for (const std::string &p : preferred) {
		const std::string_view language = std::string_view(p).substr(0, std::min(p.find_first_of("-_"), p.size()));
		for (const LanguageInfo &l : kLanguages)
			if (language == l.code)
				return l.code;
	}
	return "en";
}

std::vector<std::string> SystemLanguages() {
	std::vector<std::string> out;
	for (const sdl3::Locale &l : sdl3::locale::Preferred())
		out.emplace_back(l.language.CStr());
	return out;
}

bool LoadLanguage(std::string_view code, const std::filesystem::path &directory) {
	if (code == "fr") { // langue des sources
		ui::I18n().Clear();
		Current() = "fr";
		return true;
	}
	if (LanguageIndex(code) == 0)
		return false;
	if (auto loaded = ui::I18n().LoadFile(directory / (std::string(code) + ".json")); loaded.IsError())
		return false;
	Current() = std::string(code);
	return true;
}

std::string_view CurrentLanguage() noexcept { return Current(); }

std::string Tr(std::string_view french) {
	const std::string *found = ui::I18n().Find(french);
	return found ? *found : std::string(french);
}

bool SamePlaceholders(std::string_view french, std::string_view translated) { return Fields(french) == Fields(translated); }

std::string TrArg(std::string_view french, std::string_view argument) {
	std::string pattern = Tr(french);
	if (!SamePlaceholders(french, pattern))
		pattern = std::string(french);
	if (const size_t at = pattern.find("{}"); at != std::string::npos)
		pattern.replace(at, 2, argument);
	return pattern;
}

TrList::TrList(std::span<const std::string_view> french) {
	storage.reserve(french.size());
	for (std::string_view f : french)
		storage.push_back(Tr(f));
	views.assign(storage.begin(), storage.end());
}

} // namespace scribe
