// Tests d'Abyssale : relief, composition du monde, écosystème (entités, comportements,
// collisions), ambiance et synthèse sonore (sans GPU). Le monde est désormais découpé en
// chunks : chaque test peuple la carte d'entrée (chunk 0,0) via `SpawnChunk`.
#include <cpp-maker/test.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <vector>

#include "abyssale/chunk.hpp"
#include "abyssale/config.hpp"
#include "abyssale/ambience.hpp"
#include "abyssale/ecosystem.hpp"
#include "abyssale/meshes.hpp"
#include "abyssale/ocean_sound.hpp"
#include "abyssale/terrain.hpp"
#include "abyssale/textures.hpp"
#include "abyssale/world.hpp"

using namespace abyssale;

TEST(Terrain_CanyonsAreDeterministic) {
	const Terrain a, b;
	CHECK_EQ(a.Height(12.f, -7.f), b.Height(12.f, -7.f)); // déterministe
	// Les lieux de la carte d'entrée sont des cuvettes profondes.
	for (const Landmark &l : landmarks::kAll) {
		CHECK(a.Openness(l.center.x, l.center.y) > 0.9f);
		CHECK(a.Height(l.center.x, l.center.y) < -30.f);
	}
	// Le relief s'étend au-delà d'un chunk (monde continu, plus de bord fermé).
	CHECK(a.Height(500.f, 500.f) < 1.f);
	// Le relief est continu (points proches, hors des falaises).
	CHECK_NEAR(a.Height(50.f, 30.f), a.Height(50.5f, 30.f), 0.25f);
	const auto mesh = a.BuildChunkMesh(0, 0);
	CHECK_EQ(mesh.vertices.size(), size_t(Terrain::kGrid) * Terrain::kGrid);
	CHECK_EQ(mesh.indices.size(), size_t(Terrain::kGrid - 1) * (Terrain::kGrid - 1) * 6);
}

TEST(Layout_BuildsAMaze) {
	const Layout layout(42);
	CHECK(layout.InMaze(0, 0));
	CHECK_EQ(layout.At(0, 0).kind, ChunkKind::Entry);
	// Au-delà du labyrinthe : océan ouvert, sans falaise.
	CHECK_EQ(layout.At(1000, 1000).kind, ChunkKind::OpenOcean);
	CHECK_EQ(layout.EdgeStateAt(1000, 1000, Edge::North), EdgeState::Open);
	// Deux cellules voisines partagent la même arête (symétrie).
	CHECK_EQ(layout.EdgeStateAt(1, 0, Edge::West), layout.EdgeStateAt(0, 0, Edge::East));
}

TEST(World_PlacementAndViewpoints) {
	const Terrain t;
	const World w(t, kQualities[2]);
	size_t kelp = 0;
	for (const auto &v : w.kelp)
		kelp += v.size();
	CHECK(kelp > 20u);
	CHECK(!w.tufts.empty());
	CHECK(!w.barrels.empty());
	CHECK_EQ(w.arches.size(), 3u);
	// Le varech reste sous la surface (sommet du plant mis à l'échelle).
	for (size_t v = 0; v < w.kelp.size(); ++v)
		for (const auto &inst : w.kelp[v]) {
			const float baseY = inst.model.m[3][1];
			const float scaleY = std::hypot(inst.model.m[1][0], inst.model.m[1][1], inst.model.m[1][2]);
			CHECK(baseY + kKelpHeights[v] * scaleY < kSurfaceY);
		}
	// Chaque point de vue est dans l'eau, au-dessus du fond.
	for (const Viewpoint &v : w.views) {
		CHECK(v.position.y < kCeilingY + 1e-3f);
		CHECK(v.position.y > t.Height(v.position.x, v.position.z));
	}
	// Collisions : le nageur ne traverse ni le fond ni la surface.
	math::FVector3 q{0.f, -200.f, 0.f};
	w.Collide(t, q, 0.6f);
	CHECK(q.y > t.Height(0.f, 0.f));
	CHECK(q.y <= kCeilingY);
}

