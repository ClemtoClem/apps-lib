#include "physics/ecs.hpp"

namespace physics {

physics::World &PhysicsOf(ecs::World &world) {
	if (auto *p = world.GetResource<physics::World>())
		return *p;
	return world.SetResource<physics::World>();
}

BodyId AttachBody(ecs::World &world, ecs::Entity e, Body body) {
	body.user = UserOf(e);
	const BodyId id = PhysicsOf(world).AddBody(body);
	world.Add(e, RigidBody{id});
	world.Add(e, Position{body.center});
	return id;
}

ColliderId AttachMesh(ecs::World &world, ecs::Entity e, std::span<const math::FVector3> localVertices, const math::FMatrix4 &transform) {
	const ColliderId id = PhysicsOf(world).AddMesh(localVertices, transform, UserOf(e));
	world.Add(e, KinematicMesh{id, transform});
	return id;
}

void StepSystem(ecs::World &world, float dt) {
	physics::World &physics = PhysicsOf(world);
	const Gravity *gravity = world.GetResource<Gravity>();
	world.MakeQuery<KinematicMesh>().Each([&](KinematicMesh &m) {
		if (m.id >= 0)
			physics.SetTransform(m.id, m.transform);
	});
	world.MakeQuery<RigidBody, Position>().Each([&](RigidBody &rb, Position &p) {
		if (rb.id >= 0 && !physics.GetBody(rb.id).dynamic)
			physics.GetBody(rb.id).center = p.value;
	});
	physics.Step(dt, gravity ? gravity->value : Gravity{}.value);
	world.MakeQuery<RigidBody, Position>().Each([&](RigidBody &rb, Position &p) {
		if (rb.id >= 0)
			p.value = physics.GetBody(rb.id).center;
	});
}

} // namespace physics
