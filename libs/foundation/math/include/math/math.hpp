#pragma once
/**
 * @file math.hpp
 * @brief Mathématiques de jeu constexpr : vecteurs 2/3/4 (float et int), matrice 4×4
 *        colonne-majeure (convention Vulkan/SDL_GPU : profondeur [0,1], Y clip vers le
 *        haut), quaternions, AABB, plans, frustum, rayons, et types d'UI (Corners, Sides).
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace math {

inline constexpr float PI = std::numbers::pi_v<float>;
inline constexpr float TAU = 2.0f * PI;
inline constexpr float DEG2RAD = PI / 180.0f;
inline constexpr float RAD2DEG = 180.0f / PI;

template <typename T> [[nodiscard]] constexpr T Clamp(T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> [[nodiscard]] constexpr T Lerp(T a, T b, float t) noexcept { return a + (b - a) * t; }
template <typename T> [[nodiscard]] constexpr T Min(T a, T b) noexcept { return b < a ? b : a; }
template <typename T> [[nodiscard]] constexpr T Max(T a, T b) noexcept { return a < b ? b : a; }
[[nodiscard]] constexpr float Saturate(float v) noexcept { return Clamp(v, 0.0f, 1.0f); }
[[nodiscard]] constexpr float SmoothStep(float e0, float e1, float x) noexcept {
	const float t = Saturate((x - e0) / (e1 - e0));
	return t * t * (3.0f - 2.0f * t);
}
[[nodiscard]] constexpr int FloorDiv(int a, int b) noexcept { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
[[nodiscard]] constexpr int FloorMod(int a, int b) noexcept { return a - FloorDiv(a, b) * b; }
[[nodiscard]] inline int FastFloor(float v) noexcept { return static_cast<int>(std::floor(v)); }

// ── Types d'UI (bords et coins) ───────────────────────────────────────────
struct Corners {
	float tl = 0.f, tr = 0.f, bl = 0.f, br = 0.f;
	constexpr Corners() = default;
	constexpr explicit Corners(float r) : tl(r), tr(r), bl(r), br(r) {}
	constexpr Corners(float a, float b, float c, float d) : tl(a), tr(b), bl(c), br(d) {}
	constexpr bool operator==(const Corners &) const = default;
};
struct Sides {
	float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;
	constexpr Sides() = default;
	constexpr explicit Sides(float w) : left(w), top(w), right(w), bottom(w) {}
	constexpr Sides(float h, float v) : left(h), top(v), right(h), bottom(v) {}
	constexpr Sides(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
	[[nodiscard]] constexpr float H() const noexcept { return left + right; }
	[[nodiscard]] constexpr float V() const noexcept { return top + bottom; }
	constexpr bool operator==(const Sides &) const = default;
};

// ── Vecteurs ───────────────────────────────────────────────────────────────
struct FVector2 {
	float x = 0.f, y = 0.f;
	constexpr FVector2() noexcept = default;
	constexpr FVector2(float x_, float y_) noexcept : x(x_), y(y_) {}
	constexpr explicit FVector2(float s) noexcept : x(s), y(s) {}
	[[nodiscard]] constexpr FVector2 operator+(FVector2 o) const noexcept { return {x + o.x, y + o.y}; }
	[[nodiscard]] constexpr FVector2 operator-(FVector2 o) const noexcept { return {x - o.x, y - o.y}; }
	[[nodiscard]] constexpr FVector2 operator*(float s) const noexcept { return {x * s, y * s}; }
	[[nodiscard]] constexpr FVector2 operator/(float s) const noexcept { return {x / s, y / s}; }
	[[nodiscard]] constexpr FVector2 operator-() const noexcept { return {-x, -y}; }
	constexpr FVector2 &operator+=(FVector2 o) noexcept { x += o.x; y += o.y; return *this; }
	constexpr FVector2 &operator-=(FVector2 o) noexcept { x -= o.x; y -= o.y; return *this; }
	constexpr FVector2 &operator*=(float s) noexcept { x *= s; y *= s; return *this; }
	[[nodiscard]] constexpr FVector2 operator*(FVector2 o) const noexcept { return {x * o.x, y * o.y}; }
	[[nodiscard]] constexpr FVector2 operator/(FVector2 o) const noexcept { return {x / o.x, y / o.y}; }
	constexpr FVector2 &operator/=(float s) noexcept { x /= s; y /= s; return *this; }
	constexpr FVector2 &operator*=(FVector2 o) noexcept { x *= o.x; y *= o.y; return *this; }
	constexpr FVector2 &operator/=(FVector2 o) noexcept { x /= o.x; y /= o.y; return *this; }
	[[nodiscard]] constexpr float operator[](int i) const noexcept { return i == 0 ? x : y; }
	constexpr float &operator[](int i) noexcept { return i == 0 ? x : y; }
	/// Perpendiculaire (rotation de +90°).
	[[nodiscard]] constexpr FVector2 Perp() const noexcept { return {-y, x}; }
	[[nodiscard]] constexpr float Dot(FVector2 o) const noexcept { return x * o.x + y * o.y; }
	/// Produit vectoriel 2D (composante z) : > 0 si `o` est à gauche.
	[[nodiscard]] constexpr float Cross(FVector2 o) const noexcept { return x * o.y - y * o.x; }
	[[nodiscard]] constexpr float LengthSq() const noexcept { return Dot(*this); }
	[[nodiscard]] float Length() const noexcept { return std::sqrt(Dot(*this)); }
	[[nodiscard]] constexpr float DistanceSq(FVector2 o) const noexcept { return (*this - o).LengthSq(); }
	[[nodiscard]] float Distance(FVector2 o) const noexcept { return (*this - o).Length(); }
	[[nodiscard]] FVector2 Normalized() const noexcept {
		const float l = Length();
		return l > 1e-12f ? *this / l : FVector2{};
	}
	constexpr bool operator==(const FVector2 &) const = default;
};

struct FVector3 {
	float x = 0.f, y = 0.f, z = 0.f;
	constexpr FVector3() noexcept = default;
	constexpr FVector3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}
	constexpr explicit FVector3(float s) noexcept : x(s), y(s), z(s) {}
	constexpr FVector3(FVector2 xy, float z_) noexcept : x(xy.x), y(xy.y), z(z_) {}
	[[nodiscard]] constexpr FVector2 Xy() const noexcept { return {x, y}; }
	[[nodiscard]] constexpr FVector3 operator+(const FVector3 &o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
	[[nodiscard]] constexpr FVector3 operator-(const FVector3 &o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
	[[nodiscard]] constexpr FVector3 operator*(const FVector3 &o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
	[[nodiscard]] constexpr FVector3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
	[[nodiscard]] constexpr FVector3 operator/(float s) const noexcept { return {x / s, y / s, z / s}; }
	[[nodiscard]] constexpr FVector3 operator/(const FVector3 &o) const noexcept { return {x / o.x, y / o.y, z / o.z}; }
	constexpr FVector3 &operator/=(float s) noexcept { x /= s; y /= s; z /= s; return *this; }
	constexpr FVector3 &operator*=(const FVector3 &o) noexcept { x *= o.x; y *= o.y; z *= o.z; return *this; }
	constexpr FVector3 &operator/=(const FVector3 &o) noexcept { x /= o.x; y /= o.y; z /= o.z; return *this; }
	[[nodiscard]] constexpr FVector3 operator-() const noexcept { return {-x, -y, -z}; }
	constexpr FVector3 &operator+=(const FVector3 &o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
	constexpr FVector3 &operator-=(const FVector3 &o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
	constexpr FVector3 &operator*=(float s) noexcept { x *= s; y *= s; z *= s; return *this; }
	[[nodiscard]] constexpr float operator[](int i) const noexcept { return i == 0 ? x : (i == 1 ? y : z); }
	constexpr float &operator[](int i) noexcept { return i == 0 ? x : (i == 1 ? y : z); }
	[[nodiscard]] constexpr float Dot(const FVector3 &o) const noexcept { return x * o.x + y * o.y + z * o.z; }
	[[nodiscard]] constexpr FVector3 Cross(const FVector3 &o) const noexcept {
		return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
	}
	[[nodiscard]] constexpr float LengthSq() const noexcept { return Dot(*this); }
	[[nodiscard]] float Length() const noexcept { return std::sqrt(LengthSq()); }
	[[nodiscard]] constexpr float DistanceSq(const FVector3 &o) const noexcept { return (*this - o).LengthSq(); }
	[[nodiscard]] float Distance(const FVector3 &o) const noexcept { return (*this - o).Length(); }
	[[nodiscard]] FVector3 Normalized() const noexcept {
		const float l = Length();
		return l > 1e-12f ? *this / l : FVector3{};
	}
	constexpr bool operator==(const FVector3 &) const = default;
};
[[nodiscard]] constexpr FVector3 operator*(float s, const FVector3 &v) noexcept { return v * s; }
[[nodiscard]] inline FVector3 Floor(const FVector3 &v) noexcept { return {std::floor(v.x), std::floor(v.y), std::floor(v.z)}; }
[[nodiscard]] constexpr FVector3 Min(const FVector3 &a, const FVector3 &b) noexcept {
	return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
[[nodiscard]] constexpr FVector3 Max(const FVector3 &a, const FVector3 &b) noexcept {
	return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

struct FVector4 {
	float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
	constexpr FVector4() noexcept = default;
	constexpr FVector4(float x_, float y_, float z_, float w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}
	constexpr FVector4(const FVector3 &v, float w_) noexcept : x(v.x), y(v.y), z(v.z), w(w_) {}
	[[nodiscard]] constexpr FVector3 Xyz() const noexcept { return {x, y, z}; }
	[[nodiscard]] constexpr float Dot(const FVector4 &o) const noexcept { return x * o.x + y * o.y + z * o.z + w * o.w; }
	[[nodiscard]] constexpr FVector4 operator+(const FVector4 &o) const noexcept { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
	[[nodiscard]] constexpr FVector4 operator-(const FVector4 &o) const noexcept { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
	[[nodiscard]] constexpr FVector4 operator*(float s) const noexcept { return {x * s, y * s, z * s, w * s}; }
	[[nodiscard]] constexpr FVector4 operator/(float s) const noexcept { return {x / s, y / s, z / s, w / s}; }
	[[nodiscard]] constexpr float operator[](int i) const noexcept { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
	constexpr float &operator[](int i) noexcept { return i == 0 ? x : i == 1 ? y : i == 2 ? z : w; }
	[[nodiscard]] float Length() const noexcept { return std::sqrt(Dot(*this)); }
	[[nodiscard]] FVector4 Normalized() const noexcept {
		const float l = Length();
		return l > 1e-12f ? *this / l : FVector4{};
	}
	constexpr bool operator==(const FVector4 &) const = default;
};

struct IVec2 {
	int32_t x = 0, y = 0;
	constexpr IVec2() noexcept = default;
	constexpr IVec2(int32_t x_, int32_t y_) noexcept : x(x_), y(y_) {}
	[[nodiscard]] constexpr IVec2 operator+(const IVec2 &o) const noexcept { return {x + o.x, y + o.y}; }
	[[nodiscard]] constexpr IVec2 operator-(const IVec2 &o) const noexcept { return {x - o.x, y - o.y}; }
	[[nodiscard]] constexpr IVec2 operator*(int32_t s) const noexcept { return {x * s, y * s}; }
	[[nodiscard]] constexpr IVec2 operator+(int32_t s) const noexcept { return {x + s, y + s}; }
	constexpr IVec2 &operator+=(const IVec2 &o) noexcept { x += o.x; y += o.y; return *this; }
	constexpr IVec2 &operator+=(int32_t s) noexcept { x += s; y += s; return *this; }
	[[nodiscard]] constexpr int32_t operator[](int i) const noexcept { return i == 0 ? x : y; }
	constexpr int32_t &operator[](int i) noexcept { return i == 0 ? x : y; }
	[[nodiscard]] constexpr FVector2 ToFloat() const noexcept { return {static_cast<float>(x), static_cast<float>(y)}; }
	constexpr bool operator==(const IVec2 &) const = default;
	constexpr auto operator<=>(const IVec2 &) const = default;
};

struct IVec3 {
	int32_t x = 0, y = 0, z = 0;
	constexpr IVec3() noexcept = default;
	constexpr IVec3(int32_t x_, int32_t y_, int32_t z_) noexcept : x(x_), y(y_), z(z_) {}
	[[nodiscard]] constexpr IVec3 operator+(const IVec3 &o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
	[[nodiscard]] constexpr IVec3 operator-(const IVec3 &o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
	[[nodiscard]] constexpr IVec3 operator*(int32_t s) const noexcept { return {x * s, y * s, z * s}; }
	[[nodiscard]] constexpr IVec3 operator+(int32_t s) const noexcept { return {x + s, y + s, z + s}; }
	constexpr IVec3 &operator+=(const IVec3 &o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
	constexpr IVec3 &operator+=(int32_t s) noexcept { x += s; y += s; z += s; return *this; }
	[[nodiscard]] constexpr int32_t operator[](int i) const noexcept { return i == 0 ? x : (i == 1 ? y : z); }
	constexpr int32_t &operator[](int i) noexcept { return i == 0 ? x : (i == 1 ? y : z); }
	[[nodiscard]] constexpr FVector3 ToFloat() const noexcept {
		return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
	}
	constexpr bool operator==(const IVec3 &) const = default;
	constexpr auto operator<=>(const IVec3 &) const = default;
};
[[nodiscard]] inline IVec3 FloorToInt(const FVector3 &v) noexcept { return {FastFloor(v.x), FastFloor(v.y), FastFloor(v.z)}; }

// ── Matrice 4×4 colonne-majeure : m[colonne][ligne] ────────────────────────
struct FMatrix4 {
	float m[4][4]{};

	[[nodiscard]] static constexpr FMatrix4 Identity() noexcept {
		FMatrix4 r;
		r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.f;
		return r;
	}
	[[nodiscard]] constexpr FMatrix4 operator*(const FMatrix4 &o) const noexcept {
		FMatrix4 r;
		for (int c = 0; c < 4; ++c)
			for (int l = 0; l < 4; ++l)
				r.m[c][l] = m[0][l] * o.m[c][0] + m[1][l] * o.m[c][1] + m[2][l] * o.m[c][2] + m[3][l] * o.m[c][3];
		return r;
	}
	[[nodiscard]] constexpr FVector4 operator*(const FVector4 &v) const noexcept {
		return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
				m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
				m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
				m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
	}
	[[nodiscard]] constexpr FVector3 TransformPoint(const FVector3 &p) const noexcept { return (*this * FVector4(p, 1.f)).Xyz(); }
	/// Point transformé puis divisé par w (projection perspective, NDC).
	[[nodiscard]] constexpr FVector3 ProjectPoint(const FVector3 &p) const noexcept {
		const FVector4 h = *this * FVector4(p, 1.f);
		return h.w != 0.f ? FVector3{h.x / h.w, h.y / h.w, h.z / h.w} : h.Xyz();
	}
	/// Direction transformée (translation ignorée).
	[[nodiscard]] constexpr FVector3 TransformDirection(const FVector3 &d) const noexcept { return (*this * FVector4(d, 0.f)).Xyz(); }
	[[nodiscard]] static constexpr FMatrix4 Translation(const FVector3 &t) noexcept {
		FMatrix4 r = Identity();
		r.m[3][0] = t.x;
		r.m[3][1] = t.y;
		r.m[3][2] = t.z;
		return r;
	}
	[[nodiscard]] static constexpr FMatrix4 Scale(const FVector3 &s) noexcept {
		FMatrix4 r;
		r.m[0][0] = s.x;
		r.m[1][1] = s.y;
		r.m[2][2] = s.z;
		r.m[3][3] = 1.f;
		return r;
	}
	/// Rotation de `angle` radians autour de `axis` (normalisé ici), sens trigonométrique.
	[[nodiscard]] static FMatrix4 Rotation(const FVector3 &axis, float angle) noexcept {
		const FVector3 a = axis.Normalized();
		const float c = std::cos(angle), s = std::sin(angle), k = 1.f - c;
		FMatrix4 r = Identity();
		r.m[0][0] = c + a.x * a.x * k;
		r.m[0][1] = a.y * a.x * k + a.z * s;
		r.m[0][2] = a.z * a.x * k - a.y * s;
		r.m[1][0] = a.x * a.y * k - a.z * s;
		r.m[1][1] = c + a.y * a.y * k;
		r.m[1][2] = a.z * a.y * k + a.x * s;
		r.m[2][0] = a.x * a.z * k + a.y * s;
		r.m[2][1] = a.y * a.z * k - a.x * s;
		r.m[2][2] = c + a.z * a.z * k;
		return r;
	}
	/// Perspective main droite, profondeur [0,1] (Vulkan/SDL_GPU), fovY en radians.
	[[nodiscard]] static FMatrix4 Perspective(float fovY, float aspect, float zNear, float zFar) noexcept {
		const float f = 1.0f / std::tan(fovY * 0.5f);
		FMatrix4 r;
		r.m[0][0] = f / aspect;
		r.m[1][1] = f;
		r.m[2][2] = zFar / (zNear - zFar);
		r.m[2][3] = -1.f;
		r.m[3][2] = (zFar * zNear) / (zNear - zFar);
		return r;
	}
	[[nodiscard]] static constexpr FMatrix4 Ortho(float l, float r_, float b, float t, float n, float f) noexcept {
		FMatrix4 r = Identity();
		r.m[0][0] = 2.f / (r_ - l);
		r.m[1][1] = 2.f / (t - b);
		r.m[2][2] = 1.f / (n - f);
		r.m[3][0] = -(r_ + l) / (r_ - l);
		r.m[3][1] = -(t + b) / (t - b);
		r.m[3][2] = n / (n - f);
		return r;
	}
	[[nodiscard]] static FMatrix4 LookAt(const FVector3 &eye, const FVector3 &target, const FVector3 &up) noexcept {
		const FVector3 f = (target - eye).Normalized();
		const FVector3 s = f.Cross(up).Normalized();
		const FVector3 u = s.Cross(f);
		FMatrix4 r = Identity();
		r.m[0][0] = s.x; r.m[1][0] = s.y; r.m[2][0] = s.z;
		r.m[0][1] = u.x; r.m[1][1] = u.y; r.m[2][1] = u.z;
		r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
		r.m[3][0] = -s.Dot(eye);
		r.m[3][1] = -u.Dot(eye);
		r.m[3][2] = f.Dot(eye);
		return r;
	}
	/// Inverse générale (cofacteurs) ; identité si la matrice est singulière.
	[[nodiscard]] constexpr FMatrix4 Inverse() const noexcept {
		const float *a = &m[0][0];
		float inv[16];
		inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
		inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
		inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
		inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
		inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
		inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
		inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
		inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
		inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
		inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
		inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
		inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
		inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
		inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
		inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
		inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
		const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
		if (det == 0.f)
			return Identity();
		FMatrix4 r;
		for (int i = 0; i < 16; ++i)
			(&r.m[0][0])[i] = inv[i] / det;
		return r;
	}
	[[nodiscard]] constexpr FMatrix4 Transposed() const noexcept {
		FMatrix4 r;
		for (int c = 0; c < 4; ++c)
			for (int l = 0; l < 4; ++l)
				r.m[c][l] = m[l][c];
		return r;
	}
	[[nodiscard]] constexpr const float *Data() const noexcept { return &m[0][0]; }
};

// ── Quaternion (rotation) ─────────────────────────────────────────────────
struct FQuaternion {
	float x = 0.f, y = 0.f, z = 0.f, w = 1.f;
	[[nodiscard]] static constexpr FQuaternion Identity() noexcept { return {}; }
	/// Angles d'Euler (radians) appliqués dans l'ordre Y (lacet), X (tangage), Z (roulis).
	[[nodiscard]] static FQuaternion FromEuler(float pitch, float yaw, float roll) noexcept {
		return AxisAngle({0.f, 1.f, 0.f}, yaw) * AxisAngle({1.f, 0.f, 0.f}, pitch) * AxisAngle({0.f, 0.f, 1.f}, roll);
	}
	[[nodiscard]] constexpr float Dot(const FQuaternion &q) const noexcept { return x * q.x + y * q.y + z * q.z + w * q.w; }
	[[nodiscard]] float Length() const noexcept { return std::sqrt(Dot(*this)); }
	[[nodiscard]] FQuaternion Normalized() const noexcept {
		const float l = Length();
		return l > 1e-12f ? FQuaternion{x / l, y / l, z / l, w / l} : FQuaternion{};
	}
	[[nodiscard]] constexpr FQuaternion Conjugate() const noexcept { return {-x, -y, -z, w}; }
	[[nodiscard]] FQuaternion Inverse() const noexcept {
		const float l2 = Dot(*this);
		return l2 > 1e-12f ? FQuaternion{-x / l2, -y / l2, -z / l2, w / l2} : FQuaternion{};
	}
	/// Interpolation sphérique (chemin le plus court), `t` dans [0, 1].
	[[nodiscard]] static FQuaternion Slerp(FQuaternion a, FQuaternion b, float t) noexcept {
		float cosTheta = a.Dot(b);
		if (cosTheta < 0.f) { // chemin le plus court
			b = {-b.x, -b.y, -b.z, -b.w};
			cosTheta = -cosTheta;
		}
		if (cosTheta > 0.9995f) // presque alignés : interpolation linéaire normalisée
			return FQuaternion{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t}.Normalized();
		const float theta = std::acos(cosTheta), s = std::sin(theta);
		const float wa = std::sin((1.f - t) * theta) / s, wb = std::sin(t * theta) / s;
		return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
	}
	/// Matrice de rotation équivalente.
	[[nodiscard]] constexpr FMatrix4 ToMatrix() const noexcept {
		FMatrix4 r = FMatrix4::Identity();
		const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
		r.m[0][0] = 1.f - 2.f * (yy + zz);
		r.m[0][1] = 2.f * (xy + wz);
		r.m[0][2] = 2.f * (xz - wy);
		r.m[1][0] = 2.f * (xy - wz);
		r.m[1][1] = 1.f - 2.f * (xx + zz);
		r.m[1][2] = 2.f * (yz + wx);
		r.m[2][0] = 2.f * (xz + wy);
		r.m[2][1] = 2.f * (yz - wx);
		r.m[2][2] = 1.f - 2.f * (xx + yy);
		return r;
	}
	constexpr bool operator==(const FQuaternion &) const = default;
	[[nodiscard]] static FQuaternion AxisAngle(const FVector3 &axis, float angle) noexcept {
		const FVector3 a = axis.Normalized() * std::sin(angle * 0.5f);
		return {a.x, a.y, a.z, std::cos(angle * 0.5f)};
	}
	[[nodiscard]] constexpr FQuaternion operator*(const FQuaternion &q) const noexcept {
		return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
				w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
	}
	[[nodiscard]] constexpr FVector3 Rotate(const FVector3 &v) const noexcept {
		const FVector3 u{x, y, z};
		const FVector3 t = u.Cross(v) * 2.f;
		return v + t * w + u.Cross(t);
	}
};

// ── Volumes ────────────────────────────────────────────────────────────────
struct FAABB {
	FVector3 min, max;
	[[nodiscard]] constexpr FVector3 Center() const noexcept { return (min + max) * 0.5f; }
	[[nodiscard]] constexpr FVector3 Extent() const noexcept { return (max - min) * 0.5f; }
	[[nodiscard]] constexpr bool Intersects(const FAABB &o) const noexcept {
		return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z;
	}
	[[nodiscard]] constexpr bool Contains(const FVector3 &p) const noexcept {
		return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
	}
	[[nodiscard]] constexpr FAABB Translated(const FVector3 &d) const noexcept { return {min + d, max + d}; }
	/// Découpe en 8 octants (bit 0 = x supérieur, bit 1 = y supérieur, bit 2 = z supérieur).
	[[nodiscard]] constexpr std::array<FAABB, 8> Split() const noexcept {
		const FVector3 c = Center();
		std::array<FAABB, 8> out;
		for (size_t i = 0; i < 8; ++i) {
			out[i].min = {(i & 1) ? c.x : min.x, (i & 2) ? c.y : min.y, (i & 4) ? c.z : min.z};
			out[i].max = {(i & 1) ? max.x : c.x, (i & 2) ? max.y : c.y, (i & 4) ? max.z : c.z};
		}
		return out;
	}
	/// Intersection avec une sphère (distance du centre à la boîte).
	[[nodiscard]] constexpr bool IntersectsSphere(const FVector3 &center, float radius) const noexcept {
		float d2 = 0.f;
		for (int i = 0; i < 3; ++i) {
			const float c = center[i], lo = min[i], hi = max[i];
			const float q = c < lo ? lo - c : (c > hi ? c - hi : 0.f);
			d2 += q * q;
		}
		return d2 <= radius * radius;
	}
	constexpr bool operator==(const FAABB &) const = default;
};

struct FPlane {
	FVector3 n;
	float d = 0.f;
	[[nodiscard]] constexpr float Distance(const FVector3 &p) const noexcept { return n.Dot(p) + d; }
};

/// Frustum extrait d'une matrice vue-projection (profondeur [0,1]).
struct FFrustum {
	FPlane planes[6];
	[[nodiscard]] static FFrustum FromMatrix(const FMatrix4 &vp) noexcept {
		auto row = [&](int l) { return FVector4(vp.m[0][l], vp.m[1][l], vp.m[2][l], vp.m[3][l]); };
		const FVector4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
		auto make = [](FVector4 p) {
			const float len = p.Xyz().Length();
			return FPlane{p.Xyz() / len, p.w / len};
		};
		auto add = [](FVector4 a, FVector4 b) { return FVector4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); };
		auto sub = [](FVector4 a, FVector4 b) { return FVector4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); };
		FFrustum f;
		f.planes[0] = make(add(r3, r0));
		f.planes[1] = make(sub(r3, r0));
		f.planes[2] = make(add(r3, r1));
		f.planes[3] = make(sub(r3, r1));
		f.planes[4] = make(r2);
		f.planes[5] = make(sub(r3, r2));
		return f;
	}
	/// Test conservatif boîte/frustum (p-vertex) : faux seulement si entièrement dehors.
	[[nodiscard]] bool Intersects(const FAABB &box) const noexcept {
		for (const FPlane &p : planes) {
			const FVector3 v{p.n.x >= 0 ? box.max.x : box.min.x, p.n.y >= 0 ? box.max.y : box.min.y,
							 p.n.z >= 0 ? box.max.z : box.min.z};
			if (p.Distance(v) < 0.f)
				return false;
		}
		return true;
	}
};

struct FRay {
	FVector3 origin, dir;
	[[nodiscard]] constexpr FVector3 At(float t) const noexcept { return origin + dir * t; }
};

// ── Fonctions libres (formes courtes, comme GLSL) ─────────────────────────
[[nodiscard]] constexpr float Radians(float degrees) noexcept { return degrees * DEG2RAD; }
[[nodiscard]] constexpr float Degrees(float radians) noexcept { return radians * RAD2DEG; }
[[nodiscard]] constexpr FVector2 operator*(float s, FVector2 v) noexcept { return v * s; }
[[nodiscard]] constexpr float Dot(FVector2 a, FVector2 b) noexcept { return a.Dot(b); }
[[nodiscard]] constexpr float Dot(const FVector3 &a, const FVector3 &b) noexcept { return a.Dot(b); }
[[nodiscard]] constexpr float Dot(const FVector4 &a, const FVector4 &b) noexcept { return a.Dot(b); }
[[nodiscard]] constexpr float Cross(FVector2 a, FVector2 b) noexcept { return a.Cross(b); }
[[nodiscard]] constexpr FVector3 Cross(const FVector3 &a, const FVector3 &b) noexcept { return a.Cross(b); }
[[nodiscard]] inline float Length(FVector2 v) noexcept { return v.Length(); }
[[nodiscard]] inline float Length(const FVector3 &v) noexcept { return v.Length(); }
[[nodiscard]] inline float Length(const FVector4 &v) noexcept { return v.Length(); }
[[nodiscard]] inline float Distance(FVector2 a, FVector2 b) noexcept { return a.Distance(b); }
[[nodiscard]] inline float Distance(const FVector3 &a, const FVector3 &b) noexcept { return a.Distance(b); }
[[nodiscard]] inline FVector2 Normalize(FVector2 v) noexcept { return v.Normalized(); }
[[nodiscard]] inline FVector3 Normalize(const FVector3 &v) noexcept { return v.Normalized(); }
[[nodiscard]] inline FVector4 Normalize(const FVector4 &v) noexcept { return v.Normalized(); }
[[nodiscard]] constexpr FVector2 Min(FVector2 a, FVector2 b) noexcept { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
[[nodiscard]] constexpr FVector2 Max(FVector2 a, FVector2 b) noexcept { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
[[nodiscard]] inline FVector3 Abs(const FVector3 &v) noexcept { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
/// Réflexion de `v` sur la surface de normale `n` (unitaire).
[[nodiscard]] constexpr FVector3 Reflect(const FVector3 &v, const FVector3 &n) noexcept { return v - n * (2.f * v.Dot(n)); }
/// Projection de `v` sur la direction `onto`.
[[nodiscard]] constexpr FVector3 Project(const FVector3 &v, const FVector3 &onto) noexcept {
	const float l2 = onto.LengthSq();
	return l2 > 1e-12f ? onto * (v.Dot(onto) / l2) : FVector3{};
}
/// Angle (radians, [0, π]) entre deux vecteurs.
[[nodiscard]] inline float Angle(const FVector3 &a, const FVector3 &b) noexcept {
	const float l = a.Length() * b.Length();
	return l > 1e-12f ? std::acos(Clamp(a.Dot(b) / l, -1.f, 1.f)) : 0.f;
}
/// Interpolation sphérique de deux directions (longueur interpolée linéairement).
[[nodiscard]] inline FVector3 Slerp(const FVector3 &a, const FVector3 &b, float t) noexcept {
	const float la = a.Length(), lb = b.Length();
	if (la < 1e-12f || lb < 1e-12f)
		return Lerp(a, b, t);
	const FVector3 ua = a / la, ub = b / lb;
	const float cosTheta = Clamp(ua.Dot(ub), -1.f, 1.f);
	if (cosTheta > 0.9995f)
		return Lerp(a, b, t);
	const float theta = std::acos(cosTheta), s = std::sin(theta);
	return (ua * (std::sin((1.f - t) * theta) / s) + ub * (std::sin(t * theta) / s)) * Lerp(la, lb, t);
}
/// Normale unitaire du triangle (a, b, c), sens trigonométrique.
[[nodiscard]] inline FVector3 TriangleNormal(const FVector3 &a, const FVector3 &b, const FVector3 &c) noexcept { return (b - a).Cross(c - a).Normalized(); }
/// Rotation plane de `angle` radians (sens trigonométrique).
[[nodiscard]] inline FVector2 Rotate2D(FVector2 v, float angle) noexcept {
	const float c = std::cos(angle), s = std::sin(angle);
	return {v.x * c - v.y * s, v.x * s + v.y * c};
}
/// Rotation de `v` par le quaternion `q`.
[[nodiscard]] constexpr FVector3 Rotate(const FQuaternion &q, const FVector3 &v) noexcept { return q.Rotate(v); }
/// Translation · rotation · échelle.
[[nodiscard]] constexpr FMatrix4 TRS(const FVector3 &t, const FQuaternion &r, const FVector3 &s) noexcept {
	return FMatrix4::Translation(t) * r.ToMatrix() * FMatrix4::Scale(s);
}
/// Point du monde → fenêtre : x, y en pixels (origine `origin`, taille `size`, y vers le bas),
/// z = profondeur normalisée [0, 1].
[[nodiscard]] constexpr FVector3 ProjectToViewport(const FVector3 &p, const FMatrix4 &viewProj, FVector2 origin, FVector2 size) noexcept {
	const FVector3 ndc = viewProj.ProjectPoint(p);
	return {origin.x + (ndc.x * 0.5f + 0.5f) * size.x, origin.y + (0.5f - ndc.y * 0.5f) * size.y, ndc.z};
}

/// Direction de vue à partir de lacet/tangage (radians), Y vers le haut.
[[nodiscard]] inline FVector3 DirectionFromYawPitch(float yaw, float pitch) noexcept {
	return {std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)};
}

// ── Angles, arrondis et ordres d'Euler ─────────────────────────────────────
/// Ramène un angle (radians) dans [-π, π].
[[nodiscard]] inline float WrapAngle(float rad) noexcept {
	float r = std::fmod(rad, TAU);
	if (r > PI)
		r -= TAU;
	else if (r < -PI)
		r += TAU;
	return r;
}
/// Enveloppe `v` dans [lo, hi) (module).
[[nodiscard]] inline float Wrapf(float v, float lo, float hi) noexcept {
	const float range = hi - lo;
	if (range <= 0.f)
		return lo;
	float r = std::fmod(v - lo, range);
	if (r < 0.f)
		r += range;
	return lo + r;
}
/// Arrondit `v` au multiple de `step` le plus proche (aimantation).
[[nodiscard]] inline float RoundToStep(float v, float step) noexcept {
	if (step <= 0.f)
		return v;
	return v - std::remainder(v, step);
}
/// Test d'intersection rayon/boîte (slab) : rend [tmin, tmax] ; faux si disjoint.
[[nodiscard]] inline bool RayAabbIntersection(const FRay &ray, const FAABB &box, float &tmin, float &tmax) noexcept {
	tmin = -std::numeric_limits<float>::infinity();
	tmax = std::numeric_limits<float>::infinity();
	for (int i = 0; i < 3; ++i) {
		const float o = ray.origin[i], d = ray.dir[i], lo = box.min[i], hi = box.max[i];
		if (std::fabs(d) < 1e-12f) {
			if (o < lo || o > hi)
				return false;
		} else {
			float t1 = (lo - o) / d, t2 = (hi - o) / d;
			if (t1 > t2)
				std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax)
				return false;
		}
	}
	return true;
}
/// Angle lissé vers une cible sur le plus court arc (`speed` en rad/s).
struct SmoothAngle {
	float angle = 0.f, target = 0.f, speed = 0.f;
	/// Avance l'angle vers la cible de `speed * dt` ; rend la nouvelle valeur.
	float Update(float dt) noexcept {
		const float delta = WrapAngle(target - angle);
		const float maxStep = speed * dt;
		if (std::fabs(delta) <= maxStep)
			angle = target;
		else
			angle += (delta > 0.f ? 1.f : -1.f) * maxStep;
		angle = WrapAngle(angle);
		return angle;
	}
};
/// Ordre d'application des angles d'Euler (le premier est appliqué en dernier).
enum class RotationOrder : uint8_t { XYZ, XZY, YZX, YXZ, ZXY, ZYX };
/// Quaternion d'angles d'Euler (radians) dans l'ordre demandé. `euler` = {pitch (x), yaw (y), roll (z)}.
[[nodiscard]] inline FQuaternion QuatFromEuler(const FVector3 &euler, RotationOrder order) noexcept {
	const FVector3 x{1.f, 0.f, 0.f}, y{0.f, 1.f, 0.f}, z{0.f, 0.f, 1.f};
	const auto q = [](const FVector3 &axis, float ang) { return FQuaternion::AxisAngle(axis, ang); };
	switch (order) {
		case RotationOrder::XYZ: return q(x, euler.x) * q(y, euler.y) * q(z, euler.z);
		case RotationOrder::XZY: return q(x, euler.x) * q(z, euler.z) * q(y, euler.y);
		case RotationOrder::YZX: return q(y, euler.y) * q(z, euler.z) * q(x, euler.x);
		case RotationOrder::YXZ: return q(y, euler.y) * q(x, euler.x) * q(z, euler.z);
		case RotationOrder::ZXY: return q(z, euler.z) * q(x, euler.x) * q(y, euler.y);
		case RotationOrder::ZYX: return q(z, euler.z) * q(y, euler.y) * q(x, euler.x);
	}
	return q(y, euler.y) * q(x, euler.x) * q(z, euler.z);
}

} // namespace math
