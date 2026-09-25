// Transport decorators, token buckets, and pagination, tested against an
// in-memory transport with scripted responses.

#include "kalshi/api.hpp"
#include "kalshi/pagination.hpp"
#include "kalshi/rate_limit.hpp"
#include "kalshi/retry.hpp"

#include <chrono>
#include <deque>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

class ScriptedTransport final : public kalshi::HttpTransport {
public:
	mutable std::deque<kalshi::Result<kalshi::HttpResponse>> responses;
	mutable int calls{0};

	[[nodiscard]] kalshi::Result<kalshi::HttpResponse> request(kalshi::HttpMethod, std::string_view,
															   std::string_view) const override {
		++calls;
		if (responses.empty()) {
			return kalshi::HttpResponse{200, "{}", {}};
		}
		kalshi::Result<kalshi::HttpResponse> next = std::move(responses.front());
		responses.pop_front();
		return next;
	}
};

kalshi::HttpResponse status(int code) {
	return kalshi::HttpResponse{code, "{}", {}};
}

kalshi::RetryPolicy fast_policy() {
	kalshi::RetryPolicy policy;
	policy.initial_delay = 1ms;
	policy.max_delay = 5ms;
	policy.jitter_factor = 0.0;
	policy.max_attempts = 3;
	return policy;
}

} // namespace

TEST(Retry, DelayGrowsExponentiallyAndCaps) {
	kalshi::RetryPolicy policy;
	policy.initial_delay = 100ms;
	policy.max_delay = 250ms;
	policy.jitter_factor = 0.0;
	EXPECT_EQ(kalshi::retry_delay(1, policy), 100ms);
	EXPECT_EQ(kalshi::retry_delay(2, policy), 200ms);
	EXPECT_EQ(kalshi::retry_delay(3, policy), 250ms);
	EXPECT_EQ(kalshi::retry_delay(200, policy), 250ms);
}

TEST(Retry, JitterStaysWithinTheConfiguredSpread) {
	kalshi::RetryPolicy policy;
	policy.initial_delay = 1000ms;
	policy.max_delay = 5000ms;
	policy.jitter_factor = 0.1;
	for (int i = 0; i < 100; ++i) {
		const std::chrono::milliseconds delay = kalshi::retry_delay(1, policy);
		EXPECT_GE(delay, 900ms);
		EXPECT_LE(delay, 1100ms);
	}
}

TEST(Retry, PostRetriesOnlyWhenTheServerRejectedIt) {
	const kalshi::RetryPolicy policy;
	EXPECT_TRUE(kalshi::should_retry(kalshi::HttpMethod::POST, status(429), policy));
	EXPECT_FALSE(kalshi::should_retry(kalshi::HttpMethod::POST, status(503), policy));
	EXPECT_FALSE(
		kalshi::should_retry(kalshi::HttpMethod::POST, kalshi::Error::network("reset"), policy));
	EXPECT_TRUE(kalshi::should_retry(kalshi::HttpMethod::GET, status(503), policy));
	EXPECT_TRUE(
		kalshi::should_retry(kalshi::HttpMethod::DEL, kalshi::Error::network("reset"), policy));
	EXPECT_FALSE(kalshi::should_retry(kalshi::HttpMethod::GET, status(404), policy));

	kalshi::RetryPolicy unsafe = policy;
	unsafe.retry_non_idempotent = true;
	EXPECT_TRUE(kalshi::should_retry(kalshi::HttpMethod::POST, status(503), unsafe));
}

TEST(Retry, TransportRetriesTransientFailuresThenReturnsTheResult) {
	const std::shared_ptr<ScriptedTransport> inner = std::make_shared<ScriptedTransport>();
	inner->responses.emplace_back(std::unexpected(kalshi::Error::network("reset")));
	inner->responses.emplace_back(status(503));
	inner->responses.emplace_back(status(200));
	const kalshi::RetryingTransport transport(inner, fast_policy());

	const kalshi::Result<kalshi::HttpResponse> response = transport.get("/markets");

	ASSERT_TRUE(response.has_value());
	EXPECT_EQ(response->status_code, 200);
	EXPECT_EQ(inner->calls, 3);
}

