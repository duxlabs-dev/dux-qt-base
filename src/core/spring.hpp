#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace qs::core {

inline std::array<double, 4>
springCoefficients(double stiffness, double damping, double mass, double seconds) {
	if (!std::isfinite(seconds) || !std::isfinite(stiffness) || !std::isfinite(damping)
	    || !std::isfinite(mass))
		return {};
	if (seconds <= 0) return {1, 0, 0, 1};

	const auto inverseMass = 1 / std::max(0.001, mass);
	const auto omegaSquared = std::max(0.0, stiffness) * inverseMass;
	const auto decay = std::max(0.0, damping) * inverseMass / 2;
	const auto discriminant = decay * decay - omegaSquared;
	const auto tolerance = 1e-10 * std::max(1.0, omegaSquared);
	if (discriminant > tolerance) {
		const auto root = std::sqrt(discriminant);
		const auto slow = -omegaSquared / (decay + root);
		const auto fast = -decay - root;
		const auto slowExp = std::exp(slow * seconds);
		const auto fastExp = std::exp(fast * seconds);
		const auto divisor = slow - fast;
		const auto b = (slowExp - fastExp) / divisor;
		return {
		    (-fast * slowExp + slow * fastExp) / divisor,
		    b,
		    -omegaSquared * b,
		    (slow * slowExp - fast * fastExp) / divisor,
		};
	}

	const auto envelope = std::exp(-decay * seconds);
	auto cosine = 1.0;
	auto sine = seconds;
	if (discriminant < -tolerance) {
		const auto frequency = std::sqrt(-discriminant);
		cosine = std::cos(frequency * seconds);
		sine = std::sin(frequency * seconds) / frequency;
	}
	const auto b = envelope * sine;
	return {envelope * cosine + decay * b, b, -omegaSquared * b, envelope * cosine - decay * b};
}

} // namespace qs::core
