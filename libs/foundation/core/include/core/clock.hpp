#pragma once
/**
 * @file clock.hpp
 * @brief Horloges `std::chrono::steady_clock` : chronomètre et horloge d'images
 *        (dt borné, moyenne glissante exponentielle des FPS).
 */
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace core {

using SteadyClock = std::chrono::steady_clock;

[[nodiscard]] inline int64_t NowNs() noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now().time_since_epoch()).count();
}

class Stopwatch {
	SteadyClock::time_point m_start = SteadyClock::now();

public:
	void Restart() noexcept { m_start = SteadyClock::now(); }
	[[nodiscard]] double Seconds() const noexcept { return std::chrono::duration<double>(SteadyClock::now() - m_start).count(); }
	[[nodiscard]] double Millis() const noexcept { return Seconds() * 1000.0; }
};

class FrameClock {
	SteadyClock::time_point m_last = SteadyClock::now();
	double m_dt = 1.0 / 60.0;
	double m_smoothed = 1.0 / 60.0;
	double m_elapsed = 0.0;
	uint64_t m_frame = 0;

public:
	/// À appeler une fois par image ; retourne dt (borné à `maxDt`).
	double Tick(double maxDt = 0.1) noexcept {
		const auto now = SteadyClock::now();
		m_dt = std::min(std::chrono::duration<double>(now - m_last).count(), maxDt);
		m_last = now;
		m_smoothed += (m_dt - m_smoothed) * 0.05;
		m_elapsed += m_dt;
		++m_frame;
		return m_dt;
	}
	[[nodiscard]] double Dt() const noexcept { return m_dt; }
	[[nodiscard]] double Fps() const noexcept { return m_smoothed > 0 ? 1.0 / m_smoothed : 0.0; }
	[[nodiscard]] double FrameMs() const noexcept { return m_smoothed * 1000.0; }
	[[nodiscard]] double Elapsed() const noexcept { return m_elapsed; }
	[[nodiscard]] uint64_t Frame() const noexcept { return m_frame; }
};

} // namespace core
