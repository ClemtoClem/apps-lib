#include <charconv>
#include "core/string.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

Option<int64_t> String::TryParseInt(int base) const {
	if (empty())
		return NONE;
	const char *first = data(), *last = data() + size();
	if (*first == '+')
		++first;
	int64_t value = 0;
	const auto [end, ec] = std::from_chars(first, last, value, base);
	if (ec != std::errc{} || end != last)
		return NONE;
	return Option<int64_t>(value);
}

Option<double> String::TryParseDouble() const {
	if (empty())
		return NONE;
	const char *first = data(), *last = data() + size();
	if (*first == '+')
		++first;
	double value = 0;
	const auto [end, ec] = std::from_chars(first, last, value);
	if (ec != std::errc{} || end != last)
		return NONE;
	return Option<double>(value);
}

Option<String> String::ReadFile(const char *path) {
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return NONE;
	String out;
	in.seekg(0, std::ios::end);
	const auto len = in.tellg();
	if (len > 0) {
		out.resize(static_cast<size_t>(len));
		in.seekg(0);
		in.read(out.data(), len);
	}
	return Some(std::move(out));
}

bool String::WriteFile(const char *path) const {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.write(data(), static_cast<std::streamsize>(size()));
	return static_cast<bool>(out);
}

bool String::EqualsIgnoreCase(std::string_view o) const noexcept {
	return size() == o.size() && std::equal(begin(), end(), o.begin(), [](char a, char b) {
			   return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		   });
}

size_t String::Count(char c) const noexcept { return static_cast<size_t>(std::count(begin(), end(), c)); }

String String::ToLower() const {
	String r(*this);
	std::ranges::transform(r, r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return r;
}
String String::ToUpper() const {
	String r(*this);
	std::ranges::transform(r, r.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return r;
}
String String::Replace(std::string_view find, std::string_view by) const {
	if (find.empty())
		return *this;
	String r;
	r.reserve(size());
	size_t pos = 0;
	for (size_t hit; (hit = this->find(find, pos)) != npos; pos = hit + find.size()) {
		r.append(data() + pos, hit - pos);
		r.append(by);
	}
	r.append(data() + pos, size() - pos);
	return r;
}
String String::Replace(char find, char by) const {
	String r(*this);
	std::ranges::replace(r, find, by);
	return r;
}
String String::PadLeft(size_t width, char pad) const {
	return size() >= width ? *this : String(std::string(width - size(), pad) + *this);
}
String String::PadRight(size_t width, char pad) const {
	return size() >= width ? *this : String(*this + std::string(width - size(), pad));
}
String String::Repeat(size_t n) const {
	String r;
	r.reserve(size() * n);
	for (size_t i = 0; i < n; ++i)
		r.append(*this);
	return r;
}
std::vector<String> String::Split(std::string_view delim, size_t maxSplits) const {
	std::vector<String> out;
	if (delim.empty()) {
		out.emplace_back(*this);
		return out;
	}
	size_t pos = 0;
	for (size_t hit; (hit = find(delim, pos)) != npos && (maxSplits == 0 || out.size() < maxSplits); pos = hit + delim.size())
		out.emplace_back(std::string_view(data() + pos, hit - pos));
	out.emplace_back(std::string_view(data() + pos, size() - pos));
	return out;
}
String String::Join(const std::vector<String> &parts, std::string_view sep) {
	String r;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i)
			r.append(sep);
		r.append(parts[i]);
	}
	return r;
}

namespace unicode {

char32_t DecodeUtf8(std::string_view s, size_t &i) noexcept {
	const auto b0 = static_cast<unsigned char>(s[i++]);
	if (b0 < 0x80)
		return b0;
	int extra = (b0 >= 0xF0) ? 3 : (b0 >= 0xE0) ? 2 : (b0 >= 0xC0) ? 1 : -1;
	if (extra < 0 || i + static_cast<size_t>(extra) > s.size())
		return 0xFFFD;
	char32_t cp = b0 & (0x3F >> extra);
	for (int k = 0; k < extra; ++k) {
		const auto b = static_cast<unsigned char>(s[i]);
		if ((b & 0xC0) != 0x80)
			return 0xFFFD;
		cp = (cp << 6) | (b & 0x3F);
		++i;
	}
	return cp;
}

void AppendUtf8(std::string &out, char32_t cp) {
	if (cp < 0x80) {
		out.push_back(static_cast<char>(cp));
	} else if (cp < 0x800) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp < 0x10000) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
}

size_t CodepointCount(std::string_view s) noexcept {
	size_t n = 0;
	for (char c : s)
		n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
	return n;
}

std::u32string ToUtf32(std::string_view s) {
	std::u32string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();)
		out.push_back(DecodeUtf8(s, i));
	return out;
}
std::u16string ToUtf16(std::string_view s) {
	std::u16string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size();) {
		char32_t cp = DecodeUtf8(s, i);
		if (cp >= 0x10000) {
			cp -= 0x10000;
			out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
			out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
		} else {
			out.push_back(static_cast<char16_t>(cp));
		}
	}
	return out;
}
std::string FromUtf32(std::u32string_view s) {
	std::string out;
	out.reserve(s.size());
	for (char32_t c : s)
		AppendUtf8(out, c);
	return out;
}
std::string FromUtf16(std::u16string_view s) {
	std::string out;
	out.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		char32_t c = s[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size()) {
			const char32_t lo = s[i + 1];
			if (lo >= 0xDC00 && lo < 0xE000) {
				c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
				++i;
			}
		}
		AppendUtf8(out, c);
	}
	return out;
}

} // namespace unicode
