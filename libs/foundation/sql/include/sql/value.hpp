#pragma once
/**
 * @file value.hpp
 * @brief Valeur SQL typée (`std::variant`) : NULL, entier 64 bits, réel, texte, blob.
 */
#include <compare>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sql {

enum class ColumnType : uint8_t { Null, Int, Real, Text, Blob };

using Blob = std::vector<uint8_t>;

class Value {
public:
	using Storage = std::variant<std::monostate, int64_t, double, std::string, Blob>;

	Value() noexcept = default;
	Value(std::nullptr_t) noexcept {}
	Value(int v) noexcept : m_v(static_cast<int64_t>(v)) {}
	Value(int64_t v) noexcept : m_v(v) {}
	Value(uint64_t v) noexcept : m_v(static_cast<int64_t>(v)) {}
	Value(uint32_t v) noexcept : m_v(static_cast<int64_t>(v)) {}
	Value(bool v) noexcept : m_v(static_cast<int64_t>(v)) {}
	Value(double v) noexcept : m_v(v) {}
	Value(float v) noexcept : m_v(static_cast<double>(v)) {}
	Value(const char *s) : m_v(std::string(s ? s : "")) {}
	Value(std::string s) noexcept : m_v(std::move(s)) {}
	Value(std::string_view s) : m_v(std::string(s)) {}
	Value(Blob b) noexcept : m_v(std::move(b)) {}

	[[nodiscard]] ColumnType Type() const noexcept { return static_cast<ColumnType>(m_v.index()); }
	[[nodiscard]] bool IsNull() const noexcept { return m_v.index() == 0; }
	[[nodiscard]] bool IsNumeric() const noexcept { return m_v.index() == 1 || m_v.index() == 2; }

	[[nodiscard]] int64_t AsInt(int64_t def = 0) const noexcept;
	[[nodiscard]] double AsReal(double def = 0.0) const noexcept;
	[[nodiscard]] std::string_view AsText() const noexcept;
	[[nodiscard]] std::span<const uint8_t> AsBlob() const noexcept;
	[[nodiscard]] bool AsBool() const noexcept { return AsInt() != 0; }
	[[nodiscard]] std::string ToString() const;

	[[nodiscard]] const Storage &Raw() const noexcept { return m_v; }
	[[nodiscard]] Storage &Raw() noexcept { return m_v; }

	/// Comparaison SQL : NULL < nombres < texte < blob ; entiers et réels comparés numériquement.
	[[nodiscard]] std::weak_ordering Compare(const Value &o) const noexcept;
	bool operator==(const Value &o) const noexcept { return Compare(o) == 0; }
	std::weak_ordering operator<=>(const Value &o) const noexcept { return Compare(o); }

private:
	Storage m_v;
};

using Row = std::vector<Value>;

[[nodiscard]] std::string_view TypeName(ColumnType t) noexcept;

} // namespace sql
