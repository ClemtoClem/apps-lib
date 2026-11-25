#include "abyssale/textures.hpp"

#include "abyssale/config.hpp"

#include <algorithm>
#include <cmath>

#include <math/noise.hpp>

namespace abyssale {

namespace {

/// fBm tuilable sur [0, 1)² : mélange des quatre copies décalées d'une période.
float TileFbm(const math::SimplexNoise &n, float u, float v, float freq, int octaves) {
	const auto f = [&](float x, float y) { return n.Fbm2(x * freq, y * freq, octaves); };
	return f(u, v) * (1.f - u) * (1.f - v) + f(u - 1.f, v) * u * (1.f - v) + f(u, v - 1.f) * (1.f - u) * v + f(u - 1.f, v - 1.f) * u * v;
}

/// Cellules tuilables (distance au germe le plus proche, F1) sur une grille `cells` × `cells`.
float TileCells(float u, float v, int cells, uint64_t seed) {
	const float x = u * float(cells), y = v * float(cells);
	const int cx = int(std::floor(x)), cy = int(std::floor(y));
	float best = 9.f;
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx) {
			const int gx = cx + dx, gy = cy + dy;
			const int wx = ((gx % cells) + cells) % cells, wy = ((gy % cells) + cells) % cells;
			const float px = float(gx) + math::Hash01(wx, wy, 0, seed), py = float(gy) + math::Hash01(wx, wy, 1, seed);
			best = std::min(best, std::hypot(px - x, py - y));
		}
	return best;
}

uint8_t Byte(float v) { return uint8_t(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }

void Put(Image &img, uint32_t x, uint32_t y, float r, float g, float b, float a = 1.f) {
	uint8_t *p = &img.rgba[(size_t(y) * img.width + x) * 4];
	p[0] = Byte(r);
	p[1] = Byte(g);
	p[2] = Byte(b);
	p[3] = Byte(a);
}

Image Blank(uint32_t w, uint32_t h) {
	Image img;
	img.width = w;
	img.height = h;
	img.rgba.assign(size_t(w) * h * 4, 255);
	return img;
}

/// Roche : strates déformées, fissures fines (crêtes de bruit), plusieurs échelles de grain,
/// concrétions pâles et plaques de mousse — aucun motif cellulaire répétitif.
void RockPixel(const math::SimplexNoise &n, float u, float v, float &r, float &g, float &b) {
	const float warp = TileFbm(n, u + 0.13f, v + 0.71f, 3.f, 4) * 0.35f;
	const float strata = 0.5f + 0.5f * std::sin((v * 9.f + warp * 3.f) * kTau);
	const float large = 0.5f + 0.5f * TileFbm(n, u, v, 4.f, 5);
	const float medium = 0.5f + 0.5f * TileFbm(n, u + 0.37f, v + 0.11f, 13.f, 4);
	const float fine = 0.5f + 0.5f * TileFbm(n, u + 0.61f, v + 0.43f, 41.f, 2);
	// Fissures : là où un bruit tuilable passe par zéro, trait fin et sombre.
	const float c1 = std::abs(TileFbm(n, u + 0.2f, v + 0.9f, 7.f, 3));
	const float crack = std::clamp(c1 * 14.f, 0.f, 1.f);
	const float crust = std::clamp((TileFbm(n, u + 0.8f, v + 0.3f, 9.f, 3) - 0.3f) * 3.f, 0.f, 1.f);
	const float moss = std::clamp((TileFbm(n, u + 0.6f, v + 0.2f, 3.f, 4) - 0.1f) * 2.5f, 0.f, 1.f);
	float k = (0.42f + 0.3f * large + 0.12f * strata) * (0.78f + 0.22f * medium) * (0.88f + 0.12f * fine) * (0.55f + 0.45f * crack);
	r = k * 0.78f;
	g = k * 0.82f;
	b = k * 0.84f;
	r += crust * 0.12f;
	g += crust * 0.12f;
	b += crust * 0.1f;
	r = r * (1.f - moss * 0.4f) + moss * 0.16f * k;
	g = g * (1.f - moss * 0.2f) + moss * 0.3f * k;
	b = b * (1.f - moss * 0.5f) + moss * 0.14f * k;
}

} // namespace

Image TerrainAtlas(uint32_t tile) {
	math::SimplexNoise n(911);
	Image img = Blank(tile * 2, tile);
	for (uint32_t y = 0; y < tile; ++y)
		for (uint32_t x = 0; x < tile; ++x) {
			const float u = (float(x) + 0.5f) / float(tile), v = (float(y) + 0.5f) / float(tile);
			// Sable : rides parallèles déformées + grains + variations douces.
			const float warp = TileFbm(n, u, v, 3.f, 3) * 0.6f;
			const float ripples = 0.5f + 0.5f * std::sin((v * 14.f + warp * 4.f) * kTau);
			const float grain = 0.5f + 0.5f * TileFbm(n, u + 0.3f, v + 0.7f, 64.f, 2);
			const float patch = 0.5f + 0.5f * TileFbm(n, u + 0.9f, v + 0.4f, 2.f, 3);
			const float s = 0.62f + 0.14f * ripples + 0.12f * grain + 0.12f * patch;
			Put(img, x, y, s * 0.86f, s * 0.84f, s * 0.74f);
			float r, g, b;
			RockPixel(n, u, v, r, g, b);
			Put(img, x + tile, y, r, g, b);
		}
	return img;
}

Image RockTexture(uint32_t size) {
	math::SimplexNoise n(912);
	Image img = Blank(size, size);
	for (uint32_t y = 0; y < size; ++y)
		for (uint32_t x = 0; x < size; ++x) {
			float r, g, b;
			RockPixel(n, (float(x) + 0.5f) / float(size), (float(y) + 0.5f) / float(size), r, g, b);
			Put(img, x, y, r, g, b);
		}
	return img;
}

Image LeafTexture(uint32_t w, uint32_t h) {
	math::SimplexNoise n(913);
	Image img = Blank(w, h);
	for (uint32_t y = 0; y < h; ++y)
		for (uint32_t x = 0; x < w; ++x) {
			const float u = (float(x) + 0.5f) / float(w), v = (float(y) + 0.5f) / float(h);
			const float across = std::abs(u - 0.5f) * 2.f; // 0 : nervure, 1 : bord
			const float rib = std::exp(-across * across * 60.f);
			const float edge = std::pow(across, 6.f);
			const float mottle = 0.5f + 0.5f * n.Fbm2(u * 3.f, v * 8.f, 4);
			const float k = 0.72f + 0.28f * mottle;
			float r = 0.32f * k, g = 0.52f * k, b = 0.26f * k;
			r += rib * 0.18f + edge * 0.55f;
			g += rib * 0.2f + edge * 0.42f;
			b += rib * 0.06f + edge * 0.05f;
			Put(img, x, y, r, g, b);
		}
	return img;
}

Image SpongeTexture(uint32_t size) {
	math::SimplexNoise n(914);
	Image img = Blank(size, size);
	for (uint32_t y = 0; y < size; ++y)
		for (uint32_t x = 0; x < size; ++x) {
			const float u = (float(x) + 0.5f) / float(size), v = (float(y) + 0.5f) / float(size);
			const float pores = std::clamp(TileCells(u, v, 34, 31) * 2.4f, 0.f, 1.f);
			const float bumps = 0.5f + 0.5f * TileFbm(n, u, v, 10.f, 4);
			const float k = (0.62f + 0.38f * pores) * (0.7f + 0.3f * bumps);
			Put(img, x, y, k, k * 0.97f, k * 0.93f);
		}
	return img;
}

Image WoodTexture(uint32_t size) {
	math::SimplexNoise n(915);
	Image img = Blank(size, size);
	for (uint32_t y = 0; y < size; ++y)
		for (uint32_t x = 0; x < size; ++x) {
			const float u = (float(x) + 0.5f) / float(size), v = (float(y) + 0.5f) / float(size);
			const float grain = 0.5f + 0.5f * std::sin((u * 22.f + TileFbm(n, u, v, 3.f, 3) * 2.5f) * kTau);
			const float planks = std::abs(std::sin(v * 8.f * 3.1415927f));
			const float rot = std::clamp(TileFbm(n, u + 0.5f, v, 5.f, 4) * 1.6f, 0.f, 1.f);
			const float k = (0.55f + 0.25f * grain) * (0.55f + 0.45f * std::pow(planks, 0.15f)) * (1.f - rot * 0.45f);
			// Algues encroûtantes et concrétions pâles.
			const float algae = std::clamp((TileFbm(n, u + 0.2f, v + 0.8f, 4.f, 4) - 0.1f) * 2.5f, 0.f, 1.f);
			const float crust = std::clamp((TileFbm(n, u + 0.7f, v + 0.1f, 9.f, 3) - 0.35f) * 4.f, 0.f, 1.f);
			float r = k * 0.62f, g = k * 0.56f, b = k * 0.48f;
			r = r * (1.f - algae * 0.5f) + algae * 0.16f;
			g = g * (1.f - algae * 0.3f) + algae * 0.28f;
			b = b * (1.f - algae * 0.5f) + algae * 0.14f;
			r += crust * 0.25f;
			g += crust * 0.25f;
			b += crust * 0.22f;
			Put(img, x, y, r, g, b);
		}
	return img;
}

Image MantaTexture(uint32_t w, uint32_t h) {
	math::SimplexNoise n(916);
	Image img = Blank(w, h);
	for (uint32_t y = 0; y < h; ++y)
		for (uint32_t x = 0; x < w; ++x) {
			const float u = (float(x) + 0.5f) / float(w), v = (float(y) + 0.5f) / float(h);
			if (v < 0.5f) { // dos : bleu-gris sombre, taches pâles près des épaules
				const float mottle = 0.5f + 0.5f * n.Fbm2(u * 9.f, v * 18.f, 4);
				const float spots = std::clamp((n.Noise2(u * 26.f, v * 52.f) - 0.55f) * 6.f, 0.f, 1.f) * std::clamp(1.f - std::abs(u - 0.5f) * 3.f, 0.f, 1.f);
				const float k = 0.22f + 0.1f * mottle;
				Put(img, x, y, k * 0.9f + spots * 0.4f, k + spots * 0.42f, k * 1.15f + spots * 0.42f);
			} else { // ventre : blanc crème, bords grisés
				const float edge = std::pow(std::abs(u - 0.5f) * 2.f, 3.f);
				const float k = 0.92f - 0.45f * edge + 0.05f * n.Fbm2(u * 7.f, v * 14.f, 3);
				Put(img, x, y, k, k * 0.98f, k * 0.95f);
			}
		}
	return img;
}

Image FishTexture(uint32_t w, uint32_t h) {
	math::SimplexNoise n(733);
	Image img = Blank(w, h);
	for (uint32_t y = 0; y < h; ++y)
		for (uint32_t x = 0; x < w; ++x) {
			const float u = (float(x) + 0.5f) / float(w), v = (float(y) + 0.5f) / float(h);
			// Hauteur sur le flanc : 1 au dos (v = 0,25), −1 au ventre (v = 0,75).
			const float side = std::sin(v * 6.2831853f);
			const float back = std::clamp((side - 0.25f) * 1.6f, 0.f, 1.f), belly = std::clamp((-side - 0.1f) * 1.4f, 0.f, 1.f);
			const float bars = 0.5f + 0.5f * std::sin(u * 42.f + side * 2.f);		// rayures sombres verticales sur le dos
			const float lateral = std::exp(-std::pow((side - 0.08f) * 14.f, 2.f)); // ligne latérale
			const float grain = 0.06f * n.Fbm2(u * 24.f, v * 24.f, 3);
			float r = 0.7f + grain, g = 0.76f + grain, b = 0.82f + grain;		 // flanc argenté
			r = r * (1.f - back) + (0.12f + 0.06f * bars) * back;				 // dos bleu sombre
			g = g * (1.f - back) + (0.22f + 0.08f * bars) * back;
			b = b * (1.f - back) + (0.34f + 0.08f * bars) * back;
			r = r * (1.f - belly) + 0.95f * belly;
			g = g * (1.f - belly) + 0.95f * belly;
			b = b * (1.f - belly) + 0.93f * belly;
			const float k = 1.f - 0.35f * lateral;
			const float head = std::clamp((0.18f - u) * 8.f, 0.f, 1.f) * 0.15f; // tête un peu plus sombre
			Put(img, x, y, r * k - head, g * k - head, b * k - head * 0.5f);
		}
	return img;
}

Image ShellTexture(uint32_t size) {
	math::SimplexNoise n(512);
	Image img = Blank(size, size);
	for (uint32_t y = 0; y < size; ++y)
		for (uint32_t x = 0; x < size; ++x) {
			const float u = (float(x) + 0.5f) / float(size), v = (float(y) + 0.5f) / float(size);
			const float mottle = 0.5f + 0.5f * TileFbm(n, u, v, 5.f, 4);
			const float pits = TileCells(u, v, 18, 77); // piqûres sombres
			const float k = 0.62f + 0.3f * mottle - 0.35f * std::clamp((0.18f - pits) * 6.f, 0.f, 1.f);
			Put(img, x, y, k, k * 0.96f, k * 0.92f);
		}
	return img;
}

Image ShaftTexture(uint32_t w, uint32_t h) {
	Image img = Blank(w, h);
	for (uint32_t y = 0; y < h; ++y)
		for (uint32_t x = 0; x < w; ++x) {
			const float u = (float(x) + 0.5f) / float(w), v = (float(y) + 0.5f) / float(h);
			const float across = std::sin(u * 3.1415927f);
			const float along = std::pow(1.f - v, 1.6f) * std::clamp(v * 12.f, 0.f, 1.f);
			const float k = across * across * along;
			Put(img, x, y, k, k, k, 1.f);
		}
	return img;
}

} // namespace abyssale
