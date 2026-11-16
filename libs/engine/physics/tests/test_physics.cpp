// Tests d'engine.physics : contacts élémentaires, chute et repos, murs, marches, piles,
// sphères, maillage cinématique qui tourne (porte), évènements de contact, pont ECS.
#include <cpp-maker/test.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <physics/ecs.hpp>
#include <physics/world.hpp>

using namespace physics;

namespace {

constexpr math::FVector3 kGravity{0.f, -9.81f, 0.f};

/// Sol carré de `size` mètres en y = 0 (deux triangles).
std::vector<math::FVector3> Floor(float size) {
	const float h = size * 0.5f;
	return {{-h, 0, -h}, {h, 0, h}, {h, 0, -h}, {-h, 0, -h}, {-h, 0, h}, {h, 0, h}};
}

/// Pavé de triangles (12).
std::vector<math::FVector3> Cuboid(const math::FVector3 &lo, const math::FVector3 &hi) {
	const auto p = [&](int i) { return math::FVector3{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z}; };
	static constexpr int kFaces[6][4] = {{0, 2, 6, 4}, {1, 5, 7, 3}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 6, 7, 5}};
	std::vector<math::FVector3> out;
	for (const auto &f : kFaces)
		out.insert(out.end(), {p(f[0]), p(f[1]), p(f[2]), p(f[0]), p(f[2]), p(f[3])});
	return out;
}

void Simulate(World &w, float seconds) {
	for (int i = 0; i < int(seconds * 120.f); ++i)
		w.Step(1.f / 120.f, kGravity);
}

bool Touches(const Body &b, UserId u) { return std::ranges::find(b.touching, u) != b.touching.end(); }

} // namespace

TEST(Physics, ElementaryContacts) {
	// Boîte de demi-taille 0,5 centrée à 0,4 au-dessus d'un sol : sortie de 0,1 vers le haut.
	const auto push = BoxTriangle({0.f, 0.4f, 0.f}, {0.5f, 0.5f, 0.5f}, {-5, 0, -5}, {5, 0, 5}, {5, 0, -5});
	REQUIRE(push.has_value());
	CHECK_NEAR(push->y, 0.1f, 1e-5f);
	CHECK_NEAR(std::abs(push->x) + std::abs(push->z), 0.f, 1e-5f);
	CHECK(!BoxTriangle({0.f, 0.6f, 0.f}, {0.5f, 0.5f, 0.5f}, {-5, 0, -5}, {5, 0, 5}, {5, 0, -5}).has_value());
	CHECK(!BoxTriangle({9.f, 0.4f, 0.f}, {0.5f, 0.5f, 0.5f}, {-5, 0, -5}, {5, 0, 5}, {5, 0, -5}).has_value());
	// Sphère : sur la face, puis près d'un sommet (sortie radiale).
	const auto s = SphereTriangle({0.f, 0.3f, 0.f}, 0.5f, {-5, 0, -5}, {5, 0, 5}, {5, 0, -5});
	REQUIRE(s.has_value());
	CHECK_NEAR(s->y, 0.2f, 1e-5f);
	const auto corner = ClosestOnTriangle({6.f, 1.f, -6.f}, {-5, 0, -5}, {5, 0, 5}, {5, 0, -5});
	CHECK_NEAR(corner.x, 5.f, 1e-5f);
	CHECK_NEAR(corner.z, -5.f, 1e-5f);
	// Corps contre corps : boîte / sphère, sphère / sphère.
	Body box;
	const Body ball = Body::MakeSphere({0.f, 0.8f, 0.f}, 0.5f);
	const auto o = BodyOverlap(box, ball);
	REQUIRE(o.has_value());
	CHECK_NEAR(o->first.y, 1.f, 1e-5f);
	CHECK_NEAR(o->second, 0.2f, 1e-5f);
	const auto back = BodyOverlap(ball, box);
	REQUIRE(back.has_value());
	CHECK_NEAR(back->first.y, -1.f, 1e-5f);
	CHECK(!BodyOverlap(Body::MakeSphere({0, 0, 0}, 0.5f), Body::MakeSphere({1.1f, 0, 0}, 0.5f)).has_value());
}

