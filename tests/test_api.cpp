#include "kalshi/api.hpp"

#include <gtest/gtest.h>
#include <string>

// --- Parse side ---

TEST(Api, ParseSideYes) {
	ASSERT_EQ(kalshi::parse_side("yes"), kalshi::Side::Yes);
	ASSERT_EQ(kalshi::parse_side("Yes"), kalshi::Side::Yes);
}

TEST(Api, ParseSideNo) {
	ASSERT_EQ(kalshi::parse_side("no"), kalshi::Side::No);
	ASSERT_EQ(kalshi::parse_side("No"), kalshi::Side::No);
}

// --- Parse action ---

TEST(Api, ParseActionBuy) {
	ASSERT_EQ(kalshi::parse_action("buy"), kalshi::Action::Buy);
	ASSERT_EQ(kalshi::parse_action("Buy"), kalshi::Action::Buy);
}

TEST(Api, ParseActionSell) {
	ASSERT_EQ(kalshi::parse_action("sell"), kalshi::Action::Sell);
	ASSERT_EQ(kalshi::parse_action("Sell"), kalshi::Action::Sell);
}

// --- Parse market status ---

TEST(Api, ParseMarketStatusOpen) {
	ASSERT_EQ(kalshi::parse_market_status("active"), kalshi::MarketStatus::Open);
	ASSERT_EQ(kalshi::parse_market_status("open"), kalshi::MarketStatus::Open);
	ASSERT_EQ(kalshi::parse_market_status("initialized"), kalshi::MarketStatus::Open);
}

TEST(Api, ParseMarketStatusSettled) {
	ASSERT_EQ(kalshi::parse_market_status("settled"), kalshi::MarketStatus::Settled);
	ASSERT_EQ(kalshi::parse_market_status("determined"), kalshi::MarketStatus::Settled);
}

TEST(Api, ParseMarketStatusClosed) {
	ASSERT_EQ(kalshi::parse_market_status("closed"), kalshi::MarketStatus::Closed);
}

// --- Parse order status ---

TEST(Api, ParseOrderStatusOpen) {
	ASSERT_EQ(kalshi::parse_order_status("open"), kalshi::OrderStatus::Open);
	ASSERT_EQ(kalshi::parse_order_status("resting"), kalshi::OrderStatus::Open);
}

TEST(Api, ParseOrderStatusPending) {
	ASSERT_EQ(kalshi::parse_order_status("pending"), kalshi::OrderStatus::Pending);
}

TEST(Api, ParseOrderStatusFilled) {
	ASSERT_EQ(kalshi::parse_order_status("filled"), kalshi::OrderStatus::Filled);
	ASSERT_EQ(kalshi::parse_order_status("executed"), kalshi::OrderStatus::Filled);
}

TEST(Api, ParseOrderStatusCancelled) {
	ASSERT_EQ(kalshi::parse_order_status("cancelled"), kalshi::OrderStatus::Cancelled);
	ASSERT_EQ(kalshi::parse_order_status("canceled"), kalshi::OrderStatus::Cancelled);
}

// --- Enum to JSON string ---

TEST(Api, ToJsonStringSide) {
	ASSERT_EQ(kalshi::to_json_string(kalshi::Side::Yes), std::string_view("yes"));
	ASSERT_EQ(kalshi::to_json_string(kalshi::Side::No), std::string_view("no"));
}

TEST(Api, ToJsonStringAction) {
	ASSERT_EQ(kalshi::to_json_string(kalshi::Action::Buy), std::string_view("buy"));
	ASSERT_EQ(kalshi::to_json_string(kalshi::Action::Sell), std::string_view("sell"));
}

// --- Request parameter structures ---

TEST(Api, GetMarketsParamsDefault) {
	kalshi::GetMarketsParams params;
	ASSERT_FALSE(params.limit.has_value());
	ASSERT_FALSE(params.cursor.has_value());
	ASSERT_FALSE(params.event_ticker.has_value());
}

TEST(Api, CreateOrderParams) {
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

TEST(Api, AmendOrderParams) {
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

// --- Response structures ---

TEST(Api, EventDefaultConstruction) {
	kalshi::Event event;
	ASSERT_TRUE(event.event_ticker.empty());
	ASSERT_TRUE(event.title.empty());
}

TEST(Api, BalanceDefaultConstruction) {
	kalshi::Balance balance;
	ASSERT_EQ(balance.balance, 0);
	ASSERT_EQ(balance.available_balance, 0);
}

TEST(Api, FillDefaultConstruction) {
	kalshi::Fill fill;
	ASSERT_TRUE(fill.trade_id.empty());
	ASSERT_EQ(fill.count, 0);
	ASSERT_FALSE(fill.is_taker);
}

TEST(Api, SettlementDefaultConstruction) {
	kalshi::Settlement settlement;
	ASSERT_TRUE(settlement.market_ticker.empty());
	ASSERT_EQ(settlement.yes_count, 0);
	ASSERT_EQ(settlement.no_count, 0);
}

TEST(Api, CandlestickDefaultConstruction) {
	kalshi::Candlestick candle;
	ASSERT_EQ(candle.timestamp, 0);
	ASSERT_EQ(candle.open_price, 0);
	ASSERT_EQ(candle.volume, 0);
}

TEST(Api, PublicTradeDefaultConstruction) {
	kalshi::PublicTrade trade;
	ASSERT_TRUE(trade.trade_id.empty());
	ASSERT_EQ(trade.count, 0);
}

TEST(Api, ExchangeStatusDefaultConstruction) {
	kalshi::ExchangeStatus status;
	ASSERT_FALSE(status.trading_active);
	ASSERT_FALSE(status.exchange_active);
}

TEST(Api, SeriesDefaultConstruction) {
	kalshi::Series series;
	ASSERT_TRUE(series.ticker.empty());
	ASSERT_TRUE(series.title.empty());
}

TEST(Api, MarketDefaultTimestamps) {
	kalshi::Market market;
	ASSERT_EQ(market.open_time, 0);
	ASSERT_EQ(market.close_time, 0);
	ASSERT_FALSE(market.expiration_time.has_value());
}