namespace {

/// Formes de collision du catalogue (construites une fois pour tous les tests).
const Shapes &TestShapes(const Terrain &t) {
	static const Shapes shapes = [&] {
		std::vector<sdl3::MeshData> models(kModelCount);
		for (size_t m = 0; m < kModelCount; ++m)
			models[m] = meshes::ModelMesh(Model(m), t);
		return MakeShapes(models);
	}();
	return shapes;
}

math::FVector3 Origin(const sdl3::MeshInstance &inst) { return {inst.model.m[3][0], inst.model.m[3][1], inst.model.m[3][2]}; }

/// Avance l'écosystème de `seconds` à 60 i/s depuis `time` (plongeur sans consigne).
float Run(Ecosystem &eco, float time, float seconds) {
	for (int i = 0; i < int(seconds * 60.f); ++i) {
		eco.SteerDiver({}, 1.f / 60.f);
		eco.Update(1.f / 60.f, time, {0.f, 0.f, -1.f}, false);
		time += 1.f / 60.f;
	}
	return time;
}

/// Monde d'entrée (chunk 0,0) peuplé, prêt pour les tests de comportement.
struct Scene {
	Terrain t;
	World w;
	const Shapes &shapes;
	Ecosystem eco;
	Scene() : t(), w(t, kQualities[2]), shapes(TestShapes(t)), eco(t, shapes, w.views[0].position, 6.f) {
		eco.SpawnChunk(MakeChunkKey(0, 0), w, kQualities[2], 0x11FE);
	}
};

} // namespace

TEST(Ecosystem_EveryObjectIsAnEntity) {
	Scene s;
	ecs::World &e = s.eco.Entities();
	const size_t staticCount = e.MakeQuery<Render, Static>().Count();
	CHECK(staticCount > 100u); // le décor est peuplé d'entités immobiles
	// Les pièces maîtresses sont bien rendues (pas seulement des triangles de collision).
	bool hasWreck = false, hasRock = false, hasArch = false, hasFluke = false;
	e.MakeQuery<Render, Static>().Each([&](Render &r, Static &) {
		hasWreck |= r.model == Model::Wreck;
		hasRock |= r.model == Model::Rock0;
		hasArch |= r.model == Model::Arch;
		hasFluke |= r.model == Model::Fluke;
	});
	CHECK(hasWreck);
	CHECK(hasRock);
	CHECK(hasArch);
	CHECK(hasFluke);
	const size_t flexibleCount = e.MakeQuery<Render, Flexible, Parts>().Count();
	CHECK(flexibleCount > 20u); // varech et algues souples
	CHECK(s.eco.Instances(Model::Fish).size() > 50u);
	const size_t fishBodies = e.MakeQuery<SchoolMember, physics::RigidBody>().Count();
	CHECK_EQ(fishBodies, s.eco.Instances(Model::Fish).size());
	CHECK_EQ(s.eco.Instances(Model::Tuft).size(), s.w.tufts.size());
	CHECK(s.eco.Instances(Model::Crab).size() >= 3u);
	// Volumes : triangles du décor (terrain + roches), chaîne de sphères par plant de varech.
	CHECK(s.eco.Physics().TriangleCount() > size_t(Terrain::kCollisionGrid - 1) * (Terrain::kCollisionGrid - 1) * 2);
	e.MakeQuery<Render, Parts>().Each([](Render &r, Parts &p) {
		if (r.model == Model::Kelp0)
			CHECK_EQ(p.count, uint8_t(4));
	});
	CHECK(e.Has<Player>(s.eco.PlayerEntity()));
}

