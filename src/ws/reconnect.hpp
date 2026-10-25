#pragma once

#include "kalshi/websocket.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace kalshi::detail {

inline std::chrono::milliseconds reconnect_delay(const WsConfig& config, std::uint32_t failures,
												 double jitter) {
	using Milliseconds = std::chrono::milliseconds;
	// Leave headroom for the timer epoch when libwebsockets adds microseconds.
	constexpr Milliseconds timer_limit =
		std::chrono::duration_cast<Milliseconds>(std::chrono::microseconds::max() / 2);
	const Milliseconds maximum =
		std::clamp(config.max_reconnect_delay, Milliseconds::zero(), timer_limit);
	const Milliseconds initial = std::clamp(config.reconnect_delay, Milliseconds::zero(), maximum);
	const double cap = static_cast<double>(maximum.count());
	const double delay = std::min(
		std::ldexp(static_cast<double>(initial.count()), static_cast<int>(std::min(failures, 20U))),
		cap);
	const double scaled = delay * std::clamp(jitter, 0.5, 1.0);
	return scaled >= cap ? maximum : Milliseconds{std::llround(scaled)};
}

} // namespace kalshi::detail
