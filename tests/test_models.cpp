#include "kalshi/models/market.hpp"
#include "kalshi/models/order.hpp"

#include <gtest/gtest.h>
#include <string>

TEST(Models, MarketDefaultConstruction) {
	kalshi::Market market;
	ASSERT_TRUE(market.ticker.empty());
	ASSERT_EQ(market.status, kalshi::MarketStatus::Open);
	ASSERT_EQ(market.volume, 0);
}

TEST(Models, OrderbookEntryConstruction) {
	kalshi::OrderBookEntry entry{50, 100};
	ASSERT_EQ(entry.price_cents, 50);
	ASSERT_EQ(entry.quantity, 100);
}

TEST(Models, PositionDefaultConstruction) {
	kalshi::Position pos;
	ASSERT_TRUE(pos.market_ticker.empty());
	ASSERT_EQ(pos.yes_contracts, 0);
	ASSERT_EQ(pos.no_contracts, 0);
}

TEST(Models, OrderRequestConstruction) {
	kalshi::OrderRequest req;
	req.market_ticker = "TEST-MARKET";
	req.side = kalshi::Side::Yes;
	req.action = kalshi::Action::Buy;
	req.type = kalshi::OrderType::Limit;
	req.count = 10;
	req.price = 50;

	ASSERT_EQ(req.market_ticker, std::string("TEST-MARKET"));
	ASSERT_EQ(req.side, kalshi::Side::Yes);
	ASSERT_EQ(req.action, kalshi::Action::Buy);
	ASSERT_EQ(req.count, 10);
	ASSERT_TRUE(req.price.has_value());
	ASSERT_EQ(*req.price, 50);
}

TEST(Models, OrderDefaultConstruction) {
	kalshi::Order order;
	ASSERT_TRUE(order.order_id.empty());
	ASSERT_EQ(order.status, kalshi::OrderStatus::Pending);
	ASSERT_EQ(order.initial_count, 0);
}

TEST(Models, TradeDefaultConstruction) {
	kalshi::Trade trade;
	ASSERT_TRUE(trade.trade_id.empty());
	ASSERT_EQ(trade.count, 0);
	ASSERT_FALSE(trade.is_taker);
}

TEST(Models, SideEnumValues) {
	ASSERT_NE(kalshi::Side::Yes, kalshi::Side::No);
}

TEST(Models, ActionEnumValues) {
	ASSERT_NE(kalshi::Action::Buy, kalshi::Action::Sell);
}
