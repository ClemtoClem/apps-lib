#pragma once
/**
 * @file string.hpp
 * @brief `String` / `StringView` : `std::string` / `std::string_view` enrichis du
 *        vocabulaire attendu par le wrapper SDL3 (CStr, IsEmpty, Split, Trim, From…).
 *        Héritage direct des types std : aucune copie, interop totale.
 */
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "option.hpp"

class StringView : public std::string_view {
public:
	static constexpr size_t NPOS = std::string_view::npos;

	constexpr StringView() noexcept = default;
	constexpr StringView(const char *data, size_t size) noexcept : std::string_view(data, size) {}
	constexpr StringView(const char *cstr) noexcept
		: std::string_view(cstr ? std::string_view(cstr) : std::string_view()) {}
	constexpr StringView(std::string_view sv) noexcept : std::string_view(sv) {}
	StringView(const std::string &s) noexcept : std::string_view(s) {}
	StringView(std::nullptr_t) = delete;

	/// Pointeur brut ; terminé par '\0' seulement si la vue couvre une chaîne C entière.
	[[nodiscard]] constexpr const char *CStr() const noexcept { return data(); }
	[[nodiscard]] constexpr const char *GetData() const noexcept { return data(); }
	[[nodiscard]] constexpr size_t GetSize() const noexcept { return size(); }
	[[nodiscard]] constexpr size_t Length() const noexcept { return size(); }
	[[nodiscard]] constexpr bool IsEmpty() const noexcept { return empty(); }
	[[nodiscard]] constexpr char Front() const noexcept { return front(); }
	[[nodiscard]] constexpr char Back() const noexcept { return back(); }
	[[nodiscard]] constexpr const char *Begin() const noexcept { return data(); }
	[[nodiscard]] constexpr const char *End() const noexcept { return data() + size(); }
	constexpr void RemovePrefix(size_t n) noexcept { remove_prefix(n); }
	constexpr void RemoveSuffix(size_t n) noexcept { remove_suffix(n); }

	[[nodiscard]] constexpr StringView Substr(size_t pos, size_t len = NPOS) const noexcept {
		if (pos > size())
			pos = size();
		return StringView(substr(pos, len));
	}
	[[nodiscard]] constexpr size_t Find(std::string_view n, size_t from = 0) const noexcept { return find(n, from); }
	[[nodiscard]] constexpr size_t Find(char c, size_t from = 0) const noexcept { return find(c, from); }
	[[nodiscard]] constexpr size_t Rfind(std::string_view n, size_t from = NPOS) const noexcept { return rfind(n, from); }
	[[nodiscard]] constexpr size_t Rfind(char c, size_t from = NPOS) const noexcept { return rfind(c, from); }
	[[nodiscard]] constexpr bool StartsWith(std::string_view p) const noexcept { return starts_with(p); }
	[[nodiscard]] constexpr bool EndsWith(std::string_view s) const noexcept { return ends_with(s); }
	[[nodiscard]] constexpr bool Contains(std::string_view s) const noexcept { return find(s) != NPOS; }
	[[nodiscard]] constexpr bool Contains(char c) const noexcept { return find(c) != NPOS; }
	[[nodiscard]] int Compare(std::string_view o) const noexcept { return compare(o); }
	[[nodiscard]] constexpr StringView Trim() const noexcept {
		constexpr std::string_view ws = " \t\r\n\f\v";
		const size_t b = find_first_not_of(ws);
		if (b == NPOS)
			return {};
		return StringView(substr(b, find_last_not_of(ws) - b + 1));
	}
};

class String : public std::string {
public:
	static constexpr size_t NPOS = std::string::npos;

	using std::string::string;
	String() = default;
	String(const std::string &s) : std::string(s) {}
	String(std::string &&s) noexcept : std::string(std::move(s)) {}
	String(std::string_view sv) : std::string(sv) {}
	String(StringView sv) : std::string(sv.data(), sv.size()) {}
	String(const char *s) : std::string(s ? s : "") {}
	String(std::nullptr_t) = delete;

