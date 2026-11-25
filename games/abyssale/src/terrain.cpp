#include "abyssale/terrain.hpp"

#include <algorithm>
#include <cmath>

#include <math/noise.hpp>

#include "abyssale/config.hpp"

namespace abyssale {

namespace {

constexpr float kFloorY = -38.f;	///< fond des cuvettes
constexpr float kPlateauY = -14.f;	///< dessus du plateau
constexpr float kBoundaryY = -0.5f; ///< sommet des falaises (au ras de la surface)
constexpr float kWallWidth = 16.f;	///< largeur de la bande de falaise le long d'une arête fermée
constexpr float kNotchHalfWidth = 11.f; ///< demi-largeur de l'encoche d'un tunnel
constexpr float kNotchHeight = 5.f;	 ///< hauteur de la paroi basse dans l'encoche (m)

float Smoothstep(float a, float b, float x) {
	x = std::clamp((x - a) / (b - a), 0.f, 1.f);
	return x * x * (3.f - 2.f * x);
}

/// Distance d'un point au segment [a, b].
float SegmentDistance(math::FVector2 p, math::FVector2 a, math::FVector2 b) {
	const math::FVector2 ab = b - a, ap = p - a;
	const float t = std::clamp(math::Dot(ap, ab) / std::max(math::Dot(ab, ab), 1e-6f), 0.f, 1.f);
	return math::Length(ap - ab * t);
}

/// Couloirs de la carte d'entrée (indices dans landmarks::kAll).
constexpr std::array<std::array<int, 2>, 9> kEntryCorridors{{{0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {1, 3}, {2, 4}, {5, 2}, {5, 3}}};

/// Lieux (repère local) des templates autres que la carte d'entrée.
constexpr std::array<Landmark, 1> kCoralBarrier{{{{-6.f, 2.f}, 30.f}}};
constexpr std::array<Landmark, 1> kKelpForest{{{{0.f, 0.f}, 58.f}}};
constexpr std::array<Landmark, 3> kHydrothermal{{{{0.f, 0.f}, 22.f}, {{38.f, 28.f}, 16.f}, {{-36.f, -30.f}, 16.f}}};
constexpr std::array<Landmark, 1> kWhaleGraveyard{{{{0.f, 0.f}, 50.f}}};
constexpr std::array<Landmark, 1> kShipwreck{{{{0.f, 0.f}, 34.f}}};
constexpr std::array<Landmark, 3> kStatueGarden{{{{0.f, 0.f}, 32.f}, {{32.f, 26.f}, 16.f}, {{-30.f, -26.f}, 16.f}}};
constexpr std::array<Landmark, 1> kRayGathering{{{{0.f, 0.f}, 56.f}}};
constexpr std::array<Landmark, 1> kBrineLake{{{{0.f, 0.f}, 26.f}}};
constexpr std::array<Landmark, 0> kOpenOcean{};

/// Densité du tapis magenta par template (0 : roche nue, 1 : jardin dense).
constexpr float CoverZone(ChunkKind kind) noexcept {
	switch (kind) {
	case ChunkKind::Entry: return 0.55f;
	case ChunkKind::CoralBarrier: return 0.4f;
	case ChunkKind::KelpForest: return 0.3f;
	case ChunkKind::WhaleGraveyard: return 0.35f;
	case ChunkKind::Shipwreck: return 0.3f;
	case ChunkKind::StatueGarden: return 0.5f;
	case ChunkKind::RayGathering: return 0.2f;
	case ChunkKind::BrineLake: return 0.05f;
	case ChunkKind::Hydrothermal: return 0.05f;
	default: return 0.12f;
	}
}

} // namespace

std::span<const Landmark> LandmarksFor(ChunkKind kind) {
	switch (kind) {
	case ChunkKind::Entry: return landmarks::kAll;
	case ChunkKind::CoralBarrier: return kCoralBarrier;
	case ChunkKind::KelpForest: return kKelpForest;
	case ChunkKind::Hydrothermal: return kHydrothermal;
	case ChunkKind::WhaleGraveyard: return kWhaleGraveyard;
	case ChunkKind::Shipwreck: return kShipwreck;
	case ChunkKind::StatueGarden: return kStatueGarden;
	case ChunkKind::RayGathering: return kRayGathering;
	case ChunkKind::BrineLake: return kBrineLake;
	default: return kOpenOcean;
	}
}

float Terrain::Step() noexcept { return 2.f * kHalfExtent / float(kGrid - 1); }

int32_t Terrain::ChunkOf(float x) noexcept { return int32_t(std::floor((x + kHalfExtent) / kChunkSize)); }

Terrain::Terrain(uint64_t seed) : m_seed(seed), m_layout(seed), m_noise(seed) {}
Terrain::Terrain(uint64_t seed, const Layout &layout) : m_seed(seed), m_layout(layout), m_noise(seed) {}

float Terrain::CanyonOpenness(float x, float z) const noexcept {
	const int32_t cx = ChunkOf(x), cz = ChunkOf(z);
	const ChunkInfo info = m_layout.At(cx, cz);
	const float cx0 = float(cx) * kChunkSize, cz0 = float(cz) * kChunkSize; // centre du chunk
	const float lx = x - cx0, lz = z - cz0;
	const math::FVector2 q{lx + m_noise.Fbm2(x * 0.03f, z * 0.03f, 3) * 9.f, lz + m_noise.Fbm2(x * 0.03f + 40.f, z * 0.03f - 17.f, 3) * 9.f};
	float open = 0.f;
	for (const Landmark &l : LandmarksFor(info.kind))
		open = std::max(open, Smoothstep(l.radius + 7.f, l.radius - 3.f, std::hypot(q.x - l.center.x, q.y - l.center.y)));
	if (info.kind == ChunkKind::Entry)
		for (const auto &c : kEntryCorridors) {
			const Landmark &a = landmarks::kAll[size_t(c[0])], &b = landmarks::kAll[size_t(c[1])];
			const float width = 8.f + 4.f * m_noise.Noise2(x * 0.02f + 3.f, z * 0.02f);
			open = std::max(open, Smoothstep(width + 5.f, width - 2.f, SegmentDistance(q, a.center, b.center)));
		}
	const float bx = 1.f - Smoothstep(kHalfExtent * 0.72f, kHalfExtent, std::abs(lx));
	const float bz = 1.f - Smoothstep(kHalfExtent * 0.72f, kHalfExtent, std::abs(lz));
	return open * std::min(bx, bz);
}

float Terrain::FloorHeightAt(float x, float z, float open) const noexcept {
	const float floorY = kFloorY + m_noise.Fbm2(x * 0.014f, z * 0.014f, 4) * 4.f + m_noise.Fbm2(x * 0.07f, z * 0.07f, 3) * 0.8f;
	const float plateauY = kPlateauY + m_noise.Fbm2(x * 0.02f + 9.f, z * 0.02f, 5) * 7.f + m_noise.Ridged2(x * 0.05f, z * 0.05f, 3) * 4.f;
	return floorY + (plateauY - floorY) * (1.f - open);
}

float Terrain::HeightWithWalls(float x, float z, float floor) const noexcept {
	const int32_t cx = ChunkOf(x), cz = ChunkOf(z);
	const ChunkInfo info = m_layout.At(cx, cz);
	const float minX = ChunkOrigin(cx), minZ = ChunkOrigin(cz);
	const float centerX = minX + kHalfExtent, centerZ = minZ + kHalfExtent;
	float h = floor;
	for (const Edge e : {Edge::North, Edge::South, Edge::West, Edge::East}) {
		const EdgeState s = EdgeAt(info.edges, e);
		if (s == EdgeState::Open)
			continue;
		float d, t;
		switch (e) {
		case Edge::North: d = z - minZ; t = x - centerX; break;
		case Edge::South: d = minZ + kChunkSize - z; t = x - centerX; break;
		case Edge::West: d = x - minX; t = z - centerZ; break;
		default: d = minX + kChunkSize - x; t = z - centerZ; break;
		}
		if (d < 0.f)
			continue;
		const float w = 1.f - Smoothstep(0.f, kWallWidth, d);
		if (w <= 0.f)
			continue;
		float top = kBoundaryY;
		if (s == EdgeState::Tunnel) {
			const float n = Smoothstep(kNotchHalfWidth, kNotchHalfWidth - 3.f, std::abs(t));
			top = math::Lerp(kBoundaryY, floor + kNotchHeight, n);
		}
		h = std::max(h, floor + (top - floor) * w);
	}
	return h;
}

float Terrain::CoverAt(float x, float z, float open) const noexcept {
	const int32_t cx = ChunkOf(x), cz = ChunkOf(z);
	const float zone = CoverZone(m_layout.At(cx, cz).kind);
	const float patches = Smoothstep(-0.25f, 0.35f, m_noise.Fbm2(x * 0.09f + 70.f, z * 0.09f - 30.f, 4));
	return zone * patches * open;
}

const Terrain::ChunkGrid &Terrain::EnsureChunk(int32_t cx, int32_t cz) const {
	const ChunkKey key = MakeChunkKey(cx, cz);
	{
		std::scoped_lock lock(m_cacheMutex);
		if (const auto it = m_cache.find(key); it != m_cache.end())
			return *it->second;
	}
	auto grid = std::make_shared<ChunkGrid>();
	grid->height.resize(size_t(kGrid) * kGrid);
	grid->open.resize(grid->height.size());
	grid->cover.resize(grid->height.size());
	const float originX = ChunkOrigin(cx), originZ = ChunkOrigin(cz);
	const float step = Step();
	for (int gz = 0; gz < kGrid; ++gz)
		for (int gx = 0; gx < kGrid; ++gx) {
			const float x = originX + float(gx) * step, z = originZ + float(gz) * step;
			const float open = CanyonOpenness(x, z);
			const float floor = FloorHeightAt(x, z, open);
			const size_t i = size_t(gz) * kGrid + size_t(gx);
			grid->open[i] = open;
			grid->height[i] = HeightWithWalls(x, z, floor);
			grid->cover[i] = CoverAt(x, z, open);
		}
	std::scoped_lock lock(m_cacheMutex);
	return *m_cache.emplace(key, std::move(grid)).first->second;
}

float Terrain::Sample(const std::vector<float> &grid, float x, float z) const noexcept {
	const float gx = std::clamp(x / Step(), 0.f, float(kGrid - 1) - 1e-3f);
	const float gz = std::clamp(z / Step(), 0.f, float(kGrid - 1) - 1e-3f);
	const int ix = int(gx), iz = int(gz);
	const float fx = gx - float(ix), fz = gz - float(iz);
	const auto at = [&](int a, int b) { return grid[size_t(b) * kGrid + size_t(a)]; };
	return (at(ix, iz) * (1.f - fx) + at(ix + 1, iz) * fx) * (1.f - fz) + (at(ix, iz + 1) * (1.f - fx) + at(ix + 1, iz + 1) * fx) * fz;
}

void Terrain::EvictChunk(int32_t cx, int32_t cz) {
	std::scoped_lock lock(m_cacheMutex);
	m_cache.erase(MakeChunkKey(cx, cz));
}

float Terrain::Height(float x, float z) const noexcept {
	const int32_t cx = ChunkOf(x), cz = ChunkOf(z);
	return Sample(EnsureChunk(cx, cz).height, x - ChunkOrigin(cx), z - ChunkOrigin(cz));
}

float Terrain::FloorHeight(float x, float z) const noexcept { return FloorHeightAt(x, z, CanyonOpenness(x, z)); }

float Terrain::Openness(float x, float z) const noexcept {
	const int32_t cx = ChunkOf(x), cz = ChunkOf(z);
	return Sample(EnsureChunk(cx, cz).open, x - ChunkOrigin(cx), z - ChunkOrigin(cz));
}

float Terrain::Cover(float x, float z) const noexcept {
	const int32_t cx = ChunkOf(x), cz = ChunkOf(z);
	return Sample(EnsureChunk(cx, cz).cover, x - ChunkOrigin(cx), z - ChunkOrigin(cz));
}

math::FVector3 Terrain::Normal(float x, float z) const noexcept {
	const float e = Step();
	return math::FVector3{Height(x - e, z) - Height(x + e, z), 2.f * e, Height(x, z - e) - Height(x, z + e)}.Normalized();
}

sdl3::MeshData Terrain::BuildChunkMesh(int32_t cx, int32_t cz) const {
	const ChunkGrid &g = EnsureChunk(cx, cz);
	const float originX = ChunkOrigin(cx), originZ = ChunkOrigin(cz);
	const float step = Step();
	sdl3::MeshData m;
	m.vertices.reserve(g.height.size());
	for (int gz = 0; gz < kGrid; ++gz)
		for (int gx = 0; gx < kGrid; ++gx) {
			const int lx = std::clamp(gx - 1, 0, kGrid - 1), rx = std::clamp(gx + 1, 0, kGrid - 1);
			const int lz = std::clamp(gz - 1, 0, kGrid - 1), rz = std::clamp(gz + 1, 0, kGrid - 1);
			const float x = originX + float(gx) * step, z = originZ + float(gz) * step;
			const size_t i = size_t(gz) * kGrid + size_t(gx);
			const math::FVector3 normal{g.height[size_t(gz) * kGrid + size_t(lx)] - g.height[size_t(gz) * kGrid + size_t(rx)], 2.f * step,
									   g.height[size_t(lz) * kGrid + size_t(gx)] - g.height[size_t(rz) * kGrid + size_t(gx)]};
			m.vertices.push_back({{x, g.height[i], z}, normal.Normalized(), {g.cover[i], 0.f}});
		}
	m.indices.reserve(size_t(kGrid - 1) * (kGrid - 1) * 6);
	for (int gz = 0; gz + 1 < kGrid; ++gz)
		for (int gx = 0; gx + 1 < kGrid; ++gx) {
			const uint32_t a = uint32_t(gz * kGrid + gx), b = a + 1, c = a + uint32_t(kGrid), d = c + 1;
			if (std::abs(m.vertices[a].position.y - m.vertices[d].position.y) < std::abs(m.vertices[b].position.y - m.vertices[c].position.y))
				m.indices.insert(m.indices.end(), {a, c, d, a, d, b});
			else
				m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
		}
	return m;
}

std::vector<math::FVector3> Terrain::BuildChunkCollision(int32_t cx, int32_t cz) const {
	const int g = kCollisionGrid;
	const float originX = ChunkOrigin(cx), originZ = ChunkOrigin(cz);
	const float step = kChunkSize / float(g - 1);
	std::vector<float> height(size_t(g) * g);
	for (int gz = 0; gz < g; ++gz)
		for (int gx = 0; gx < g; ++gx)
			height[size_t(gz) * g + size_t(gx)] = Height(originX + float(gx) * step, originZ + float(gz) * step);
	std::vector<math::FVector3> out;
	out.reserve(size_t(g - 1) * (g - 1) * 6);
	for (int gz = 0; gz + 1 < g; ++gz)
		for (int gx = 0; gx + 1 < g; ++gx) {
			const math::FVector3 a{originX + float(gx) * step, height[size_t(gz) * g + size_t(gx)], originZ + float(gz) * step};
			const math::FVector3 b{originX + float(gx + 1) * step, height[size_t(gz) * g + size_t(gx + 1)], originZ + float(gz) * step};
			const math::FVector3 c{originX + float(gx) * step, height[size_t(gz + 1) * g + size_t(gx)], originZ + float(gz + 1) * step};
			const math::FVector3 d{originX + float(gx + 1) * step, height[size_t(gz + 1) * g + size_t(gx + 1)], originZ + float(gz + 1) * step};
			out.insert(out.end(), {a, c, b, b, c, d});
		}
	return out;
}

} // namespace abyssale
