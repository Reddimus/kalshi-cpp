#pragma once

#include "kalshi/error.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace kalshi {

/// Token bucket rate limiter
///
/// Implements a token bucket algorithm to limit request rates.
/// Thread-safe for concurrent access.
class RateLimiter {
public:
	/// Configuration for rate limiting
	struct Config {
		std::int32_t max_tokens = 10;					   // Maximum tokens in bucket
		std::chrono::milliseconds refill_interval{1000};   // Time to add one token
		std::int32_t initial_tokens = 10;				   // Starting tokens
		std::optional<std::chrono::milliseconds> max_wait; // Max time to wait
	};

	explicit RateLimiter(Config config);

	/// Try to acquire a token, returns true if successful
	[[nodiscard]] bool try_acquire() noexcept;

	/// Acquire a token, blocking if necessary
	/// Returns false if max_wait exceeded
	[[nodiscard]] bool acquire();

	/// Acquire a token, blocking up to max_wait
	[[nodiscard]] bool acquire_for(std::chrono::milliseconds max_wait);

	/// Get current number of available tokens
	[[nodiscard]] std::int32_t available_tokens() const noexcept;

	/// Reset the rate limiter to initial state
	void reset() noexcept;

	/// Get the configuration
	[[nodiscard]] const Config& config() const noexcept;

private:
	void refill() noexcept;

	Config config_;
	mutable std::mutex mutex_;
	std::int32_t tokens_;
	std::chrono::steady_clock::time_point last_refill_;
};

/// Scoped rate limit acquisition
///
/// RAII wrapper that acquires a rate limit token on construction.
class ScopedRateLimit {
public:
	explicit ScopedRateLimit(RateLimiter& limiter);

	/// Check if acquisition was successful
	[[nodiscard]] bool acquired() const noexcept;

	/// Implicit conversion to bool for easy checking
	explicit operator bool() const noexcept;

private:
	bool acquired_;
};

} // namespace kalshi
