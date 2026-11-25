#include <cpp-maker/test.hpp>

#include <atomic>
#include <chrono>
#include <memory>
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

TEST(AsyncValue_WaitBlocksUntilCancelledJobsFinish) {
	jobs::ThreadPool pool(2);
	jobs::AsyncValue<int> value;
	value.Wait(); // rien en vol : rend la main tout de suite
	std::atomic<bool> started{false}, finished{false};
	value.Request(pool, [&](std::stop_token) {
		started = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		finished = true;
		return 1;
	});
	while (!started.load())
		std::this_thread::yield();
	value.Cancel(); // annulée mais déjà en cours : Wait() doit attendre sa fin
	value.Wait();
	CHECK(finished.load());
	CHECK(!value.Loading());
	CHECK(!value.Poll());
}

TEST(WaitGroup_SafeToDestroyRightAfterWait) {
	// Le WaitGroup est détruit dès que l'attente rend la main : un `Done()` encore en
	// train de notifier toucherait un objet détruit (détecté par ASan / TSan).
	jobs::ThreadPool pool(4);
	for (int round = 0; round < 2000; ++round) {
		auto wg = std::make_unique<jobs::WaitGroup>();
		wg->Add(3);
		for (int t = 0; t < 3; ++t)
			pool.Submit([raw = wg.get()] { raw->Done(); });
		wg->Wait();
		CHECK_EQ(wg->Pending(), int64_t(0));
		wg.reset();
	}
	// Même scénario à travers ParallelFor (WaitGroup sur la pile de l'appelant).
	std::atomic<size_t> sum{0};
	for (int round = 0; round < 2000; ++round)
		jobs::ParallelFor(pool, 0, 8, 1, [&](size_t i) { sum.fetch_add(i, std::memory_order_relaxed); });
	CHECK_EQ(sum.load(), size_t(2000 * 28));
}
