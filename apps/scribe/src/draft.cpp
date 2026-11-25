#include "scribe/draft.hpp"

#include <format>
#include <fstream>

#include <document/persistence.hpp>

namespace scribe {

using namespace doc;

std::optional<DraftInfo> DraftStore::Load() const {
	std::error_code ec;
	if (!std::filesystem::is_regular_file(DocumentFile(), ec))
		return std::nullopt;
	DraftInfo info;
	std::ifstream in(InfoFile());
	for (std::string line; std::getline(in, line);) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
		if (key == "name")
			info.name = value;
		else if (key == "source")
			info.source = value;
	}
	return info;
}

Status DraftStore::Save(const DocumentView &document, const DraftInfo &info) const {
	std::error_code ec;
	std::filesystem::create_directories(m_directory, ec);
	if (ec)
		return Failure(ErrorCode::Io, std::format("dossier du brouillon inaccessible : {}", ec.message()));
	const std::filesystem::path docTmp = DocumentFile().string() + ".tmp", infoTmp = InfoFile().string() + ".tmp";
	std::filesystem::remove(docTmp, ec);
	if (auto s = SaveDocument(document, docTmp); !s)
		return s;
	{
		std::ofstream out(infoTmp, std::ios::trunc);
		// Une fin de ligne dans le nom casserait le format « clé=valeur » : remplacée par un espace.
		std::string name = info.name;
		for (char &c : name)
			if (c == '\n' || c == '\r')
				c = ' ';
		out << "name=" << name << "\nsource=" << info.source.string() << "\n";
		if (!out)
			return Failure(ErrorCode::Io, std::format("écriture de {} impossible", infoTmp.string()));
	}
	std::filesystem::rename(docTmp, DocumentFile(), ec);
	if (!ec)
		std::filesystem::rename(infoTmp, InfoFile(), ec);
	if (ec) {
		std::error_code ignored;
		std::filesystem::remove(docTmp, ignored);
		std::filesystem::remove(infoTmp, ignored);
		return Failure(ErrorCode::Io, std::format("brouillon non enregistré : {}", ec.message()));
	}
	return Success();
}

void DraftStore::Remove() const {
	std::error_code ec;
	std::filesystem::remove(DocumentFile(), ec);
	std::filesystem::remove(InfoFile(), ec);
}

} // namespace scribe