TEST(Ecosystem_FishScatterFromTheDiver) {
	Scene s;
	s.eco.SetDiverPhysics(false);
	const math::FVector3 at = Origin(s.eco.Instances(Model::Fish)[0]);
	const auto meanNear = [&](float radius) {
		float sum = 0.f;
		int n = 0;
		for (const auto &f : s.eco.Instances(Model::Fish))
			if (const float d = (Origin(f) - at).Length(); d < radius) {
				sum += d;
				++n;
			}
		return std::pair{n > 0 ? sum / float(n) : 0.f, n};
	};
	const auto [before, count] = meanNear(4.f);
	REQUIRE(count >= 3);
	s.eco.PlaceDiver(at);
	float time = 6.f;
	for (int i = 0; i < 45; ++i) {
		s.eco.PlaceDiver(at);
		s.eco.Update(1.f / 60.f, time, {0.f, 0.f, -1.f}, false);
		time += 1.f / 60.f;
	}
	float panic = 0.f;
	s.eco.Entities().MakeQuery<SchoolMember>().Each([&](SchoolMember &f) { panic = std::max(panic, f.panic); });
	CHECK(panic > 0.5f);
	const auto [after, remaining] = meanNear(4.f);
	CHECK(remaining < count || after > before + 0.5f);
	for (const auto &f : s.eco.Instances(Model::Fish)) {
		const math::FVector3 p = Origin(f);
		CHECK(p.y < kSurfaceY);
		CHECK(p.y > s.t.Height(p.x, p.z) - 0.5f);
	}
}

TEST(Ecosystem_CrustaceansWalkOnTheFloor) {
	Scene s;
	std::vector<math::FVector3> start;
	for (const auto &c : s.eco.Instances(Model::Crab))
		start.push_back(Origin(c));
	Run(s.eco, 6.f, 4.f);
	const auto &crabs = s.eco.Instances(Model::Crab);
	REQUIRE(crabs.size() == start.size());
	int walked = 0;
	for (size_t i = 0; i < crabs.size(); ++i) {
		const math::FVector3 p = Origin(crabs[i]);
		walked += std::hypot(p.x - start[i].x, p.z - start[i].z) > 0.2f ? 1 : 0;
	}
	s.eco.Entities().MakeQuery<Walker, physics::RigidBody>().Each([&](Walker &, physics::RigidBody &rb) {
		const physics::Body &b = s.eco.Physics().GetBody(rb.id);
		float lo = 1e9f, hi = -1e9f;
		for (const float dx : {-b.half.x, 0.f, b.half.x})
			for (const float dz : {-b.half.z, 0.f, b.half.z}) {
				const float h = s.t.Height(b.center.x + dx, b.center.z + dz);
				lo = std::min(lo, h);
				hi = std::max(hi, h);
			}
		const float bottom = b.center.y - b.half.y;
		CHECK(bottom > lo - 0.1f);
		CHECK(bottom < hi + 0.15f);
	});
	CHECK(walked * 2 > int(crabs.size()));
}

TEST(Ecosystem_DiverSinksSlowlyAndLandsOnRock) {
	Scene s;
	const float y0 = s.eco.DiverPosition().y;
	float time = Run(s.eco, 6.f, 2.f);
	const float drop = y0 - s.eco.DiverPosition().y;
	CHECK(drop > 0.2f && drop < 1.5f);
	math::FVector3 toward = s.w.views[1].target - s.eco.DiverPosition();
	toward.y = 0.f;
	toward = toward.Normalized();
	for (int i = 0; i < 90; ++i) {
		s.eco.SteerDiver(toward * 2.f, 1.f / 60.f);
		s.eco.Update(1.f / 60.f, time, toward, false);
		time += 1.f / 60.f;
	}
	CHECK(std::abs(s.eco.DiverVelocity().y) < 0.05f);
	CHECK(s.eco.DiverVelocity().Dot(toward) > 1.5f);
	REQUIRE(!s.w.rocks[0].empty());
	const math::FVector3 rock = Origin(*std::max_element(s.w.rocks[0].begin(), s.w.rocks[0].end(), [](const auto &a, const auto &b) {
		return a.model.m[0][0] < b.model.m[0][0];
	}));
	const auto hit = s.eco.Physics().Raycast({rock + math::FVector3{0.f, 30.f, 0.f}, {0.f, -1.f, 0.f}}, 60.f);
	REQUIRE(hit.has_value());
	// Lâché sur le bloc, il se pose dessus (collision avec la roche, pas le fond).
	s.eco.PlaceDiver(hit->point + math::FVector3{0.f, 0.6f, 0.f});
	Run(s.eco, time, 1.f);
	CHECK(s.eco.DiverPosition().y > s.t.Height(hit->point.x, hit->point.z) + 1.f);
	CHECK(s.eco.DiverPosition().y <= hit->point.y + 0.75f);
}

