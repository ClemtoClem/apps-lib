#include <cpp-maker/test.hpp>

#include <filesystem>
#include <thread>

#include <diagnostics/assert.hpp>
#include <diagnostics/capture.hpp>
#include <diagnostics/trace.hpp>

TEST(Trace_RecordAndParse) {
	const auto path = (std::filesystem::temp_directory_path() / "apps_lib_trace_test.vcap").string();
	REQUIRE(diagnostics::trace::Start(path));
	DIAG_THREAD_NAME("main");
	for (int f = 0; f < 3; ++f) {
		DIAG_ZONE("Frame");
		{
			DIAG_ZONE("Update");
			DIAG_COUNTER("entities", 100 + f);
		}
		std::jthread worker([] {
			DIAG_THREAD_NAME("worker");
			DIAG_ZONE("Job");
		});
		worker.join();
		DIAG_FRAME();
	}
	REQUIRE(diagnostics::trace::Stop());
	auto cap = diagnostics::capture::Load(path);
	REQUIRE(cap.IsOk());
	const auto &c = cap.Value();
	CHECK_EQ(c.frames.size(), 3u);
	size_t zones = 0, updates = 0;
	for (const auto &t : c.threads)
		for (const auto &z : t.zones) {
			++zones;
			if (c.Name(z.name) == "Update") {
				++updates;
				CHECK_EQ(z.depth, 1u);
			}
		}
	CHECK_EQ(updates, 3u);
	CHECK_GE(zones, 9u);
	std::filesystem::remove(path);
}

TEST(Assert_Verify) {
	CHECK_FALSE(DIAG_VERIFY(1 == 2, "attendu {}", 3));
	CHECK(DIAG_VERIFY(true, "ok"));
}