TEST(Physics, FallRestWallsStepsAndStacks) {
	World w;
	w.AddTriangles(Floor(40.f));
	w.AddTriangles(Cuboid({3.f, 0.f, -5.f}, {3.5f, 3.f, 5.f}), 7);		// mur en x = 3
	w.AddTriangles(Cuboid({-3.5f, 0.f, -1.f}, {-2.5f, 0.25f, 1.f}), 8); // marche de 25 cm
	Body crate;
	crate.center = {0.f, 4.f, -3.f};
	const BodyId c = w.AddBody(crate);
	Simulate(w, 2.f);
	CHECK_NEAR(w.GetBody(c).center.y, 0.5f, 0.01f); // posée sur le sol
	CHECK(w.GetBody(c).grounded);
	CHECK(std::abs(w.GetBody(c).velocity.y) < 0.05f);
	// Une seconde caisse tombe dessus : pile.
	Body top = crate;
	top.center = {0.f, 3.f, -3.f};
	const BodyId t = w.AddBody(top);
	Simulate(w, 2.f);
	CHECK_NEAR(w.GetBody(t).center.y, 1.5f, 0.05f);
	// Le joueur marche vers le mur : arrêté contre lui.
	Body player;
	player.center = {0.f, 0.92f, 2.f};
	player.half = {0.3f, 0.9f, 0.3f};
	player.friction = 0.f;
	player.stepHeight = 0.35f;
	const BodyId p = w.AddBody(player);
	for (int i = 0; i < 360; ++i) {
		w.GetBody(p).velocity.x = 4.f;
		w.Step(1.f / 120.f, kGravity);
	}
	CHECK_NEAR(w.GetBody(p).center.x, 3.f - 0.3f, 0.02f);
	CHECK(Touches(w.GetBody(p), 7));
	// Vers la marche de 25 cm : franchie sans sauter.
	w.GetBody(p).center = {0.f, 0.92f, 0.f};
	for (int i = 0; i < 120 && w.GetBody(p).center.x > -3.f; ++i) {
		w.GetBody(p).velocity.x = -3.f;
		w.Step(1.f / 120.f, kGravity);
	}
	CHECK(w.GetBody(p).center.x <= -3.f); // au-dessus de la marche (x de -3,5 à -2,5)
	CHECK_NEAR(w.GetBody(p).center.y, 0.25f + 0.9f, 0.05f);
	// Rayon : premier obstacle.
	const auto hit = w.Raycast({{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}}, 10.f, p);
	REQUIRE(hit.has_value());
	CHECK_NEAR(hit->point.x, 3.f, 1e-3f);
	CHECK_EQ(hit->user, UserId(7));
}

TEST(Physics, SpheresRestBounceAndRay) {
	World w;
	w.AddTriangles(Floor(40.f));
	Body ball = Body::MakeSphere({0.f, 3.f, 0.f}, 0.25f);
	ball.bounce = 0.f;
	const BodyId b = w.AddBody(ball);
	Simulate(w, 2.f);
	CHECK_NEAR(w.GetBody(b).center.y, 0.25f, 0.01f);
	CHECK(w.GetBody(b).grounded);
	// Une boîte immobile sous une sphère qui tombe : la sphère se pose dessus.
	Body block;
	block.center = {3.f, 0.5f, 0.f};
	block.dynamic = false;
	block.user = 5;
	w.AddBody(block);
	const BodyId s = w.AddBody(Body::MakeSphere({3.f, 3.f, 0.f}, 0.25f));
	Simulate(w, 3.f);
	CHECK_NEAR(w.GetBody(s).center.y, 1.25f, 0.03f);
	CHECK(Touches(w.GetBody(s), 5));
	// Rayon horizontal vers la première sphère : face avant.
	const auto hit = w.Raycast({{-5.f, 0.25f, 0.f}, {1.f, 0.f, 0.f}}, 10.f);
	REQUIRE(hit.has_value());
	CHECK_EQ(hit->body, b);
	CHECK_NEAR(hit->point.x, -0.25f, 0.02f);
	CHECK_NEAR(hit->normal.x, -1.f, 0.02f);
}

TEST(Physics, RotatingDoorBlocksPassesAndPushes) {
	World w;
	w.AddTriangles(Floor(40.f));
	// Porte : 1 m × 2 m × 10 cm, charnière à l'origine (axe Y).
	const std::vector<math::FVector3> door = Cuboid({0.f, 0.f, -0.05f}, {1.f, 2.f, 0.05f});
	const ColliderId d = w.AddMesh(door, math::FMatrix4::Identity(), 42);
	Body player;
	player.center = {0.5f, 0.92f, 2.f};
	player.half = {0.3f, 0.9f, 0.3f};
	player.friction = 0.f;
	player.user = 1;
	const BodyId p = w.AddBody(player);
	const auto walk = [&](float seconds) {
		for (int i = 0; i < int(seconds * 120.f); ++i) {
			w.GetBody(p).velocity.z = -3.f;
			w.Step(1.f / 120.f, kGravity);
		}
	};
	// Porte fermée : le joueur est arrêté contre sa face (z = 0,05).
	walk(1.5f);
	CHECK_NEAR(w.GetBody(p).center.z, 0.05f + 0.3f, 0.02f);
	CHECK(Touches(w.GetBody(p), 42));
	const auto ray = w.Raycast({{0.5f, 1.f, 5.f}, {0.f, 0.f, -1.f}}, 10.f, p);
	REQUIRE(ray.has_value());
	CHECK_EQ(ray->collider, d);
	// Ouverte d'un quart de tour (le battant part vers −z) : le passage est libre.
	w.SetTransform(d, math::FMatrix4::Rotation({0.f, 1.f, 0.f}, math::PI * 0.5f));
	CHECK_NEAR(w.MeshBounds(d).min.z, -1.f, 1e-4f);
	walk(1.5f);
	CHECK(w.GetBody(p).center.z < -2.f);
	// Une caisse dans le débattement (côté +z) : refermée puis rouverte vers +z, la porte la pousse.
	w.SetTransform(d, math::FMatrix4::Identity());
	Body crate;
	crate.center = {0.6f, 0.2f, 0.5f};
	crate.half = {0.2f, 0.2f, 0.2f};
	const BodyId c = w.AddBody(crate);
	Simulate(w, 0.5f);
	for (int i = 1; i <= 120; ++i) {
		w.SetTransform(d, math::FMatrix4::Rotation({0.f, 1.f, 0.f}, -math::PI * 0.5f * float(i) / 120.f));
		w.Step(1.f / 120.f, kGravity);
	}
	Simulate(w, 0.2f);
	const math::FAABB doorBox = w.MeshBounds(d), crateBox = w.GetBody(c).Bounds();
	const bool apart = crateBox.max.x <= doorBox.min.x + 0.01f || crateBox.min.x >= doorBox.max.x - 0.01f || crateBox.min.z >= doorBox.max.z - 0.01f;
	CHECK(apart); // jamais dans le battant
	CHECK(w.GetBody(c).center.x < 0.f);
	// Porte désactivée (cachée) : traversable et ignorée par les rayons.
	w.SetMeshEnabled(d, false);
	const auto none = w.Raycast({{-0.5f, 1.f, 0.5f}, {1.f, 0.f, 0.f}}, 2.f);
	CHECK(!none || none->collider != d);
}

