#pragma once

#include <cstdint>
#include <cstdlib>
#include <unordered_map>

namespace qs::core {

// Locks each window's frame times to the cadence its frames arrive at, so animation time follows
// the display instead of the jitter in frame callback delivery.
class FramePhase {
public:
	// The time to animate a frame at that started at now. The nominal period only seeds the lock:
	// the cadence is measured, since outputs do not always refresh at the rate they report.
	[[nodiscard]] std::int64_t lock(const void* window, std::int64_t now, std::int64_t nominal) {
		auto& state = this->windows[window];
		if (state.nominal != nominal) {
			state = {.nominal = nominal, .last = now, .first = now};
			return now;
		}

		const auto period = state.period ? state.period : nominal;
		const auto steps = (now - state.last + period / 2) / period;
		// The cadence outlives a pause or a repeated frame, the phase does not.
		if (steps < 1 || steps > MAX_STEPS) {
			state.rephase(now);
			return now;
		}

		const auto predicted = state.last + steps * period;
		const auto error = now - predicted;
		const auto onCadence = std::abs(error) <= period / LOCK_DIVISOR;

		if (!state.period) {
			if (!onCadence) {
				state.rephase(now);
				return now;
			}

			state.last = now;
			state.steps += steps;
			if (state.steps >= MEASURE_STEPS) state.period = (now - state.first) / state.steps;
			return now;
		}

		if (!onCadence) {
			// One late start still lands on its refresh. More means the cadence is gone, as with
			// variable refresh or a throttled surface, so follow the real time again.
			if (++state.misses < MAX_MISSES) {
				state.last = predicted;
				return predicted;
			}

			state = {.nominal = nominal, .last = now, .first = now};
			return now;
		}

		state.misses = 0;
		state.last = predicted + error / PHASE_DIVISOR;
		state.period += error / (PERIOD_DIVISOR * steps);
		return state.last;
	}

	void forget(const void* window) { this->windows.erase(window); }

private:
	static constexpr std::int64_t MAX_STEPS = 4;
	static constexpr std::int64_t MEASURE_STEPS = 8;
	static constexpr std::int64_t LOCK_DIVISOR = 4;
	static constexpr std::int64_t PHASE_DIVISOR = 8;
	static constexpr std::int64_t PERIOD_DIVISOR = 256;
	static constexpr int MAX_MISSES = 2;

	struct Window {
		std::int64_t nominal = 0;
		std::int64_t last = 0;
		std::int64_t first = 0;
		std::int64_t steps = 0;
		std::int64_t period = 0;
		int misses = 0;

		void rephase(std::int64_t now) {
			this->last = now;
			this->first = now;
			this->steps = 0;
			this->misses = 0;
		}
	};

	std::unordered_map<const void*, Window> windows;
};

} // namespace qs::core
