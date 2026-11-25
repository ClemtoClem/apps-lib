// Définitions de dataio/file.hpp — fractionnement transparent des fichiers volumineux.
#include "dataio/file.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace dataio {

namespace {

bool Exists(const std::filesystem::path &p) {
	std::error_code ec;
	return std::filesystem::exists(p, ec);
}

void Remove(const std::filesystem::path &p) {
	std::error_code ec;
	std::filesystem::remove(p, ec);
}

/// Fragments excédentaires : supprime `nom.N.ext` pour N ≥ `from`, jusqu'au premier absent.
void RemoveFragmentsFrom(const std::filesystem::path &path, std::size_t from) {
	for (std::size_t n = from;; ++n) {
		const std::filesystem::path frag = FragmentPath(path, n);
		if (!Exists(frag))
			break;
		Remove(frag);
	}
}

} // namespace

std::filesystem::path FragmentPath(const std::filesystem::path &path, std::size_t n) {
	const std::string frag = path.stem().string() + "." + std::to_string(n) + path.extension().string();
	return path.parent_path() / frag;
}

std::optional<std::string> WriteFile(const std::filesystem::path &path, std::string_view text, const FileOptions &options) {
	std::error_code ec;
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);

	if (options.split && text.size() > options.maxSize) {
		// Fractionnement en morceaux de `maxSize` octets (au moins un octet).
		const std::size_t max = std::max<std::size_t>(1, options.maxSize);
		const std::size_t count = (text.size() + max - 1) / max;
		for (std::size_t i = 0; i < count; ++i) {
			const std::size_t off = i * max;
			const std::size_t len = std::min(max, text.size() - off);
			const std::filesystem::path frag = FragmentPath(path, i + 1);
			std::ofstream out(frag, std::ios::binary | std::ios::trunc);
			if (!out)
				return "impossible d'écrire " + frag.string();
			out.write(text.data() + off, std::streamsize(len));
			if (!out)
				return "écriture incomplète de " + frag.string();
		}
		Remove(path); // remplacé par les fragments
		RemoveFragmentsFrom(path, count + 1); // le fichier a rétréci : fragments excédentaires
		return std::nullopt;
	}

	// Écriture simple, puis suppression d'éventuels anciens fragments (fichier redevenu petit).
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return "impossible d'écrire " + path.string();
	out.write(text.data(), std::streamsize(text.size()));
	if (!out)
		return "écriture incomplète de " + path.string();
	RemoveFragmentsFrom(path, 1);
	return std::nullopt;
}

std::optional<std::string> ReadFile(const std::filesystem::path &path) {
	if (Exists(FragmentPath(path, 1))) {
		// Fichier fractionné : réassemblage des fragments dans l'ordre.
		std::string text;
		for (std::size_t n = 1;; ++n) {
			const std::filesystem::path frag = FragmentPath(path, n);
			if (!Exists(frag))
				break;
			std::ifstream in(frag, std::ios::binary);
			if (!in)
				return std::nullopt; // fragment manquant : fichier illisible
			std::stringstream ss;
			ss << in.rdbuf();
			text += ss.str();
		}
		return text;
	}
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return std::nullopt;
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

} // namespace dataio
