#include <cpp-maker/test.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <jobs/jobs.hpp>

TEST(ParallelFor_VisitsEveryIndexOnce) {
	jobs::ThreadPool pool(3);
	std::vector<std::atomic<int>> hits(10000);
	jobs::ParallelFor(pool, 0, hits.size(), 97, [&](size_t i) { hits[i].fetch_add(1); });
	for (const auto &h : hits)
		CHECK_EQ(h.load(), 1);
}

TEST(AsyncValue_LatestRequestWins) {
	jobs::ThreadPool pool(1);
	jobs::AsyncValue<std::string> value;
	CHECK(value.Get() == nullptr);
	std::atomic<bool> release{false};
	std::atomic<bool> firstCancelled{false}, started{false};
	// Première demande : bloquée jusqu'à ce que la seconde soit soumise.
	value.Request(pool, [&](std::stop_token stop) {
		started = true;
		while (!release.load())
			std::this_thread::yield();
		firstCancelled = stop.stop_requested();
		return std::string("ancienne");
	});
	CHECK(value.Loading());
	while (!started.load())
		std::this_thread::yield();
	value.Request(pool, [](std::stop_token) { return std::string("récente"); });
	release = true;
	pool.WaitIdle();
	CHECK(firstCancelled.load());
	CHECK(!value.Loading());
	CHECK(value.Poll());
	REQUIRE(value.Get() != nullptr);
	CHECK_EQ(*value.Get(), std::string("récente"));
	CHECK(!value.Poll()); // rien de neuf
}

TEST(AsyncValue_CancelAndClear) {
	jobs::ThreadPool pool(1);
	jobs::AsyncValue<int> value;
	value.Request(pool, [](std::stop_token) { return 1; });
	pool.WaitIdle();
	CHECK(value.Poll());
	CHECK_EQ(*value.Get(), 1);
	value.Request(pool, [](std::stop_token) { return 2; });
	value.Cancel();
	pool.WaitIdle();
	CHECK(!value.Poll());
	CHECK_EQ(*value.Get(), 1); // l'ancienne valeur reste affichée
	value.Clear();
	CHECK(value.Get() == nullptr);
}
