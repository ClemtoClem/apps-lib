#pragma once
#include <cstdint>
#include <cstdio>

#include "settings.hpp"

namespace consoles::arm {

// Diagnostics : sur la sortie d'erreur. LOG_VERBOSE (registres ou instructions inconnus,
// susceptibles de se répéter des millions de fois par image) n'est compilé qu'avec
// ARM_VERBOSE_LOG.
#define LOG(fmt, ...) std::fprintf(stderr, "arm %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#ifdef ARM_VERBOSE_LOG
#define LOG_VERBOSE(fmt, ...) LOG(fmt, ##__VA_ARGS__)
#else
#define LOG_VERBOSE(fmt, ...) \
	do {                      \
	} while (0)
#endif
#define FORCE_INLINE inline __attribute__((always_inline))
#define BIT(i) (1u << (i))
#define SWAP(a, b) \
	{              \
		auto c = a; \
		a = b;     \
		b = c;     \
	}
#define U8TO16(data, index) ((data)[index] | ((data)[(index) + 1] << 8))
#define U8TO32(data, index) \
	((data)[index] | ((data)[(index) + 1] << 8) | ((data)[(index) + 2] << 16) | ((data)[(index) + 3] << 24))
#define U8TO64(data, index) ((uint64_t)U8TO32(data, (index) + 4) << 32) | (uint32_t)U8TO32(data, index)
#define U32TO8(data, index, value)                  \
	(data)[(index) + 0] = (uint8_t)((value) >> 0);  \
	(data)[(index) + 1] = (uint8_t)((value) >> 8);  \
	(data)[(index) + 2] = (uint8_t)((value) >> 16); \
	(data)[(index) + 3] = (uint8_t)((value) >> 24);
} // namespace consoles::arm

