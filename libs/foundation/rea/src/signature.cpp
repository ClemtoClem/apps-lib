// Recherche de motifs : signatures d'algorithmes connus.
#include "rea/signature.hpp"

#include <algorithm>

namespace rea {

namespace {

std::vector<uint8_t> Hex(std::string_view hex) {
	std::vector<uint8_t> out;
	out.reserve(hex.size() / 3 + 1);
	for (size_t i = 0; i + 1 < hex.size();) {
		if (hex[i] == ' ' || hex[i] == '\n' || hex[i] == '\t') {
			++i;
			continue;
		}
		auto nib = [](char c) -> int {
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return 0;
		};
		out.push_back(uint8_t(nib(hex[i]) * 16 + nib(hex[i + 1])));
		i += 2;
	}
	return out;
}

} // namespace

std::vector<ByteSignature> BuiltinSignatures() {
	// Boîte S du chiffrement AES (table 16×16 standard, FIPS-197).
	constexpr std::string_view kAesSbox = R"(
		63 7c 77 7b f2 6b 6f c5 30 01 67 2b fe d7 ab 76
		ca 82 c9 7d fa 59 47 f0 ad d4 a2 af 9c a4 72 c0
		b7 fd 93 26 36 3f f7 cc 34 a5 e5 f1 71 d8 31 15
		04 c7 23 c3 18 96 05 9a 07 12 80 e2 eb 27 b2 75
		09 83 2c 1a 1b 6e 5a a0 52 3b d6 b3 29 e3 2f 84
		53 d1 00 ed 20 fc b1 5b 6a cb be 39 4a 4c 58 cf
		d0 ef aa fb 43 4d 33 85 45 f9 02 7f 50 3c 9f a8
		51 a3 40 8f 92 9d 38 f5 bc b6 da 21 10 ff f3 d2
		cd 0c 13 ec 5f 97 44 17 c4 a7 7e 3d 64 5d 19 73
		60 81 4f dc 22 2a 90 88 46 ee b8 14 de 5e 0b db
		e0 32 3a 0a 49 06 24 5c c2 d3 ac 62 91 95 e4 79
		e7 c8 37 6d 8d d5 4e a9 6c 56 f4 ea 65 7a ae 08
		ba 78 25 2e 1c a6 b4 c6 e8 dd 74 1f 4b bd 8b 8a
		70 3e b5 66 48 03 f6 0e 61 35 57 b9 86 c1 1d 9e
		e1 f8 98 11 69 d9 8e 94 9b 1e 87 e9 ce 55 28 df
		8c a1 89 0d bf e6 42 68 41 99 2d 0f b0 54 bb 16)";
	// Vecteurs d'initialisation de SHA-256 (H0..H7, grand-boutiste).
	constexpr std::string_view kSha256Iv = "6a 09 e6 67 bb 67 ae 85 3c 6e f3 72 a5 4f f5 3a 51 0e 52 7f 9b 05 68 8c 1f 83 d9 ab 5b e0 cd 19";
	// Vecteurs d'initialisation de MD5 (A, B, C, D — petit-boutiste).
	constexpr std::string_view kMd5Iv = "01 23 45 67 89 ab cd ef fe dc ba 98 76 54 32 10";

	return {
		{"AES", Hex(kAesSbox), "boîte S du chiffrement AES (FIPS-197)"},
		{"SHA-256", Hex(kSha256Iv), "vecteurs d'initialisation de SHA-256"},
		{"MD5", Hex(kMd5Iv), "vecteurs d'initialisation de MD5"},
	};
}

std::vector<ByteSignatureMatch> SearchSignatures(const uint8_t *data, size_t size, const std::vector<ByteSignature> &signatures) {
	std::vector<ByteSignatureMatch> out;
	for (const ByteSignature &sig : signatures) {
		if (sig.bytes.empty())
			continue;
		const uint8_t *it = std::search(data, data + size, sig.bytes.begin(), sig.bytes.end());
		while (it != data + size) {
			ByteSignatureMatch m;
			m.name = sig.name;
			m.description = sig.description;
			m.offset = uint64_t(it - data);
			out.push_back(std::move(m));
			it = std::search(it + 1, data + size, sig.bytes.begin(), sig.bytes.end());
		}
	}
	return out;
}

std::vector<ByteSignatureMatch> SearchSignatures(const Elf &elf, const std::vector<ByteSignature> &signatures) {
	const std::vector<uint8_t> &raw = elf.Raw();
	std::vector<ByteSignatureMatch> out = SearchSignatures(raw.data(), raw.size(), signatures);
	for (ByteSignatureMatch &m : out)
		for (const ElfSection &s : elf.Sections())
			if (s.address != 0 && s.HasFileBytes() && m.offset >= s.offset && m.offset < s.offset + s.size) {
				m.address = s.address + (m.offset - s.offset);
				break;
			}
	return out;
}

} // namespace rea
