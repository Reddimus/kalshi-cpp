#include "kalshi/rate_limit.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace kalshi {

namespace {

double non_negative(double value) noexcept {
	return std::isfinite(value) && value > 0.0 ? value : 0.0;
}

} // namespace

TokenBucket::TokenBucket(Config config) : config_(config), last_refill_(Clock::now()) {
	config_.capacity = non_negative(config_.capacity);
	config_.refill_per_second = non_negative(config_.refill_per_second);
	tokens_ =
		std::min(non_negative(config_.initial_tokens.value_or(config_.capacity)), config_.capacity);
}

void TokenBucket::refill(Clock::time_point now) const noexcept {
	const std::chrono::duration<double> elapsed = now - last_refill_;
	if (elapsed.count() > 0.0) {
		tokens_ = std::min(config_.capacity, tokens_ + elapsed.count() * config_.refill_per_second);
		last_refill_ = now;
	}
}

std::chrono::nanoseconds TokenBucket::wait_time_locked(double cost) const noexcept {
	cost = non_negative(cost);
	if (tokens_ >= cost) {
		return std::chrono::nanoseconds::zero();
	}
	if (cost > config_.capacity || config_.refill_per_second <= 0.0) {
		return std::chrono::nanoseconds::max();
	}
	const double seconds = (cost - tokens_) / config_.refill_per_second;
	// Beyond ~30 years the nanosecond count would overflow; treat it as never.
	if (!(seconds < 1e9)) {
		return std::chrono::nanoseconds::max();
	}
	return std::chrono::ceil<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds));
}

bool TokenBucket::try_acquire(double cost) noexcept {
	const std::scoped_lock lock(mutex_);
	refill(Clock::now());
	cost = non_negative(cost);
	if (tokens_ < cost) {
		return false;
	}
	tokens_ -= cost;
	return true;
}

bool TokenBucket::acquire_for(double cost, std::chrono::nanoseconds max_wait) {
	const Clock::time_point start = Clock::now();
	const Clock::time_point deadline = max_wait >= Clock::time_point::max() - start
										   ? Clock::time_point::max()
										   : start + std::max(max_wait, std::chrono::nanoseconds{});
	while (true) {
		std::chrono::nanoseconds wait{};
		{
			const std::scoped_lock lock(mutex_);
			const Clock::time_point now = Clock::now();
			refill(now);
			wait = wait_time_locked(cost);
			if (wait == std::chrono::nanoseconds::zero()) {
				tokens_ -= non_negative(cost);
				return true;
			}
			if (wait == std::chrono::nanoseconds::max() || wait > deadline - now) {
				return false;
			}
		}
		// Other threads may take tokens meanwhile; the loop re-checks.
		std::this_thread::sleep_for(wait);
	}
}

std::chrono::nanoseconds TokenBucket::wait_time(double cost) const noexcept {
	const std::scoped_lock lock(mutex_);
	refill(Clock::now());
	return wait_time_locked(cost);
}

double TokenBucket::available() const noexcept {
	const std::scoped_lock lock(mutex_);
	refill(Clock::now());
	return tokens_;
}

void TokenBucket::reset() noexcept {
	const std::scoped_lock lock(mutex_);
	tokens_ =
		std::min(non_negative(config_.initial_tokens.value_or(config_.capacity)), config_.capacity);
	last_refill_ = Clock::now();
}

} // namespace kalshi
