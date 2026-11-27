#pragma once

#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalshi {

/// Token bucket that refills continuously, the model Kalshi's rate limits use.
/// A request proceeds when the bucket covers its cost. Thread-safe, and waiting
/// callers are served in arrival order: a waiter reserves its tokens up front,
/// so a large request is not starved by a stream of small ones.
class TokenBucket {
public:
	struct Config {
		double capacity{0.0};
		double refill_per_second{0.0};
		/// Starting balance. Defaults to a full bucket.
		std::optional<double> initial_tokens;
	};

	explicit TokenBucket(Config config);

	/// Takes `cost` tokens if the bucket holds them.
	[[nodiscard]] bool try_acquire(double cost = 1.0) noexcept;

	/// Waits up to `max_wait` for `cost` tokens. Returns false without taking
	/// tokens if they would not arrive in time or `cost` exceeds the capacity.
	[[nodiscard]] bool acquire_for(double cost, std::chrono::nanoseconds max_wait);

	/// How long until `cost` tokens are available: zero if they are available
	/// now, `nanoseconds::max()` if they never will be (cost above capacity).
	[[nodiscard]] std::chrono::nanoseconds wait_time(double cost) const noexcept;

	/// Tokens available now. Zero while earlier waiters hold reservations.
	[[nodiscard]] double available() const noexcept;
	void reset() noexcept;
	[[nodiscard]] const Config& config() const noexcept { return config_; }

private:
	using Clock = std::chrono::steady_clock;

	void refill(Clock::time_point now) const noexcept;
	[[nodiscard]] std::chrono::nanoseconds wait_time_locked(double cost) const noexcept;

	Config config_;
	mutable std::mutex mutex_;
	mutable double tokens_{0.0};
	mutable Clock::time_point last_refill_;
};

/// Token cost of requests matching `method` and `path`. `{name}` segments in
/// `path` match any single segment, as in `/portfolio/orders/{order_id}`.
struct EndpointCostRule {
	HttpMethod method{HttpMethod::GET};
	std::string path;
	double cost{0.0};
};

/// Defaults match Kalshi's Basic tier. Use `rate_limit_config()` for your
/// account's actual budgets.
struct RateLimitConfig {
	TokenBucket::Config read{.capacity = 600.0, .refill_per_second = 200.0, .initial_tokens = {}};
	TokenBucket::Config write{.capacity = 100.0, .refill_per_second = 100.0, .initial_tokens = {}};
	/// Cost of requests without a matching rule. Kalshi's default is 10.
	double default_cost{10.0};
	std::vector<EndpointCostRule> costs;
	/// Longest wait for tokens before failing with ErrorCode::RateLimited. A
	/// request that costs more than its bucket holds fails with InvalidRequest,
	/// because Kalshi rejects it too; split large batches.
	std::chrono::milliseconds max_wait{std::chrono::seconds{5}};
	/// Independent balance calls per second; Basic and Advanced allow one.
	TokenBucket::Config balance{.capacity = 1.0, .refill_per_second = 1.0, .initial_tokens = {}};
	/// POST /portfolio/intra_exchange_instance_transfer has its own bucket, the
	/// same on every tier: 600 tokens per minute, holding 600.
	TokenBucket::Config transfer{
		.capacity = 600.0, .refill_per_second = 10.0, .initial_tokens = {}};
	/// POST /fcm/positions/transfers has its own bucket too. Kalshi publishes no
	/// size for it, so this default is the intra-account transfer bucket's. The
	/// X-RateLimit-Limit header on a transfer's response gives the real capacity.
	TokenBucket::Config fcm_transfer{
		.capacity = 600.0, .refill_per_second = 10.0, .initial_tokens = {}};
};

/// Transport decorator that paces requests to Kalshi's Read and Write budgets
/// so they are not rejected with 429.
///
/// Writes follow Kalshi's Write bucket: order, order-group, exit-trigger, RFQ,
/// and quote mutations, block-trade accepts, weather-point and concert
/// market creation, target balance allocation, and API usage upgrades. Other
/// requests use the Read budget, except GET /portfolio/balance, which has its
/// own one-token-per-call bucket, intra-account transfers, which use
/// `transfer`, and FCM position transfers, which use `fcm_transfer`. Batch
/// requests cost their per-item cost
/// times the number of items. Per-shard Write buckets are not modeled
/// separately, so pacing is conservative when orders target several shards.
///
/// Build a config from the account's limits with `rate_limit_config()` in
/// `kalshi/api.hpp`.
class RateLimitedTransport final : public HttpTransport {
public:
	RateLimitedTransport(std::shared_ptr<const HttpTransport> inner, RateLimitConfig config);

	[[nodiscard]] Result<HttpResponse> request(HttpMethod method, std::string_view path,
											   std::string_view body = {}) const override;

	/// Whether a request draws from the Write budget.
	[[nodiscard]] static bool is_write(HttpMethod method, std::string_view path) noexcept;

	/// Token cost this transport charges for a request.
	[[nodiscard]] double cost(HttpMethod method, std::string_view path,
							  std::string_view body) const;

private:
	std::shared_ptr<const HttpTransport> inner_;
	RateLimitConfig config_;
	mutable TokenBucket read_;
	mutable TokenBucket write_;
	mutable TokenBucket balance_;
	mutable TokenBucket transfer_;
	mutable TokenBucket fcm_transfer_;
};

} // namespace kalshi
