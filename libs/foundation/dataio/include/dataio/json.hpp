#pragma once
/**
 * @file json.hpp
 * @brief Valeur JSON (`std::variant`), lecteur tolérant (commentaires ligne, bloc et dièse,
 *        `#`, virgules finales) avec position d'erreur, écrivain compact ou indenté.
 *        Les objets gardent l'ordre d'insertion (vecteur de paires).
 */
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <core/result.hpp>

namespace dataio {

class Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::vector<std::pair<std::string, Json>>;

enum class JsonType : uint8_t { Null, Bool, Number, String, Array, Object };

class Json {
public:
	using Storage = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

	Json() noexcept : m_value(nullptr) {}
	Json(std::nullptr_t) noexcept : m_value(nullptr) {}
	Json(bool b) noexcept : m_value(b) {}
	Json(int v) noexcept : m_value(static_cast<double>(v)) {}
	Json(int64_t v) noexcept : m_value(static_cast<double>(v)) {}
	Json(uint64_t v) noexcept : m_value(static_cast<double>(v)) {}
	Json(double v) noexcept : m_value(v) {}
	Json(float v) noexcept : m_value(static_cast<double>(v)) {}
	Json(const char *s) : m_value(std::string(s ? s : "")) {}
	Json(std::string s) noexcept : m_value(std::move(s)) {}
	Json(std::string_view s) : m_value(std::string(s)) {}
	Json(JsonArray a) noexcept : m_value(std::move(a)) {}
	Json(JsonObject o) noexcept : m_value(std::move(o)) {}

	[[nodiscard]] static Json MakeArray() { return Json(JsonArray{}); }
	[[nodiscard]] static Json MakeObject() { return Json(JsonObject{}); }

	[[nodiscard]] JsonType Type() const noexcept { return static_cast<JsonType>(m_value.index()); }
	[[nodiscard]] bool IsNull() const noexcept { return Type() == JsonType::Null; }
	[[nodiscard]] bool IsBool() const noexcept { return Type() == JsonType::Bool; }
	[[nodiscard]] bool IsNumber() const noexcept { return Type() == JsonType::Number; }
	[[nodiscard]] bool IsString() const noexcept { return Type() == JsonType::String; }
	[[nodiscard]] bool IsArray() const noexcept { return Type() == JsonType::Array; }
	[[nodiscard]] bool IsObject() const noexcept { return Type() == JsonType::Object; }

	// Lectures tolérantes : valeur par défaut si le type ne correspond pas.
	[[nodiscard]] bool AsBool(bool def = false) const noexcept;
	[[nodiscard]] double AsNumber(double def = 0.0) const noexcept;
	[[nodiscard]] float AsFloat(float def = 0.f) const noexcept { return static_cast<float>(AsNumber(def)); }
	[[nodiscard]] int64_t AsInt(int64_t def = 0) const noexcept;
	[[nodiscard]] std::string_view AsString(std::string_view def = {}) const noexcept;
	[[nodiscard]] const JsonArray &AsArray() const noexcept;
	[[nodiscard]] const JsonObject &AsObject() const noexcept;
	[[nodiscard]] JsonArray &MutArray();
	[[nodiscard]] JsonObject &MutObject();

	/// Membre d'objet (nullptr si absent ou si ce n'est pas un objet).
	[[nodiscard]] const Json *Find(std::string_view key) const noexcept;
	[[nodiscard]] Json *Find(std::string_view key) noexcept;
	/// Membre d'objet ou valeur nulle partagée.
	[[nodiscard]] const Json &operator[](std::string_view key) const noexcept;
	[[nodiscard]] const Json &operator[](size_t index) const noexcept;
	/// Chemin pointé : `Get("player.speed")`.
	[[nodiscard]] const Json &At(std::string_view dottedPath) const noexcept;
	/// Insère ou remplace (convertit en objet si nécessaire).
	Json &Set(std::string_view key, Json value);
	Json &Push(Json value);
	[[nodiscard]] size_t Size() const noexcept;
	/// Fusion profonde : les objets sont fusionnés récursivement, le reste est remplacé.
	void MergeFrom(const Json &other);

	[[nodiscard]] const Storage &Raw() const noexcept { return m_value; }
	bool operator==(const Json &o) const = default;

private:
	Storage m_value;
};

struct JsonError {
	std::string message;
	int line = 0;
	int column = 0;
	[[nodiscard]] std::string Format() const;
};

[[nodiscard]] Result<Json, JsonError> ParseJson(std::string_view text);
[[nodiscard]] Result<Json, JsonError> LoadJsonFile(const std::string &path);
/// `indent < 0` : sortie compacte.
[[nodiscard]] std::string WriteJson(const Json &value, int indent = 2);
bool SaveJsonFile(const std::string &path, const Json &value, int indent = 2);

/// Positions [début, fin) des éléments du tableau racine d'un texte JSON (commentaires
/// `//`, `#` et `/* */` tolérés) ; vide si la racine n'est pas un tableau.
[[nodiscard]] std::vector<std::pair<size_t, size_t>> JsonArrayElementSpans(std::string_view text);
/// Remplace l'élément `index` du tableau racine par `value`, sans toucher au reste du
/// texte (commentaires, mise en forme des autres éléments) ; indentation (tabulations
/// ou espaces) reprise de l'élément remplacé. `nullopt` si l'élément n'existe pas.
[[nodiscard]] std::optional<std::string> ReplaceJsonArrayElement(std::string_view text, size_t index, const Json &value);
/// Ajoute `value` à la fin du tableau racine (même indentation que le dernier élément).
[[nodiscard]] std::optional<std::string> AppendJsonArrayElement(std::string_view text, const Json &value);

} // namespace dataio
