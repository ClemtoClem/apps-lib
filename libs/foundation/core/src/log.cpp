#include "core/log.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <print>

namespace core::log {
namespace {
std::mutex gMutex;
Sink gSink;
std::atomic<Level> gMin{Level::Info};
} // namespace

Sink SetSink(Sink sink) {
	std::scoped_lock lock(gMutex);
	return std::exchange(gSink, std::move(sink));
}
void SetMinLevel(Level level) noexcept { gMin.store(level, std::memory_order_relaxed); }
Level MinLevel() noexcept { return gMin.load(std::memory_order_relaxed); }

std::string_view LevelName(Level level) noexcept {
	switch (level) {
	case Level::Trace: return "trace";
	case Level::Debug: return "debug";
	case Level::Info: return "info";
	case Level::Warn: return "warn";
	case Level::Error: return "error";
	}
	return "?";
}

void Write(Level level, std::string_view message) {
	std::scoped_lock lock(gMutex);
	if (gSink) {
		gSink(level, message);
		return;
	}
	std::println(stderr, "[{}] {}", LevelName(level), message);
}

} // namespace core::log