TEST(Ecosystem_PlantCollidersFollowTheSway) {
	Scene s;
	const float time = 9.3f;
	s.eco.Update(0.f, time, {0.f, 0.f, -1.f}, false);
	int checked = 0;
	s.eco.Entities().MakeQuery<Render, Parts>().Each([&](Render &r, Parts &p) {
		const SwayParams sway = PlantSway(r.model);
		if (!sway.Active() || checked > 20)
			return;
		const size_t top = p.count - 1u;
		const math::FVector3 expected = r.instance.model.TransformPoint(p.local[top]) + PlantSwayOffset(r.instance.model, p.local[top], time, sway);
		CHECK((s.eco.Physics().GetBody(p.body[top]).center - expected).Length() < 1e-3f);
		CHECK(PlantSwayOffset(r.instance.model, p.local[top], time, sway).Length() > 0.f);
		CHECK(PlantSwayOffset(r.instance.model, {0.f, 0.f, 0.f}, time, sway).Length() < 1e-6f);
		++checked;
	});
	CHECK(checked > 0);
}

TEST(Ecosystem_KelpBendsAwayAndRecovers) {
	Scene s;
	ecs::World &e = s.eco.Entities();
	ecs::Entity plant = ecs::kNull;
	e.MakeQuery<Render, Flexible, Parts>().Each([&](ecs::Entity id, Render &r, Flexible &, Parts &) {
		if (!plant && r.model == Model::Kelp0)
			plant = id;
	});
	REQUIRE(plant);
	const Parts &parts = *e.Get<Parts>(plant);
	const physics::Body &stem = s.eco.Physics().GetBody(parts.body[2]);
	float time = 6.f;
	for (int i = 0; i < 60; ++i) {
		s.eco.PlaceDiver(stem.center - math::FVector3{stem.radius * 0.8f, 0.f, 0.f});
		s.eco.Update(1.f / 60.f, time, {1.f, 0.f, 0.f}, false);
		time += 1.f / 60.f;
	}
	const Flexible &fx = *e.Get<Flexible>(plant);
	CHECK(fx.bend.x > 0.2f);
	const math::FVector3 top = e.Get<Render>(plant)->instance.model.TransformPoint({0.f, kKelpHeights[0], 0.f});
	CHECK(top.x > fx.base.x + 0.2f);
	s.eco.PlaceDiver(fx.base + math::FVector3{0.f, 40.f, 0.f});
	for (int i = 0; i < 600; ++i) {
		s.eco.PlaceDiver(fx.base + math::FVector3{40.f, 10.f, 0.f});
		s.eco.Update(1.f / 60.f, time, {1.f, 0.f, 0.f}, false);
		time += 1.f / 60.f;
	}
	CHECK(math::Length(fx.bend) < 0.1f);
	int fromKelp = 0;
	e.MakeQuery<BubbleState>().Each([&](BubbleState &b) {
		if (!b.source || !b.alive)
			return;
		++fromKelp;
		const Flexible *plantFx = e.Get<Flexible>(b.source);
		REQUIRE(plantFx != nullptr);
		CHECK(std::hypot(b.pos.x - plantFx->base.x, b.pos.z - plantFx->base.z) < 8.f);
		CHECK(b.pos.y > plantFx->base.y && b.pos.y < kSurfaceY);
	});
	CHECK(fromKelp > 10);
}

