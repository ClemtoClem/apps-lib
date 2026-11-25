#include "ui/catalog.hpp"

#include <cctype>

namespace ui {

namespace {

bool ContainsIgnoreCase(std::string_view haystack, std::string_view needle) noexcept {
	if (needle.empty())
		return true;
	if (needle.size() > haystack.size())
		return false;
	for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
		bool ok = true;
		for (size_t j = 0; j < needle.size(); ++j) {
			if (std::tolower(static_cast<unsigned char>(haystack[i + j])) != std::tolower(static_cast<unsigned char>(needle[j]))) {
				ok = false;
				break;
			}
		}
		if (ok)
			return true;
	}
	return false;
}

} // namespace

std::vector<size_t> Catalog::Search(std::string_view query) const {
	std::vector<size_t> out;
	for (size_t i = 0; i < entries.size(); ++i) {
		const CatalogEntry &e = entries[i];
		if (ContainsIgnoreCase(e.id, query) || ContainsIgnoreCase(e.category, query) || ContainsIgnoreCase(e.name, query) ||
			ContainsIgnoreCase(e.summary, query) || ContainsIgnoreCase(e.description, query))
			out.push_back(i);
	}
	return out;
}

} // namespace ui
