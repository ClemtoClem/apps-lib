#include "voxel/mechanics.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>

#include <core/hash.hpp>
#include <core/robin_map.hpp>

#include "voxel/entities.hpp"

namespace voxel {

namespace {

constexpr float kDeg = std::numbers::pi_v<float> / 180.f;

dataio::NodePtr Vec(const math::FVector3 &v) {
	dataio::NodePtr a = dataio::Node::MakeArray();
	a->Push(dataio::Node::MakeFloat(double(v.x)));
	a->Push(dataio::Node::MakeFloat(double(v.y)));
	a->Push(dataio::Node::MakeFloat(double(v.z)));
	return a;
}

math::FVector3 Vec(const dataio::NodePtr &j, math::FVector3 def = {}) {
	if (!j->IsArray() || j->Size() < 3)
		return def;
	return {(*j)[0]->AsFloat(), (*j)[1]->AsFloat(), (*j)[2]->AsFloat()};
}

uint64_t NextUid() {
	static std::atomic<uint64_t> counter{uint64_t(std::chrono::steady_clock::now().time_since_epoch().count())};
	uint64_t v;
	do
		v = core::Mix64(counter.fetch_add(0x9E3779B97F4A7C15ULL, std::memory_order_relaxed));
	while (v == 0);
	return v;
}

bool Limited(const Joint &j) noexcept { return j.minLimit < j.maxLimit; }

} // namespace

const char *JointTypeId(JointType t) noexcept {
	switch (t) {
	case JointType::Attach: return "attach";
	case JointType::Hinge: return "hinge";
	case JointType::Slider: return "slider";
	case JointType::Pulley: return "pulley";
	case JointType::Rope: return "rope";
	}
	return "?";
}

const char *JointTypeLabel(JointType t) noexcept {
	switch (t) {
	case JointType::Attach: return "Attache";
	case JointType::Hinge: return "Charnière";
	case JointType::Slider: return "Glissière";
	case JointType::Pulley: return "Poulie";
	case JointType::Rope: return "Corde";
	}
	return "?";
}

std::optional<JointType> ParseJointType(std::string_view id) noexcept {
	for (JointType t : {JointType::Attach, JointType::Hinge, JointType::Slider, JointType::Pulley, JointType::Rope})
		if (id == JointTypeId(t))
			return t;
	return std::nullopt;
}

math::FVector3 RotateYaw(const math::FVector3 &v, float yaw) noexcept {
	const float c = std::cos(yaw), s = std::sin(yaw);
	return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

uint64_t EnsureUid(ecs::World &world, ecs::Entity e) {
	if (!world.IsAlive(e))
		return 0;
	if (const Uid *u = world.Get<Uid>(e); u && u->value)
		return u->value;
	const uint64_t v = NextUid();
	world.Add(e, Uid{v});
	return v;
}

ecs::Entity FindByUid(ecs::World &world, uint64_t uid) {
	ecs::Entity found{};
	if (uid == 0)
		return found;
	world.MakeQuery<const Uid>().Each([&](ecs::Entity e, const Uid &u) {
		if (u.value == uid)
			found = e;
	});
	return found;
}

MechanicsStats SolveJoints(ecs::World &world, float dt, float gravity) {
	MechanicsStats stats;
	if (dt <= 0)
		return stats;
	core::RobinMap<uint64_t, ecs::Entity> byUid;
	world.MakeQuery<const Uid>().Each([&](ecs::Entity e, const Uid &u) {
		if (u.value)
			byUid.InsertOrAssign(u.value, e);
	});
	std::vector<ecs::Entity> owners;
	world.MakeQuery<Joints, Transform>().Each([&](ecs::Entity e, Joints &, Transform &) { owners.push_back(e); });
	for (ecs::Entity a : owners) {
		Joints *joints = world.Get<Joints>(a);
		for (Joint &j : joints->list) {
			++stats.joints;
			const ecs::Entity *bp = byUid.Find(j.target);
			if (!bp || *bp == a || !world.IsAlive(*bp))
				continue;
			const ecs::Entity b = *bp;
			Transform *ta = world.Get<Transform>(a);
			Transform *tb = world.Get<Transform>(b);
			if (!ta || !tb)
				continue;
			++stats.active;
			Velocity *va = world.Get<Velocity>(a);
			Velocity *vb = world.Get<Velocity>(b);
			const math::FVector3 anchorWorld = ta->position + RotateYaw(j.anchor, ta->yaw);
			if (!j.initialized) { // état de référence à la création
				j.offset = RotateYaw(tb->position - anchorWorld, -ta->yaw);
				j.restYaw = tb->yaw - ta->yaw;
				if (j.type == JointType::Pulley && j.length == 0)
					j.length = ta->position.y + j.ratio * tb->position.y;
				if (j.type == JointType::Pulley)
					j.position = ta->position.y;
				if (j.type == JointType::Rope && j.length == 0)
					j.length = (tb->position - anchorWorld).Length();
				j.initialized = true;
			}
			// Moteur (charnière / glissière) : vitesse imposée, sinon amortissement.
			auto drive = [&] {
				if (j.motor)
					j.velocity = j.motorSpeed;
				else
					j.velocity *= std::pow(0.2f, dt);
				j.position += j.velocity * dt;
				if (Limited(j) && (j.position < j.minLimit || j.position > j.maxLimit)) {
					j.position = std::clamp(j.position, j.minLimit, j.maxLimit);
					j.velocity = 0;
				}
			};
			switch (j.type) {
			case JointType::Attach:
				tb->position = anchorWorld + RotateYaw(j.offset, ta->yaw);
				tb->yaw = ta->yaw + j.restYaw;
				if (vb)
					vb->value = va ? va->value : math::FVector3{};
				break;
			case JointType::Hinge: {
				drive();
				const float yaw = ta->yaw + j.position * kDeg;
				tb->position = anchorWorld + RotateYaw(j.offset, yaw);
				tb->yaw = yaw + j.restYaw;
				if (vb)
					vb->value = {};
				break;
			}
			case JointType::Slider: {
				drive();
				const math::FVector3 dir = RotateYaw(j.axis.LengthSq() > 0 ? j.axis.Normalized() : math::FVector3{1, 0, 0}, ta->yaw);
				tb->position = anchorWorld + RotateYaw(j.offset, ta->yaw) + dir * j.position;
				tb->yaw = ta->yaw + j.restYaw;
				if (vb)
					vb->value = dir * j.velocity;
				break;
			}
			case JointType::Pulley: {
				// Un degré de liberté q = y(A) ; y(B) = (L − q) / ratio.
				const float ratio = std::max(0.05f, std::fabs(j.ratio));
				const Mass *ma = world.Get<Mass>(a);
				const Mass *mb = world.Get<Mass>(b);
				const float mA = ma ? std::max(0.1f, ma->kg) : 70.f, mB = mb ? std::max(0.1f, mb->kg) : 70.f;
				const float mEff = mA + mB / (ratio * ratio);
				const float ag = -gravity * (mA - mB / ratio) / mEff;
				float acc = ag;
				if (j.motor) {
					const float wanted = (j.motorSpeed - j.velocity) / dt;
					if (j.motorForce > 0) {
						const float aMax = j.motorForce / mEff;
						acc = std::clamp(wanted, ag - aMax, ag + aMax);
					} else {
						acc = wanted; // moteur idéal
					}
				}
				j.velocity += acc * dt;
				if (!j.motor)
					j.velocity *= std::pow(0.7f, dt); // frottements du câble
				j.position += j.velocity * dt;
				if (Limited(j) && (j.position < j.minLimit || j.position > j.maxLimit)) {
					j.position = std::clamp(j.position, j.minLimit, j.maxLimit);
					j.velocity = 0;
				}
				ta->position.y = j.position;
				tb->position.y = (j.length - j.position) / ratio;
				if (va)
					va->value.y = j.velocity;
				if (vb)
					vb->value.y = -j.velocity / ratio;
				break;
			}
			case JointType::Rope: {
				const math::FVector3 d = tb->position - anchorWorld;
				const float dist = d.Length();
				if (dist > j.length && dist > 1e-5f) {
					const math::FVector3 n = d * (1.f / dist);
					tb->position = anchorWorld + n * j.length;
					if (vb) {
						const float outward = vb->value.Dot(n);
						if (outward > 0)
							vb->value = vb->value - n * outward;
					}
				}
				break;
			}
			}
		}
	}
	return stats;
}

// ── Persistance ──────────────────────────────────────────────────────────────

dataio::NodePtr JointToJson(const Joint &j) {
	dataio::NodePtr o = dataio::Node::MakeObject();
	o->Set("type", dataio::Node::MakeString(JointTypeId(j.type)));
	o->Set("target", dataio::Node::MakeString(std::to_string(j.target))); // 64 bits : en texte (double insuffisant)
	if (!j.name.empty())
		o->Set("name", dataio::Node::MakeString(j.name));
	o->Set("anchor", Vec(j.anchor));
	o->Set("axis", Vec(j.axis));
	o->Set("ratio", dataio::Node::MakeFloat(double(j.ratio)));
	o->Set("length", dataio::Node::MakeFloat(double(j.length)));
	o->Set("min", dataio::Node::MakeFloat(double(j.minLimit)));
	o->Set("max", dataio::Node::MakeFloat(double(j.maxLimit)));
	o->Set("motor", dataio::Node::MakeBool(j.motor));
	o->Set("speed", dataio::Node::MakeFloat(double(j.motorSpeed)));
	o->Set("force", dataio::Node::MakeFloat(double(j.motorForce)));
	o->Set("initialized", dataio::Node::MakeBool(j.initialized));
	o->Set("offset", Vec(j.offset));
	o->Set("restYaw", dataio::Node::MakeFloat(double(j.restYaw)));
	o->Set("position", dataio::Node::MakeFloat(double(j.position)));
	o->Set("velocity", dataio::Node::MakeFloat(double(j.velocity)));
	return o;
}

std::optional<Joint> JointFromJson(const dataio::NodePtr &o) {
	const auto type = ParseJointType((*o)["type"]->AsString());
	if (!type)
		return std::nullopt;
	Joint j;
	j.type = *type;
	j.target = std::strtoull(std::string((*o)["target"]->AsString("0")).c_str(), nullptr, 10);
	j.name = std::string((*o)["name"]->AsString());
	j.anchor = Vec((*o)["anchor"]);
	j.axis = Vec((*o)["axis"], {1, 0, 0});
	j.ratio = (*o)["ratio"]->AsFloat(1.f);
	j.length = (*o)["length"]->AsFloat(0.f);
	j.minLimit = (*o)["min"]->AsFloat(0.f);
	j.maxLimit = (*o)["max"]->AsFloat(0.f);
	j.motor = (*o)["motor"]->AsBool(false);
	j.motorSpeed = (*o)["speed"]->AsFloat(0.f);
	j.motorForce = (*o)["force"]->AsFloat(0.f);
	j.initialized = (*o)["initialized"]->AsBool(false);
	j.offset = Vec((*o)["offset"]);
	j.restYaw = (*o)["restYaw"]->AsFloat(0.f);
	j.position = (*o)["position"]->AsFloat(0.f);
	j.velocity = (*o)["velocity"]->AsFloat(0.f);
	return j;
}

std::string CaptureMechanics(ecs::World &world, ecs::Entity e) {
	dataio::NodePtr o = dataio::Node::MakeObject();
	bool any = false;
	if (const Mass *m = world.Get<Mass>(e)) {
		o->Set("mass", dataio::Node::MakeFloat(double(m->kg)));
		any = true;
	}
	if (const LightEmitter *l = world.Get<LightEmitter>(e)) {
		dataio::NodePtr lj = dataio::Node::MakeObject();
		lj->Set("color", Vec(l->color));
		lj->Set("intensity", dataio::Node::MakeFloat(double(l->intensity)));
		lj->Set("range", dataio::Node::MakeFloat(double(l->range)));
		lj->Set("offset", Vec(l->offset));
		lj->Set("enabled", dataio::Node::MakeBool(l->enabled));
		o->Set("light", std::move(lj));
		any = true;
	}
	if (const SoundEmitter *s = world.Get<SoundEmitter>(e)) {
		dataio::NodePtr sj = dataio::Node::MakeObject();
		sj->Set("sound", dataio::Node::MakeString(s->sound));
		sj->Set("volume", dataio::Node::MakeFloat(double(s->volume)));
		sj->Set("range", dataio::Node::MakeFloat(double(s->range)));
		sj->Set("loop", dataio::Node::MakeBool(s->loop));
		sj->Set("enabled", dataio::Node::MakeBool(s->enabled));
		o->Set("sound", std::move(sj));
		any = true;
	}
	if (const Joints *js = world.Get<Joints>(e); js && !js->list.empty()) {
		dataio::NodePtr arr = dataio::Node::MakeArray();
		for (const Joint &j : js->list)
			arr->Push(JointToJson(j));
		o->Set("joints", std::move(arr));
		any = true;
	}
	return any ? dataio::WriteJson(o, -1) : std::string();
}

void ApplyMechanics(ecs::World &world, ecs::Entity e, std::string_view json) {
	if (json.empty() || !world.IsAlive(e))
		return;
	auto parsed = dataio::ParseJson(json);
	if (!parsed || !parsed.Value()->IsObject())
		return;
	const dataio::NodePtr &o = parsed.Value();
	if (const dataio::NodePtr m = o->Get("mass"))
		world.Add(e, Mass{m->AsFloat(70.f)});
	if (const dataio::NodePtr l = o->Get("light")) {
		LightEmitter le;
		le.color = Vec((*l)["color"], le.color);
		le.intensity = (*l)["intensity"]->AsFloat(1.f);
		le.range = (*l)["range"]->AsFloat(8.f);
		le.offset = Vec((*l)["offset"]);
		le.enabled = (*l)["enabled"]->AsBool(true);
		world.Add(e, le);
	}
	if (const dataio::NodePtr s = o->Get("sound")) {
		SoundEmitter se;
		se.sound = std::string((*s)["sound"]->AsString());
		se.volume = (*s)["volume"]->AsFloat(1.f);
		se.range = (*s)["range"]->AsFloat(16.f);
		se.loop = (*s)["loop"]->AsBool(true);
		se.enabled = (*s)["enabled"]->AsBool(true);
		world.Add(e, std::move(se));
	}
	if (const dataio::NodePtr js = o->Get("joints")) {
		Joints joints;
		for (const dataio::NodePtr &j : js->AsArray())
			if (auto joint = JointFromJson(j))
				joints.list.push_back(std::move(*joint));
		if (!joints.list.empty())
			world.Add(e, std::move(joints));
	}
}

} // namespace voxel
