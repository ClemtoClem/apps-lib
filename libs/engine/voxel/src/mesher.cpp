#include "voxel/mesher.hpp"

#include <cmath>
#include <cstring>

#include <diagnostics/trace.hpp>

#include "voxel/orientation.hpp"

namespace voxel {

std::shared_ptr<const MeshContext> MeshContext::Build(const BlockRegistry &reg) {
	auto ctx = std::make_shared<MeshContext>();
	const size_t n = reg.Count();
	ctx->opaque.resize(n);
	ctx->visible.resize(n);
	ctx->cross.resize(n);
	ctx->detail.resize(n);
	ctx->light.resize(n);
	ctx->material.resize(n);
	ctx->layers.resize(n);
	ctx->reflectivity.resize(n);
	for (size_t i = 0; i < n; ++i) {
		const BlockDef &d = reg.Get(static_cast<BlockId>(i));
		ctx->opaque[i] = d.IsOpaque();
		ctx->visible[i] = d.IsVisible();
		ctx->cross[i] = d.cross;
		ctx->detail[i] = d.HasShape();
		ctx->light[i] = d.light;
		ctx->material[i] = d.material;
		ctx->layers[i] = d.layers;
		ctx->reflectivity[i] = static_cast<uint8_t>(std::clamp(d.reflectivity, 0.f, 1.f) * 255.f);
	}
	ctx->boxes.resize(n);
	ctx->meshes.resize(n);
	ctx->connect.resize(n);
	ctx->connectsTo.resize(n);
	for (size_t i = 0; i < n; ++i) {
		const BlockDef &d = reg.Get(static_cast<BlockId>(i));
		ctx->boxes[i] = d.boxes;
		ctx->meshes[i] = d.mesh;
		ctx->connect[i] = d.connect;
		if (!d.connect.Empty()) {
			ctx->connectsTo[i].resize(n);
			for (size_t o = 0; o < n; ++o)
				ctx->connectsTo[i][o] = reg.Connects(d.connect, static_cast<BlockId>(o)) ? 1 : 0;
		}
	}
	return ctx;
}

void ShapeBoxes(const MeshContext &ctx, BlockId block, const std::array<BlockId, 6> &neighbors, std::vector<BlockBox> &out) {
	out = ctx.boxes[block];
	const std::vector<uint8_t> &to = ctx.connectsTo[block];
	if (to.empty())
		return;
	for (size_t f = 0; f < 6; ++f)
		if (neighbors[f] < to.size() && to[neighbors[f]])
			out.insert(out.end(), ctx.connect[block].faces[f].begin(), ctx.connect[block].faces[f].end());
}

namespace {

struct FaceKey {
	uint16_t layer;
	uint8_t ao; // 4 × 2 bits
	uint8_t light;
	uint8_t reflect; // réflectance 0..255
	uint8_t valid; // 0 = aucune face, 1 = opaque/découpé, 2 = translucide
	bool operator==(const FaceKey &) const = default;
};

inline int Ao(bool s1, bool s2, bool c) noexcept { return (s1 && s2) ? 0 : 3 - (int(s1) + int(s2) + int(c)); }

void EmitQuad(std::vector<ChunkVertex> &out, const int corners[4][3], int face, const FaceKey &k, int texS, int texT) {
	const int ao[4] = {k.ao & 3, (k.ao >> 2) & 3, (k.ao >> 4) & 3, (k.ao >> 6) & 3};
	// Diagonale la plus lumineuse : évite les dégradés asymétriques de l'AO.
	const int start = (ao[0] + ao[2] < ao[1] + ao[3]) ? 1 : 0;
	for (int n = 0; n < 4; ++n) {
		const int c = (n + start) & 3;
		ChunkVertex v{};
		v.x = static_cast<uint8_t>(corners[c][0]);
		v.y = static_cast<uint8_t>(corners[c][1]);
		v.z = static_cast<uint8_t>(corners[c][2]);
		v.face = static_cast<uint8_t>(face);
		v.u = static_cast<uint8_t>(corners[c][texS]);
		v.v = static_cast<uint8_t>(kChunkSize - corners[c][texT]);
		v.ao = static_cast<uint8_t>(ao[c]);
		v.light = k.light;
		v.layer = k.layer;
		v.pad = k.reflect;
		out.push_back(v);
	}
}

void EmitCross(std::vector<ChunkVertex> &out, int x, int y, int z, uint16_t layer, uint8_t light) {
	// Deux plans diagonaux, chacun dans les deux sens (pas de suppression des faces arrière).
	const int quads[4][4][3] = {
		{{x, y, z}, {x + 1, y, z + 1}, {x + 1, y + 1, z + 1}, {x, y + 1, z}},
		{{x + 1, y, z + 1}, {x, y, z}, {x, y + 1, z}, {x + 1, y + 1, z + 1}},
		{{x + 1, y, z}, {x, y, z + 1}, {x, y + 1, z + 1}, {x + 1, y + 1, z}},
		{{x, y, z + 1}, {x + 1, y, z}, {x + 1, y + 1, z}, {x, y + 1, z + 1}},
	};
	static constexpr uint8_t us[4] = {0, 1, 1, 0}, vs[4] = {1, 1, 0, 0};
	for (const auto &q : quads)
		for (int c = 0; c < 4; ++c)
			out.push_back(ChunkVertex{static_cast<uint8_t>(q[c][0]), static_cast<uint8_t>(q[c][1]), static_cast<uint8_t>(q[c][2]), 6,
									  us[c], vs[c], 3, light, layer, 0});
}

void PushDetail(std::vector<DetailVertex> &out, float x, float y, float z, float u, float v, uint16_t layer, int face, uint8_t light) {
	out.push_back({x, y, z, u, v, layer, uint16_t(face | (light << 8))});
}

/// Faces d'une boîte (centre du bloc cx, cy, cz) ; une face posée sur le bord du bloc est
/// omise quand le voisin de ce côté est un cube opaque. UV alignées sur le monde (comme
/// les cubes : la texture se prolonge d'un bloc à l'autre).
void EmitDetailBox(std::vector<DetailVertex> &out, float cx, float cy, float cz, const BlockBox &b, const std::array<BlockId, 6> &nb,
				   const MeshContext &ctx, const std::array<uint16_t, 6> &layers, uint8_t light) {
	const float x0 = cx + b[0], y0 = cy + b[1], z0 = cz + b[2], x1 = cx + b[3], y1 = cy + b[4], z1 = cz + b[5];
	const float edge[6] = {b[3], -b[0], b[4], -b[1], b[5], -b[2]}; // distance de la face au centre
	const float k = float(kChunkSize);
	for (int f = 0; f < 6; ++f) {
		if (edge[f] >= 0.4999f && nb[size_t(f)] < ctx.opaque.size() && ctx.opaque[nb[size_t(f)]])
			continue; // collée à un cube plein : invisible
		const uint16_t layer = layers[size_t(f)];
		// Coins dans l'ordre du contour, vus de l'extérieur (sens trigonométrique).
		float p[4][3];
		switch (f) {
		case 0: { const float q[4][3] = {{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}; std::memcpy(p, q, sizeof p); break; }
		case 1: { const float q[4][3] = {{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}; std::memcpy(p, q, sizeof p); break; }
		case 2: { const float q[4][3] = {{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}; std::memcpy(p, q, sizeof p); break; }
		case 3: { const float q[4][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}; std::memcpy(p, q, sizeof p); break; }
		case 4: { const float q[4][3] = {{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}; std::memcpy(p, q, sizeof p); break; }
		default: { const float q[4][3] = {{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}; std::memcpy(p, q, sizeof p); break; }
		}
		const int d = f >> 1;
		const int texS = d == 0 ? 2 : 0, texT = d == 1 ? 2 : 1;
		for (const auto &c : p)
			PushDetail(out, c[0], c[1], c[2], c[texS], k - c[texT], layer, f, light);
	}
}

void EmitDetailTriangle(std::vector<DetailVertex> &out, float cx, float cy, float cz, const BlockTriangle &t, uint16_t layer, uint8_t light) {
	// Face dominante (ombrage) d'après la normale.
	const float ax = t.position[3] - t.position[0], ay = t.position[4] - t.position[1], az = t.position[5] - t.position[2];
	const float bx = t.position[6] - t.position[0], by = t.position[7] - t.position[1], bz = t.position[8] - t.position[2];
	const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
	const float mx = std::fabs(nx), my = std::fabs(ny), mz = std::fabs(nz);
	const int face = my >= mx && my >= mz ? (ny > 0 ? 2 : 3) : mx >= mz ? (nx > 0 ? 0 : 1) : (nz > 0 ? 4 : 5);
	for (int k = 0; k < 4; ++k) {
		const int i = std::min(k, 2);
		PushDetail(out, cx + t.position[size_t(i * 3)], cy + t.position[size_t(i * 3 + 1)], cz + t.position[size_t(i * 3 + 2)], t.uv[size_t(i * 2)],
				   t.uv[size_t(i * 2 + 1)], layer, face, light);
	}
}

} // namespace

void BuildChunkMesh(std::span<const BlockId> pad, std::span<const uint8_t> orient, const MeshContext &ctx, MeshData &out) {
	DIAG_ZONE("Mesher::Build");
	out.opaque.clear();
	out.translucent.clear();
	const size_t known = ctx.visible.size();
	auto at = [&](int x, int y, int z) -> BlockId {
		const BlockId b = pad[static_cast<size_t>(PaddedIndex(x, y, z))];
		return b < known ? b : kAir; // bloc enregistré après la construction des tables
	};
	auto oat = [&](int x, int y, int z) -> uint8_t {
		return orient.empty() ? uint8_t{0} : orient[static_cast<size_t>(PaddedIndex(x, y, z))];
	};
	auto solid = [&](int x, int y, int z) { return ctx.opaque[at(x, y, z)] != 0; };

	FaceKey mask[kChunkArea];
	for (int face = 0; face < 6; ++face) {
		const int d = face >> 1;
		const int sign = (face & 1) ? -1 : 1;
		const int u = (d + 1) % 3, v = (d + 2) % 3;
		// Axes de texture : s horizontal, t vertical (Y vers le haut sur les côtés).
		const int texS = d == 0 ? 2 : 0;
		const int texT = d == 1 ? 2 : 1;
		for (int s = 0; s < kChunkSize; ++s) {
			// 1) Masque des faces visibles de la tranche.
			for (int j = 0; j < kChunkSize; ++j) {
				for (int i = 0; i < kChunkSize; ++i) {
					int p[3];
					p[d] = s;
					p[u] = i;
					p[v] = j;
					const BlockId a = at(p[0], p[1], p[2]);
					FaceKey &k = mask[i + j * kChunkSize];
					k = {};
					if (!ctx.visible[a] || ctx.cross[a] || ctx.detail[a])
						continue;
					int q[3] = {p[0], p[1], p[2]};
					q[d] += sign;
					const BlockId b = at(q[0], q[1], q[2]);
					if (ctx.opaque[b])
						continue;
					const Material ma = ctx.material[a];
					const bool transparentA = ma == Material::Translucent || ma == Material::Liquid;
					if (transparentA && b == a)
						continue; // pas de faces internes dans l'eau / le verre
					if (ma == Material::Cutout && b == a && ctx.material[b] == Material::Cutout && sign < 0)
						continue; // feuilles adjacentes : une seule face partagée
					int ao = 3 | (3 << 2) | (3 << 4) | (3 << 6);
					if (!transparentA) {
						auto side = [&](int du, int dv) {
							int r[3] = {q[0], q[1], q[2]};
							r[u] += du;
							r[v] += dv;
							return solid(r[0], r[1], r[2]);
						};
						const bool n0 = side(-1, 0), n1 = side(1, 0), n2 = side(0, -1), n3 = side(0, 1);
						const int a0 = Ao(n0, n2, side(-1, -1));
						const int a1 = Ao(n1, n2, side(1, -1));
						const int a2 = Ao(n1, n3, side(1, 1));
						const int a3 = Ao(n0, n3, side(-1, 1));
						ao = a0 | (a1 << 2) | (a2 << 4) | (a3 << 6);
					}
					k.layer = ctx.layers[a][static_cast<size_t>(OrientFace(oat(p[0], p[1], p[2]), face))];
					k.ao = static_cast<uint8_t>(ao);
					k.light = ctx.light[a];
					k.reflect = ctx.reflectivity[a];
					k.valid = transparentA ? 2 : 1;
				}
			}
			// 2) Fusion gloutonne des rectangles identiques.
			for (int j = 0; j < kChunkSize; ++j) {
				for (int i = 0; i < kChunkSize;) {
					const FaceKey k = mask[i + j * kChunkSize];
					if (!k.valid) {
						++i;
						continue;
					}
					int w = 1;
					while (i + w < kChunkSize && mask[i + w + j * kChunkSize] == k)
						++w;
					int h = 1;
					for (; j + h < kChunkSize; ++h) {
						bool row = true;
						for (int t = 0; t < w && row; ++t)
							row = mask[i + t + (j + h) * kChunkSize] == k;
						if (!row)
							break;
					}
					for (int y = 0; y < h; ++y)
						for (int x = 0; x < w; ++x)
							mask[i + x + (j + y) * kChunkSize].valid = 0;
					const int plane = s + (sign > 0 ? 1 : 0);
					int corners[4][3];
					const int cu[4] = {i, i + w, i + w, i}, cv[4] = {j, j, j + h, j + h};
					for (int c = 0; c < 4; ++c) {
						corners[c][d] = plane;
						corners[c][u] = cu[c];
						corners[c][v] = cv[c];
					}
					if (sign < 0) { // ordre inversé : face vue depuis -d
						std::swap(corners[1][0], corners[3][0]);
						std::swap(corners[1][1], corners[3][1]);
						std::swap(corners[1][2], corners[3][2]);
					}
					FaceKey ek = k;
					if (sign < 0) // l'AO suit les coins permutés
						ek.ao = static_cast<uint8_t>((k.ao & 3) | (((k.ao >> 6) & 3) << 2) | (((k.ao >> 4) & 3) << 4) | (((k.ao >> 2) & 3) << 6));
					EmitQuad(k.valid == 2 ? out.translucent : out.opaque, corners, face, ek, texS, texT);
					i += w;
				}
			}
		}
	}
	// Végétation en croix, formes détaillées (nodebox, connectivité, maillages).
	out.detail.clear();
	std::vector<BlockBox> boxes;
	for (int y = 0; y < kChunkSize; ++y)
		for (int z = 0; z < kChunkSize; ++z)
			for (int x = 0; x < kChunkSize; ++x) {
				const BlockId b = at(x, y, z);
				if (ctx.cross[b]) {
					EmitCross(out.opaque, x, y, z, ctx.layers[b][0], ctx.light[b]);
					continue;
				}
				if (!ctx.detail[b])
					continue;
				std::array<BlockId, 6> nb;
				for (size_t f = 0; f < 6; ++f)
					nb[f] = at(x + kFaceNormals[f].x, y + kFaceNormals[f].y, z + kFaceNormals[f].z);
				ShapeBoxes(ctx, b, nb, boxes);
				const float cx = float(x) + 0.5f, cy = float(y) + 0.5f, cz = float(z) + 0.5f;
				const uint8_t o = oat(x, y, z);
				for (const BlockBox &bx : boxes) {
					if (o == 0) {
						EmitDetailBox(out.detail, cx, cy, cz, bx, nb, ctx, ctx.layers[b], ctx.light[b]);
						continue;
					}
					float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
					for (int i = 0; i < 8; ++i) {
						const math::FVector3 p = RotateOrient({bx[i & 1 ? 3 : 0], bx[i & 2 ? 4 : 1], bx[i & 4 ? 5 : 2]}, o);
						mn[0] = std::min(mn[0], p.x); mn[1] = std::min(mn[1], p.y); mn[2] = std::min(mn[2], p.z);
						mx[0] = std::max(mx[0], p.x); mx[1] = std::max(mx[1], p.y); mx[2] = std::max(mx[2], p.z);
					}
					EmitDetailBox(out.detail, cx, cy, cz, BlockBox{mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]}, nb, ctx, ctx.layers[b], ctx.light[b]);
				}
				for (const BlockTriangle &t : ctx.meshes[b]) {
					if (o == 0) {
						EmitDetailTriangle(out.detail, cx, cy, cz, t, ctx.layers[b][0], ctx.light[b]);
						continue;
					}
					BlockTriangle rt = t;
					for (int k = 0; k < 4; ++k) {
						const int vi = std::min(k, 2);
						const math::FVector3 p = RotateOrient({t.position[size_t(vi * 3)], t.position[size_t(vi * 3 + 1)], t.position[size_t(vi * 3 + 2)]}, o);
						rt.position[size_t(vi * 3)] = p.x;
						rt.position[size_t(vi * 3 + 1)] = p.y;
						rt.position[size_t(vi * 3 + 2)] = p.z;
					}
					EmitDetailTriangle(out.detail, cx, cy, cz, rt, ctx.layers[b][0], ctx.light[b]);
				}
			}
}

} // namespace voxel