	// ── Fabriques ───────────────────────────────────────────────────────────
	template <typename... Args> [[nodiscard]] static String Format(std::format_string<Args...> fmt, Args &&...args) {
		return String(std::format(fmt, std::forward<Args>(args)...));
	}
	[[nodiscard]] static String From(long long v, int base = 10) { return Integer(v, base); }
	[[nodiscard]] static String From(int v, int base = 10) { return Integer(v, base); }
	[[nodiscard]] static String From(long v, int base = 10) { return Integer(v, base); }
	[[nodiscard]] static String From(unsigned long long v, int base = 10) { return Integer(v, base); }
	[[nodiscard]] static String From(unsigned v, int base = 10) { return Integer(v, base); }
	[[nodiscard]] static String From(unsigned long v, int base = 10) { return Integer(v, base); }
	[[nodiscard]] static String From(double v, int decimals = 2) { return String(std::format("{:.{}f}", v, decimals)); }
	[[nodiscard]] static String From(float v, int decimals = 2) { return From(static_cast<double>(v), decimals); }
	[[nodiscard]] static String From(bool v) { return String(v ? "true" : "false"); }
	/// Représentation la plus courte qui se relit à l'identique (`std::to_chars`) : 0.1 → « 0.1 ».
	[[nodiscard]] static String FromStream(double v) {
		char buf[64];
		auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
		return ec == std::errc{} ? String(buf, static_cast<size_t>(end - buf)) : String();
	}
	/// Entier ou réel complet (tout le texte doit être consommé), sinon NONE.
	[[nodiscard]] Option<int64_t> TryParseInt(int base = 10) const;
	[[nodiscard]] Option<double> TryParseDouble() const;
	[[nodiscard]] static Option<String> ReadFile(const char *path);
	[[nodiscard]] static Option<String> ReadFile(const String &path) { return ReadFile(path.c_str()); }
	[[nodiscard]] bool WriteFile(const char *path) const;
	[[nodiscard]] bool WriteFile(const String &path) const { return WriteFile(path.c_str()); }

	// ── Accès ───────────────────────────────────────────────────────────────
	[[nodiscard]] const char *CStr() const noexcept { return c_str(); }
	[[nodiscard]] const char *GetData() const noexcept { return data(); }
	[[nodiscard]] char *GetData() noexcept { return data(); }
	[[nodiscard]] StringView View() const noexcept { return StringView(data(), size()); }
	[[nodiscard]] size_t GetSize() const noexcept { return size(); }
	[[nodiscard]] size_t Length() const noexcept { return size(); }
	[[nodiscard]] bool IsEmpty() const noexcept { return empty(); }
	[[nodiscard]] explicit operator bool() const noexcept { return !empty(); }
	[[nodiscard]] char CharAt(size_t i) const noexcept { return i < size() ? (*this)[i] : '\0'; }
	[[nodiscard]] char Front() const noexcept { return empty() ? '\0' : front(); }
	[[nodiscard]] char Back() const noexcept { return empty() ? '\0' : back(); }

	// ── Recherche ───────────────────────────────────────────────────────────
	[[nodiscard]] size_t Find(std::string_view s, size_t from = 0) const noexcept { return find(s, from); }
	[[nodiscard]] size_t Find(char c, size_t from = 0) const noexcept { return find(c, from); }
	[[nodiscard]] size_t Rfind(std::string_view s, size_t from = NPOS) const noexcept { return rfind(s, from); }
	[[nodiscard]] size_t Rfind(char c, size_t from = NPOS) const noexcept { return rfind(c, from); }
	[[nodiscard]] bool Contains(std::string_view s) const noexcept { return find(s) != NPOS; }
	[[nodiscard]] bool Contains(char c) const noexcept { return find(c) != NPOS; }
	[[nodiscard]] bool StartsWith(std::string_view p) const noexcept { return starts_with(p); }
	[[nodiscard]] bool EndsWith(std::string_view s) const noexcept { return ends_with(s); }
	[[nodiscard]] bool Equals(std::string_view o) const noexcept { return std::string_view(*this) == o; }
	[[nodiscard]] bool EqualsIgnoreCase(std::string_view o) const noexcept;
	[[nodiscard]] size_t Count(char c) const noexcept;

