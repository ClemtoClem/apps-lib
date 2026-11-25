#include "voxel/entities.hpp"

#include "voxel/mechanics.hpp"

namespace voxel {

namespace {
void PutVec(dataio::ByteWriter &w, const math::FVector3 &v) {
	w.Put<float>(v.x);
	w.Put<float>(v.y);
	w.Put<float>(v.z);
}
math::FVector3 GetVec(dataio::ByteReader &r) {
	const float x = r.Get<float>(), y = r.Get<float>(), z = r.Get<float>();
	return {x, y, z};
}
} // namespace

void WriteEntityRecord(dataio::ByteWriter &w, const EntityRecord &r) {
	w.PutString(r.kind);
	PutVec(w, r.position);
	PutVec(w, r.velocity);
	w.Put<float>(r.yaw);
	w.Put<float>(r.health);
	w.Put<float>(r.maxHealth);
	PutVec(w, r.halfExtents);
	w.PutString(r.props);
	w.PutU8(r.wander ? 1 : 0);
	w.Put<uint64_t>(r.uid);
	w.PutString(r.ext);
}

bool ReadEntityRecord(dataio::ByteReader &r, EntityRecord &out, uint32_t version) {
	out.kind = r.GetString();
	out.position = GetVec(r);
	out.velocity = GetVec(r);
	out.yaw = r.Get<float>();
	out.health = r.Get<float>();
	out.maxHealth = r.Get<float>();
	out.halfExtents = GetVec(r);
	out.props = r.GetString();
	out.wander = r.GetU8() != 0;
	if (version >= 2) {
		out.uid = r.Get<uint64_t>();
		out.ext = r.GetString();
	}
	return r.Ok();
}

EntityRecord CaptureEntity(ecs::World &world, ecs::Entity e) {
	EntityRecord r;
	if (const Kind *k = world.Get<Kind>(e))
		r.kind = k->id;
	if (const Transform *t = world.Get<Transform>(e)) {
		r.position = t->position;
		r.yaw = t->yaw;
	}
	if (const Velocity *v = world.Get<Velocity>(e))
		r.velocity = v->value;
	if (const Health *h = world.Get<Health>(e)) {
		r.health = h->current;
		r.maxHealth = h->max;
	}
	if (const Body *b = world.Get<Body>(e))
		r.halfExtents = b->halfExtents;
	if (const Props *p = world.Get<Props>(e))
		r.props = p->json;
	r.wander = world.Has<Wander>(e);
	if (const Uid *u = world.Get<Uid>(e))
		r.uid = u->value;
	r.ext = CaptureMechanics(world, e);
	return r;
}

ecs::Entity InstantiateEntity(ecs::World &world, const EntityRecord &r) {
	ecs::Entity e = world.Spawn(Transform{r.position, r.yaw}, Velocity{r.velocity}, Body{r.halfExtents}, Health{r.health, r.maxHealth},
								Kind{r.kind}, GridCell{});
	if (!r.props.empty())
		world.Add(e, Props{r.props});
	if (r.wander)
		world.Add(e, Wander{0.f, 1.5f, e.index * 2654435761u});
	if (r.uid)
		world.Add(e, Uid{r.uid});
	ApplyMechanics(world, e, r.ext);
	return e;
}

} // namespace voxel
