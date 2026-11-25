#pragma once
/**
 * @file hash.hpp
 * @brief Hachage 64 bits rapide : mélangeurs d'entiers (splitmix/fmix), hachage de
 *        blocs d'octets à multiplication 128 bits, `core::Hasher<T>` pour RobinMap.
 */
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace core {

/// Mélangeur d'entier 64 bits (finaliseur de splitmix64) : avalanche complète.
[[nodiscard]] constexpr uint64_t Mix64(uint64_t x) noexcept {
	x ^= x >> 30;
	x *= 0xbf58476d1ce4e5b9ULL;
	x ^= x >> 27;
	x *= 0x94d049bb133111ebULL;
	x ^= x >> 31;
	return x;
}

[[nodiscard]] constexpr uint64_t HashCombine(uint64_t seed, uint64_t v) noexcept {
	return Mix64(seed ^ (v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2)));
}

namespace detail {
[[nodiscard]] inline uint64_t MulMix(uint64_t a, uint64_t b) noexcept {
	__extension__ using U128 = unsigned __int128;
	const U128 r = static_cast<U128>(a) * b;
	return static_cast<uint64_t>(r) ^ static_cast<uint64_t>(r >> 64);
}
[[nodiscard]] inline uint64_t Read64(const unsigned char *p) noexcept {
	uint64_t v;
	std::memcpy(&v, p, 8);
	return v;
}
[[nodiscard]] inline uint64_t Read32(const unsigned char *p) noexcept {
	uint32_t v;
	std::memcpy(&v, p, 4);
	return v;
}
} // namespace detail

/// Hachage d'un bloc d'octets (lecture 16 octets par tour, mélange 128 bits).
[[nodiscard]] inline uint64_t HashBytes(const void *data, size_t len, uint64_t seed = 0x2d358dccaa6c78a5ULL) noexcept {
	constexpr uint64_t k0 = 0xa0761d6478bd642fULL, k1 = 0xe7037ed1a0b428dbULL, k2 = 0x8ebc6af09c88c6e3ULL;
	const auto *p = static_cast<const unsigned char *>(data);
	seed ^= detail::MulMix(seed ^ k0, k1);
	uint64_t a = 0, b = 0;
	if (len <= 16) {
		if (len >= 4) {
			a = (detail::Read32(p) << 32) | detail::Read32(p + ((len >> 3) << 2));
			b = (detail::Read32(p + len - 4) << 32) | detail::Read32(p + len - 4 - ((len >> 3) << 2));
		} else if (len > 0) {
			a = (uint64_t(p[0]) << 16) | (uint64_t(p[len >> 1]) << 8) | p[len - 1];
		}
	} else {
		size_t i = len;
		while (i > 16) {
			seed = detail::MulMix(detail::Read64(p) ^ k1, detail::Read64(p + 8) ^ seed);
			p += 16;
			i -= 16;
		}
		a = detail::Read64(p + i - 16);
		b = detail::Read64(p + i - 8);
	}
	return detail::MulMix(k1 ^ len, detail::MulMix(a ^ k1, b ^ seed) ^ k2);
}

[[nodiscard]] inline uint64_t HashString(std::string_view s) noexcept { return HashBytes(s.data(), s.size()); }

/// Hachage FNV-1a constexpr (identifiants de types/événements à la compilation).
[[nodiscard]] constexpr uint64_t Fnv1a(std::string_view s) noexcept {
	uint64_t h = 0xcbf29ce484222325ULL;
	for (char c : s) {
		h ^= static_cast<unsigned char>(c);
		h *= 0x100000001b3ULL;
	}
	return h;
}

/// Foncteur de hachage par défaut pour `RobinMap`.
template <typename T, typename = void> struct Hasher {
	[[nodiscard]] size_t operator()(const T &v) const noexcept { return std::hash<T>{}(v); }
};
template <typename T> struct Hasher<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>> {
	[[nodiscard]] constexpr size_t operator()(T v) const noexcept { return Mix64(static_cast<uint64_t>(v)); }
};
template <typename T> struct Hasher<T *> {
	[[nodiscard]] size_t operator()(const T *p) const noexcept { return Mix64(std::bit_cast<uintptr_t>(p)); }
};
template <> struct Hasher<std::string> {
	using is_transparent = void;
	[[nodiscard]] size_t operator()(std::string_view s) const noexcept { return HashString(s); }
};
template <> struct Hasher<std::string_view> {
	using is_transparent = void;
	[[nodiscard]] size_t operator()(std::string_view s) const noexcept { return HashString(s); }
};

} // namespace core
