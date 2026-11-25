#pragma once
/**
 * @file binary.hpp
 * @brief Sérialisation binaire petit-boutiste : `ByteWriter` / `ByteReader`
 *        (entiers fixes, varints LEB128, zigzag, flottants, chaînes, blocs),
 *        CRC-32 (table constexpr) et compression RLE d'octets/mots.
 */
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace dataio {

class ByteWriter {
	std::vector<uint8_t> m_buffer;

public:
	ByteWriter() = default;
	explicit ByteWriter(size_t reserve) { m_buffer.reserve(reserve); }

	template <typename T>
		requires std::is_arithmetic_v<T>
	void Put(T v) {
		const size_t at = m_buffer.size();
		m_buffer.resize(at + sizeof(T));
		std::memcpy(m_buffer.data() + at, &v, sizeof(T)); // hôte petit-boutiste (x86/ARM)
	}
	void PutU8(uint8_t v) { m_buffer.push_back(v); }
	void PutVarUint(uint64_t v) {
		while (v >= 0x80) {
			m_buffer.push_back(static_cast<uint8_t>(v | 0x80));
			v >>= 7;
		}
		m_buffer.push_back(static_cast<uint8_t>(v));
	}
	void PutVarInt(int64_t v) { PutVarUint((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63)); }
	void PutString(std::string_view s) {
		PutVarUint(s.size());
		PutBytes(s.data(), s.size());
	}
	void PutBytes(const void *data, size_t size) {
		const auto *p = static_cast<const uint8_t *>(data);
		m_buffer.insert(m_buffer.end(), p, p + size);
	}
	void PutBlob(std::span<const uint8_t> bytes) {
		PutVarUint(bytes.size());
		PutBytes(bytes.data(), bytes.size());
	}
	/// Réserve 4 octets à remplir plus tard (taille de section…). Retourne la position.
	size_t ReserveU32() {
		const size_t at = m_buffer.size();
		Put<uint32_t>(0);
		return at;
	}
	void PatchU32(size_t at, uint32_t v) { std::memcpy(m_buffer.data() + at, &v, 4); }

	[[nodiscard]] size_t Size() const noexcept { return m_buffer.size(); }
	[[nodiscard]] std::span<const uint8_t> Bytes() const noexcept { return m_buffer; }
	[[nodiscard]] std::vector<uint8_t> &Buffer() noexcept { return m_buffer; }
	[[nodiscard]] std::vector<uint8_t> Take() noexcept { return std::move(m_buffer); }
	void Clear() noexcept { m_buffer.clear(); }
};

class ByteReader {
	std::span<const uint8_t> m_data;
	size_t m_pos = 0;
	bool m_ok = true;

	bool Need(size_t n) noexcept {
		if (!m_ok || m_pos + n > m_data.size()) {
			m_ok = false;
			return false;
		}
		return true;
	}

public:
	ByteReader() = default;
	explicit ByteReader(std::span<const uint8_t> data) noexcept : m_data(data) {}

	template <typename T>
		requires std::is_arithmetic_v<T>
	T Get() noexcept {
		T v{};
		if (Need(sizeof(T))) {
			std::memcpy(&v, m_data.data() + m_pos, sizeof(T));
			m_pos += sizeof(T);
		}
		return v;
	}
	uint8_t GetU8() noexcept { return Get<uint8_t>(); }
	uint64_t GetVarUint() noexcept {
		uint64_t v = 0;
		for (int shift = 0; shift < 64; shift += 7) {
			if (!Need(1))
				return 0;
			const uint8_t b = m_data[m_pos++];
			v |= static_cast<uint64_t>(b & 0x7f) << shift;
			if (!(b & 0x80))
				return v;
		}
		m_ok = false;
		return 0;
	}
	int64_t GetVarInt() noexcept {
		const uint64_t z = GetVarUint();
		return static_cast<int64_t>((z >> 1) ^ (~(z & 1) + 1));
	}
	std::string GetString() {
		const uint64_t n = GetVarUint();
		if (!Need(n))
			return {};
		std::string s(reinterpret_cast<const char *>(m_data.data() + m_pos), n);
		m_pos += n;
		return s;
	}
	/// Vue sur un bloc préfixé de sa taille (aucune copie).
	std::span<const uint8_t> GetBlob() noexcept {
		const uint64_t n = GetVarUint();
		return GetBytes(n);
	}
	std::span<const uint8_t> GetBytes(size_t n) noexcept {
		if (!Need(n))
			return {};
		auto s = m_data.subspan(m_pos, n);
		m_pos += n;
		return s;
	}
	void Skip(size_t n) noexcept {
		if (Need(n))
			m_pos += n;
	}

	[[nodiscard]] bool Ok() const noexcept { return m_ok; }
	[[nodiscard]] bool AtEnd() const noexcept { return m_pos >= m_data.size(); }
	[[nodiscard]] size_t Position() const noexcept { return m_pos; }
	[[nodiscard]] size_t Remaining() const noexcept { return m_data.size() - m_pos; }
};

namespace detail {
constexpr std::array<uint32_t, 256> MakeCrcTable() {
	std::array<uint32_t, 256> t{};
	for (uint32_t i = 0; i < 256; ++i) {
		uint32_t c = i;
		for (int k = 0; k < 8; ++k)
			c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		t[i] = c;
	}
	return t;
}
inline constexpr std::array<uint32_t, 256> kCrcTable = MakeCrcTable();
} // namespace detail

/// CRC-32 (IEEE 802.3), chaînable via `seed`.
[[nodiscard]] constexpr uint32_t Crc32(std::span<const uint8_t> data, uint32_t seed = 0) noexcept {
	uint32_t c = ~seed;
	for (uint8_t b : data)
		c = detail::kCrcTable[(c ^ b) & 0xff] ^ (c >> 8);
	return ~c;
}

/// RLE de mots 16 bits : paires (varint longueur, valeur). Idéal pour des voxels
/// (grands volumes d'air ou de pierre homogènes).
void RleEncode16(std::span<const uint16_t> in, ByteWriter &out);
/// Décode exactement `out.size()` mots ; faux si les données sont corrompues.
[[nodiscard]] bool RleDecode16(ByteReader &in, std::span<uint16_t> out);

} // namespace dataio
