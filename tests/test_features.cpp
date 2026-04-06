#include "kalshi/pagination.hpp"
#include "kalshi/rate_limit.hpp"
#include "kalshi/retry.hpp"
#include "kalshi/websocket.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <string>

// --- Pagination tests ---

TEST(Pagination, CursorEmpty) {
	kalshi::Cursor cursor;
	ASSERT_TRUE(cursor.empty());

	cursor.value = "abc123";
	ASSERT_FALSE(cursor.empty());
}

TEST(Pagination, ParamsBuildQuery) {
	kalshi::PaginationParams params;
	params.limit = 50;

	std::string query = kalshi::build_paginated_query("/markets", params);
	ASSERT_EQ(query, std::string("/markets?limit=50"));
}

TEST(Pagination, ParamsWithCursor) {
	kalshi::PaginationParams params;
	params.limit = 25;
	params.cursor = kalshi::Cursor{"cursor_token"};

	std::string query = kalshi::build_paginated_query("/orders", params);
	ASSERT_EQ(query, std::string("/orders?limit=25&cursor=cursor_token"));
}

TEST(Pagination, ResponseHasMore) {
	kalshi::PaginatedResponse<int> response;
	response.items = {1, 2, 3};
	response.next_cursor = std::nullopt;
	ASSERT_FALSE(response.has_more());

	response.next_cursor = kalshi::Cursor{"next"};
	ASSERT_TRUE(response.has_more());
}

// --- Rate limiter tests ---

TEST(RateLimiter, InitialTokens) {
	kalshi::RateLimiter::Config config;
	config.initial_tokens = 5;
	config.max_tokens = 10;

	kalshi::RateLimiter limiter(config);
	ASSERT_EQ(limiter.available_tokens(), static_cast<std::uint16_t>(5));
}

TEST(RateLimiter, TryAcquire) {
	kalshi::RateLimiter::Config config;
	config.initial_tokens = 2;
	config.max_tokens = 2;

	kalshi::RateLimiter limiter(config);
	ASSERT_TRUE(limiter.try_acquire());
	ASSERT_TRUE(limiter.try_acquire());
	ASSERT_FALSE(limiter.try_acquire());
}

TEST(RateLimiter, Reset) {
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

// --- Retry policy tests ---

TEST(Retry, CalculateDelayFirstAttempt) {
	kalshi::RetryPolicy policy;
	policy.initial_delay = std::chrono::milliseconds(100);
	policy.backoff_multiplier = 2.0;
	policy.jitter_factor = 0.0;

	std::chrono::milliseconds delay = kalshi::calculate_retry_delay(1, policy);
	ASSERT_EQ(delay.count(), 100);
}

TEST(Retry, CalculateDelayExponential) {
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

TEST(Retry, ShouldRetryRateLimit) {
	kalshi::RetryPolicy policy;
	policy.retry_on_rate_limit = true;

	kalshi::HttpResponse response;
	response.status_code = 429;

	ASSERT_TRUE(kalshi::should_retry(response, policy));
}

TEST(Retry, ShouldRetryServerError) {
	kalshi::RetryPolicy policy;
	policy.retry_on_server_error = true;

	kalshi::HttpResponse response;
	response.status_code = 503;

	ASSERT_TRUE(kalshi::should_retry(response, policy));
}

TEST(Retry, ShouldNotRetryClientError) {
	kalshi::RetryPolicy policy;

	kalshi::HttpResponse response;
	response.status_code = 400;

	ASSERT_FALSE(kalshi::should_retry(response, policy));
}

// --- WebSocket tests ---

TEST(WebSocket, ChannelToString) {
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::OrderbookDelta),
			  std::string_view("orderbook_delta"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::Trade), std::string_view("trade"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::Fill), std::string_view("fill"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::MarketLifecycle),
			  std::string_view("market_lifecycle"));
}

TEST(WebSocket, OrderbookSnapshotDefault) {
	kalshi::OrderbookSnapshot snap;
	ASSERT_EQ(snap.sid, 0);
	ASSERT_EQ(snap.seq, 0);
	ASSERT_TRUE(snap.market_ticker.empty());
	ASSERT_TRUE(snap.yes.empty());
	ASSERT_TRUE(snap.no.empty());
}

TEST(WebSocket, OrderbookDeltaDefault) {
	kalshi::OrderbookDelta delta;
	ASSERT_EQ(delta.sid, 0);
	ASSERT_EQ(delta.seq, 0);
	ASSERT_TRUE(delta.market_ticker.empty());
	ASSERT_EQ(delta.price, 0);
	ASSERT_EQ(delta.delta, 0);
}

TEST(WebSocket, TradeDefault) {
	kalshi::WsTrade trade;
	ASSERT_EQ(trade.sid, 0);
	ASSERT_TRUE(trade.trade_id.empty());
	ASSERT_TRUE(trade.market_ticker.empty());
	ASSERT_EQ(trade.yes_price, 0);
	ASSERT_EQ(trade.count, 0);
}

TEST(WebSocket, SubscriptionId) {
	kalshi::SubscriptionId sub;
	sub.sid = 42;
	sub.channel = kalshi::Channel::OrderbookDelta;
	ASSERT_EQ(sub.sid, 42);
	ASSERT_EQ(sub.channel, kalshi::Channel::OrderbookDelta);
}

TEST(WebSocket, WsErrorDefault) {
	kalshi::WsError err;
	ASSERT_EQ(err.code, 0);
	ASSERT_TRUE(err.message.empty());
}

TEST(WebSocket, WsConfigDefaults) {
	kalshi::WsConfig config;
	ASSERT_EQ(config.url, std::string("wss://api.elections.kalshi.com/trade-api/ws/v2"));
	ASSERT_TRUE(config.auto_reconnect);
	ASSERT_EQ(config.max_reconnect_attempts, 10);
}
