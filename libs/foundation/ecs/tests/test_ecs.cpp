#include <cpp-maker/test.hpp>

#include <chrono>
#include <cstdio>
#include <string>

#include <ecs/ecs.hpp>

namespace {
struct Position { float x, y, z; };
struct Velocity { float x, y, z; };
struct Health { int hp; };
struct Name { std::string value; };
struct Frozen {};

double Ms(auto t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
} // namespace

TEST(Ecs_SpawnGetDespawn) {
	ecs::World w;
	ecs::Entity a = w.Spawn(Position{1, 2, 3}, Name{"alpha"});
	ecs::Entity b = w.Spawn(Position{4, 5, 6});
	CHECK(w.IsAlive(a));
	CHECK_EQ(w.EntityCount(), 2u);
	CHECK_EQ(w.Get<Name>(a)->value, std::string("alpha"));
	CHECK(w.Get<Name>(b) == nullptr);
	CHECK(w.Despawn(a));
	CHECK_FALSE(w.IsAlive(a));
	CHECK_EQ(w.Get<Position>(b)->x, 4.0f);
	ecs::Entity c = w.Spawn();
	CHECK_EQ(c.index, a.index);
	CHECK_NE(c.generation, a.generation);
}

TEST(Ecs_AddRemoveMovesArchetypes) {
	ecs::World w;
	ecs::Entity e = w.Spawn(Position{1, 1, 1});
	w.Add(e, Name{"bob"});
	w.Add(e, Velocity{2, 0, 0});
	CHECK(w.Has<Name>(e));
	CHECK_EQ(w.Get<Position>(e)->x, 1.0f);
	CHECK(w.Remove<Name>(e));
	CHECK_FALSE(w.Has<Name>(e));
	CHECK_EQ(w.Get<Velocity>(e)->x, 2.0f);
	CHECK_FALSE(w.Remove<Name>(e));
}

TEST(Ecs_QueryEachWithoutAndEntity) {
	ecs::World w;
	for (int i = 0; i < 100; ++i) {
		ecs::Entity e = w.Spawn(Position{float(i), 0, 0}, Velocity{1, 0, 0});
		if (i % 2)
			w.Add(e, Frozen{});
	}
	auto q = w.MakeQuery<Position, const Velocity>().Without<Frozen>();
	CHECK_EQ(q.Count(), 50u);
	q.Each([](Position &p, const Velocity &v) { p.x += v.x; });
	int n = 0;
	w.MakeQuery<Position>().Each([&](ecs::Entity e, Position &p) {
		if (!w.Has<Frozen>(e))
			CHECK_EQ(int(p.x) % 2, 1);
		++n;
	});
	CHECK_EQ(n, 100);
}

TEST(Ecs_CommandBufferAndResources) {
	ecs::World w;
	w.SetResource<Health>(Health{42});
	CHECK_EQ(w.GetResource<Health>()->hp, 42);
	ecs::CommandBuffer cb;
	w.SpawnBatch(10, Health{1});
	w.MakeQuery<Health>().Each([&](ecs::Entity e, Health &) { cb.Despawn(e); });
	cb.Spawn(Position{0, 0, 0});
	w.Apply(cb);
	CHECK_EQ(w.EntityCount(), 1u);
}

TEST(Ecs_Benchmark_200k) {
	ecs::World w;
	constexpr size_t N = 200'000;
	auto t0 = std::chrono::steady_clock::now();
	w.SpawnBatch(N, Position{0, 0, 0}, Velocity{1, 2, 3});
	const double spawnMs = Ms(t0);
	auto q = w.MakeQuery<Position, const Velocity>();
	t0 = std::chrono::steady_clock::now();
	q.Each([](Position &p, const Velocity &v) { p.x += v.x * 0.016f; p.y += v.y * 0.016f; p.z += v.z * 0.016f; });
	const double eachMs = Ms(t0);
	t0 = std::chrono::steady_clock::now();
	q.ParEach([](Position &p, const Velocity &v) { p.x += v.x * 0.016f; p.y += v.y * 0.016f; p.z += v.z * 0.016f; });
	const double parMs = Ms(t0);
	std::printf("    ecs 200k : spawn %.2f ms, Each %.2f ms, ParEach %.2f ms\n", spawnMs, eachMs, parMs);
	float sum = 0;
	q.Each([&](Position &p, const Velocity &) { sum += p.x; });
	CHECK_NEAR(sum, float(N) * 0.032f, float(N) * 0.001f);
	CHECK_EQ(q.Count(), N);
}
