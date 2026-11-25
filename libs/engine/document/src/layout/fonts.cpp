#include "document/fonts.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <set>

namespace doc {

namespace {

std::atomic<uint64_t> g_nextIdentity{1};

std::string Lower(std::string_view s) {
	std::string out(s);
	for (char &c : out)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return out;
}

bool IsStandardFamily(std::string_view lower) { return lower == "helvetica" || lower == "times" || lower == "courier"; }

} // namespace

FontLibrary::FontLibrary(std::vector<pdf::FontDescription> fonts) : m_fonts(std::move(fonts)), m_identity(g_nextIdentity++) {
	for (size_t i = 0; i < m_fonts.size(); ++i)
		m_byFamily[Lower(m_fonts[i].family)].push_back(i);
}

std::shared_ptr<FontLibrary> FontLibrary::Scan(std::span<const std::filesystem::path> directories) {
	return std::make_shared<FontLibrary>(pdf::ScanFonts(directories));
}

std::vector<std::string> FontLibrary::Families() const {
	std::vector<std::string> out{"Helvetica", "Times", "Courier"};
	std::set<std::string> seen{"helvetica", "times", "courier"};
	std::vector<std::string> installed;
	for (const pdf::FontDescription &d : m_fonts)
		if (seen.insert(Lower(d.family)).second)
			installed.push_back(d.family);
	std::ranges::sort(installed, [](const std::string &a, const std::string &b) { return Lower(a) < Lower(b); });
	out.insert(out.end(), installed.begin(), installed.end());
	return out;
}

const pdf::FontDescription *FontLibrary::Best(std::string_view family, bool bold, bool italic) const {
	const std::string key = Lower(family);
	if (IsStandardFamily(key))
		return nullptr; // familles standard : polices du PDF, sans incorporation
	const auto it = m_byFamily.find(key);
	if (it == m_byFamily.end())
		return nullptr;
	const pdf::FontDescription *best = nullptr;
	int bestScore = 1 << 30;
	for (size_t i : it->second) {
		const pdf::FontDescription &d = m_fonts[i];
		const int score = std::abs(d.weight - (bold ? 700 : 400)) + (d.italic != italic ? 1000 : 0);
		if (score < bestScore) {
			bestScore = score;
			best = &d;
		}
	}
	return best;
}

std::filesystem::path FontLibrary::PathOf(std::string_view family, bool bold, bool italic) const {
	const pdf::FontDescription *d = Best(family, bold, italic);
	return d ? d->path : std::filesystem::path();
}

pdf::FontRef FontLibrary::Resolve(std::string_view family, bool bold, bool italic) const {
	pdf::FontRef ref;
	ref.standard = pdf::MatchFont(family, bold, italic);
	const pdf::FontDescription *d = Best(family, bold, italic);
	if (!d)
		return ref;
	std::scoped_lock lock(m_mutex);
	if (const auto it = m_loaded.find(d->path); it != m_loaded.end()) {
		ref.face = it->second;
		return ref;
	}
	if (m_failed.contains(d->path))
		return ref;
	auto loaded = pdf::TrueTypeFont::Load(d->path);
	if (!loaded) {
		m_failed[d->path] = true; // police illisible : police standard (mesure et PDF cohérents)
		return ref;
	}
	ref.face = loaded.Value();
	m_loaded[d->path] = ref.face;
	return ref;
}

} // namespace doc
