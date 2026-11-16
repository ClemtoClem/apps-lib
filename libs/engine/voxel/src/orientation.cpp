#include "voxel/orientation.hpp"

#include <cmath>
#include <cstring>

namespace voxel {

math::FVector3 RotateOrient(math::FVector3 p, BlockOrient o) noexcept {
	const int up = o >> 2, yaw = o & 3;
	// 1) Cap : rotation autour de Y (0 = identité ; le front −Z va vers est/sud/ouest).
	switch (yaw) {
	case 1: p = {-p.z, p.y, p.x}; break;
	case 2: p = {-p.x, p.y, -p.z}; break;
	case 3: p = {p.z, p.y, -p.x}; break;
	default: break;
	}
	// 2) « Haut » : +Y → kOrientUp[up].
	switch (up) {
	case 0: break;						 // +Y
	case 1: p = {p.x, -p.y, -p.z}; break; // −Y : RotX(180)
	case 2: p = {p.x, -p.z, p.y}; break;	 // +Z : RotX(+90)
	case 3: p = {p.x, p.z, -p.y}; break;	 // −Z : RotX(−90)
	case 4: p = {p.y, -p.x, p.z}; break;	 // +X : RotZ(−90)
	case 5: p = {-p.y, p.x, p.z}; break;	 // −X : RotZ(+90)
	default: break;
	}
	return p;
}

int OrientFace(BlockOrient o, int face) noexcept {
	// Face locale dont la normale, après rotation, devient la normale du monde `face`.
	const IVec3 n = kFaceNormals[face];
	for (int lf = 0; lf < 6; ++lf) {
		const math::FVector3 r = RotateOrient({float(kFaceNormals[lf].x), float(kFaceNormals[lf].y), float(kFaceNormals[lf].z)}, o);
		if (int(std::lround(r.x)) == n.x && int(std::lround(r.y)) == n.y && int(std::lround(r.z)) == n.z)
			return lf;
	}
	return face;
}

BlockOrient OrientFromAxis(int axis) noexcept {
	// « haut » local +Y → monde : axis Y = +Y, axis X = +X, axis Z = +Z.
	switch (axis) {
	case 0: return BlockOrient(4 * 4); // up = +X
	case 1: return BlockOrient(0);	  // up = +Y
	case 2: return BlockOrient(2 * 4); // up = +Z
	default: return BlockOrient(0);
	}
}

int YawFromDirection(float dx, float dz) noexcept {
	// Cap du bloc = direction vers laquelle sa face avant (−Z) doit pointer.
	// 0 = −Z (nord), 1 = +X (est), 2 = +Z (sud), 3 = −X (ouest).
	const float ax = std::fabs(dx), az = std::fabs(dz);
	if (ax > az)
		return dx > 0.f ? 1 : 3;
	return dz > 0.f ? 2 : 0;
}

std::optional<BlockOrient> OrientFromName(std::string_view name) noexcept {
	auto eq = [&](std::string_view v) { return name == v; };
	if (eq("north") || eq("n") || eq("nord"))
		return BlockOrient(0);
	if (eq("east") || eq("e") || eq("est"))
		return BlockOrient(1);
	if (eq("south") || eq("s") || eq("sud"))
		return BlockOrient(2);
	if (eq("west") || eq("w") || eq("ouest"))
		return BlockOrient(3);
	if (eq("top") || eq("up") || eq("haut"))
		return BlockOrient(0);
	if (eq("bottom") || eq("down") || eq("bas"))
		return BlockOrient(1 * 4);
	if (eq("axis=x") || eq("x"))
		return OrientFromAxis(0);
	if (eq("axis=y") || eq("y"))
		return OrientFromAxis(1);
	if (eq("axis=z") || eq("z"))
		return OrientFromAxis(2);
	return std::nullopt;
}

BlockOrient OrientFromAngles(math::FVector3 deg) noexcept {
	// Cas courants : rotation autour d'un seul axe, en multiples de 90°.
	const auto snap = [](float v) { return int(std::lround(v / 90.f)) & 3; };
	const int rx = snap(deg.x), ry = snap(deg.y), rz = snap(deg.z);
	if (rx != 0 && ry == 0 && rz == 0) {
		// Rotation autour de X : le « haut » bascule vers ±Z.
		if (rx == 1)
			return BlockOrient(2 * 4);
		if (rx == 3)
			return BlockOrient(3 * 4);
		return BlockOrient(1 * 4); // 180°
	}
	if (rx == 0 && ry == 0 && rz != 0) {
		if (rz == 1)
			return BlockOrient(4 * 4);
		if (rz == 3)
			return BlockOrient(5 * 4);
		return BlockOrient(1 * 4);
	}
	return BlockOrient(ry & 3); // cap seul
}

const char *OrientName(BlockOrient o) noexcept {
	const int up = o >> 2, yaw = o & 3;
	if (up == 0) {
		static constexpr const char *kN[] = {"nord", "est", "sud", "ouest"};
		return kN[yaw];
	}
	static constexpr const char *kU[] = {"haut", "bas", "axe z", "axe z", "axe x", "axe x"};
	(void)up;
	return kU[up];
}

} // namespace voxel
