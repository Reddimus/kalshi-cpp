#include "kalshi/api.hpp"

#include <iostream>
#include <string>
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

// Test API helper functions

TEST(parse_side_yes) {
	ASSERT_EQ(kalshi::parse_side("yes"), kalshi::Side::Yes);
	ASSERT_EQ(kalshi::parse_side("Yes"), kalshi::Side::Yes);
}

TEST(parse_side_no) {
	ASSERT_EQ(kalshi::parse_side("no"), kalshi::Side::No);
	ASSERT_EQ(kalshi::parse_side("No"), kalshi::Side::No);
}

TEST(parse_action_buy) {
	ASSERT_EQ(kalshi::parse_action("buy"), kalshi::Action::Buy);
	ASSERT_EQ(kalshi::parse_action("Buy"), kalshi::Action::Buy);
}

TEST(parse_action_sell) {
	ASSERT_EQ(kalshi::parse_action("sell"), kalshi::Action::Sell);
	ASSERT_EQ(kalshi::parse_action("Sell"), kalshi::Action::Sell);
}

TEST(parse_market_status_open) {
	ASSERT_EQ(kalshi::parse_market_status("active"), kalshi::MarketStatus::Open);
	ASSERT_EQ(kalshi::parse_market_status("open"), kalshi::MarketStatus::Open);
	ASSERT_EQ(kalshi::parse_market_status("initialized"), kalshi::MarketStatus::Open);
}

TEST(parse_market_status_settled) {
	ASSERT_EQ(kalshi::parse_market_status("settled"), kalshi::MarketStatus::Settled);
	ASSERT_EQ(kalshi::parse_market_status("determined"), kalshi::MarketStatus::Settled);
}

TEST(parse_market_status_closed) {
	ASSERT_EQ(kalshi::parse_market_status("closed"), kalshi::MarketStatus::Closed);
}

TEST(parse_order_status_open) {
	ASSERT_EQ(kalshi::parse_order_status("open"), kalshi::OrderStatus::Open);
	ASSERT_EQ(kalshi::parse_order_status("resting"), kalshi::OrderStatus::Open);
}

TEST(parse_order_status_pending) {
	ASSERT_EQ(kalshi::parse_order_status("pending"), kalshi::OrderStatus::Pending);
}

TEST(parse_order_status_filled) {
	ASSERT_EQ(kalshi::parse_order_status("filled"), kalshi::OrderStatus::Filled);
	ASSERT_EQ(kalshi::parse_order_status("executed"), kalshi::OrderStatus::Filled);
}

TEST(parse_order_status_cancelled) {
	ASSERT_EQ(kalshi::parse_order_status("cancelled"), kalshi::OrderStatus::Cancelled);
	ASSERT_EQ(kalshi::parse_order_status("canceled"), kalshi::OrderStatus::Cancelled);
}

TEST(to_json_string_side) {
	ASSERT_EQ(kalshi::to_json_string(kalshi::Side::Yes), std::string_view("yes"));
	ASSERT_EQ(kalshi::to_json_string(kalshi::Side::No), std::string_view("no"));
}

TEST(to_json_string_action) {
	ASSERT_EQ(kalshi::to_json_string(kalshi::Action::Buy), std::string_view("buy"));
	ASSERT_EQ(kalshi::to_json_string(kalshi::Action::Sell), std::string_view("sell"));
}

// Test request parameter structures

TEST(get_markets_params_default) {
	kalshi::GetMarketsParams params;
	ASSERT_FALSE(params.limit.has_value());
	ASSERT_FALSE(params.cursor.has_value());
	ASSERT_FALSE(params.event_ticker.has_value());
}

TEST(create_order_params) {
	kalshi::CreateOrderParams params;
	params.ticker = "TEST-MARKET";
	params.side = kalshi::Side::Yes;
	params.action = kalshi::Action::Buy;
	params.type = "limit";
	params.count = 10;
	params.yes_price = 50;

	ASSERT_EQ(params.ticker, std::string("TEST-MARKET"));
	ASSERT_EQ(params.side, kalshi::Side::Yes);
	ASSERT_EQ(params.action, kalshi::Action::Buy);
	ASSERT_EQ(params.type, std::string("limit"));
	ASSERT_EQ(params.count, 10);
	ASSERT_TRUE(params.yes_price.has_value());
	ASSERT_EQ(*params.yes_price, 50);
}

TEST(amend_order_params) {
	kalshi::AmendOrderParams params;
	params.order_id = "order-123";
	params.count = 5;
	params.yes_price = 55;

	ASSERT_EQ(params.order_id, std::string("order-123"));
	ASSERT_TRUE(params.count.has_value());
	ASSERT_EQ(*params.count, 5);
	ASSERT_TRUE(params.yes_price.has_value());
	ASSERT_EQ(*params.yes_price, 55);
}

// Test response structures

TEST(event_default_construction) {
	kalshi::Event event;
	ASSERT_TRUE(event.event_ticker.empty());
	ASSERT_TRUE(event.title.empty());
}

TEST(balance_default_construction) {
	kalshi::Balance balance;
	ASSERT_EQ(balance.balance, 0);
	ASSERT_EQ(balance.available_balance, 0);
}

TEST(fill_default_construction) {
	kalshi::Fill fill;
	ASSERT_TRUE(fill.trade_id.empty());
	ASSERT_EQ(fill.count, 0);
	ASSERT_FALSE(fill.is_taker);
}

TEST(settlement_default_construction) {
	kalshi::Settlement settlement;
	ASSERT_TRUE(settlement.market_ticker.empty());
	ASSERT_EQ(settlement.yes_count, 0);
	ASSERT_EQ(settlement.no_count, 0);
}

TEST(candlestick_default_construction) {
	kalshi::Candlestick candle;
	ASSERT_EQ(candle.timestamp, 0);
	ASSERT_EQ(candle.open_price, 0);
	ASSERT_EQ(candle.volume, 0);
}

TEST(public_trade_default_construction) {
	kalshi::PublicTrade trade;
	ASSERT_TRUE(trade.trade_id.empty());
	ASSERT_EQ(trade.count, 0);
}

TEST(exchange_status_default_construction) {
	kalshi::ExchangeStatus status;
	ASSERT_FALSE(status.trading_active);
	ASSERT_FALSE(status.exchange_active);
}

TEST(series_default_construction) {
	kalshi::Series series;
	ASSERT_TRUE(series.ticker.empty());
	ASSERT_TRUE(series.title.empty());
}

TEST(market_default_timestamps) {
	// Verify Market struct has correct default values
	kalshi::Market market;
	ASSERT_EQ(market.open_time, 0);
	ASSERT_EQ(market.close_time, 0);
	ASSERT_FALSE(market.expiration_time.has_value());
	// Note: ISO 8601 datetime parsing is tested via integration tests
	// The extract_datetime function parses strings like "2021-08-11T00:00:00Z" to Unix timestamps
}