TEST(Ecosystem_AnimalsStartInFreeRoom) {
	Scene s;
	physics::World &phys = s.eco.Physics();
	int blocked = 0, total = 0;
	const auto check = [&](physics::RigidBody &rb) {
		++total;
		blocked += phys.Blocked(phys.GetBody(rb.id), rb.id, false) ? 1 : 0;
	};
	s.eco.Entities().MakeQuery<Walker, physics::RigidBody>().Each([&](Walker &, physics::RigidBody &rb) { check(rb); });
	s.eco.Entities().MakeQuery<Hoverer, physics::RigidBody>().Each([&](Hoverer &, physics::RigidBody &rb) { check(rb); });
	int fishInside = 0;
	s.eco.Entities().MakeQuery<SchoolMember, physics::RigidBody>().Each([&](SchoolMember &, physics::RigidBody &rb) {
		fishInside += phys.InsideSolid(phys.GetBody(rb.id).center) ? 1 : 0;
	});
	CHECK(total > 10);
	CHECK_EQ(blocked, 0);
	CHECK_EQ(fishInside, 0);
}

TEST(Ecosystem_BlockedWalkerTurnsAround) {
	Scene s;
	ecs::World &e = s.eco.Entities();
	ecs::Entity crab = ecs::kNull;
	e.MakeQuery<Walker, physics::RigidBody>().Each([&](ecs::Entity id, Walker &k, physics::RigidBody &) {
		if (!crab && k.sideways)
			crab = id;
	});
	REQUIRE(crab);
	physics::Body &body = s.eco.Physics().GetBody(e.Get<physics::RigidBody>(crab)->id);
	const math::FVector3 wall = body.center;
	float time = 6.f, jump = 0.f;
	for (int i = 0; i < 150; ++i) {
		const float before = e.Get<Walker>(crab)->heading;
		body.center = wall;
		s.eco.Update(1.f / 60.f, time, {0.f, 0.f, -1.f}, false);
		jump = std::max(jump, std::abs(e.Get<Walker>(crab)->heading - before));
		time += 1.f / 60.f;
	}
	CHECK(jump > 1.5f);
}

TEST(Ecosystem_WreckCanBeEntered) {
	Scene s;
	physics::World &phys = s.eco.Physics();
	const math::FVector3 inside = s.w.wreck.TransformPoint({-4.8f, -0.6f, 0.f});
	CHECK(inside.y > s.t.Height(inside.x, inside.z));
	CHECK(!phys.Blocked(physics::Body::MakeSphere(inside, 0.5f), -1, false));
	const math::FVector3 out = s.w.wreck.TransformPoint({-4.8f, -0.6f, -9.f});
	const math::FVector3 dir = (out - inside).Normalized();
	const auto hit = phys.Raycast({inside, dir}, (out - inside).Length());
	CHECK(!(hit && hit->user != physics::kWorld));
	const math::FVector3 aft = s.w.wreck.TransformPoint({2.f, -0.6f, 0.f}), aftOut = s.w.wreck.TransformPoint({2.f, -0.6f, -9.f});
	const auto wall = phys.Raycast({aft, (aftOut - aft).Normalized()}, (aftOut - aft).Length());
	CHECK(wall.has_value());
}

