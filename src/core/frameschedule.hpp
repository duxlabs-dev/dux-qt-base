#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include <ranges>

namespace qs::core {

// Predicts each rendering window's next frame from its last frame start and its screen's period.
class FrameSchedule {
public:
	static constexpr std::int64_t NONE = std::numeric_limits<std::int64_t>::max();

	void started(const void* window, std::int64_t now, std::int64_t period) {
		this->windows.insert_or_assign(window, Window {.start = now, .period = period});
	}

	void forget(const void* window) { this->windows.erase(window); }

	// The latest time work can run until without delaying a window that renders every refresh.
	[[nodiscard]] std::int64_t deadline(std::int64_t now, std::int64_t idleSlice) {
		this->expire(now);
		auto end = now + idleSlice;
		for (const auto& frame: std::views::values(this->windows)) {
			const auto next = frame.start + frame.period;
			end = std::min(end, std::max(now, next - frame.period / MARGIN_DIVISOR));
		}
		return end;
	}

	// The period of the fastest rendering window, or idle when none renders.
	[[nodiscard]] std::int64_t shortestPeriod(std::int64_t idle) const {
		if (this->windows.empty()) return idle;
		return std::ranges::min(std::views::values(this->windows), {}, &Window::period).period;
	}

	// When the earliest window stops counting as rendering, for when its next frame never starts.
	[[nodiscard]] std::int64_t expiry() const {
		auto earliest = NONE;
		for (const auto& frame: std::views::values(this->windows)) {
			earliest = std::min(earliest, frame.start + frame.period * STALE_PERIODS);
		}
		return earliest;
	}

private:
	static constexpr std::int64_t MARGIN_DIVISOR = 8;
	static constexpr std::int64_t STALE_PERIODS = 2;

	struct Window {
		std::int64_t start = 0;
		std::int64_t period = 0;
	};

	void expire(std::int64_t now) {
		std::erase_if(this->windows, [now](const auto& entry) {
			return now >= entry.second.start + entry.second.period * STALE_PERIODS;
		});
	}

	std::unordered_map<const void*, Window> windows;
};

} // namespace qs::core
