#pragma once
/**
 * dataio::CsvDocument — codec CSV (RFC 4180 : champs entre guillemets,
 * guillemet doublé pour échapper, retours à la ligne autorisés dans un
 * champ cité).
 *
 * Contrainte (partagée avec SDL3pp_dataScripts.h) : le CSV n'a qu'un seul
 * niveau de hiérarchie — la racine est un Object dont chaque valeur est un
 * Array (une colonne nommée). La première ligne du fichier est l'en-tête
 * (noms de colonnes), les suivantes sont les données, une cellule par
 * colonne et par ligne.
 *
 * Aucune exception : les erreurs de syntaxe (champ cité non terminé) sont
 * remontées via Option<ParseError>.
 */
#include "../document.hpp"

namespace dataio {

class CsvDocument final : public Document {
public:
	CsvDocument() = default;

	[[nodiscard]] String EncodeStr() const override;

	/// Grille brute, sans en-tête nommé (tableaux dont la première ligne n'est pas un jeu de noms
	/// de colonnes uniques) : une ligne par vecteur, fin de ligne CRLF, échappement RFC 4180.
	[[nodiscard]] static String EncodeRows(const std::vector<std::vector<String>> &rows);
	/// Lecture d'une grille brute (même syntaxe que `DecodeStr`, sans interprétation d'en-tête).
	[[nodiscard]] static Result<std::vector<std::vector<String>>, ParseError> DecodeRows(const String &content);

protected:
	[[nodiscard]] Option<ParseError> DecodeImpl(const String &content) override;

private:
	static void WriteField(String &out, const String &field);

	// Découpe le contenu en lignes de cellules, en respectant les guillemets
	// (une cellule citée peut contenir des virgules et des retours à la ligne).
	[[nodiscard]] static Option<ParseError> ParseRows(const String &content,
													   std::vector<std::vector<String>> &rows);
};

DOC_REGISTER_FORMAT("csv", CsvDocument, ".csv")

} // namespace dataio