TEST(Physics, ContactEvents) {
	World w;
	w.AddTriangles(Floor(10.f), 3);
	Body box;
	box.center = {0.f, 0.6f, 0.f};
	box.user = 9;
	const BodyId b = w.AddBody(box);
	int began = 0;
	for (int i = 0; i < 60; ++i) {
		w.Step(1.f / 120.f, kGravity);
		for (const ContactEvent &e : w.Events())
			if (e.began && e.self == 9 && e.other == 3 && e.body == b)
				++began;
	}
	CHECK_EQ(began, 1); // un seul début tant que le contact dure
	w.GetBody(b).center = {0.f, 5.f, 0.f};
	w.GetBody(b).velocity = {};
	w.Step(1.f / 120.f, kGravity);
	REQUIRE(w.Events().size() == 1);
	CHECK(!w.Events()[0].began);
	CHECK_EQ(w.Events()[0].other, UserId(3));
}

TEST(Physics, EcsBridge) {
	ecs::World world;
	World &physics = PhysicsOf(world);
	physics.AddTriangles(Floor(20.f));
	world.SetResource<Gravity>(Gravity{{0.f, -9.81f, 0.f}});
	const ecs::Entity ball = world.Spawn();
	AttachBody(world, ball, Body::MakeSphere({0.f, 2.f, 0.f}, 0.5f));
	// Plateau immobile placé par le jeu (Position lue avant le pas).
	const ecs::Entity pad = world.Spawn();
	Body padBody;
	padBody.center = {5.f, 0.5f, 0.f};
	padBody.dynamic = false;
	AttachBody(world, pad, padBody);
	// Porte : maillage cinématique dont la transformation est un composant.
	const ecs::Entity door = world.Spawn();
	const std::vector<math::FVector3> slab = Cuboid({0.f, 0.f, -0.05f}, {1.f, 2.f, 0.05f});
	AttachMesh(world, door, slab, math::FMatrix4::Translation({-5.f, 0.f, 0.f}));
	ecs::Schedule schedule;
	schedule.Add("physique", StepSystem);
	for (int i = 0; i < 240; ++i)
		schedule.Run(world, 1.f / 120.f);
	REQUIRE(world.Get<Position>(ball) != nullptr);
	CHECK_NEAR(world.Get<Position>(ball)->value.y, 0.5f, 0.02f);
	world.Get<Position>(pad)->value = {5.f, 0.5f, 3.f};
	world.Get<KinematicMesh>(door)->transform = math::FMatrix4::Translation({-5.f, 0.f, 4.f});
	schedule.Run(world, 1.f / 120.f);
	CHECK_NEAR(physics.GetBody(world.Get<RigidBody>(pad)->id).center.z, 3.f, 1e-5f);
	CHECK_NEAR(physics.MeshBounds(world.Get<KinematicMesh>(door)->id).min.z, 3.95f, 1e-4f);
	// Rayons et contacts renvoient aux entités.
	const auto hit = physics.Raycast({{0.f, 5.f, 0.f}, {0.f, -1.f, 0.f}}, 10.f);
	REQUIRE(hit.has_value());
	CHECK(EntityOf(hit->user) == ball);
	CHECK(EntityOf(kWorld).IsNull());
	const auto doorHit = physics.Raycast({{-4.5f, 1.f, 0.f}, {0.f, 0.f, 1.f}}, 10.f);
	REQUIRE(doorHit.has_value());
	CHECK(EntityOf(doorHit->user) == door);
}