TEST(Ecosystem_LongSwimStaysHealthy) {
	Scene s;
	const char *env = std::getenv("ABYSSALE_SOAK_SECONDS");
	const float seconds = env ? float(std::atof(env)) : 8.f;
	math::Rng rng(91);
	math::FVector3 wish{};
	float time = 6.f, nextChange = 0.f, nextBreath = 4.5f, worstMs = 0.f;
	const size_t entities0 = s.eco.Entities().EntityCount();
	int nonFinite = 0;
	for (int i = 0; i < int(seconds * 60.f); ++i) {
		if (time >= nextChange) {
			nextChange = time + 1.f + 2.f * rng.NextFloat();
			wish = rng.NextFloat() < 0.25f ? math::FVector3{}
										   : math::FVector3{rng.NextFloat() - 0.5f, (rng.NextFloat() - 0.5f) * 0.6f, rng.NextFloat() - 0.5f}.Normalized() *
												 (rng.NextFloat() < 0.3f ? 9.f : 4.f);
		}
		const bool exhale = time >= nextBreath;
		if (exhale)
			nextBreath = time + 4.5f;
		const auto t0 = std::chrono::steady_clock::now();
		s.eco.SteerDiver(wish, 1.f / 60.f);
		s.eco.Update(1.f / 60.f, time, wish.LengthSq() > 0.f ? wish.Normalized() : math::FVector3{0.f, 0.f, -1.f}, exhale);
		worstMs = std::max(worstMs, std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count());
		const math::FVector3 p = s.eco.DiverPosition();
		nonFinite += std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) ? 0 : 1;
		time += 1.f / 60.f;
	}
	CHECK_EQ(nonFinite, 0);
	for (size_t b = 0; b < s.eco.Physics().BodyCount(); ++b) {
		const physics::Body &body = s.eco.Physics().GetBody(physics::BodyId(b));
		CHECK(std::isfinite(body.center.x) && std::isfinite(body.center.y) && std::isfinite(body.center.z));
	}
	CHECK(s.eco.Entities().EntityCount() < entities0 + 400);
	CHECK(s.eco.DiverPosition().y < kSurfaceY && s.eco.DiverPosition().y > s.t.Height(s.eco.DiverPosition().x, s.eco.DiverPosition().z) - 0.5f);
	std::printf("endurance : %.0f s simulées, pire pas %.1f ms, entités %zu → %zu\n", double(seconds), double(worstMs), entities0, s.eco.Entities().EntityCount());
}

TEST(Meshes_AllVerticesAreFinite) {
	const Terrain t;
	for (size_t m = 0; m < kModelCount; ++m) {
		const sdl3::MeshData mesh = meshes::ModelMesh(Model(m), t);
		size_t bad = 0;
		for (const auto &v : mesh.vertices)
			if (!(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z) && std::isfinite(v.normal.x) &&
				  std::isfinite(v.normal.y) && std::isfinite(v.normal.z)))
				++bad;
		CHECK_EQ(bad, size_t(0));
		if (bad)
			std::printf("catégorie %zu : %zu sommets non finis\n", m, bad);
	}
	const sdl3::MeshData far = meshes::FishFar();
	for (const auto &v : far.vertices)
		CHECK(std::isfinite(v.position.x) && std::isfinite(v.position.y) && std::isfinite(v.position.z));
}

TEST(Ecosystem_WalkersRestOnTheirFeet) {
	Scene s;
	Run(s.eco, 6.f, 3.f);
	physics::RayFilter ground;
	ground.bodies = false;
	float maxTilt = 0.f;
	int walkers = 0;
	s.eco.Entities().MakeQuery<Walker, Render>().Each([&](Walker &k, Render &r) {
		++walkers;
		float deviation = 0.f;
		for (size_t i = 0; i < k.footCount; ++i) {
			const math::FVector3 foot = r.instance.model.TransformPoint(k.feet[i]);
			const auto hit = s.eco.Physics().Raycast({foot + math::FVector3{0.f, 1.f, 0.f}, {0.f, -1.f, 0.f}}, 3.f, ground);
			deviation += std::abs(foot.y - (hit ? hit->point.y : s.t.Height(foot.x, foot.z))) / float(k.footCount);
		}
		CHECK(deviation < 0.15f);
		const math::FVector3 up = math::FVector3{r.instance.model.m[1][0], r.instance.model.m[1][1], r.instance.model.m[1][2]}.Normalized();
		const math::FVector3 slope = s.t.Normal(r.instance.model.m[3][0], r.instance.model.m[3][2]);
		CHECK(std::acos(std::clamp(up.Dot(slope), -1.f, 1.f)) < 0.45f);
		maxTilt = std::max(maxTilt, std::acos(std::clamp(up.y, -1.f, 1.f)));
	});
	CHECK(walkers > 3);
	CHECK(maxTilt > 0.05f);
}

