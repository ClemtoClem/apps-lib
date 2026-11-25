#pragma once
/**
 * @file object.hpp
 * @brief Objets PDF (null, booléen, entier, réel, chaîne, nom, tableau, dictionnaire, flux,
 *        référence indirecte) et analyseur syntaxique (ISO 32000-1 §7.3).
 */
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <core/result.hpp>

namespace pdf {

using Bytes = std::vector<uint8_t>;

enum class ObjectKind : uint8_t { Null, Bool, Integer, Real, String, Name, Array, Dictionary, Stream, Reference };

struct Reference {
	uint32_t number = 0;
	uint16_t generation = 0;
	friend bool operator==(const Reference &, const Reference &) = default;
};

/// Valeur PDF. Les dictionnaires (et le dictionnaire d'un flux) gardent l'ordre de leurs clés.
struct Object {
	ObjectKind kind = ObjectKind::Null;
	bool boolean = false;
	int64_t integer = 0;
	double real = 0;
	std::string text;			   ///< octets d'une chaîne, ou nom sans « / »
	std::vector<Object> items;	   ///< éléments d'un tableau ; valeurs d'un dictionnaire
	std::vector<std::string> keys; ///< clés d'un dictionnaire (parallèles à `items`)
	Reference ref;				   ///< référence indirecte
	Bytes data;					   ///< contenu brut (encodé) d'un flux

	[[nodiscard]] bool IsNull() const noexcept { return kind == ObjectKind::Null; }
	[[nodiscard]] bool IsNumber() const noexcept { return kind == ObjectKind::Integer || kind == ObjectKind::Real; }
	[[nodiscard]] bool IsDictionary() const noexcept { return kind == ObjectKind::Dictionary || kind == ObjectKind::Stream; }
	[[nodiscard]] bool IsName(std::string_view name) const noexcept { return kind == ObjectKind::Name && text == name; }
	[[nodiscard]] double Number() const noexcept { return kind == ObjectKind::Integer ? double(integer) : kind == ObjectKind::Real ? real : 0.0; }
	/// Valeur d'une clé de dictionnaire (ou de flux) ; nullptr si absente.
	[[nodiscard]] const Object *Get(std::string_view key) const noexcept;
	/// Ajoute ou remplace une clé.
	void Set(std::string key, Object value);

	[[nodiscard]] static Object MakeInteger(int64_t v);
	[[nodiscard]] static Object MakeReal(double v);
	[[nodiscard]] static Object MakeName(std::string name);
	[[nodiscard]] static Object MakeString(std::string bytes);
	[[nodiscard]] static Object MakeReference(Reference r);
};

/// Analyseur d'objets sur un tampon d'octets.
class Parser {
public:
	/// Résout la longueur d'un flux dont /Length est une référence indirecte.
	using LengthResolver = std::function<std::optional<int64_t>(Reference)>;

	explicit Parser(std::span<const uint8_t> data, size_t position = 0) noexcept : m_data(data), m_pos(position) {}

	void SetLengthResolver(LengthResolver resolver) { m_resolveLength = std::move(resolver); }
	[[nodiscard]] size_t Position() const noexcept { return m_pos; }
	void Seek(size_t position) noexcept { m_pos = std::min(position, m_data.size()); }
	[[nodiscard]] bool AtEnd() noexcept;

	/// Objet suivant (une référence « n g R » est reconnue).
	[[nodiscard]] Result<Object, std::string> ParseObject();
	/// Objet indirect « n g obj … endobj » ; rend aussi son numéro.
	[[nodiscard]] Result<std::pair<Reference, Object>, std::string> ParseIndirect();
	/// Lexème suivant d'un flux de contenu : opérande (objet) ou opérateur (mot-clé en `text`,
	/// kind == Null et `keyword` vrai).
	struct Token {
		Object value;
		bool keyword = false;
	};
	[[nodiscard]] Result<Token, std::string> NextToken();
	/// Mot-clé suivant (ex. « xref », « trailer ») sans le consommer s'il ne correspond pas.
	bool Expect(std::string_view keyword);
	void SkipWhitespace() noexcept;

private:
	[[nodiscard]] Result<Object, std::string> ParseValue(int depth);
	[[nodiscard]] Result<Object, std::string> ParseLiteralString();
	[[nodiscard]] Result<Object, std::string> ParseHexString();
	[[nodiscard]] Result<Object, std::string> ParseName();
	[[nodiscard]] Result<Object, std::string> ParseNumber();
	[[nodiscard]] Result<Object, std::string> ParseStreamBody(Object dict);
	[[nodiscard]] std::string_view Word() noexcept;

	std::span<const uint8_t> m_data;
	size_t m_pos;
	LengthResolver m_resolveLength;
};

} // namespace pdf