TEST(Retry, TransportStopsAtMaxAttemptsAndNeverRepeatsAnAmbiguousPost) {
	const std::shared_ptr<ScriptedTransport> inner = std::make_shared<ScriptedTransport>();
	for (int i = 0; i < 5; ++i) {
		inner->responses.emplace_back(status(500));
	}
	const kalshi::RetryingTransport transport(inner, fast_policy());

	const kalshi::Result<kalshi::HttpResponse> get = transport.get("/markets");
	ASSERT_TRUE(get.has_value());
	EXPECT_EQ(get->status_code, 500);
	EXPECT_EQ(inner->calls, 3);

	inner->calls = 0;
	const kalshi::Result<kalshi::HttpResponse> post = transport.post("/portfolio/events/orders");
	ASSERT_TRUE(post.has_value());
	EXPECT_EQ(inner->calls, 1);
}

TEST(Retry, TransportWaitsAtLeastRetryAfter) {
	const std::shared_ptr<ScriptedTransport> inner = std::make_shared<ScriptedTransport>();
	inner->responses.emplace_back(kalshi::HttpResponse{429, "{}", {{"retry-after", "1"}}});
	inner->responses.emplace_back(status(200));
	const kalshi::RetryingTransport transport(inner, fast_policy());

	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const kalshi::Result<kalshi::HttpResponse> response = transport.get("/markets");

	ASSERT_TRUE(response.has_value());
	EXPECT_EQ(response->status_code, 200);
	EXPECT_GE(std::chrono::steady_clock::now() - start, 950ms);
}

TEST(TokenBucket, StartsFullAndSpendsFractionalCosts) {
	kalshi::TokenBucket bucket({.capacity = 20.0, .refill_per_second = 0.0, .initial_tokens = {}});
	EXPECT_DOUBLE_EQ(bucket.available(), 20.0);
	EXPECT_TRUE(bucket.try_acquire(10.0));
	EXPECT_TRUE(bucket.try_acquire(9.5));
	EXPECT_FALSE(bucket.try_acquire(1.0));
	EXPECT_EQ(bucket.wait_time(1.0), std::chrono::nanoseconds::max());
	bucket.reset();
	EXPECT_DOUBLE_EQ(bucket.available(), 20.0);
}

TEST(TokenBucket, RefillsContinuouslyWithoutLosingPartialTime) {
	// 1,000 tokens/s: a millisecond-resolution limiter could not express this.
	kalshi::TokenBucket bucket(
		{.capacity = 10.0, .refill_per_second = 1000.0, .initial_tokens = 0.0});
	EXPECT_FALSE(bucket.try_acquire(10.0));
	EXPECT_LE(bucket.wait_time(10.0), 10ms);
	EXPECT_TRUE(bucket.acquire_for(10.0, 200ms));
	std::this_thread::sleep_for(50ms);
	EXPECT_DOUBLE_EQ(bucket.available(), 10.0); // capped at capacity
}

TEST(TokenBucket, AcquireForGivesUpWhenTokensCannotArriveInTime) {
	kalshi::TokenBucket bucket(
		{.capacity = 100.0, .refill_per_second = 10.0, .initial_tokens = 0.0});
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	EXPECT_FALSE(bucket.acquire_for(50.0, 100ms)); // needs 5 s
	EXPECT_LT(std::chrono::steady_clock::now() - start, 50ms);
	EXPECT_FALSE(bucket.acquire_for(500.0, 10s)); // more than capacity
}

TEST(TokenBucket, ExtremeWaitsSaturateInsteadOfOverflowing) {
	kalshi::TokenBucket bucket(
		{.capacity = 10.0, .refill_per_second = 1e-15, .initial_tokens = 0.0});
	EXPECT_EQ(bucket.wait_time(10.0), std::chrono::nanoseconds::max());
	EXPECT_FALSE(bucket.acquire_for(10.0, std::chrono::nanoseconds::max()));
}

TEST(TokenBucket, RejectsNegativeAndNonFiniteConfiguration) {
	kalshi::TokenBucket bucket({.capacity = -5.0, .refill_per_second = -1.0, .initial_tokens = {}});
	EXPECT_DOUBLE_EQ(bucket.config().capacity, 0.0);
	EXPECT_TRUE(bucket.try_acquire(0.0));
	EXPECT_FALSE(bucket.try_acquire(1.0));
}