TEST(Ecosystem_MantasSkimWithoutPiercing) {
	Scene s;
	physics::RayFilter ground;
	ground.bodies = false;
	float time = 6.f, worstClearance = 1e9f, maxLift = 0.f, maxAvoid = 0.f;
	for (int i = 0; i < 720; ++i) {
		s.eco.Update(1.f / 60.f, time, {0.f, 0.f, -1.f}, false);
		time += 1.f / 60.f;
		if (i % 6)
			continue;
		s.eco.Entities().MakeQuery<Cruiser, Render>().Each([&](Cruiser &c, Render &r) {
			for (const float z : {0.f, -0.6f, 0.6f}) {
				const math::FVector3 p = r.instance.model.TransformPoint({0.f, 0.f, z});
				const auto hit = s.eco.Physics().Raycast({p + math::FVector3{0.f, 30.f, 0.f}, {0.f, -1.f, 0.f}}, 60.f, ground);
				const float floorY = hit && hit->point.y <= p.y + 0.01f ? hit->point.y : s.t.Height(p.x, p.z);
				worstClearance = std::min(worstClearance, p.y - floorY);
			}
			maxLift = std::max(maxLift, c.lift);
			maxAvoid = std::max(maxAvoid, std::abs(c.roll - std::clamp(c.orbit.speed * 5.f, -0.4f, 0.4f)));
		});
	}
	CHECK(worstClearance > 0.2f);
	CHECK(maxLift > 0.1f || maxAvoid > 0.05f);
	std::printf("raies : marge minimale %.2f m, montée maximale %.2f m, roulis d'évitement maximal %.2f rad\n", double(worstClearance), double(maxLift),
				double(maxAvoid));
}

TEST(Ambience_StaysAroundTheCamera) {
	const Terrain t;
	Ambience ambience(t, kQualities[2]);
	const math::FVector3 cam{0.f, -20.f, 0.f};
	for (int i = 0; i < 30; ++i)
		ambience.Update(1.f / 30.f, 6.f + float(i) / 30.f, cam, math::FVector3{0.22f, -1.f, 0.12f}.Normalized());
	CHECK(!ambience.motes.empty());
	for (const auto &m : ambience.motes)
		CHECK((Origin(m) - cam).Length() < 40.f);
}

TEST(Textures_AreTileableAndSized) {
	const Image atlas = TerrainAtlas(64);
	CHECK_EQ(atlas.width, 128u);
	CHECK_EQ(atlas.rgba.size(), size_t(128) * 64 * 4);
	const Image shaft = ShaftTexture(16, 32);
	CHECK_EQ(shaft.rgba[0], uint8_t(0));
	CHECK(shaft.rgba[(4 * 16 + 8) * 4] > uint8_t(100));
}

TEST(OceanSynth_ProducesBoundedLivingSound) {
	OceanSynth synth(48000.f, 7);
	synth.SetVolume(1.f);
	synth.SetState({6.f, 8.f, 8, true});
	synth.TriggerWhale();
	std::vector<float> buffer(48000 * 3 * 2);
	synth.Render(buffer);
	float peak = 0.f, energy = 0.f, diff = 0.f;
	for (size_t i = 48000 * 2; i < buffer.size(); i += 2) {
		CHECK(std::isfinite(buffer[i]) && std::isfinite(buffer[i + 1]));
		peak = std::max({peak, std::abs(buffer[i]), std::abs(buffer[i + 1])});
		energy += buffer[i] * buffer[i];
		diff += std::abs(buffer[i] - buffer[i + 1]);
	}
	CHECK(peak <= 0.5f + 1e-4f);
	CHECK(std::sqrt(energy / 96000.f) > 1e-3f);
	CHECK(diff > 1.f);

	synth.SetVolume(0.f);
	std::vector<float> quiet(48000 * 2);
	synth.Render(quiet);
	float tail = 0.f;
	for (size_t i = quiet.size() * 9 / 10; i < quiet.size(); ++i)
		tail = std::max(tail, std::abs(quiet[i]));
	CHECK(tail < 1e-3f);
	CHECK_EQ(synth.Volume(), 0.f);
}
