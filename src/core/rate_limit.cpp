#include "kalshi/rate_limit.hpp"

#include <algorithm>
#include <thread>

namespace kalshi {

RateLimiter::RateLimiter(Config config)
	: config_(std::move(config)), tokens_(config_.initial_tokens),
	  last_refill_(std::chrono::steady_clock::now()) {}

void RateLimiter::refill() noexcept {
	std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	std::chrono::milliseconds elapsed =
		std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill_);

	if (elapsed >= config_.refill_interval) {
		// Use wider type for arithmetic to avoid overflow
		std::int64_t intervals = elapsed / config_.refill_interval;
		std::int64_t new_tokens = static_cast<std::int64_t>(tokens_) + intervals;
		tokens_ = static_cast<std::uint16_t>(
			std::min(new_tokens, static_cast<std::int64_t>(config_.max_tokens)));
		last_refill_ = now;
	}
}

bool RateLimiter::try_acquire() noexcept {
	std::lock_guard lock(mutex_);
	refill();

	if (tokens_ > 0) {
		--tokens_;
		return true;
	}
	return false;
}

bool RateLimiter::acquire() {
	if (config_.max_wait) {
		return acquire_for(*config_.max_wait);
	}

	while (!try_acquire()) {
		std::this_thread::sleep_for(config_.refill_interval / 10);
	}
	return true;
}

bool RateLimiter::acquire_for(std::chrono::milliseconds max_wait) {
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + max_wait;

	while (std::chrono::steady_clock::now() < deadline) {
		if (try_acquire()) {
			return true;
		}
		std::chrono::milliseconds sleep_time = std::min(
			config_.refill_interval / 10, std::chrono::duration_cast<std::chrono::milliseconds>(
											  deadline - std::chrono::steady_clock::now()));
		if (sleep_time.count() > 0) {
			std::this_thread::sleep_for(sleep_time);
		}
	}

	return false;
}

std::uint16_t RateLimiter::available_tokens() const noexcept {
	std::lock_guard lock(mutex_);
	return tokens_;
}

void RateLimiter::reset() noexcept {
	std::lock_guard lock(mutex_);
	tokens_ = config_.initial_tokens;
	last_refill_ = std::chrono::steady_clock::now();
}

const RateLimiter::Config& RateLimiter::config() const noexcept {
	return config_;
}

// ScopedRateLimit
ScopedRateLimit::ScopedRateLimit(RateLimiter& limiter) : acquired_(limiter.acquire()) {}

bool ScopedRateLimit::acquired() const noexcept {
	return acquired_;
}

ScopedRateLimit::operator bool() const noexcept {
	return acquired_;
}

} // namespace kalshi
