#include "dataio/base64.hpp"

#include <array>

namespace dataio {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr std::array<int8_t, 256> MakeReverse() {
	std::array<int8_t, 256> r{};
	r.fill(-1);
	for (size_t i = 0; i < kAlphabet.size(); ++i)
		r[uint8_t(kAlphabet[i])] = int8_t(i);
	return r;
}
constexpr std::array<int8_t, 256> kReverse = MakeReverse();

} // namespace

std::string Base64Encode(std::span<const uint8_t> bytes) {
	std::string out;
	out.reserve((bytes.size() + 2) / 3 * 4);
	size_t i = 0;
	for (; i + 2 < bytes.size(); i += 3) {
		const uint32_t v = uint32_t(bytes[i]) << 16 | uint32_t(bytes[i + 1]) << 8 | bytes[i + 2];
		out.push_back(kAlphabet[v >> 18 & 63]);
		out.push_back(kAlphabet[v >> 12 & 63]);
		out.push_back(kAlphabet[v >> 6 & 63]);
		out.push_back(kAlphabet[v & 63]);
	}
	if (const size_t rest = bytes.size() - i; rest > 0) {
		const uint32_t v = uint32_t(bytes[i]) << 16 | (rest == 2 ? uint32_t(bytes[i + 1]) << 8 : 0u);
		out.push_back(kAlphabet[v >> 18 & 63]);
		out.push_back(kAlphabet[v >> 12 & 63]);
		out.push_back(rest == 2 ? kAlphabet[v >> 6 & 63] : '=');
		out.push_back('=');
	}
	return out;
}

std::optional<std::vector<uint8_t>> Base64Decode(std::string_view text) {
	std::vector<uint8_t> out;
	out.reserve(text.size() / 4 * 3);
	uint32_t acc = 0;
	int bits = 0, count = 0, padding = 0;
	for (const char c : text) {
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			continue;
		if (c == '=') {
			++padding;
			continue;
		}
		const int8_t v = kReverse[uint8_t(c)];
		if (v < 0 || padding > 0) // caractère invalide, ou donnée après le remplissage
			return std::nullopt;
		acc = acc << 6 | uint32_t(v);
		bits += 6;
		++count;
		if (bits >= 8) {
			bits -= 8;
			out.push_back(uint8_t(acc >> bits));
		}
	}
	const int tail = count % 4;
	if (tail == 1 || padding > 2 || (padding > 0 && (count + padding) % 4 != 0))
		return std::nullopt;
	if ((acc & ((1u << bits) - 1)) != 0) // bits de bourrage non nuls : encodage non canonique
		return std::nullopt;
	return out;
}

} // namespace dataio