TEST(RateLimitedTransport, RoutesWritesAndChargesEndpointCosts) {
	EXPECT_TRUE(kalshi::RateLimitedTransport::is_write(kalshi::HttpMethod::POST,
													   "/portfolio/events/orders"));
	EXPECT_TRUE(kalshi::RateLimitedTransport::is_write(
		kalshi::HttpMethod::DEL, "/portfolio/events/orders/abc?subaccount=1"));
	EXPECT_TRUE(kalshi::RateLimitedTransport::is_write(kalshi::HttpMethod::PUT,
													   "/communications/quotes/q/accept"));
	EXPECT_FALSE(kalshi::RateLimitedTransport::is_write(kalshi::HttpMethod::GET,
														"/portfolio/events/orders"));
	EXPECT_FALSE(kalshi::RateLimitedTransport::is_write(kalshi::HttpMethod::POST, "/api_keys"));

	kalshi::RateLimitConfig config;
	config.costs = {
		{kalshi::HttpMethod::DEL, "/trade-api/v2/portfolio/events/orders/{order_id}", 2.0},
		{kalshi::HttpMethod::DEL, "/portfolio/events/orders/batched", 2.0}};
	const kalshi::RateLimitedTransport transport(std::make_shared<ScriptedTransport>(), config);

	EXPECT_DOUBLE_EQ(transport.cost(kalshi::HttpMethod::GET, "/markets?limit=5", ""), 10.0);
	EXPECT_DOUBLE_EQ(
		transport.cost(kalshi::HttpMethod::DEL, "/portfolio/events/orders/abc?subaccount=2", ""),
		2.0);
	EXPECT_DOUBLE_EQ(transport.cost(kalshi::HttpMethod::DEL, "/portfolio/events/orders/a/b", ""),
					 10.0);
	EXPECT_DOUBLE_EQ(
		transport.cost(kalshi::HttpMethod::DEL, "/portfolio/events/orders/batched",
					   R"({"orders":[{"order_id":"a"},{"order_id":"b"},{"order_id":"c"}]})"),
		6.0);
	EXPECT_DOUBLE_EQ(transport.cost(kalshi::HttpMethod::POST, "/portfolio/events/orders/batched",
									R"({"orders":[{"ticker":"A"},{"ticker":"B"}]})"),
					 20.0);
}

TEST(RateLimitedTransport, FailsFastInsteadOfWaitingPastMaxWait) {
	const std::shared_ptr<ScriptedTransport> inner = std::make_shared<ScriptedTransport>();
	kalshi::RateLimitConfig config;
	config.read = {.capacity = 10.0, .refill_per_second = 1.0, .initial_tokens = {}};
	config.max_wait = 50ms;
	const kalshi::RateLimitedTransport transport(inner, config);

	ASSERT_TRUE(transport.get("/markets").has_value());
	const kalshi::Result<kalshi::HttpResponse> limited = transport.get("/markets");

	ASSERT_FALSE(limited.has_value());
	EXPECT_EQ(limited.error().code, kalshi::ErrorCode::RateLimited);
	EXPECT_EQ(inner->calls, 1);
}

TEST(RateLimitedTransport, ConfigComesFromAccountLimits) {
	kalshi::AccountApiLimits limits;
	limits.read = {.refill_rate = 200, .bucket_capacity = 600};
	limits.write = {.refill_rate = 100, .bucket_capacity = 100};
	kalshi::EndpointCosts costs;
	costs.default_cost = 10;
	costs.endpoint_costs = {{"DELETE", "/trade-api/v2/portfolio/events/orders/batched", 2},
							{"PATCH", "/ignored", 5}};

	const kalshi::RateLimitConfig config = kalshi::rate_limit_config(limits, costs);

	EXPECT_DOUBLE_EQ(config.read.capacity, 600.0);
	EXPECT_DOUBLE_EQ(config.read.refill_per_second, 200.0);
	EXPECT_DOUBLE_EQ(config.write.capacity, 100.0);
	ASSERT_EQ(config.costs.size(), 1U);
	EXPECT_EQ(config.costs[0].method, kalshi::HttpMethod::DEL);
	EXPECT_DOUBLE_EQ(config.costs[0].cost, 2.0);
}

TEST(Pagination, IteratorFollowsCursorsUntilExhausted) {
	std::vector<std::string> cursors_seen;
	kalshi::PaginatedIterator<int> pages(
		[&cursors_seen](const kalshi::PaginationParams& params)
			-> kalshi::Result<kalshi::PaginatedResponse<int>> {
			const std::string cursor = params.cursor ? params.cursor->value : "";
			cursors_seen.push_back(cursor);
			if (cursor.empty()) {
				return kalshi::PaginatedResponse<int>{{1, 2}, kalshi::Cursor{"page-2"}};
			}
			return kalshi::PaginatedResponse<int>{{3}, std::nullopt};
		},
		2);

	const kalshi::Result<std::vector<int>> all = pages.fetch_all();

	ASSERT_TRUE(all.has_value());
	EXPECT_EQ(*all, (std::vector<int>{1, 2, 3}));
	EXPECT_EQ(cursors_seen, (std::vector<std::string>{"", "page-2"}));
	EXPECT_FALSE(pages.has_more());
}
