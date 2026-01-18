#include "kalshi/pagination.hpp"
#include "kalshi/rate_limit.hpp"
#include "kalshi/retry.hpp"
#include "kalshi/websocket.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Test declarations from test_main.cpp
struct TestResult {
	std::string name;
	bool passed;
	std::string message;
};
extern std::vector<TestResult> g_results;

#define TEST(name)                                             \
	void test_##name();                                        \
	struct TestRegister_##name {                               \
		TestRegister_##name() {                                \
			try {                                              \
				test_##name();                                 \
				g_results.push_back({#name, true, ""});        \
			} catch (const std::exception& e) {                \
				g_results.push_back({#name, false, e.what()}); \
			}                                                  \
		}                                                      \
	} g_register_##name;                                       \
	void test_##name()

#define ASSERT_TRUE(expr) \
	if (!(expr))          \
	throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_FALSE(expr) \
	if (expr)              \
	throw std::runtime_error("Assertion failed: NOT " #expr)

#define ASSERT_EQ(a, b) \
	if ((a) != (b))     \
	throw std::runtime_error("Assertion failed: " #a " == " #b)

// Pagination tests
TEST(pagination_cursor_empty) {
	kalshi::Cursor cursor;
	ASSERT_TRUE(cursor.empty());

	cursor.value = "abc123";
	ASSERT_FALSE(cursor.empty());
}

TEST(pagination_params_build_query) {
	kalshi::PaginationParams params;
	params.limit = 50;

	std::string query = kalshi::build_paginated_query("/markets", params);
	ASSERT_EQ(query, std::string("/markets?limit=50"));
}

TEST(pagination_params_with_cursor) {
	kalshi::PaginationParams params;
	params.limit = 25;
	params.cursor = kalshi::Cursor{"cursor_token"};

	std::string query = kalshi::build_paginated_query("/orders", params);
	ASSERT_EQ(query, std::string("/orders?limit=25&cursor=cursor_token"));
}

TEST(paginated_response_has_more) {
	kalshi::PaginatedResponse<int> response;
	response.items = {1, 2, 3};
	response.next_cursor = std::nullopt;
	ASSERT_FALSE(response.has_more());

	response.next_cursor = kalshi::Cursor{"next"};
	ASSERT_TRUE(response.has_more());
}

// Rate limiter tests
TEST(rate_limiter_initial_tokens) {
	kalshi::RateLimiter::Config config;
	config.initial_tokens = 5;
	config.max_tokens = 10;

	kalshi::RateLimiter limiter(config);
	ASSERT_EQ(limiter.available_tokens(), static_cast<std::uint16_t>(5));
}

TEST(rate_limiter_try_acquire) {
	kalshi::RateLimiter::Config config;
	config.initial_tokens = 2;
	config.max_tokens = 2;

	kalshi::RateLimiter limiter(config);
	ASSERT_TRUE(limiter.try_acquire());
	ASSERT_TRUE(limiter.try_acquire());
	ASSERT_FALSE(limiter.try_acquire());
}

TEST(rate_limiter_reset) {
	kalshi::RateLimiter::Config config;
	config.initial_tokens = 3;
	config.max_tokens = 3;

	kalshi::RateLimiter limiter(config);
	(void)limiter.try_acquire();
	(void)limiter.try_acquire();
	ASSERT_EQ(limiter.available_tokens(), static_cast<std::uint16_t>(1));

	limiter.reset();
	ASSERT_EQ(limiter.available_tokens(), static_cast<std::uint16_t>(3));
}

// Retry policy tests
TEST(retry_calculate_delay_first_attempt) {
	kalshi::RetryPolicy policy;
	policy.initial_delay = std::chrono::milliseconds(100);
	policy.backoff_multiplier = 2.0;
	policy.jitter_factor = 0.0; // No jitter for predictable testing

	std::chrono::milliseconds delay = kalshi::calculate_retry_delay(1, policy);
	ASSERT_EQ(delay.count(), 100);
}

TEST(retry_calculate_delay_exponential) {
	kalshi::RetryPolicy policy;
	policy.initial_delay = std::chrono::milliseconds(100);
	policy.backoff_multiplier = 2.0;
	policy.jitter_factor = 0.0;
	policy.max_delay = std::chrono::milliseconds(10000);

	std::chrono::milliseconds delay1 = kalshi::calculate_retry_delay(1, policy);
	std::chrono::milliseconds delay2 = kalshi::calculate_retry_delay(2, policy);
	std::chrono::milliseconds delay3 = kalshi::calculate_retry_delay(3, policy);

	ASSERT_EQ(delay1.count(), 100);
	ASSERT_EQ(delay2.count(), 200);
	ASSERT_EQ(delay3.count(), 400);
}

TEST(retry_should_retry_rate_limit) {
	kalshi::RetryPolicy policy;
	policy.retry_on_rate_limit = true;

	kalshi::HttpResponse response;
	response.status_code = 429;

	ASSERT_TRUE(kalshi::should_retry(response, policy));
}

TEST(retry_should_retry_server_error) {
	kalshi::RetryPolicy policy;
	policy.retry_on_server_error = true;

	kalshi::HttpResponse response;
	response.status_code = 503;

	ASSERT_TRUE(kalshi::should_retry(response, policy));
}

TEST(retry_should_not_retry_client_error) {
	kalshi::RetryPolicy policy;

	kalshi::HttpResponse response;
	response.status_code = 400;

	ASSERT_FALSE(kalshi::should_retry(response, policy));
}

// WebSocket tests
TEST(websocket_channel_to_string) {
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::OrderbookDelta),
			  std::string_view("orderbook_delta"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::Trade), std::string_view("trade"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::Fill), std::string_view("fill"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::MarketLifecycle),
			  std::string_view("market_lifecycle"));
}
