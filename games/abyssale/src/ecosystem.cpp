#include "abyssale/ecosystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <core/morton.hpp>
#include <diagnostics/assert.hpp>
#include <diagnostics/trace.hpp>
#include <math/noise.hpp>

#include "abyssale/meshes.hpp"

namespace abyssale {

namespace detail {

/// État partagé des systèmes (ressource du monde ECS).
struct EcoState {
	const Terrain *terrain = nullptr;
	float time = 0.f;
	math::FVector3 diver{};
	math::FVector3 forward{0.f, 0.f, -1.f};
	bool exhale = false;
	math::Rng rng{0x11FE};
	std::map<uint64_t, std::vector<Orbit>> schools;			   ///< bancs par chunk
	std::map<uint64_t, std::vector<math::FVector3>> ventsByChunk; ///< sources de bulles par chunk
	int nearbyBubbles = 0;
	int exhaleQueue = 0; ///< bulles du souffle encore à émettre (une à une)
	float exhaleTimer = 0.f;
};

} // namespace detail

namespace {

using Sim = detail::EcoState;
using math::FMatrix4;
using math::FVector3;
using math::FVector4;

constexpr float kPi = 3.1415927f;
constexpr float kCaptureTime = 6.f; ///< instant des captures fixes (trajectoires calées dessus)

float Smoothstep(float a, float b, float x) {
	x = std::clamp((x - a) / (b - a), 0.f, 1.f);
	return x * x * (3.f - 2.f * x);
}

float WrapAngle(float a) {
	while (a > kPi)
		a -= kTau;
	while (a < -kPi)
		a += kTau;
	return a;
}

/// Repère d'un animal nez +X orienté selon `heading`, incliné de `roll` autour de son axe.
FMatrix4 Oriented(FVector3 pos, FVector3 heading, float roll, FVector3 scale) {
	const float yaw = std::atan2(-heading.z, heading.x);
	const float pitch = std::asin(std::clamp(heading.y / std::max(heading.Length(), 1e-5f), -1.f, 1.f));
	return FMatrix4::Translation(pos) * FMatrix4::Rotation({0.f, 1.f, 0.f}, yaw) * FMatrix4::Rotation({0.f, 0.f, 1.f}, pitch) *
		   FMatrix4::Rotation({1.f, 0.f, 0.f}, roll) * FMatrix4::Scale(scale);
}

/// Repère : colonnes x, y, z et origine.
FMatrix4 Basis(const FVector3 &x, const FVector3 &y, const FVector3 &z, const FVector3 &origin) {
	FMatrix4 m = FMatrix4::Identity();
	for (int i = 0; i < 3; ++i) {
		m.m[0][i] = x[i];
		m.m[1][i] = y[i];
		m.m[2][i] = z[i];
		m.m[3][i] = origin[i];
	}
	return m;
}

/// Hauteur du décor (relief, roches, épave) sous `q`, sondée de `above` mètres au-dessus sur
/// `reach` mètres ; à défaut, le relief.
float GroundBelow(const physics::World &phys, const Terrain &terrain, const FVector3 &q, float above, float reach, physics::UserId self) {
	physics::RayFilter filter;
	filter.bodies = false; // ni poissons, ni éponges : le décor seul porte
	filter.ignoreUser = self;
	if (const auto hit = phys.Raycast({q + FVector3{0.f, above, 0.f}, {0.f, -1.f, 0.f}}, reach, filter))
		return hit->point.y;
	return terrain.Height(q.x, q.z);
}

/// Facteur de lissage exponentiel pour une constante de temps `rate` (1/s).
float Smoothing(float dt, float rate) { return 1.f - std::exp(-dt * rate); }

/// Échelle (uniforme) d'une matrice d'instance.
float ScaleOf(const FMatrix4 &m) { return std::hypot(m.m[0][0], m.m[0][1], m.m[0][2]); }

/// Boîte immobile alignée sur les axes autour de la boîte locale `local` placée par `m`.
physics::Body BoxBody(const FMatrix4 &m, const math::FAABB &local, float shrink) {
	FVector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
	for (int i = 0; i < 8; ++i) {
		const FVector3 c{i & 1 ? local.max.x : local.min.x, i & 2 ? local.max.y : local.min.y, i & 4 ? local.max.z : local.min.z};
		const FVector3 p = m.TransformPoint(c);
		lo = math::Min(lo, p);
		hi = math::Max(hi, p);
	}
	physics::Body b;
	b.center = (lo + hi) * 0.5f;
	b.half = (hi - lo) * (0.5f * shrink);
	b.dynamic = false;
	return b;
}

/// Demi-dimensions locales d'une boîte.
FVector3 HalfOf(const math::FAABB &b) { return (b.max - b.min) * 0.5f; }

/// Repère d'un banc à l'instant `time` : centre, axes et vitesse de la formation.
struct SchoolFrame {
	FVector3 center, heading, side, up, velocity;
};

SchoolFrame FrameOf(const Orbit &o, const Terrain &t, float time) {
	const float a = o.phase + time * o.speed;
	const float da = 0.02f * (o.speed > 0.f ? 1.f : -1.f);
	const FVector3 c = o.At(t, a), next = o.At(t, a + da);
	SchoolFrame f;
	f.center = c;
	f.heading = (next - c).Normalized();
	f.side = FVector3{0.f, 1.f, 0.f}.Cross(f.heading).Normalized();
	f.up = f.heading.Cross(f.side);
	f.velocity = (next - c) * (o.speed / da);
	return f;
}

/// Place du poisson dans la formation : elle respire lentement (le banc se déforme).
FVector3 SlotTarget(const SchoolFrame &s, const SchoolMember &f, float time) {
	const FVector3 drift{std::sin(time * 0.7f + f.phase) * 0.5f, std::sin(time * 0.9f + f.phase * 1.7f) * 0.25f,
						 std::cos(time * 0.6f + f.phase * 0.8f) * 0.4f};
	const FVector3 off = f.slot + drift;
	return s.center + s.heading * off.x + s.up * off.y + s.side * off.z;
}

// ── Systèmes ─────────────────────────────────────────────────────────────────

/// Bancs : chaque poisson rejoint sa place et fuit le plongeur trop proche.
void SchoolSystem(ecs::World &w, float dt) {
	Sim &sim = *w.GetResource<Sim>();
	physics::World &phys = physics::PhysicsOf(w);
	std::map<uint64_t, std::vector<SchoolFrame>> frames;
	for (const auto &[key, orbits] : sim.schools) {
		std::vector<SchoolFrame> &fs = frames[key];
		fs.reserve(orbits.size());
		for (const Orbit &o : orbits)
			fs.push_back(FrameOf(o, *sim.terrain, sim.time));
	}
	constexpr float kAlarm = 7.f;
	constexpr float kFleeSpeed = 7.f;
	w.MakeQuery<SchoolMember, physics::RigidBody, ChunkTag>().Each([&](SchoolMember &f, physics::RigidBody &rb, ChunkTag &tag) {
		const auto it = frames.find(tag.key);
		if (it == frames.end() || f.school >= it->second.size())
			return;
		physics::Body &b = phys.GetBody(rb.id);
		const SchoolFrame &s = it->second[f.school];
		FVector3 target = SlotTarget(s, f, sim.time);
		target.y = std::max(target.y, sim.terrain->Height(target.x, target.z) + 1.2f);
		FVector3 desired = s.velocity + (target - b.center) * 1.2f;
		const FVector3 away = b.center - sim.diver;
		const float dist = away.Length();
		f.panic = std::max(0.f, f.panic - dt * 0.35f);
		if (dist < kAlarm)
			f.panic = std::max(f.panic, 1.f - dist / kAlarm);
		if (f.panic > 0.f && dist > 1e-3f)
			desired += away * (kFleeSpeed * f.panic / dist);
		if (dt > 0.f) {
			const float want = desired.Length(), got = b.velocity.Length();
			f.stuck = want > 0.8f && got < 0.3f * want ? f.stuck + dt : std::max(0.f, f.stuck - dt);
		}
		if (f.stuck > 1.f)
			desired.y += 3.f;
		if (f.stuck > 5.f) {
			physics::Body probe = physics::Body::MakeSphere(target, b.radius);
			if (!phys.Blocked(probe, -1, false)) {
				b.center = target;
				b.velocity = s.velocity;
				f.stuck = 0.f;
			} else
				f.stuck = 4.f;
		}
		const float speed = desired.Length();
		if (speed > 9.f)
			desired *= 9.f / speed;
		b.velocity += (desired - b.velocity) * (1.f - std::exp(-dt * (2.5f + 4.f * f.panic)));
		if (b.velocity.LengthSq() > 1e-4f)
			f.heading = (f.heading + (b.velocity.Normalized() - f.heading) * std::min(1.f, dt * 5.f)).Normalized();
	});
}

/// Raies : grandes boucles qui épousent le relief.
void CruiserSystem(ecs::World &w, float dt) {
	const Sim &sim = *w.GetResource<Sim>();
	const physics::World &phys = physics::PhysicsOf(w);
	const FVector3 up{0.f, 1.f, 0.f};
	w.MakeQuery<Cruiser, Render>().Each([&](ecs::Entity e, Cruiser &c, Render &r) {
		const Orbit &o = c.orbit;
		const float a = o.phase + sim.time * o.speed;
		const FVector3 base = o.At(*sim.terrain, a), next = o.At(*sim.terrain, a + 0.01f * (o.speed > 0.f ? 1.f : -1.f));
		FVector3 dir = next - base;
		dir.y = 0.f;
		dir = dir.LengthSq() > 1e-8f ? dir.Normalized() : FVector3{1.f, 0.f, 0.f};
		const FVector3 side{-dir.z, 0.f, dir.x};
		const physics::UserId self = physics::UserOf(e);
		const auto ground = [&](const FVector3 &q) { return GroundBelow(phys, *sim.terrain, q, 8.f, 40.f, self); };
		const float gUnder = ground(base), gAhead = ground(base + dir * (c.span * 1.6f));
		const float gLeft = ground(base - side * (c.span * 0.9f)), gRight = ground(base + side * (c.span * 0.9f));
		const float clearance = 1.5f;
		const float wanted = std::max(0.f, std::max({gUnder, gAhead, gLeft, gRight}) + clearance - base.y);
		const float prevLift = c.lift;
		c.lift = c.posed ? c.lift + (wanted - c.lift) * Smoothing(dt, 1.5f) : wanted;
		const FVector3 p = base + up * c.lift;
		const float climb = dt > 0.f ? (c.lift - prevLift) / dt : 0.f;
		const float pitchTarget = std::clamp(std::atan2(gAhead - gUnder, c.span * 1.6f) * 0.5f + climb * 0.12f, -0.45f, 0.45f);
		const float turnBank = std::clamp(o.speed * 5.f, -0.4f, 0.4f);
		float avoid = std::clamp(((p.y - gRight) - (p.y - gLeft)) * 0.25f, -0.6f, 0.6f);
		physics::RayFilter walls;
		walls.bodies = false;
		walls.ignoreUser = self;
		const float reach = c.span * 1.6f;
		for (const float s : {-1.f, 1.f})
			if (const auto hit = phys.Raycast({p, side * s}, reach, walls))
				avoid -= s * 0.6f * (1.f - hit->distance / reach);
		const float rollTarget = std::clamp(turnBank + avoid, -0.8f, 0.8f);
		c.pitch = c.posed ? c.pitch + (pitchTarget - c.pitch) * Smoothing(dt, 2.f) : pitchTarget;
		c.roll = c.posed ? c.roll + (rollTarget - c.roll) * Smoothing(dt, 2.f) : rollTarget;
		c.posed = true;
		const FVector3 heading = (dir + up * std::tan(c.pitch)).Normalized();
		r.instance.model = Oriented(p, heading, c.roll, {c.scale, c.scale, c.scale});
	});
}

/// Crabes et homards : cap qui dérive, rappel au terrier.
void WalkerSystem(ecs::World &w, float dt) {
	const Sim &sim = *w.GetResource<Sim>();
	physics::World &phys = physics::PhysicsOf(w);
	w.MakeQuery<Walker, physics::RigidBody>().Each([&](Walker &k, physics::RigidBody &rb) {
		physics::Body &b = phys.GetBody(rb.id);
		k.phase += dt;
		if (dt > 0.f) {
			const float progress = std::hypot(b.center.x - k.last.x, b.center.z - k.last.z);
			k.stuck = progress < k.speed * 0.25f * dt ? k.stuck + dt : std::max(0.f, k.stuck - dt);
		}
		if (k.stuck > 1.2f) {
			k.heading += kPi * (0.6f + 0.4f * std::sin(k.phase * 7.3f));
			k.stuck = 0.f;
		}
		const float drift = k.sideways ? 0.6f * std::sin(sim.time * 0.31f + k.phase * 1.7f) + 0.3f * std::sin(sim.time * 0.73f + k.phase * 2.9f)
									   : 0.4f * std::sin(sim.time * 0.25f + k.phase * 1.3f) + 0.2f * std::sin(sim.time * 0.6f + k.phase * 2.1f);
		k.heading += drift * dt;
		const FVector3 fromHome = b.center - k.home;
		if (std::hypot(fromHome.x, fromHome.z) > k.range) {
			const float back = std::atan2(-fromHome.z, -fromHome.x);
			const float want = k.sideways ? back - kPi * 0.5f : back;
			k.heading += WrapAngle(want - k.heading) * std::min(1.f, dt * 1.2f);
		}
		const FVector3 fwd{std::cos(k.heading), 0.f, std::sin(k.heading)};
		const FVector3 dir = k.sideways ? FVector3{-fwd.z, 0.f, fwd.x} : fwd;
		const float gait = k.sideways ? 0.55f + 0.45f * std::abs(std::sin(sim.time * 4.f + k.phase)) : 1.f;
		b.velocity.x = dir.x * k.speed * gait;
		b.velocity.z = dir.z * k.speed * gait;
		k.last = b.center;
	});
}

/// Crevettes : flottement autour d'un point, dards irréguliers, fuite devant le plongeur.
void HoverSystem(ecs::World &w, float dt) {
	const Sim &sim = *w.GetResource<Sim>();
	physics::World &phys = physics::PhysicsOf(w);
	w.MakeQuery<Hoverer, physics::RigidBody>().Each([&](Hoverer &h, physics::RigidBody &rb) {
		physics::Body &b = phys.GetBody(rb.id);
		h.phase += dt;
		h.dartTimer -= dt;
		const FVector3 away = b.center - sim.diver;
		const float dist = away.Length();
		if (dist < 2.5f && dist > 1e-3f && h.dartTimer < 1.2f) {
			h.dart = away * (3.f / dist);
			h.dartTimer = 1.5f;
		} else if (h.dartTimer <= 0.f) {
			const float ang = h.phase * 3.7f;
			h.dart = FVector3{std::cos(ang), 0.15f * std::sin(ang * 1.7f), std::sin(ang)} * (1.2f + 0.8f * std::abs(std::sin(h.phase * 5.3f)));
			h.dartTimer = 1.5f + 2.f * std::abs(std::sin(h.phase * 2.1f));
		}
		h.dart *= std::max(0.f, 1.f - dt * 2.f);
		const FVector3 hover{std::sin(sim.time * 0.4f + h.phase) * 0.4f, std::sin(sim.time * 0.7f + h.phase * 1.3f) * 0.2f,
							 std::cos(sim.time * 0.35f + h.phase * 1.1f) * 0.4f};
		if (dt > 0.f)
			b.velocity = (h.home + hover - b.center) * 1.5f + h.dart;
	});
}

/// Parties : les corps des tiges suivent l'ondulation du shader, ceux des raies leur vol.
void PartsSystem(ecs::World &w, float) {
	const Sim &sim = *w.GetResource<Sim>();
	physics::World &phys = physics::PhysicsOf(w);
	w.MakeQuery<Render, Parts>().Each([&](Render &r, Parts &parts) {
		const SwayParams sway = PlantSway(r.model);
		for (size_t i = 0; i < parts.count; ++i)
			phys.GetBody(parts.body[i]).center =
				r.instance.model.TransformPoint(parts.local[i]) + PlantSwayOffset(r.instance.model, parts.local[i], sim.time, sway);
	});
}

/// Pose d'une plante pliée : inclinaison autour de son pied vers le déplacement `bend`.
FMatrix4 BentPose(const Flexible &fx) {
	const float len = math::Length(fx.bend);
	if (len < 1e-4f)
		return fx.rest;
	const FVector3 dir{fx.bend.x / len, 0.f, fx.bend.y / len};
	return FMatrix4::Translation(fx.base) * FMatrix4::Rotation({dir.z, 0.f, -dir.x}, std::atan2(len, fx.reach)) * FMatrix4::Translation(fx.base * -1.f) * fx.rest;
}

/// Plantes souples : ce qui traverse leurs volumes les écarte, un ressort amorti les redresse.
void FlexSystem(ecs::World &w, float dt) {
	physics::World &phys = physics::PhysicsOf(w);
	std::vector<std::pair<FVector3, float>> sweepers;
	w.MakeQuery<Cruiser, Parts>().Each([&](Cruiser &, Parts &p) {
		for (size_t i = 0; i < p.count; ++i)
			sweepers.emplace_back(phys.GetBody(p.body[i]).center, phys.GetBody(p.body[i]).radius);
	});
	physics::Body *diver = nullptr;
	w.MakeQuery<Player, physics::RigidBody>().Each([&](Player &, physics::RigidBody &rb) { diver = &phys.GetBody(rb.id); });
	constexpr float kStiffness = 1.2f, kDamping = 1.7f, kGain = 9.f;
	w.MakeQuery<Flexible, Render, Parts>().Each([&](Flexible &fx, Render &r, Parts &parts) {
		math::FVector2 push{};
		bool slowsDiver = false;
		const float top = std::max(parts.local[parts.count - 1u].y, 1e-3f);
		for (size_t i = 0; i < parts.count; ++i) {
			const physics::Body &part = phys.GetBody(parts.body[i]);
			const float lever = parts.local[i].y / top;
			const auto add = [&](const FVector3 &other, float radius) {
				const FVector3 d = part.center - other;
				const float overlap = part.radius + radius - d.Length();
				if (overlap <= 0.f)
					return;
				const float flat = std::hypot(d.x, d.z);
				const math::FVector2 dir = flat > 1e-4f ? math::FVector2{d.x / flat, d.z / flat} : math::FVector2{1.f, 0.f};
				push += dir * (overlap * lever);
			};
			for (const physics::UserId u : part.touching) {
				const ecs::Entity e = physics::EntityOf(u);
				const physics::RigidBody *rb = w.Get<physics::RigidBody>(e);
				if (!rb || !phys.GetBody(rb->id).dynamic)
					continue;
				const physics::Body &other = phys.GetBody(rb->id);
				add(other.center, std::max({other.Extent().x, other.Extent().y, other.Extent().z}));
				slowsDiver = slowsDiver || &other == diver;
			}
			for (const auto &[center, radius] : sweepers)
				add(center, radius);
		}
		fx.velocity += (push * kGain - fx.bend * kStiffness - fx.velocity * kDamping) * dt;
		fx.bend += fx.velocity * dt;
		const float len = math::Length(fx.bend), limit = fx.reach * 0.8f;
		if (len > limit)
			fx.bend *= limit / len;
		r.instance.model = BentPose(fx);
		if (slowsDiver && diver)
			diver->velocity *= std::exp(-1.2f * dt);
	});
}

/// Point d'émission d'une plante (flotteur), ondulation comprise.
FVector3 SourcePoint(const Render &plant, const FVector3 &local, float time) {
	return plant.instance.model.TransformPoint(local) + PlantSwayOffset(plant.instance.model, local, time, PlantSway(plant.model));
}

/// Limites de l'eau : rien ne sort par la surface ni sous le fond (filet de sécurité).
void BoundsSystem(ecs::World &w, float) {
	const Sim &sim = *w.GetResource<Sim>();
	physics::World &phys = physics::PhysicsOf(w);
	const physics::Body *player = nullptr;
	w.MakeQuery<Player, physics::RigidBody>().Each([&](Player &, physics::RigidBody &rb) { player = &phys.GetBody(rb.id); });
	const auto finite = [](const FVector3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
	for (size_t i = 0; i < phys.BodyCount(); ++i) {
		physics::Body &b = phys.GetBody(physics::BodyId(i));
		if (!b.enabled || !b.dynamic)
			continue;
		if (!DIAG_VERIFY(finite(b.center) && finite(b.velocity), "corps {} non fini : centre ({}, {}, {}), vitesse ({}, {}, {})", i, b.center.x, b.center.y,
						 b.center.z, b.velocity.x, b.velocity.y, b.velocity.z)) {
			b.velocity = {};
			b.center = finite(b.center) ? b.center : FVector3{0.f, sim.terrain->Height(0.f, 0.f) + 3.f, 0.f};
		}
		const bool isPlayer = &b == player;
		const float ceiling = isPlayer ? kCeilingY : kSurfaceY - 1.f;
		if (b.center.y > ceiling) {
			b.center.y = ceiling;
			b.velocity.y = std::min(b.velocity.y, 0.f);
		}
		const float floor = sim.terrain->Height(b.center.x, b.center.z) + b.Extent().y * 0.5f;
		if (b.center.y < floor) {
			b.center.y = floor;
			b.velocity.y = std::max(b.velocity.y, 0.f);
		}
	}
}

/// Poses : matrices d'affichage des animaux simulés.
void PoseSystem(ecs::World &w, float dt) {
	const Sim &sim = *w.GetResource<Sim>();
	physics::World &phys = physics::PhysicsOf(w);
	w.MakeQuery<SchoolMember, physics::RigidBody, Render>().Each([&](SchoolMember &f, physics::RigidBody &rb, Render &r) {
		r.instance.model = Oriented(phys.GetBody(rb.id).center, f.heading, 0.f, {f.scale, f.scale, f.scale});
	});
	w.MakeQuery<Walker, physics::RigidBody, Render>().Each([&](ecs::Entity e, Walker &k, physics::RigidBody &rb, Render &r) {
		const float wobble = k.sideways ? 0.05f * std::sin(sim.time * 4.f + k.phase) : 0.f;
		const FVector3 fwd{std::cos(k.heading + wobble), 0.f, std::sin(k.heading + wobble)};
		const FVector3 side{-fwd.z, 0.f, fwd.x};
		const FVector3 c = phys.GetBody(rb.id).center;
		FVector3 normal{0.f, 1.f, 0.f};
		float plane = c.y + k.foot, footY = 0.f;
		if (k.footCount >= 3) {
			double su = 0, sv = 0, sh = 0;
			std::array<float, 8> h{};
			for (size_t i = 0; i < k.footCount; ++i) {
				const FVector3 &f = k.feet[i];
				const FVector3 q = c + fwd * f.x + side * f.z;
				h[i] = GroundBelow(phys, *sim.terrain, q, 0.8f, 2.5f, physics::UserOf(e));
				su += double(f.x);
				sv += double(f.z);
				sh += double(h[i]);
				footY += f.y / float(k.footCount);
			}
			const double n = double(k.footCount), mu = su / n, mv = sv / n, mh = sh / n;
			double suu = 0, svv = 0, suv = 0, suh = 0, svh = 0;
			for (size_t i = 0; i < k.footCount; ++i) {
				const double du = double(k.feet[i].x) - mu, dv = double(k.feet[i].z) - mv, dh = double(h[i]) - mh;
				suu += du * du;
				svv += dv * dv;
				suv += du * dv;
				suh += du * dh;
				svh += dv * dh;
			}
			const double det = suu * svv - suv * suv;
			const double slopeU = std::abs(det) > 1e-9 ? (suh * svv - svh * suv) / det : 0.0;
			const double slopeV = std::abs(det) > 1e-9 ? (svh * suu - suh * suv) / det : 0.0;
			normal = (FVector3{0.f, 1.f, 0.f} - fwd * float(slopeU) - side * float(slopeV)).Normalized();
			if (normal.y < 0.82f) {
				const FVector3 flat = FVector3{normal.x, 0.f, normal.z}.Normalized();
				normal = (flat * 0.572f + FVector3{0.f, 0.82f, 0.f}).Normalized();
			}
			plane = float(mh - slopeU * mu - slopeV * mv);
		}
		k.up = k.posed ? (k.up + (normal - k.up) * Smoothing(dt, 8.f)).Normalized() : normal;
		const float height = plane - footY;
		k.height = k.posed ? k.height + (height - k.height) * Smoothing(dt, 10.f) : height;
		k.posed = true;
		const FVector3 x = (fwd - k.up * fwd.Dot(k.up)).Normalized();
		const FVector3 origin = FVector3{c.x, k.height, c.z} + (k.up - FVector3{0.f, 1.f, 0.f}) * (-footY);
		r.instance.model = Basis(x, k.up, x.Cross(k.up), origin);
	});
	w.MakeQuery<Hoverer, physics::RigidBody, Render>().Each([&](Hoverer &h, physics::RigidBody &rb, Render &r) {
		const physics::Body &b = phys.GetBody(rb.id);
		const FVector3 dir = h.dart.LengthSq() > 0.01f ? h.dart : b.velocity;
		const float yaw = dir.LengthSq() > 1e-4f ? std::atan2(-dir.z, dir.x) : 0.f;
		r.instance.model = FMatrix4::Translation(b.center) * FMatrix4::Rotation({0.f, 1.f, 0.f}, yaw);
	});
}

/// Benthos animé : respiration des anémones, rétraction des vers près du plongeur.
void BenthosSystem(ecs::World &w, float dt) {
	const Sim &sim = *w.GetResource<Sim>();
	w.MakeQuery<Pulsing, Render>().Each([&](Pulsing &p, Render &r) {
		const float breath = std::sin(sim.time * 1.8f + p.phase);
		const float pulse = (1.f + 0.06f * breath + 0.02f * std::sin(sim.time * 4.3f + p.phase * 2.1f)) * p.scale;
		r.instance.model = FMatrix4::Translation(p.base) * FMatrix4::Scale({pulse, (1.f + 0.04f * breath) * p.scale, pulse});
	});
	w.MakeQuery<Retracting, Render>().Each([&](Retracting &f, Render &r) {
		const float target = Smoothstep(2.f, 6.f, (f.base - sim.diver).Length());
		f.deploy += (target - f.deploy) * std::min(1.f, dt * 2.f);
		const float sway = 0.08f * std::sin(sim.time * 0.9f + f.phase) * f.deploy;
		const float sxz = 0.7f + 0.3f * f.deploy;
		r.instance.model = FMatrix4::Translation(f.base) * FMatrix4::Rotation({1.f, 0.f, 0.f}, sway) * FMatrix4::Rotation({0.f, 0.f, 1.f}, sway * 0.7f) *
						   FMatrix4::Scale({sxz, 0.35f + 0.65f * f.deploy, sxz});
	});
}

/// Bulles : colonnes des sources du fond et souffle du plongeur.
void BubbleSystem(ecs::World &w, float dt) {
	Sim &sim = *w.GetResource<Sim>();
	if (sim.exhale)
		sim.exhaleQueue += 8 + int(sim.rng.NextFloat() * 5.f);
	sim.exhaleTimer -= dt;
	int toSpawn = 0;
	auto bubbles = w.MakeQuery<BubbleState, Render>();
	while (sim.exhaleQueue > 0 && sim.exhaleTimer <= 0.f) {
		const FVector3 side = FVector3{0.f, 1.f, 0.f}.Cross(sim.forward).Normalized();
		const FVector3 p = sim.diver + sim.forward * 0.3f + side * ((sim.rng.NextFloat() - 0.5f) * 0.25f) + FVector3{0.f, -0.55f, 0.f};
		const BubbleState fresh{p, 0.8f + 0.4f * sim.rng.NextFloat(), 0.008f + 0.016f * sim.rng.NextFloat(), sim.rng.NextFloat() * kTau, -1, true};
		bool reused = false;
		bubbles.Each([&](BubbleState &b, Render &) {
			if (!reused && !b.alive) {
				b = fresh;
				reused = true;
			}
		});
		if (!reused)
			++toSpawn;
		--sim.exhaleQueue;
		sim.exhaleTimer += 0.05f + 0.05f * sim.rng.NextFloat();
	}
	for (int i = 0; i < toSpawn; ++i) {
		const FVector3 side = FVector3{0.f, 1.f, 0.f}.Cross(sim.forward).Normalized();
		const FVector3 p = sim.diver + sim.forward * 0.3f + side * ((sim.rng.NextFloat() - 0.5f) * 0.25f) + FVector3{0.f, -0.55f, 0.f};
		w.Spawn(BubbleState{p, 0.8f + 0.4f * sim.rng.NextFloat(), 0.008f + 0.016f * sim.rng.NextFloat(), sim.rng.NextFloat() * kTau, -1, true},
				Render{Model::Bubble, {}});
	}
	sim.nearbyBubbles = 0;
	w.MakeQuery<BubbleState, Render>().Each([&](BubbleState &b, Render &r) {
		if (b.alive) {
			b.pos.y += b.speed * dt;
			b.pos.x += std::sin(sim.time * 3.1f + b.phase) * 0.25f * dt;
			b.pos.z += std::cos(sim.time * 2.7f + b.phase) * 0.25f * dt;
			if (b.pos.y > kSurfaceY - 0.4f) {
				if (b.source) {
					if (const Render *plant = w.Get<Render>(b.source))
						b.pos = SourcePoint(*plant, b.local, sim.time);
					else
						b.alive = false;
				} else if (b.vent < 0) {
					b.alive = false;
				} else {
					const auto it = sim.ventsByChunk.find(b.chunk);
					if (it == sim.ventsByChunk.end() || it->second.empty())
						b.alive = false;
					else {
						const std::vector<FVector3> &vents = it->second;
						b.pos = vents[size_t(b.vent) % vents.size()] + FVector3{(sim.rng.NextFloat() - 0.5f) * 0.4f, 0.f, (sim.rng.NextFloat() - 0.5f) * 0.4f};
					}
				}
			}
		}
		if (!b.alive) {
			r.instance.model = FMatrix4::Scale({0.f, 0.f, 0.f});
			return;
		}
		float grow = 1.f + 0.4f * Smoothstep(-40.f, 0.f, b.pos.y);
		if (b.vent < 0 && !b.source)
			grow *= 0.3f + 0.7f * Smoothstep(0.f, 2.f, (b.pos - sim.diver).y + 0.55f);
		const float wobble = 1.f + 0.12f * std::sin(sim.time * 9.f + b.phase);
		const float sz = b.size * grow;
		if ((b.pos - sim.diver).LengthSq() < 36.f)
			++sim.nearbyBubbles;
		r.instance.model = FMatrix4::Translation(b.pos) * FMatrix4::Scale({sz * wobble, sz / wobble, sz * wobble});
	});
}

// ── Sérialisation binaire ─────────────────────────────────────────────────────

void WriteF(std::vector<uint8_t> &out, float v) {
	uint32_t bits;
	std::memcpy(&bits, &v, 4);
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(bits >> (i * 8)));
}
float ReadF(const uint8_t *&p) {
	const uint32_t bits = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
	p += 4;
	float v;
	std::memcpy(&v, &bits, 4);
	return v;
}
void WriteU(std::vector<uint8_t> &out, uint32_t v) {
	for (int i = 0; i < 4; ++i)
		out.push_back(uint8_t(v >> (i * 8)));
}
uint32_t ReadU(const uint8_t *&p) {
	const uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
	p += 4;
	return v;
}
void WriteV3(std::vector<uint8_t> &out, const FVector3 &v) {
	WriteF(out, v.x);
	WriteF(out, v.y);
	WriteF(out, v.z);
}
FVector3 ReadV3(const uint8_t *&p) { return {ReadF(p), ReadF(p), ReadF(p)}; }

} // namespace

// ── Trajectoires ─────────────────────────────────────────────────────────────

math::FVector3 Orbit::At(const Terrain &terrain, float angle) const noexcept {
	const float r = terrainRelative ? radius * (1.f + 0.25f * std::sin(angle * 2.3f)) : radius;
	const float x = center.x + std::cos(angle) * r, z = center.z + std::sin(angle) * r;
	const float bob = 2.f * std::sin(angle * 1.7f) * (terrainRelative ? 1.f : 0.3f);
	const float y = terrainRelative ? terrain.Height(x, z) + height + bob : std::max(height + bob, terrain.Height(x, z) + 2.f);
	return {x, std::min(y, kSurfaceY - 4.f), z};
}

Orbit Orbit::Through(const FVector3 &pivot, const FVector3 &through, float speed, float time) {
	Orbit o;
	o.center = pivot;
	o.radius = std::max(3.f, std::hypot(through.x - pivot.x, through.z - pivot.z));
	o.height = through.y;
	o.speed = speed;
	o.phase = std::atan2(through.z - pivot.z, through.x - pivot.x) - time * speed;
	o.terrainRelative = false;
	return o;
}

// ── Formes ───────────────────────────────────────────────────────────────────

std::vector<math::FVector3> TriangleSoup(const sdl3::MeshData &mesh) {
	std::vector<math::FVector3> out;
	out.reserve(mesh.indices.size());
	for (const uint32_t i : mesh.indices)
		out.push_back(mesh.vertices[i].position);
	return out;
}

Shapes MakeShapes(std::span<const sdl3::MeshData> models) {
	Shapes s;
	for (size_t m = 0; m < models.size() && m < kModelCount; ++m)
		if (!models[m].vertices.empty())
			s.bounds[m] = models[m].Bounds();
	const auto soup = [&](Model m) { return size_t(m) < models.size() ? TriangleSoup(models[size_t(m)]) : std::vector<math::FVector3>{}; };
	s.arch = soup(Model::Arch);
	s.wreck = soup(Model::Wreck);
	s.fluke = soup(Model::Fluke);
	for (size_t i = 0; i < s.rocks.size(); ++i) {
		const meshes::RockShape &shape = meshes::kRockShapes[i];
		s.rocks[i] = TriangleSoup(meshes::Rock(16, shape.roughness, shape.flatten, shape.seed));
	}
	return s;
}

std::vector<uint8_t> SerializeRecords(std::span<const EntityRecord> records) {
	std::vector<uint8_t> out;
	out.reserve(records.size() * 96);
	for (const EntityRecord &r : records) {
		out.push_back(uint8_t(r.kind));
		WriteV3(out, r.position);
		WriteV3(out, r.velocity);
		WriteV3(out, r.anchor);
		WriteV3(out, r.headingVec);
		WriteF(out, r.heading);
		WriteF(out, r.scale);
		WriteF(out, r.phase);
		WriteF(out, r.radius);
		WriteF(out, r.height);
		WriteF(out, r.speed);
		WriteF(out, r.lift);
		WriteF(out, r.pitch);
		WriteF(out, r.roll);
		WriteF(out, r.range);
		WriteU(out, r.school);
		WriteU(out, r.flags);
	}
	return out;
}

std::vector<EntityRecord> DeserializeRecords(std::span<const uint8_t> bytes) {
	std::vector<EntityRecord> out;
	const uint8_t *p = bytes.data();
	const uint8_t *end = bytes.data() + bytes.size();
	while (p < end) {
		EntityRecord r;
		r.kind = EntityKind(*p++);
		r.position = ReadV3(p);
		r.velocity = ReadV3(p);
		r.anchor = ReadV3(p);
		r.headingVec = ReadV3(p);
		r.heading = ReadF(p);
		r.scale = ReadF(p);
		r.phase = ReadF(p);
		r.radius = ReadF(p);
		r.height = ReadF(p);
		r.speed = ReadF(p);
		r.lift = ReadF(p);
		r.pitch = ReadF(p);
		r.roll = ReadF(p);
		r.range = ReadF(p);
		r.school = ReadU(p);
		r.flags = ReadU(p);
		out.push_back(r);
	}
	return out;
}

// ── Écosystème ───────────────────────────────────────────────────────────────

Ecosystem::Ecosystem(const Terrain &terrain, const Shapes &shapes, const FVector3 &diverStart, float startTime, uint64_t seed)
	: m_terrain(terrain), m_shapes(shapes) {
	Sim &sim = m_ecs.SetResource<Sim>();
	m_state = &sim;
	m_physics = &physics::PhysicsOf(m_ecs);
	sim.terrain = &terrain;
	sim.time = startTime;
	sim.rng = math::Rng(seed);
	sim.diver = diverStart;
	m_ecs.SetResource<physics::Gravity>();

	m_player = m_ecs.Spawn(Player{});
	physics::Body diver = physics::Body::MakeSphere(diverStart, 0.5f);
	diver.mass = 80.f;
	diver.gravityScale = 0.05f;
	diver.drag = 1.1f;
	diver.bounce = 0.f;
	diver.friction = 0.5f;
	m_playerBody = physics::AttachBody(m_ecs, m_player, diver);

	const auto traced = [](const char *name, void (*system)(ecs::World &, float)) {
		return [id = diagnostics::trace::Intern(std::string("Ecosystem::") + name), system](ecs::World &w, float dt) {
			const diagnostics::trace::Zone zone(id);
			system(w, dt);
		};
	};
	m_schedule.Add("bancs", traced("bancs", SchoolSystem))
		.Add("raies", traced("raies", CruiserSystem))
		.Add("crustacés", traced("crustaces", WalkerSystem))
		.Add("crevettes", traced("crevettes", HoverSystem))
		.Add("parties", traced("parties", PartsSystem))
		.Add("physique", traced("physique", physics::StepSystem))
		.Add("flexion", traced("flexion", FlexSystem))
		.Add("limites", traced("limites", BoundsSystem))
		.Add("poses", traced("poses", PoseSystem))
		.Add("benthos", traced("benthos", BenthosSystem))
		.Add("bulles", traced("bulles", BubbleSystem));
	CollectInstances(true);
}

Ecosystem::~Ecosystem() = default;

void Ecosystem::SpawnScenery(uint64_t key, const World &world) {
	physics::World &phys = *m_physics;
	const auto [cx, cz] = core::morton::Decode2(key);
	// Entité porteuse des triangles du décor du chunk (retirée d'un coup au déchargement).
	const ecs::Entity ground = m_ecs.Spawn(ChunkTag{key});
	m_chunkGround[key] = ground;
	const physics::UserId groundUser = physics::UserOf(ground);
	const FVector4 white{1.f, 1.f, 1.f, 1.f};
	const auto still = [&](Model m, const sdl3::MeshInstance &inst) { return m_ecs.Spawn(Render{m, inst}, Static{}, ChunkTag{key}); };
	const auto solid = [&](const std::vector<FVector3> &local, const FMatrix4 &transform) {
		std::vector<FVector3> placed;
		placed.reserve(local.size());
		for (const FVector3 &v : local)
			placed.push_back(transform.TransformPoint(v));
		phys.AddTriangles(placed, groundUser);
	};

	phys.AddTriangles(m_terrain.BuildChunkCollision(cx, cz), groundUser);
	// Rochers, arches, épave et nageoire : rendus ET bloquants (les deux, sinon invisibles).
	for (size_t v = 0; v < world.rocks.size(); ++v)
		for (const sdl3::MeshInstance &inst : world.rocks[v]) {
			still(Variant(Model::Rock0, v), inst);
			solid(m_shapes.rocks[v], inst.model);
		}
	for (const sdl3::MeshInstance &inst : world.arches) {
		still(Model::Arch, inst);
		solid(m_shapes.arch, inst.model);
	}
	if (world.Kind() == ChunkKind::Entry || world.Kind() == ChunkKind::Shipwreck) {
		still(Model::Wreck, {world.wreck, white});
		solid(m_shapes.wreck, world.wreck);
	}
	if (world.Kind() == ChunkKind::Entry) {
		still(Model::Fluke, {world.fluke, white});
		solid(m_shapes.fluke, world.fluke);
	}
	const auto boxed = [&](Model m, const sdl3::MeshInstance &inst, float shrink) {
		physics::AttachBody(m_ecs, still(m, inst), BoxBody(inst.model, m_shapes.bounds[size_t(m)], shrink));
	};
	const auto stem = [&](Model m, const sdl3::MeshInstance &inst, float height, float radius, size_t count) {
		const ecs::Entity e = m_ecs.Spawn(Render{m, inst}, ChunkTag{key});
		Parts parts;
		const float s = ScaleOf(inst.model);
		for (size_t i = 0; i < count; ++i) {
			parts.local[i] = {0.f, height * (0.15f + 0.75f * float(i) / float(count - 1)), 0.f};
			physics::Body b = physics::Body::MakeSphere(inst.model.TransformPoint(parts.local[i]), radius * s);
			b.dynamic = false;
			b.ghost = true;
			b.user = physics::UserOf(e);
			parts.body[i] = phys.AddBody(b);
		}
		parts.count = uint8_t(count);
		m_ecs.Add(e, parts);
		m_ecs.Add(e, Flexible{inst.model, {inst.model.m[3][0], inst.model.m[3][1], inst.model.m[3][2]}, height * s * 0.75f, {}, {}});
	};
	for (size_t v = 0; v < world.kelp.size(); ++v)
		for (const sdl3::MeshInstance &inst : world.kelp[v])
			stem(Variant(Model::Kelp0, v), inst, kKelpHeights[v], 0.55f, 4);
	for (size_t v = 0; v < world.fronds.size(); ++v)
		for (const sdl3::MeshInstance &inst : world.fronds[v])
			stem(Variant(Model::Frond0, v), inst, kFrondHeights[v], 0.35f, 3);

	for (const sdl3::MeshInstance &inst : world.tufts)
		still(Model::Tuft, inst);
	for (size_t v = 0; v < world.seagrass.size(); ++v)
		for (const sdl3::MeshInstance &inst : world.seagrass[v])
			still(Variant(Model::Seagrass0, v), inst);
	for (const sdl3::MeshInstance &inst : world.tubeWorms)
		still(Model::TubeWorm, inst);
	for (const sdl3::MeshInstance &inst : world.barnacles)
		still(Model::Barnacles, inst);
	for (size_t i = 0; i < world.barrels.size(); ++i)
		boxed(i % 2 ? Model::Barrel1 : Model::Barrel0, world.barrels[i], 0.85f);
	for (const sdl3::MeshInstance &inst : world.tubes)
		boxed(Model::Tubes, inst, 0.8f);
	for (const sdl3::MeshInstance &inst : world.corals)
		boxed(Model::Coral, inst, 0.75f);
	for (const sdl3::MeshInstance &inst : world.fans)
		boxed(Model::Fan, inst, 0.7f);
	for (const sdl3::MeshInstance &inst : world.mushroomCorals)
		boxed(Model::Mushroom, inst, 0.85f);
	for (const sdl3::MeshInstance &inst : world.seaWhips)
		boxed(Model::SeaWhip, inst, 0.5f);
	for (const sdl3::MeshInstance &inst : world.seaPens)
		boxed(Model::SeaPen, inst, 0.5f);
	for (const sdl3::MeshInstance &inst : world.chimneys)
		boxed(Model::VentChimney, inst, 0.75f);
	for (const sdl3::MeshInstance &inst : world.statues)
		boxed(Model::Statue, inst, 0.8f);
}

void Ecosystem::SpawnLife(uint64_t key, const World &world, const Quality &q, uint64_t chunkSeed, std::span<const EntityRecord> saved) {
	Sim &sim = *m_ecs.GetResource<Sim>();
	physics::World &phys = *m_physics;
	math::Rng rng(chunkSeed);
	const Terrain &terrain = m_terrain;
	const auto &lms = world.Landmarks();

	std::vector<const EntityRecord *> savedFish, savedManta, savedCrab, savedLobster, savedShrimp;
	for (const EntityRecord &r : saved) {
		switch (r.kind) {
		case EntityKind::Fish: savedFish.push_back(&r); break;
		case EntityKind::Manta: savedManta.push_back(&r); break;
		case EntityKind::Crab: savedCrab.push_back(&r); break;
		case EntityKind::Lobster: savedLobster.push_back(&r); break;
		case EntityKind::Shrimp: savedShrimp.push_back(&r); break;
		}
	}
	const FVector3 fishHalf = HalfOf(m_shapes.bounds[size_t(Model::Fish)]);
	const float fishRadius = 0.45f * (fishHalf.x + std::max(fishHalf.y, fishHalf.z));

	// ── Bancs (structure déterministe, recréée à chaque chargement) ──
	std::vector<Orbit> &schools = sim.schools[key];
	schools.clear();
	for (size_t s = 0; s < lms.size(); ++s)
		schools.push_back({{lms[s].center.x, 0.f, lms[s].center.y}, lms[s].radius * (0.4f + 0.25f * rng.NextFloat()), 6.f + 9.f * rng.NextFloat(),
							(0.05f + 0.05f * rng.NextFloat()) * (s % 2 ? 1.f : -1.f), rng.NextFloat() * kTau, true});
	sim.ventsByChunk[key] = world.vents;

	const auto spawnFish = [&](const EntityRecord &r) {
		SchoolMember f;
		f.school = r.school;
		f.slot = r.anchor;
		f.scale = r.scale;
		f.phase = r.phase;
		f.heading = r.headingVec.LengthSq() > 1e-6f ? r.headingVec.Normalized() : FVector3{1.f, 0.f, 0.f};
		const FVector4 tint{0.78f, 0.85f, 0.92f, 1.f};
		const ecs::Entity e = m_ecs.Spawn(f, Render{Model::Fish, {FMatrix4::Identity(), tint}}, ChunkTag{key});
		physics::Body b = physics::Body::MakeSphere(r.position, fishRadius * f.scale);
		b.velocity = r.velocity;
		b.gravityScale = 0.f;
		b.mass = 0.4f * f.scale;
		b.bounce = 0.f;
		b.friction = 0.f;
		physics::AttachBody(m_ecs, e, b);
	};
	if (!savedFish.empty()) {
		for (const EntityRecord *rp : savedFish)
			spawnFish(*rp);
	} else {
		const size_t nSchools = std::max<size_t>(1, schools.size());
		for (size_t s = 0; s < schools.size(); ++s) {
			const size_t count = size_t(float(q.fish) / float(nSchools)) + (s < size_t(q.fish) % nSchools ? 1 : 0);
			const float spread = std::cbrt(float(std::max<size_t>(1, count)) / 60.f);
			const SchoolFrame frame = FrameOf(schools[s], terrain, sim.time);
			for (size_t i = 0; i < count; ++i) {
				FVector3 o;
				do
					o = {rng.NextFloat() * 2.f - 1.f, rng.NextFloat() * 2.f - 1.f, rng.NextFloat() * 2.f - 1.f};
				while (o.LengthSq() > 1.f);
				const float g = 0.7f + 0.5f * rng.NextFloat();
				SchoolMember f;
				f.school = uint32_t(s);
				f.slot = {o.x * 3.5f * spread, o.y * 1.3f * spread, o.z * 2.4f * spread};
				f.scale = 0.7f + 0.8f * rng.NextFloat() * rng.NextFloat();
				f.phase = rng.NextFloat() * kTau;
				f.heading = frame.heading;
				const FVector4 tint{0.72f * g, 0.82f * g, 0.9f * g, 1.f};
				const ecs::Entity e = m_ecs.Spawn(f, Render{Model::Fish, {FMatrix4::Identity(), tint}}, ChunkTag{key});
				physics::Body b = physics::Body::MakeSphere(SlotTarget(frame, f, sim.time), fishRadius * f.scale);
				for (int lift = 0; lift < 6 && phys.Blocked(b, -1, false); ++lift)
					b.center.y += 1.f;
				if (phys.Blocked(b, -1, false))
					b.center = frame.center;
				b.velocity = frame.velocity;
				b.gravityScale = 0.f;
				b.mass = 0.4f * f.scale;
				b.bounce = 0.f;
				b.friction = 0.f;
				physics::AttachBody(m_ecs, e, b);
			}
		}
	}

	// ── Raies ──
	const FVector3 mantaHalf = HalfOf(m_shapes.bounds[size_t(Model::Manta)]);
	const auto spawnManta = [&](const EntityRecord &r) {
		Cruiser c;
		c.orbit.center = r.anchor;
		c.orbit.radius = r.radius;
		c.orbit.height = r.height;
		c.orbit.speed = r.speed;
		c.orbit.phase = r.phase;
		c.orbit.terrainRelative = (r.flags & 1u) != 0;
		c.scale = r.scale;
		c.span = r.range;
		c.lift = r.lift;
		c.pitch = r.pitch;
		c.roll = r.roll;
		c.posed = (r.flags & 4u) != 0;
		const ecs::Entity e = m_ecs.Spawn(c, Render{Model::Manta, {FMatrix4::Identity(), {1.f, 1.f, 1.f, 1.f}}}, ChunkTag{key});
		Parts parts;
		parts.local = {FVector3{0.f, 0.f, 0.f}, FVector3{0.f, 0.f, mantaHalf.z * 0.6f}, FVector3{0.f, 0.f, -mantaHalf.z * 0.6f}, FVector3{}};
		parts.count = 3;
		for (size_t i = 0; i < parts.count; ++i) {
			physics::Body b = physics::Body::MakeSphere({}, std::max(0.2f, 0.3f * mantaHalf.z * c.scale));
			b.dynamic = false;
			b.user = physics::UserOf(e);
			parts.body[i] = phys.AddBody(b);
		}
		m_ecs.Add(e, parts);
	};
	if (!savedManta.empty()) {
		for (const EntityRecord *rp : savedManta)
			spawnManta(*rp);
	} else {
		for (size_t i = 0; i < std::min<size_t>(lms.size(), 2); ++i) {
			const Landmark &lm = lms[i];
			EntityRecord r;
			r.anchor = {lm.center.x, 0.f, lm.center.y};
			r.radius = lm.radius * 0.55f;
			r.height = 10.f + 8.f * rng.NextFloat();
			r.speed = (0.045f + 0.03f * rng.NextFloat()) * (i % 2 ? 1.f : -1.f);
			r.phase = rng.NextFloat() * kTau;
			r.scale = 3.2f;
			r.range = mantaHalf.z * 3.2f;
			r.flags = 1u; // terrainRelative
			spawnManta(r);
		}
	}

	// ── Faune du fond ──
	const auto randomOnFloor = [&](math::FVector2 c, float rMin, float rMax) {
		const float ang = rng.NextFloat() * kTau;
		const float r = rMin + (rMax - rMin) * rng.NextFloat();
		const float x = c.x + std::cos(ang) * r, z = c.y + std::sin(ang) * r;
		return FVector3{x, terrain.Height(x, z), z};
	};
	const auto walkerSpawn = [&](Model m, const EntityRecord &r, const FVector4 &tint, const std::vector<FVector3> &feet) {
		Walker k;
		k.home = r.anchor;
		k.heading = r.heading;
		k.speed = r.speed;
		k.phase = r.phase;
		k.range = r.range;
		k.sideways = (r.flags & 2u) != 0;
		k.footCount = uint8_t(std::min(feet.size(), k.feet.size()));
		std::copy_n(feet.begin(), k.footCount, k.feet.begin());
		const math::FAABB &bounds = m_shapes.bounds[size_t(m)];
		const FVector3 half = HalfOf(bounds);
		const float side = std::max(half.x, half.z) * 0.6f;
		physics::Body b;
		b.half = {side, half.y * 0.8f, side};
		b.center = r.position;
		b.velocity = r.velocity;
		b.mass = 2.f;
		b.bounce = 0.f;
		b.friction = 0.f;
		b.stepHeight = 0.12f;
		k.last = b.center;
		k.foot = -b.half.y - bounds.min.y;
		const ecs::Entity e = m_ecs.Spawn(k, Render{m, {FMatrix4::Identity(), tint}}, ChunkTag{key});
		physics::AttachBody(m_ecs, e, b);
	};
	const auto walkerDet = [&](Model m, const auto &homes, Walker k, const FVector4 &tint, const std::vector<FVector3> &feet, int n) {
		for (int i = 0; i < n; ++i) {
			const math::FVector2 home = homes[size_t(i) % homes.size()].center;
			const auto p = [&] { return randomOnFloor(home, 3.f, 14.f); };
			k.footCount = uint8_t(std::min(feet.size(), k.feet.size()));
			std::copy_n(feet.begin(), k.footCount, k.feet.begin());
			const math::FAABB &bounds = m_shapes.bounds[size_t(m)];
			const FVector3 half = HalfOf(bounds);
			const float side = std::max(half.x, half.z) * 0.6f;
			physics::Body b;
			b.half = {side, half.y * 0.8f, side};
			FVector3 floorP = p();
			for (int attempt = 0; attempt < 12; ++attempt) {
				float top = terrain.Height(floorP.x, floorP.z);
				for (const float dx : {-side, side})
					for (const float dz : {-side, side})
						top = std::max(top, terrain.Height(floorP.x + dx, floorP.z + dz));
				b.center = {floorP.x, top + b.half.y + 0.03f, floorP.z};
				if (!phys.Blocked(b))
					break;
				floorP = p();
			}
			b.mass = 2.f;
			b.bounce = 0.f;
			b.friction = 0.f;
			b.stepHeight = 0.12f;
			k.home = floorP;
			k.last = b.center;
			k.foot = -b.half.y - bounds.min.y;
			const ecs::Entity e = m_ecs.Spawn(k, Render{m, {FMatrix4::Identity(), tint}}, ChunkTag{key});
			physics::AttachBody(m_ecs, e, b);
		}
	};

	// Crabes
	if (!savedCrab.empty()) {
		for (const EntityRecord *rp : savedCrab)
			walkerSpawn(Model::Crab, *rp, {0.85f, 0.36f, 0.2f, 1.f}, meshes::CrabFeet(1.f));
	} else {
		Walker k;
		k.heading = 0.f;
		k.speed = 0.18f;
		k.range = 8.f;
		k.sideways = true;
		walkerDet(Model::Crab, lms, k, {0.85f, 0.36f, 0.2f, 1.f}, meshes::CrabFeet(1.f), std::max(2, q.fish / 45));
	}
	// Homards
	if (!savedLobster.empty()) {
		for (const EntityRecord *rp : savedLobster)
			walkerSpawn(Model::Lobster, *rp, {0.32f, 0.42f, 0.72f, 1.f}, meshes::LobsterFeet());
	} else {
		Walker k;
		k.heading = 0.f;
		k.speed = 0.12f;
		k.range = 10.f;
		k.sideways = false;
		walkerDet(Model::Lobster, lms, k, {0.32f, 0.42f, 0.72f, 1.f}, meshes::LobsterFeet(), std::max(1, q.fish / 160));
	}
	// Crevettes
	const FVector3 shrimpHalf = HalfOf(m_shapes.bounds[size_t(Model::Shrimp)]);
	const float shrimpRadius = std::max(0.03f, 0.5f * (shrimpHalf.x + shrimpHalf.y));
	const auto shrimpSpawn = [&](const EntityRecord &r) {
		Hoverer h;
		h.home = r.anchor;
		h.phase = r.phase;
		const ecs::Entity e = m_ecs.Spawn(h, Render{Model::Shrimp, {FMatrix4::Translation(r.position), {1.f, 0.62f, 0.52f, 1.f}}}, ChunkTag{key});
		physics::Body b = physics::Body::MakeSphere(r.position, shrimpRadius);
		b.velocity = r.velocity;
		b.gravityScale = 0.f;
		b.mass = 0.05f;
		b.bounce = 0.f;
		b.friction = 0.f;
		physics::AttachBody(m_ecs, e, b);
	};
	if (!savedShrimp.empty()) {
		for (const EntityRecord *rp : savedShrimp)
			shrimpSpawn(*rp);
	} else {
		for (int i = 0; i < std::max(6, q.fish / 30); ++i) {
			const Landmark &lm = lms[size_t(i) % std::max<size_t>(1, lms.size())];
			FVector3 home{};
			for (int attempt = 0; attempt < 12; ++attempt) {
				home = randomOnFloor(lm.center, 2.f, 10.f) + FVector3{0.f, 0.3f + 1.5f * rng.NextFloat(), 0.f};
				if (!phys.Blocked(physics::Body::MakeSphere(home, shrimpRadius * 3.f), -1, false))
					break;
			}
			EntityRecord r;
			r.position = home;
			r.anchor = home;
			r.phase = rng.NextFloat() * kTau;
			shrimpSpawn(r);
		}
	}

	// ── Anémones et vers (régénérés, ils ne se déplacent pas) ──
	const int nAnemones = std::max(4, q.corals / 30);
	for (int i = 0; i < nAnemones && !lms.empty(); ++i) {
		const Landmark &lm = lms[size_t(i) % lms.size()];
		FVector3 p = randomOnFloor(lm.center, 3.f, lm.radius * 0.7f);
		const Pulsing pulse{p, rng.NextFloat() * kTau, 0.8f + 0.5f * rng.NextFloat()};
		const Model m = i % 2 ? Model::Anemone1 : Model::Anemone0;
		const FMatrix4 rest = FMatrix4::Translation(p) * FMatrix4::Scale({pulse.scale, pulse.scale, pulse.scale});
		const ecs::Entity e = m_ecs.Spawn(pulse, Render{m, {rest, {1.f, 0.8f, 0.9f, 1.f}}}, ChunkTag{key});
		physics::AttachBody(m_ecs, e, BoxBody(rest, m_shapes.bounds[size_t(m)], 0.8f));
	}
	const int nFeathers = std::max(6, q.corals / 20);
	for (int i = 0; i < nFeathers && !lms.empty(); ++i) {
		const Landmark &lm = lms[size_t(i) % lms.size()];
		const FVector3 p = randomOnFloor(lm.center, 2.f, lm.radius * 0.75f);
		const ecs::Entity e = m_ecs.Spawn(Retracting{p, rng.NextFloat() * kTau, 1.f}, Render{Model::FeatherDuster, {FMatrix4::Translation(p), {1.f, 0.85f, 0.7f, 1.f}}},
										   ChunkTag{key});
		physics::AttachBody(m_ecs, e, BoxBody(FMatrix4::Translation(p), m_shapes.bounds[size_t(Model::FeatherDuster)], 0.6f));
	}

	// ── Bulles des sources du fond ──
	for (int i = 0; i < q.bubbles && !world.vents.empty(); ++i) {
		const int v = i % int(world.vents.size());
		const FVector3 base = world.vents[size_t(v)];
		m_ecs.Spawn(BubbleState{base + FVector3{0.f, rng.NextFloat() * (kSurfaceY - base.y), 0.f}, 0.6f + 0.9f * rng.NextFloat(),
								0.025f + 0.06f * rng.NextFloat(), rng.NextFloat() * kTau, v, true, ecs::kNull, {}, key},
					Render{Model::Bubble, {}}, ChunkTag{key});
	}
	// Bulles des flotteurs du varech.
	struct Emitter {
		ecs::Entity plant;
		FVector3 local;
	};
	std::vector<Emitter> emitters;
	size_t plantIndex = 0;
	m_ecs.MakeQuery<Flexible, Render, ChunkTag>().Each([&](ecs::Entity e, Flexible &, Render &r, ChunkTag &tag) {
		if (tag.key != key)
			return;
		const size_t m = size_t(r.model);
		if (m < size_t(Model::Kelp0) || m > size_t(Model::Kelp2) || plantIndex++ % 2 != 0)
			return;
		const float h = kKelpHeights[m - size_t(Model::Kelp0)];
		emitters.push_back({e, {(rng.NextFloat() - 0.5f) * 0.5f, h * (0.3f + 0.5f * rng.NextFloat()), (rng.NextFloat() - 0.5f) * 0.5f}});
	});
	for (const Emitter &em : emitters) {
		const Render &plant = *m_ecs.Get<Render>(em.plant);
		const FVector3 start = SourcePoint(plant, em.local, sim.time);
		for (int i = 0; i < 4; ++i) {
			BubbleState b{start + FVector3{0.f, rng.NextFloat() * std::max(0.f, kSurfaceY - 0.5f - start.y), 0.f}, 0.5f + 0.4f * rng.NextFloat(),
						  0.015f + 0.02f * rng.NextFloat(), rng.NextFloat() * kTau, -1, true};
			b.source = em.plant;
			b.local = em.local;
			m_ecs.Spawn(b, Render{Model::Bubble, {}}, ChunkTag{key});
		}
	}
}

void Ecosystem::SpawnChunk(uint64_t key, const World &world, const Quality &quality, uint64_t chunkSeed, std::span<const EntityRecord> saved) {
	SpawnScenery(key, world);
	SpawnLife(key, world, quality, chunkSeed, saved);
	// Première image : parties et poses en place sans faire avancer le temps.
	PartsSystem(m_ecs, 0.f);
	PoseSystem(m_ecs, 0.f);
	BenthosSystem(m_ecs, 0.f);
	CollectInstances(true);
}

std::vector<EntityRecord> Ecosystem::CaptureChunk(uint64_t key) {
	std::vector<EntityRecord> out;
	const physics::World &phys = *m_physics;
	m_ecs.MakeQuery<SchoolMember, physics::RigidBody, ChunkTag>().Each([&](SchoolMember &f, physics::RigidBody &rb, ChunkTag &tag) {
		if (tag.key != key)
			return;
		const physics::Body &b = phys.GetBody(rb.id);
		EntityRecord r;
		r.kind = EntityKind::Fish;
		r.position = b.center;
		r.velocity = b.velocity;
		r.anchor = f.slot;
		r.headingVec = f.heading;
		r.scale = f.scale;
		r.phase = f.phase;
		r.school = f.school;
		out.push_back(r);
	});
	m_ecs.MakeQuery<Cruiser, ChunkTag>().Each([&](Cruiser &c, ChunkTag &tag) {
		if (tag.key != key)
			return;
		EntityRecord r;
		r.kind = EntityKind::Manta;
		r.anchor = c.orbit.center;
		r.radius = c.orbit.radius;
		r.height = c.orbit.height;
		r.speed = c.orbit.speed;
		r.phase = c.orbit.phase;
		r.scale = c.scale;
		r.range = c.span;
		r.lift = c.lift;
		r.pitch = c.pitch;
		r.roll = c.roll;
		r.flags = (c.orbit.terrainRelative ? 1u : 0u) | (c.posed ? 4u : 0u);
		out.push_back(r);
	});
	m_ecs.MakeQuery<Walker, physics::RigidBody, Render, ChunkTag>().Each([&](Walker &k, physics::RigidBody &rb, Render &render, ChunkTag &tag) {
		if (tag.key != key)
			return;
		const physics::Body &b = phys.GetBody(rb.id);
		EntityRecord r;
		r.kind = render.model == Model::Crab ? EntityKind::Crab : EntityKind::Lobster;
		r.position = b.center;
		r.velocity = b.velocity;
		r.anchor = k.home;
		r.heading = k.heading;
		r.speed = k.speed;
		r.phase = k.phase;
		r.range = k.range;
		r.flags = k.sideways ? 2u : 0u;
		out.push_back(r);
	});
	m_ecs.MakeQuery<Hoverer, physics::RigidBody, ChunkTag>().Each([&](Hoverer &h, physics::RigidBody &rb, ChunkTag &tag) {
		if (tag.key != key)
			return;
		const physics::Body &b = phys.GetBody(rb.id);
		EntityRecord r;
		r.kind = EntityKind::Shrimp;
		r.position = b.center;
		r.velocity = b.velocity;
		r.anchor = h.home;
		r.phase = h.phase;
		out.push_back(r);
	});
	return out;
}

void Ecosystem::UnloadChunk(uint64_t key) {
	physics::World &phys = *m_physics;
	if (const auto it = m_chunkGround.find(key); it != m_chunkGround.end()) {
		phys.RemoveTriangles(physics::UserOf(it->second));
		m_chunkGround.erase(it);
	}
	std::vector<ecs::Entity> ents;
	m_ecs.MakeQuery<ChunkTag>().Each([&](ecs::Entity e, ChunkTag &tag) {
		if (tag.key == key)
			ents.push_back(e);
	});
	for (ecs::Entity e : ents) {
		if (const auto *rb = m_ecs.Get<physics::RigidBody>(e))
			phys.RemoveBody(rb->id);
		if (const auto *p = m_ecs.Get<Parts>(e))
			for (size_t i = 0; i < p->count; ++i)
				phys.RemoveBody(p->body[i]);
		if (const auto *km = m_ecs.Get<physics::KinematicMesh>(e))
			phys.RemoveMesh(km->id);
		m_ecs.Despawn(e);
	}
	if (const auto it = m_state->schools.find(key); it != m_state->schools.end())
		m_state->schools.erase(it);
	if (const auto it = m_state->ventsByChunk.find(key); it != m_state->ventsByChunk.end())
		m_state->ventsByChunk.erase(it);
}

void Ecosystem::CollectInstances(bool statics) {
	if (statics) {
		for (auto &bucket : m_buckets)
			bucket.clear();
		m_dynamic.fill(false);
		m_ecs.MakeQuery<Render, Static>().Each([&](Render &r, Static &) { m_buckets[size_t(r.model)].push_back(r.instance); });
		m_ecs.MakeQuery<Render>().Without<Static>().Each([&](Render &r) { m_dynamic[size_t(r.model)] = true; });
	}
	for (size_t m = 0; m < kModelCount; ++m)
		if (m_dynamic[m])
			m_buckets[m].clear();
	m_ecs.MakeQuery<Render>().Without<Static>().Each([&](Render &r) {
		m_dynamic[size_t(r.model)] = true;
		m_buckets[size_t(r.model)].push_back(r.instance);
	});
}

void Ecosystem::Update(float dt, float time, const FVector3 &forward, bool exhale) {
	m_state->time = time;
	m_state->forward = forward;
	m_state->exhale = exhale;
	m_state->diver = DiverPosition();
	m_schedule.Run(m_ecs, dt);
	DIAG_ZONE("Ecosystem::CollectInstances");
	CollectInstances(false);
}

void Ecosystem::SteerDiver(const FVector3 &wish, float dt) {
	physics::Body &b = m_physics->GetBody(m_playerBody);
	const bool swimming = wish.LengthSq() > 1e-6f;
	b.gravityScale = swimming ? 0.f : 0.05f;
	b.drag = swimming ? 0.f : 1.1f;
	if (swimming)
		b.velocity += (wish - b.velocity) * (1.f - std::exp(-dt * 2.2f));
}

void Ecosystem::PlaceDiver(const FVector3 &position) {
	physics::Body &b = m_physics->GetBody(m_playerBody);
	b.center = position;
	b.velocity = {};
	m_ecs.Get<physics::Position>(m_player)->value = position;
	m_state->diver = position;
}

void Ecosystem::SetDiverPhysics(bool enabled) { m_physics->GetBody(m_playerBody).enabled = enabled; }

FVector3 Ecosystem::DiverPosition() const { return m_physics->GetBody(m_playerBody).center; }

FVector3 Ecosystem::DiverVelocity() const { return m_physics->GetBody(m_playerBody).velocity; }

int Ecosystem::NearbyBubbles() const noexcept { return m_state->nearbyBubbles; }

} // namespace abyssale
