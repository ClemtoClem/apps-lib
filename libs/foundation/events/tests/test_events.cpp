#include <cpp-maker/test.hpp>

#include <string>
#include <thread>

#include <events/events.hpp>

struct Damage { int amount; };
struct Spawned { int id; };

TEST(Signal_PriorityConsumeDisconnect) {
	events::Signal<int> sig;
	std::string order;
	auto a = sig.Connect([&](int) { order += 'a'; }, 0);
	auto b = sig.Connect([&](int v) { order += 'b'; return v > 5; }, 10);
	sig.Emit(1);
	CHECK_EQ(order, std::string("ba"));
	CHECK(sig.Emit(9));
	CHECK_EQ(order, std::string("bab"));
	b.Disconnect();
	sig.Emit(1);
	CHECK_EQ(order, std::string("baba"));
	CHECK_EQ(sig.Count(), 1u);
}

TEST(EventBus_DoubleBufferedBatches) {
	events::EventBus bus;
	int total = 0, batches = 0;
	auto c1 = bus.Subscribe<Damage>([&](std::span<const Damage> evs) {
		++batches;
		for (auto &d : evs) total += d.amount;
	});
	int spawned = 0;
	auto c2 = bus.Subscribe<Spawned>([&](const Spawned &) { ++spawned; });
	std::jthread producer([&] { for (int i = 0; i < 1000; ++i) bus.Publish(Damage{1}); });
	producer.join();
	bus.Publish(Spawned{1});
	CHECK_EQ(bus.Dispatch(), 1001u);
	CHECK_EQ(total, 1000);
	CHECK_EQ(batches, 1);
	CHECK_EQ(spawned, 1);
	CHECK_EQ(bus.Dispatch(), 0u);
}

TEST(NamedEvents_ModHooks) {
	events::NamedEvents<std::string> ev;
	std::string got;
	auto c = ev.On("world_load", [&](const std::string &s) { got = s; });
	CHECK_FALSE(ev.Emit("world_load", std::string("ok")));
	CHECK_EQ(got, std::string("ok"));
	CHECK_EQ(ev.Listeners("world_load"), 1u);
	CHECK_EQ(ev.Listeners("tick"), 0u);
}
