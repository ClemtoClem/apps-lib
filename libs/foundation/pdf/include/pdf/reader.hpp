#pragma once
/**
 * @file reader.hpp
 * @brief Lecture de fichiers PDF : en-tête, tables de références croisées (classiques et flux
 *        de références PDF 1.5, mises à jour incrémentales /Prev), flux d'objets, réparation par
 *        balayage des objets si la table est absente ou fausse, arbre des pages (attributs
 *        hérités), décodage des flux (FlateDecode avec prédicteurs PNG, ASCIIHexDecode,
 *        ASCII85Decode), métadonnées, extraction du texte.
 *
 * Non pris en charge : chiffrement, polices composites et tables /ToUnicode pour l'extraction
 * du texte (les polices simples sont décodées en WinAnsi), formulaires XObject.
 * Les méthodes `const` remplissent des caches internes : un `Reader` ne se partage pas entre fils.
 *
 *   auto file = pdf::Reader::Load("rapport.pdf");
 *   if (file) std::println("{} pages, titre : {}", file->PageCount(), file->InfoString("Title").value_or("?"));
 */
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/robin_map.hpp>

#include "pdf/object.hpp"
#include "pdf/writer.hpp"

namespace pdf {

struct PageInfo {
	double width = 0, height = 0; ///< MediaBox (points)
	int rotate = 0;				  ///< 0, 90, 180 ou 270
	Reference ref;
	Object dictionary; ///< dictionnaire de la page, attributs hérités inclus
};

class Reader {
public:
	[[nodiscard]] static PdfResult<Reader> Open(Bytes data);
	[[nodiscard]] static PdfResult<Reader> Load(const std::filesystem::path &path);

	[[nodiscard]] std::string_view Version() const noexcept { return m_version; }
	/// Vrai si la table des références était inutilisable et a été reconstruite.
	[[nodiscard]] bool Repaired() const noexcept { return m_repaired; }
	[[nodiscard]] size_t ObjectCount() const noexcept { return m_entries.Size(); }
	[[nodiscard]] const Object &Trailer() const noexcept { return m_trailer; }

	/// Objet indirect `number` (Null s'il n'existe pas).
	[[nodiscard]] PdfResult<Object> Get(uint32_t number) const;
	/// Suit les références jusqu'à un objet direct.
	[[nodiscard]] PdfResult<Object> Resolve(const Object &object) const;

	[[nodiscard]] size_t PageCount() const noexcept { return m_pages.size(); }
	[[nodiscard]] const PageInfo &Page(size_t index) const { return m_pages[index]; }
	/// Flux de contenu décodés et concaténés de la page.
	[[nodiscard]] PdfResult<Bytes> PageContent(size_t index) const;
	/// Texte de la page (UTF-8), lignes séparées par « \n », mots par des espaces.
	[[nodiscard]] PdfResult<std::string> ExtractText(size_t index) const;
	/// Entrée du dictionnaire /Info (Title, Author, Subject, Keywords, Creator, Producer…) en UTF-8.
	[[nodiscard]] std::optional<std::string> InfoString(std::string_view key) const;
	/// Décode un flux selon ses filtres.
	[[nodiscard]] PdfResult<Bytes> DecodeStream(const Object &stream) const;

private:
	struct Entry {
		uint8_t type = 1;		///< 1 : position dans le fichier ; 2 : dans un flux d'objets
		uint64_t offset = 0;	///< type 1 : position ; type 2 : numéro du flux d'objets
		uint32_t index = 0;		///< type 2 : rang dans le flux d'objets
	};

	Reader() = default;
	PdfResult<bool> ReadXref(size_t offset);
	PdfResult<bool> ReadXrefTable(Parser &parser);
	PdfResult<bool> ReadXrefStream(Parser &parser);
	void Repair();
	PdfResult<bool> CollectPages();
	[[nodiscard]] std::optional<int64_t> LengthOf(Reference ref) const;
	[[nodiscard]] PdfResult<Object> LoadFromObjectStream(uint32_t stream, uint32_t index) const;

	Bytes m_data;
	std::string m_version;
	bool m_repaired = false;
	core::RobinMap<uint32_t, Entry> m_entries;
	Object m_trailer;
	std::vector<PageInfo> m_pages;
	mutable core::RobinMap<uint32_t, Object> m_cache;
	mutable core::RobinMap<uint32_t, std::vector<Object>> m_objectStreams;
};

} // namespace pdf
