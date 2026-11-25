#include "math/noise.hpp"

#include <cmath>
#include <numeric>

namespace math {
namespace {

constexpr float kGrad3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
								 {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};

inline int Floor(float v) noexcept {
	const int i = static_cast<int>(v);
	return v < static_cast<float>(i) ? i - 1 : i;
}

uint64_t SplitMix(uint64_t &x) noexcept {
	uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}

} // namespace

SimplexNoise::SimplexNoise(uint64_t seed) noexcept {
	std::array<uint8_t, 256> p{};
	std::iota(p.begin(), p.end(), uint8_t{0});
	uint64_t state = seed;
	for (int i = 255; i > 0; --i) { // Fisher–Yates déterministe
		const auto j = static_cast<int>(SplitMix(state) % static_cast<uint64_t>(i + 1));
		std::swap(p[static_cast<size_t>(i)], p[static_cast<size_t>(j)]);
	}
	for (size_t i = 0; i < 512; ++i)
		m_perm[i] = p[i & 255];
}

float SimplexNoise::Noise2(float xin, float yin) const noexcept {
	constexpr float F2 = 0.36602540378f; // 0.5*(sqrt(3)-1)
	constexpr float G2 = 0.21132486540f; // (3-sqrt(3))/6
	const float s = (xin + yin) * F2;
	const int i = Floor(xin + s), j = Floor(yin + s);
	const float t = static_cast<float>(i + j) * G2;
	const float x0 = xin - (static_cast<float>(i) - t), y0 = yin - (static_cast<float>(j) - t);
	const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
	const float x1 = x0 - static_cast<float>(i1) + G2, y1 = y0 - static_cast<float>(j1) + G2;
	const float x2 = x0 - 1.f + 2.f * G2, y2 = y0 - 1.f + 2.f * G2;
	const int ii = i & 255, jj = j & 255;
	auto corner = [&](float x, float y, int gi) {
		float tt = 0.5f - x * x - y * y;
		if (tt < 0)
			return 0.f;
		tt *= tt;
		const float *g = kGrad3[gi % 12];
		return tt * tt * (g[0] * x + g[1] * y);
	};
	const float n0 = corner(x0, y0, m_perm[static_cast<size_t>(ii + m_perm[static_cast<size_t>(jj)])]);
	const float n1 = corner(x1, y1, m_perm[static_cast<size_t>(ii + i1 + m_perm[static_cast<size_t>(jj + j1)])]);
	const float n2 = corner(x2, y2, m_perm[static_cast<size_t>(ii + 1 + m_perm[static_cast<size_t>(jj + 1)])]);
	return 70.f * (n0 + n1 + n2);
}

float SimplexNoise::Noise3(float xin, float yin, float zin) const noexcept {
	constexpr float F3 = 1.0f / 3.0f, G3 = 1.0f / 6.0f;
	const float s = (xin + yin + zin) * F3;
	const int i = Floor(xin + s), j = Floor(yin + s), k = Floor(zin + s);
	const float t = static_cast<float>(i + j + k) * G3;
	const float x0 = xin - (static_cast<float>(i) - t), y0 = yin - (static_cast<float>(j) - t), z0 = zin - (static_cast<float>(k) - t);
	int i1, j1, k1, i2, j2, k2;
	if (x0 >= y0) {
		if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
		else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
		else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
	} else {
		if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
		else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
		else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
	}
	const float x1 = x0 - static_cast<float>(i1) + G3, y1 = y0 - static_cast<float>(j1) + G3, z1 = z0 - static_cast<float>(k1) + G3;
	const float x2 = x0 - static_cast<float>(i2) + 2 * G3, y2 = y0 - static_cast<float>(j2) + 2 * G3, z2 = z0 - static_cast<float>(k2) + 2 * G3;
	const float x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
	const int ii = i & 255, jj = j & 255, kk = k & 255;
	auto p = [&](int v) { return static_cast<int>(m_perm[static_cast<size_t>(v)]); };
	auto corner = [&](float x, float y, float z, int gi) {
		float tt = 0.6f - x * x - y * y - z * z;
		if (tt < 0)
			return 0.f;
		tt *= tt;
		const float *g = kGrad3[gi % 12];
		return tt * tt * (g[0] * x + g[1] * y + g[2] * z);
	};
	const float n0 = corner(x0, y0, z0, p(ii + p(jj + p(kk))));
	const float n1 = corner(x1, y1, z1, p(ii + i1 + p(jj + j1 + p(kk + k1))));
	const float n2 = corner(x2, y2, z2, p(ii + i2 + p(jj + j2 + p(kk + k2))));
	const float n3 = corner(x3, y3, z3, p(ii + 1 + p(jj + 1 + p(kk + 1))));
	return 32.f * (n0 + n1 + n2 + n3);
}

float SimplexNoise::Fbm2(float x, float y, int octaves, float lacunarity, float gain) const noexcept {
	float sum = 0.f, amp = 1.f, norm = 0.f;
	for (int o = 0; o < octaves; ++o) {
		sum += Noise2(x, y) * amp;
		norm += amp;
		x *= lacunarity;
		y *= lacunarity;
		amp *= gain;
	}
	return sum / norm;
}

float SimplexNoise::Fbm3(float x, float y, float z, int octaves, float lacunarity, float gain) const noexcept {
	float sum = 0.f, amp = 1.f, norm = 0.f;
	for (int o = 0; o < octaves; ++o) {
		sum += Noise3(x, y, z) * amp;
		norm += amp;
		x *= lacunarity;
		y *= lacunarity;
		z *= lacunarity;
		amp *= gain;
	}
	return sum / norm;
}

float SimplexNoise::Ridged2(float x, float y, int octaves, float lacunarity, float gain) const noexcept {
	float sum = 0.f, amp = 0.5f, norm = 0.f, prev = 1.f;
	for (int o = 0; o < octaves; ++o) {
		float n = 1.f - std::fabs(Noise2(x, y));
		n *= n;
		sum += n * amp * prev;
		norm += amp;
		prev = n;
		x *= lacunarity;
		y *= lacunarity;
		amp *= gain;
	}
	return sum / norm;
}

Rng::Rng(uint64_t seed) noexcept {
	for (auto &v : m_s)
		v = SplitMix(seed);
}

uint64_t Rng::Next() noexcept {
	auto rotl = [](uint64_t x, int k) { return (x << k) | (x >> (64 - k)); };
	const uint64_t result = rotl(m_s[1] * 5, 7) * 9;
	const uint64_t t = m_s[1] << 17;
	m_s[2] ^= m_s[0];
	m_s[3] ^= m_s[1];
	m_s[1] ^= m_s[2];
	m_s[0] ^= m_s[3];
	m_s[2] ^= t;
	m_s[3] = rotl(m_s[3], 45);
	return result;
}

} // namespace math
