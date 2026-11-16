#include "voxel/avatar.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace voxel {

using math::FVector3;

namespace {

constexpr float kPi = 3.14159265358979f;

FVector3 RotateEulerXYZ(FVector3 v, const FVector3 &r) noexcept {
	if (r.x != 0.f) {
		const float c = std::cos(r.x), s = std::sin(r.x);
		v = {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
	}
	if (r.y != 0.f) {
		const float c = std::cos(r.y), s = std::sin(r.y);
		v = {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
	}
	if (r.z != 0.f) {
		const float c = std::cos(r.z), s = std::sin(r.z);
		v = {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
	}
	return v;
}

float Smooth(float t) noexcept {
	t = std::clamp(t, 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

float Fract(float x) noexcept { return x - std::floor(x); }

/// Interpolation par clés (temps croissants dans [0, 1]) avec lissage entre clés.
struct Key {
	float t, v;
};
float Keys(float c, std::initializer_list<Key> keys) noexcept {
	const Key *prev = keys.begin();
	if (c <= prev->t)
		return prev->v;
	for (const Key *k = keys.begin() + 1; k != keys.end(); ++k) {
		if (c <= k->t)
			return prev->v + (k->v - prev->v) * Smooth((c - prev->t) / std::max(k->t - prev->t, 1e-5f));
		prev = k;
	}
	return prev->v;
}

/// Écart angulaire ramené dans [−π, π] (interpolation par le plus court chemin).
float AngleDelta(float from, float to) noexcept {
	float d = std::fmod(to - from, 2.f * kPi);
	if (d > kPi)
		d -= 2.f * kPi;
	if (d < -kPi)
		d += 2.f * kPi;
	return d;
}

bool FollowsBody(AvatarPart p) noexcept { return p == AvatarPart::Head || p == AvatarPart::ArmRight || p == AvatarPart::ArmLeft; }

FVector3 &Rot(AvatarPose &p, AvatarPart part) noexcept { return p.rotation[size_t(part)]; }

/// Bras symétriques : `x` et `ry` identiques, écart `rz` miroir.
void Arms(AvatarPose &p, float rx, float rz, float ry = 0.f) {
	Rot(p, AvatarPart::ArmRight) = {rx, ry, rz};
	Rot(p, AvatarPart::ArmLeft) = {rx, -ry, -rz};
}
void Legs(AvatarPose &p, float rx, float rz) {
	Rot(p, AvatarPart::LegRight) = {rx, 0.f, rz};
	Rot(p, AvatarPart::LegLeft) = {rx, 0.f, -rz};
}

AvatarPose SwimPose(const AvatarInput &in) {
	AvatarPose p;
	const float t = in.time;
	const float moving = std::clamp(in.speed / 1.6f, 0.f, 1.f);
	// Sur place : nage verticale (godille des bras, pédalage des jambes).
	AvatarPose tread;
	tread.rootRotation = {-0.22f, 0.f, 0.f};
	Arms(tread, 0.55f, 0.95f + 0.3f * std::sin(t * 3.2f));
	Rot(tread, AvatarPart::LegRight) = {0.45f * std::sin(t * 3.2f), 0.f, 0.08f};
	Rot(tread, AvatarPart::LegLeft) = {-0.45f * std::sin(t * 3.2f), 0.f, -0.08f};
	Rot(tread, AvatarPart::Head) = {std::clamp(in.lookPitch, -0.8f, 0.8f) + 0.22f, 0.f, 0.f};
	tread.rootOffset = {0.f, 0.04f * std::sin(t * 1.7f), 0.f};

	switch (in.swim) {
	case SwimStyle::Crawl: {
		const float ph = t * 2.f * kPi * 0.75f;
		p.rootRotation = {-kPi * 0.5f + 0.08f, 0.f, 0.f};
		// Moulinet alterné : retour aérien au-dessus du dos, traction sous le corps.
		Rot(p, AvatarPart::ArmRight) = {-ph, 0.f, 0.12f};
		Rot(p, AvatarPart::ArmLeft) = {-(ph + kPi), 0.f, -0.12f};
		Rot(p, AvatarPart::Body) = {0.f, 0.32f * std::sin(ph), 0.f}; // roulis
		Rot(p, AvatarPart::LegRight) = {0.32f * std::sin(ph * 3.f), 0.f, 0.04f};
		Rot(p, AvatarPart::LegLeft) = {-0.32f * std::sin(ph * 3.f), 0.f, -0.04f};
		// Respiration sur le côté à chaque cycle.
		Rot(p, AvatarPart::Head) = {1.05f, 0.55f * std::max(0.f, std::sin(ph)), 0.f};
		break;
	}
	case SwimStyle::Backstroke: {
		const float ph = t * 2.f * kPi * 0.6f;
		p.rootRotation = {kPi * 0.5f - 0.1f, kPi, 0.f};
		Rot(p, AvatarPart::ArmRight) = {ph, 0.f, 0.15f};
		Rot(p, AvatarPart::ArmLeft) = {ph + kPi, 0.f, -0.15f};
		Rot(p, AvatarPart::Body) = {0.f, 0.25f * std::sin(ph), 0.f};
		Rot(p, AvatarPart::LegRight) = {0.3f * std::sin(ph * 3.f), 0.f, 0.04f};
		Rot(p, AvatarPart::LegLeft) = {-0.3f * std::sin(ph * 3.f), 0.f, -0.04f};
		Rot(p, AvatarPart::Head) = {-0.25f, 0.f, 0.f};
		break;
	}
	default: { // brasse
		const float c = Fract(t / 1.4f);
		p.rootRotation = {-kPi * 0.5f + 0.18f + 0.16f * Keys(c, {{0.3f, 0.f}, {0.55f, 1.f}, {0.8f, 0.f}}), 0.f, 0.f};
		const float rx = Keys(c, {{0.f, kPi}, {0.35f, kPi}, {0.65f, kPi - 1.25f}, {0.85f, 2.35f}, {1.f, kPi}});
		const float rz = Keys(c, {{0.f, 0.1f}, {0.35f, 0.1f}, {0.65f, 0.75f}, {0.85f, 0.05f}, {1.f, 0.1f}});
		Arms(p, rx, rz);
		const float spread = Keys(c, {{0.f, 0.05f}, {0.5f, 0.05f}, {0.68f, 0.6f}, {0.85f, 0.05f}});
		const float bend = Keys(c, {{0.f, 0.f}, {0.5f, 0.f}, {0.68f, 0.55f}, {0.8f, 0.f}});
		Legs(p, bend, spread);
		Rot(p, AvatarPart::Head) = {0.75f + 0.5f * Keys(c, {{0.3f, 0.f}, {0.55f, 1.f}, {0.8f, 0.f}}), 0.f, 0.f};
		p.rootOffset = {0.f, 0.05f * std::sin(c * 2.f * kPi), 0.f};
		break;
	}
	}
	return BlendPose(tread, p, moving);
}

AvatarPose GesturePose(const AvatarInput &in, const AvatarPose &base, float &weight) {
	AvatarPose g = base;
	const float gt = in.gestureTime;
	const float dur = AvatarGestureDuration(in.gesture);
	weight = std::min(Smooth(gt / 0.25f), Smooth((dur - gt) / 0.3f));
	switch (in.gesture) {
	case AvatarGesture::Wave: // coucou de la main droite, bras levé
		Rot(g, AvatarPart::ArmRight) = {0.25f, 0.f, 2.55f + 0.42f * std::sin(gt * 11.f)};
		Rot(g, AvatarPart::Head) = {base.rotation[size_t(AvatarPart::Head)].x, 0.15f, 0.f};
		break;
	case AvatarGesture::WaveBoth: { // grands signes des deux bras, petits sauts
		const float s = std::sin(gt * 9.f);
		Rot(g, AvatarPart::ArmRight) = {0.1f, 0.f, 2.75f + 0.35f * s};
		Rot(g, AvatarPart::ArmLeft) = {0.1f, 0.f, -(2.75f - 0.35f * s)};
		g.rootOffset.y += 0.06f * std::fabs(std::sin(gt * 4.5f));
		break;
	}
	case AvatarGesture::WaveSmall: // petit coucou devant soi (poignet)
		Rot(g, AvatarPart::ArmRight) = {1.95f, 0.35f * std::sin(gt * 14.f), 0.35f};
		break;
	case AvatarGesture::EquipArmor: { // casque, plastron, jambières puis bottes
		const float k = gt / std::max(dur, 1e-3f);
		const float rx = Keys(k, {{0.f, 0.f}, {0.18f, 2.75f}, {0.3f, 2.75f}, {0.42f, 1.3f}, {0.56f, 0.4f}, {0.74f, 0.75f}, {0.9f, 0.2f}, {1.f, 0.f}});
		const float rz = Keys(k, {{0.f, 0.06f}, {0.18f, 0.35f}, {0.3f, 0.3f}, {0.42f, -0.3f}, {0.56f, 0.22f}, {0.74f, 0.1f}, {1.f, 0.06f}});
		Arms(g, rx, rz);
		Rot(g, AvatarPart::Body) = {Keys(k, {{0.f, 0.f}, {0.56f, 0.f}, {0.74f, -0.6f}, {0.9f, 0.f}}), 0.f, 0.f};
		Rot(g, AvatarPart::Head) = {Keys(k, {{0.f, 0.f}, {0.18f, -0.25f}, {0.42f, -0.45f}, {0.74f, 0.2f}, {1.f, 0.f}}), 0.f, 0.f};
		break;
	}
	default: weight = 0.f; break;
	}
	return g;
}

} // namespace

const char *AvatarStateId(AvatarState s) noexcept {
	switch (s) {
	case AvatarState::Stand: return "stand";
	case AvatarState::Walk: return "walk";
	case AvatarState::Crouch: return "crouch";
	case AvatarState::Sit: return "sit";
	case AvatarState::Sleep: return "sleep";
	case AvatarState::Swim: return "swim";
	case AvatarState::FreeFall: return "freefall";
	case AvatarState::Fly: return "fly";
	default: return "?";
	}
}

const char *AvatarStateLabel(AvatarState s) noexcept {
	switch (s) {
	case AvatarState::Stand: return "Debout";
	case AvatarState::Walk: return "Marche";
	case AvatarState::Crouch: return "Accroupi";
	case AvatarState::Sit: return "Assis";
	case AvatarState::Sleep: return "Couché";
	case AvatarState::Swim: return "Nage";
	case AvatarState::FreeFall: return "Chute libre";
	case AvatarState::Fly: return "Vol";
	default: return "?";
	}
}

const char *SwimStyleId(SwimStyle s) noexcept {
	switch (s) {
	case SwimStyle::Crawl: return "crawl";
	case SwimStyle::Breaststroke: return "breaststroke";
	case SwimStyle::Backstroke: return "backstroke";
	default: return "?";
	}
}

const char *SwimStyleLabel(SwimStyle s) noexcept {
	switch (s) {
	case SwimStyle::Crawl: return "Crawl";
	case SwimStyle::Breaststroke: return "Brasse";
	case SwimStyle::Backstroke: return "Dos crawlé";
	default: return "?";
	}
}

std::optional<SwimStyle> ParseSwimStyle(std::string_view id) noexcept {
	for (uint8_t i = 0; i < uint8_t(SwimStyle::Count); ++i)
		if (id == SwimStyleId(SwimStyle(i)))
			return SwimStyle(i);
	if (id == "brasse")
		return SwimStyle::Breaststroke;
	if (id == "dos" || id == "back")
		return SwimStyle::Backstroke;
	return std::nullopt;
}

const char *AvatarGestureId(AvatarGesture g) noexcept {
	switch (g) {
	case AvatarGesture::None: return "none";
	case AvatarGesture::Wave: return "wave";
	case AvatarGesture::WaveBoth: return "wave_both";
	case AvatarGesture::WaveSmall: return "wave_small";
	case AvatarGesture::EquipArmor: return "equip_armor";
	default: return "?";
	}
}

const char *AvatarGestureLabel(AvatarGesture g) noexcept {
	switch (g) {
	case AvatarGesture::None: return "Aucun";
	case AvatarGesture::Wave: return "Coucou";
	case AvatarGesture::WaveBoth: return "Grands signes";
	case AvatarGesture::WaveSmall: return "Petit coucou";
	case AvatarGesture::EquipArmor: return "Enfiler l'armure";
	default: return "?";
	}
}

std::optional<AvatarGesture> ParseAvatarGesture(std::string_view id) noexcept {
	for (uint8_t i = 0; i < uint8_t(AvatarGesture::Count); ++i)
		if (id == AvatarGestureId(AvatarGesture(i)))
			return AvatarGesture(i);
	return std::nullopt;
}

float AvatarGestureDuration(AvatarGesture g) noexcept {
	switch (g) {
	case AvatarGesture::Wave:
	case AvatarGesture::WaveBoth: return 2.4f;
	case AvatarGesture::WaveSmall: return 1.8f;
	case AvatarGesture::EquipArmor: return 1.6f;
	default: return 0.f;
	}
}

AvatarPose BlendPose(const AvatarPose &a, const AvatarPose &b, float t) {
	t = std::clamp(t, 0.f, 1.f);
	auto angles = [t](const FVector3 &x, const FVector3 &y) {
		return FVector3{x.x + AngleDelta(x.x, y.x) * t, x.y + AngleDelta(x.y, y.y) * t, x.z + AngleDelta(x.z, y.z) * t};
	};
	AvatarPose r;
	for (size_t i = 0; i < kAvatarParts; ++i)
		r.rotation[i] = angles(a.rotation[i], b.rotation[i]);
	r.rootRotation = angles(a.rootRotation, b.rootRotation);
	r.rootOffset = a.rootOffset + (b.rootOffset - a.rootOffset) * t;
	return r;
}

AvatarPose ComputeAvatarPose(const AvatarInput &in) {
	AvatarPose p;
	const float t = in.time;
	const float pitch = std::clamp(in.lookPitch, -1.2f, 1.2f);
	const float idle = std::sin(t * 1.6f);
	auto walk = [&](float ampScale, float maxAmp) {
		const float amp = std::clamp(in.speed / 4.3f, 0.f, maxAmp) * ampScale;
		const float s = std::sin(in.walkPhase);
		return std::pair{amp, s};
	};
	switch (in.state) {
	case AvatarState::Stand:
	case AvatarState::Walk: {
		const auto [amp, s] = walk(1.f, 1.4f);
		Legs(p, 0.f, 0.f);
		Rot(p, AvatarPart::LegRight).x = 0.75f * amp * s;
		Rot(p, AvatarPart::LegLeft).x = -0.75f * amp * s;
		Rot(p, AvatarPart::ArmRight) = {-0.6f * amp * s + 0.04f * idle, 0.f, 0.07f + 0.025f * idle};
		Rot(p, AvatarPart::ArmLeft) = {0.6f * amp * s + 0.04f * idle, 0.f, -0.07f - 0.025f * idle};
		Rot(p, AvatarPart::Head) = {pitch, 0.f, 0.f};
		p.rootOffset.y = -0.025f * amp * std::fabs(std::cos(in.walkPhase));
		break;
	}
	case AvatarState::Crouch: {
		const auto [amp, s] = walk(2.4f, 0.4f);
		Rot(p, AvatarPart::Body) = {-0.5f, 0.f, 0.f};
		Rot(p, AvatarPart::LegRight) = {0.62f + 0.35f * amp * s, 0.f, 0.06f};
		Rot(p, AvatarPart::LegLeft) = {0.62f - 0.35f * amp * s, 0.f, -0.06f};
		Rot(p, AvatarPart::ArmRight) = {0.45f - 0.3f * amp * s, 0.f, 0.1f};
		Rot(p, AvatarPart::ArmLeft) = {0.45f + 0.3f * amp * s, 0.f, -0.1f};
		Rot(p, AvatarPart::Head) = {pitch + 0.5f, 0.f, 0.f};
		p.rootOffset = {0.f, -0.13f, 0.06f};
		break;
	}
	case AvatarState::Sit:
		Rot(p, AvatarPart::Body) = {0.06f, 0.f, 0.f};
		Legs(p, kPi * 0.5f, 0.07f);
		Arms(p, 0.62f + 0.02f * idle, 0.06f);
		Rot(p, AvatarPart::Head) = {pitch - 0.06f, 0.f, 0.f};
		break;
	case AvatarState::Sleep:
		p.rootRotation = {kPi * 0.5f, 0.f, 0.f};
		Arms(p, 0.f, 0.07f + 0.015f * std::sin(t * 1.1f));
		Legs(p, 0.f, 0.03f);
		Rot(p, AvatarPart::Head) = {-0.08f, 0.f, 0.f};
		p.rootOffset.y = 0.008f * std::sin(t * 1.1f); // respiration
		break;
	case AvatarState::Swim: p = SwimPose(in); break;
	case AvatarState::FreeFall: {
		const float f = std::sin(t * 9.f);
		p.rootRotation = {-1.15f, 0.f, 0.04f * std::sin(t * 2.3f)};
		Arms(p, 0.35f + 0.05f * f, 1.5f + 0.07f * f);
		Legs(p, -0.25f, 0.42f + 0.04f * std::sin(t * 7.f));
		Rot(p, AvatarPart::Head) = {0.85f, 0.f, 0.f};
		break;
	}
	case AvatarState::Fly: {
		const float lean = std::clamp(in.speed / 12.f, 0.f, 1.f);
		p.rootRotation = {-0.3f * lean, 0.f, 0.f};
		Legs(p, -0.12f * lean + 0.03f * idle, 0.03f);
		Arms(p, -0.25f * lean + 0.04f * idle, 0.12f);
		Rot(p, AvatarPart::Head) = {pitch + 0.3f * lean, 0.f, 0.f};
		break;
	}
	default: break;
	}

	// Objet tenu (vue extérieure) : avant-bras légèrement levé.
	if (!in.firstPerson && in.holdingRight && (in.state == AvatarState::Stand || in.state == AvatarState::Walk))
		Rot(p, AvatarPart::ArmRight).x += 0.3f;
	if (!in.firstPerson && in.holdingLeft && (in.state == AvatarState::Stand || in.state == AvatarState::Walk))
		Rot(p, AvatarPart::ArmLeft).x += 0.3f;

	// Vue à la première personne : les bras suivent le regard et restent dans le champ.
	const bool upright = in.state == AvatarState::Stand || in.state == AvatarState::Walk || in.state == AvatarState::Crouch ||
						 in.state == AvatarState::Sit || in.state == AvatarState::Fly;
	if (in.firstPerson && upright) {
		const float lean = Rot(p, AvatarPart::Body).x + p.rootRotation.x;
		const float bob = 0.05f * std::sin(in.walkPhase) * std::clamp(in.speed / 4.3f, 0.f, 1.f);
		const float aim = kPi * 0.5f + pitch * 0.92f + 0.3f - lean;
		// Bras droit toujours visible (il agit) ; le gauche seulement s'il tient un objet.
		Rot(p, AvatarPart::ArmRight) = {aim + bob, 0.2f, 0.f};
		if (in.holdingLeft)
			Rot(p, AvatarPart::ArmLeft) = {aim - bob, -0.2f, 0.f};
	}

	if (in.gesture != AvatarGesture::None) {
		float w = 0.f;
		const AvatarPose g = GesturePose(in, p, w);
		if (in.firstPerson && upright && in.gesture != AvatarGesture::EquipArmor) {
			// Le bras qui salue reste visible : le coucou se fait devant soi.
			AvatarInput small = in;
			small.gesture = AvatarGesture::WaveSmall;
			float ws = 0.f;
			const AvatarPose gs = GesturePose(small, p, ws);
			p = BlendPose(p, gs, std::min(w, 1.f));
		} else {
			p = BlendPose(p, g, w);
		}
	}

	if (in.swing > 0.f && in.swing < 1.f) {
		const float k = std::sin(in.swing * kPi);
		FVector3 &arm = Rot(p, AvatarPart::ArmRight);
		arm.x += k * (in.firstPerson ? -0.75f : 1.25f);
		arm.y += k * 0.35f;
	}
	return p;
}

// ── Géométrie ─────────────────────────────────────────────────────────────

FVector3 AvatarPivot(AvatarPart p) noexcept {
	switch (p) {
	case AvatarPart::Head: return {0.f, 0.4f, 0.f};
	case AvatarPart::Body: return {0.f, -0.25f, 0.f};
	case AvatarPart::ArmRight: return {0.375f, 0.35f, 0.f};
	case AvatarPart::ArmLeft: return {-0.375f, 0.35f, 0.f};
	case AvatarPart::LegRight: return {0.1875f, -0.25f, 0.f};
	case AvatarPart::LegLeft: return {-0.1875f, -0.25f, 0.f};
	default: return {};
	}
}

AvatarBox AvatarPartBox(AvatarPart p, float inflate) {
	AvatarBox b;
	b.part = p;
	switch (p) {
	case AvatarPart::Head:
		b.min = {-0.25f, 0.f, -0.25f};
		b.max = {0.25f, 0.5f, 0.25f};
		b.uv = {{{0, 8, 8, 16}, {16, 8, 24, 16}, {8, 0, 16, 8}, {16, 0, 24, 8}, {24, 8, 32, 16}, {8, 8, 16, 16}}};
		break;
	case AvatarPart::Body:
		b.min = {-0.25f, 0.f, -0.125f};
		b.max = {0.25f, 0.65f, 0.125f};
		b.uv = {{{16, 20, 20, 32}, {28, 20, 32, 32}, {20, 16, 28, 20}, {28, 16, 36, 20}, {32, 20, 40, 32}, {20, 20, 28, 32}}};
		break;
	case AvatarPart::ArmRight:
	case AvatarPart::ArmLeft:
		b.min = {-0.125f, -0.6f, -0.125f};
		b.max = {0.125f, 0.05f, 0.125f};
		if (p == AvatarPart::ArmRight)
			b.uv = {{{40, 20, 44, 32}, {48, 20, 52, 32}, {44, 16, 48, 20}, {48, 16, 52, 20}, {52, 20, 56, 32}, {44, 20, 48, 32}}};
		else
			b.uv = {{{32, 52, 36, 64}, {40, 52, 44, 64}, {36, 48, 40, 52}, {40, 48, 44, 52}, {52, 52, 56, 64}, {36, 52, 40, 64}}};
		break;
	case AvatarPart::LegRight:
	case AvatarPart::LegLeft:
		b.min = {-0.125f, -0.65f, -0.125f};
		b.max = {0.125f, 0.f, 0.125f};
		if (p == AvatarPart::LegRight)
			b.uv = {{{0, 20, 4, 32}, {8, 20, 12, 32}, {4, 16, 8, 20}, {8, 16, 12, 20}, {12, 20, 16, 32}, {4, 20, 8, 32}}};
		else
			b.uv = {{{16, 52, 20, 64}, {24, 52, 28, 64}, {20, 48, 24, 52}, {24, 48, 28, 52}, {28, 52, 32, 64}, {20, 52, 24, 64}}};
		break;
	default: break;
	}
	b.min = b.min - FVector3(inflate);
	b.max = b.max + FVector3(inflate);
	return b;
}

FVector3 AvatarTransform(const AvatarPose &pose, AvatarPart part, const FVector3 &local) noexcept {
	FVector3 p = RotateEulerXYZ(local, pose.rotation[size_t(part)]) + AvatarPivot(part);
	if (FollowsBody(part)) {
		const FVector3 hip = AvatarPivot(AvatarPart::Body);
		p = RotateEulerXYZ(p - hip, pose.rotation[size_t(AvatarPart::Body)]) + hip;
	}
	return RotateEulerXYZ(p, pose.rootRotation) + pose.rootOffset;
}

FVector3 AvatarHandAnchor() noexcept { return {0.f, -0.58f, 0.f}; }

// Œil à l'avant du visage (la caméra est « sur le visage »).
FVector3 AvatarEye(const AvatarPose &pose) noexcept { return AvatarTransform(pose, AvatarPart::Head, {0.f, 0.32f, -0.18f}); }

AvatarBox AvatarHeldBox(bool rightHand, bool flatItem, const std::array<float, 4> &side, const std::array<float, 4> &top) {
	AvatarBox b;
	b.part = rightHand ? AvatarPart::ArmRight : AvatarPart::ArmLeft;
	if (flatItem) { // plaque tenue comme un outil, dans le plan de la main
		b.min = {-0.015f, -0.9f, -0.42f};
		b.max = {0.015f, -0.46f, 0.02f};
		b.uv = {side, side, side, side, side, side};
		// Faces ±X : l'image entière ; les tranches reprennent une ligne du bord.
		b.uv[2] = b.uv[3] = b.uv[4] = b.uv[5] = {side[0], side[1], side[0] + 1.f, side[1] + 1.f};
	} else {
		b.min = {-0.09f, -0.72f, -0.24f};
		b.max = {0.09f, -0.54f, -0.06f};
		b.uv = {side, side, top, top, side, side};
	}
	return b;
}

Model BuildAvatarRig() {
	Model m;
	m.id = "player";
	m.format = ModelFormat::Rig;
	m.unitsPerBlock = 16.f;
	m.textureWidth = 64;
	m.textureHeight = 64;
	m.boxUv = true;
	static constexpr std::pair<AvatarPart, std::string_view> kParts[] = {
		{AvatarPart::Head, "head"},		 {AvatarPart::Body, "body"},
		{AvatarPart::ArmRight, "arm_right"}, {AvatarPart::ArmLeft, "arm_left"},
		{AvatarPart::LegRight, "leg_right"}, {AvatarPart::LegLeft, "leg_left"},
	};
	for (const auto &[part, name] : kParts) {
		ModelPart p;
		p.name = std::string(name);
		p.pivot = AvatarPivot(part) * 16.f; // blocs → pixels (unitsPerBlock = 16)
		const AvatarBox ab = AvatarPartBox(part);
		ModelBox b;
		// Le `from` d'un rig est **absolu** (même repère que le pivot) : pivot + boîte
		// relative. L'avatar reste centré (pieds à −0,9 bloc), comme le maillage procédural.
		b.from = (AvatarPivot(part) + ab.min) * 16.f;
		b.size = (ab.max - ab.min) * 16.f;
		// Origine du dépliage « box UV » : coin supérieur gauche de la croix de la peau.
		b.uv = {ab.uv[size_t(BoxFace::PosX)][0], ab.uv[size_t(BoxFace::PosY)][1]};
		p.boxes.push_back(std::move(b));
		m.parts.push_back(std::move(p));
	}
	return m;
}

void BuildAvatarMesh(const AvatarPose &pose, const AvatarMeshOptions &options, std::vector<AvatarVertex> &out) {
	const float su = 1.f / options.textureWidth, sv = 1.f / options.textureHeight;
	auto emitBox = [&](const AvatarBox &b) {
		const FVector3 &mn = b.min, &mx = b.max;
		// Coins (sens antihoraire vu de l'extérieur) et UV par face, ordre +X, −X, +Y, −Y, +Z, −Z.
		struct Corner {
			FVector3 p;
			int u, v; // 0 : u0/v0, 1 : u1/v1
		};
		const std::array<std::array<Corner, 4>, 6> faces = {{
			{{{{mx.x, mn.y, mx.z}, 1, 1}, {{mx.x, mn.y, mn.z}, 0, 1}, {{mx.x, mx.y, mn.z}, 0, 0}, {{mx.x, mx.y, mx.z}, 1, 0}}},
			{{{{mn.x, mn.y, mn.z}, 0, 1}, {{mn.x, mn.y, mx.z}, 1, 1}, {{mn.x, mx.y, mx.z}, 1, 0}, {{mn.x, mx.y, mn.z}, 0, 0}}},
			{{{{mn.x, mx.y, mx.z}, 0, 1}, {{mx.x, mx.y, mx.z}, 1, 1}, {{mx.x, mx.y, mn.z}, 1, 0}, {{mn.x, mx.y, mn.z}, 0, 0}}},
			{{{{mn.x, mn.y, mn.z}, 0, 0}, {{mx.x, mn.y, mn.z}, 1, 0}, {{mx.x, mn.y, mx.z}, 1, 1}, {{mn.x, mn.y, mx.z}, 0, 1}}},
			{{{{mn.x, mn.y, mx.z}, 0, 1}, {{mx.x, mn.y, mx.z}, 1, 1}, {{mx.x, mx.y, mx.z}, 1, 0}, {{mn.x, mx.y, mx.z}, 0, 0}}},
			{{{{mx.x, mn.y, mn.z}, 1, 1}, {{mn.x, mn.y, mn.z}, 0, 1}, {{mn.x, mx.y, mn.z}, 0, 0}, {{mx.x, mx.y, mn.z}, 1, 0}}},
		}};
		for (size_t f = 0; f < 6; ++f) {
			FVector3 w[4];
			float uu[4], vv[4];
			for (size_t i = 0; i < 4; ++i) {
				w[i] = AvatarTransform(pose, b.part, faces[f][i].p);
				uu[i] = (faces[f][i].u ? b.uv[f][2] : b.uv[f][0]) * su;
				vv[i] = (faces[f][i].v ? b.uv[f][3] : b.uv[f][1]) * sv;
			}
			FVector3 n = (w[1] - w[0]).Cross(w[2] - w[0]);
			const float len = n.Length();
			n = len > 1e-8f ? n * (1.f / len) : FVector3{0.f, 1.f, 0.f};
			for (size_t i : {0u, 1u, 2u, 0u, 2u, 3u})
				out.push_back({w[i].x, w[i].y, w[i].z, n.x, n.y, n.z, uu[i], vv[i]});
		}
	};
	auto slim = [&](AvatarBox b) {
		if ((b.part == AvatarPart::ArmRight || b.part == AvatarPart::ArmLeft) && options.armThickness != 1.f) {
			b.min.x *= options.armThickness, b.max.x *= options.armThickness;
			b.min.z *= options.armThickness, b.max.z *= options.armThickness;
		}
		return b;
	};
	for (size_t i = 0; i < kAvatarParts; ++i) {
		if (!options.head && AvatarPart(i) == AvatarPart::Head)
			continue;
		emitBox(slim(AvatarPartBox(AvatarPart(i))));
	}
	for (const AvatarBox &b : options.extras)
		if (options.head || b.part != AvatarPart::Head)
			emitBox(b.part == AvatarPart::ArmRight || b.part == AvatarPart::ArmLeft ? (b.max.y - b.min.y > 0.5f ? slim(b) : b) : b);
}

void BuildAvatarExtrasMesh(const Model &rig, std::span<const PartOffset> pose, std::span<const AvatarBox> boxes, float textureWidth,
						   float textureHeight, std::vector<ModelVertex> &out) {
	const float su = 1.f / textureWidth, sv = 1.f / textureHeight;
	const float inv = 1.f / rig.unitsPerBlock;
	// Index de partie du rig pour chaque AvatarPart (par nom).
	std::array<int, kAvatarParts> partIndex;
	partIndex.fill(-1);
	for (size_t i = 0; i < rig.parts.size(); ++i)
		for (size_t p = 0; p < kAvatarParts; ++p)
			if (rig.parts[i].name == kAvatarPartRigNames[p]) {
				partIndex[p] = int(i);
				break;
			}
	static constexpr FVector3 kNormals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (const AvatarBox &b : boxes) {
		const int pi = partIndex[size_t(b.part)];
		if (pi < 0 || size_t(pi) >= pose.size())
			continue;
		const FVector3 rot = rig.parts[size_t(pi)].rotation + pose[size_t(pi)].rotation; // degrés
		const FVector3 off = pose[size_t(pi)].position * inv;							 // blocs
		const FVector3 &mn = b.min, &mx = b.max;
		struct Corner {
			FVector3 p;
			int u, v; // 0 : u0/v0, 1 : u1/v1
		};
		// Même table de coins/UV que `BuildAvatarMesh` (sens antihoraire vu de l'extérieur).
		const std::array<std::array<Corner, 4>, 6> faces = {{
			{{{{mx.x, mn.y, mx.z}, 1, 1}, {{mx.x, mn.y, mn.z}, 0, 1}, {{mx.x, mx.y, mn.z}, 0, 0}, {{mx.x, mx.y, mx.z}, 1, 0}}},
			{{{{mn.x, mn.y, mn.z}, 0, 1}, {{mn.x, mn.y, mx.z}, 1, 1}, {{mn.x, mx.y, mx.z}, 1, 0}, {{mn.x, mx.y, mn.z}, 0, 0}}},
			{{{{mn.x, mx.y, mx.z}, 0, 1}, {{mx.x, mx.y, mx.z}, 1, 1}, {{mx.x, mx.y, mn.z}, 1, 0}, {{mn.x, mx.y, mn.z}, 0, 0}}},
			{{{{mn.x, mn.y, mn.z}, 0, 0}, {{mx.x, mn.y, mn.z}, 1, 0}, {{mx.x, mn.y, mx.z}, 1, 1}, {{mn.x, mn.y, mx.z}, 0, 1}}},
			{{{{mn.x, mn.y, mx.z}, 0, 1}, {{mx.x, mn.y, mx.z}, 1, 1}, {{mx.x, mx.y, mx.z}, 1, 0}, {{mn.x, mx.y, mx.z}, 0, 0}}},
			{{{{mx.x, mn.y, mn.z}, 1, 1}, {{mn.x, mn.y, mn.z}, 0, 1}, {{mn.x, mx.y, mn.z}, 0, 0}, {{mx.x, mx.y, mn.z}, 1, 0}}},
		}};
		for (size_t f = 0; f < 6; ++f) {
			FVector3 w[4];
			float uu[4], vv[4];
			for (size_t i = 0; i < 4; ++i) {
				w[i] = RotateEuler(faces[f][i].p, rot) + off;
				uu[i] = (faces[f][i].u ? b.uv[f][2] : b.uv[f][0]) * su;
				vv[i] = (faces[f][i].v ? b.uv[f][3] : b.uv[f][1]) * sv;
			}
			const FVector3 n = RotateEuler(kNormals[f], rot);
			for (size_t i : {0u, 1u, 2u, 0u, 2u, 3u})
				out.push_back({w[i].x, w[i].y, w[i].z, n.x, n.y, n.z, uu[i], vv[i]});
		}
	}
}

} // namespace voxel