	// ── Transformations (copies) ────────────────────────────────────────────
	[[nodiscard]] String Substr(size_t pos, size_t len = NPOS) const { return pos >= size() ? String() : String(substr(pos, len)); }
	[[nodiscard]] String ToLower() const;
	[[nodiscard]] String ToUpper() const;
	[[nodiscard]] String Trim() const { return String(View().Trim()); }
	[[nodiscard]] String TrimLeft() const {
		const size_t first = find_first_not_of(" \t\r\n\v\f");
		return first == NPOS ? String() : String(substr(first));
	}
	[[nodiscard]] String TrimRight() const {
		const size_t last = find_last_not_of(" \t\r\n\v\f");
		return last == NPOS ? String() : String(substr(0, last + 1));
	}
	[[nodiscard]] String Replace(std::string_view find, std::string_view by) const;
	[[nodiscard]] String Replace(char find, char by) const;
	[[nodiscard]] String PadLeft(size_t width, char pad = ' ') const;
	[[nodiscard]] String PadRight(size_t width, char pad = ' ') const;
	[[nodiscard]] String Repeat(size_t n) const;
	[[nodiscard]] std::vector<String> Split(std::string_view delim, size_t maxSplits = 0) const;
	[[nodiscard]] std::vector<String> Split(char delim, size_t maxSplits = 0) const {
		return Split(std::string_view(&delim, 1), maxSplits);
	}
	[[nodiscard]] std::vector<String> Lines() const { return Split('\n'); }
	[[nodiscard]] static String Join(const std::vector<String> &parts, std::string_view sep = "");

	// ── Mutations ───────────────────────────────────────────────────────────
	String &Append(const char *data, size_t length) {
		append(data, length);
		return *this;
	}
	String &Append(std::string_view text) {
		append(text);
		return *this;
	}
	String &Append(const char *text) { return text ? Append(std::string_view(text)) : *this; }
	String &Append(char c) {
		push_back(c);
		return *this;
	}
	String &Append(size_t count, char c) {
		append(count, c);
		return *this;
	}
	void PushBack(char c) { push_back(c); }
	void Reserve(size_t n) { reserve(n); }
	void Resize(size_t n) { resize(n); }
	void Clear() noexcept { clear(); }
	bool Concat(std::string_view s) {
		append(s);
		return true;
	}
	bool Concat(char c) {
		push_back(c);
		return true;
	}
	void TrimInplace() { *this = Trim(); }

private:
	template <typename I> static String Integer(I v, int base) {
		char buf[72];
		auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, base);
		return ec == std::errc{} ? String(buf, static_cast<size_t>(end - buf)) : String();
	}
};

template <> struct std::hash<String> {
	size_t operator()(const String &s) const noexcept { return std::hash<std::string_view>{}(s); }
};
template <> struct std::hash<StringView> {
	size_t operator()(StringView s) const noexcept { return std::hash<std::string_view>{}(s); }
};
template <> struct std::formatter<String> : std::formatter<std::string_view> {
	auto format(const String &s, std::format_context &ctx) const {
		return std::formatter<std::string_view>::format(std::string_view(s), ctx);
	}
};
template <> struct std::formatter<StringView> : std::formatter<std::string_view> {
	auto format(StringView s, std::format_context &ctx) const {
		return std::formatter<std::string_view>::format(std::string_view(s), ctx);
	}
};

namespace unicode {
/// Décode un point de code UTF-8 à partir de `i` (avance `i`) ; U+FFFD si invalide.
[[nodiscard]] char32_t DecodeUtf8(std::string_view s, size_t &i) noexcept;
/// Ajoute l'encodage UTF-8 de `cp` à `out`.
void AppendUtf8(std::string &out, char32_t cp);
[[nodiscard]] size_t CodepointCount(std::string_view utf8) noexcept;
[[nodiscard]] std::u16string ToUtf16(std::string_view utf8);
[[nodiscard]] std::u32string ToUtf32(std::string_view utf8);
[[nodiscard]] std::string FromUtf16(std::u16string_view utf16);
[[nodiscard]] std::string FromUtf32(std::u32string_view utf32);
} // namespace unicode
