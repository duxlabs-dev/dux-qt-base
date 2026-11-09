#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <ranges>

namespace qs::core {

class FrameClock {
public:
	void request(const void* window, std::int64_t now, double refreshRate) {
		const auto rate = std::isfinite(refreshRate) && refreshRate > 0 ? refreshRate : 60;
		const auto period = static_cast<std::int64_t>(2e9 / rate);
		this->pending.try_emplace(window, now + period);
		this->fallbackPeriod = std::max(MIN_UNRENDERED_PERIOD, period);
	}

	void forget(const void* window) { this->pending.erase(window); }

	bool sample(std::int64_t now) {
		std::erase_if(this->pending, [now](const auto& entry) { return entry.second <= now; });
		const auto millisecond = now / 1000000;
		if (millisecond == this->lastSample) return false;
		this->lastSample = millisecond;
		return true;
	}

	[[nodiscard]] std::int64_t delay(std::int64_t now) const {
		if (this->pending.empty()) return this->fallbackPeriod;
		const auto deadline = std::ranges::min(std::views::values(this->pending));
		return std::max<std::int64_t>(1, deadline - now);
	}

	void reset() {
		this->pending.clear();
		this->lastSample = -1;
	}

private:
	// Animations no window renders tick at most 60 times a second.
	static constexpr std::int64_t MIN_UNRENDERED_PERIOD = 16'666'667;

	std::unordered_map<const void*, std::int64_t> pending;
	std::int64_t lastSample = -1;
	std::int64_t fallbackPeriod = 33333333;
};

} // namespace qs::core
