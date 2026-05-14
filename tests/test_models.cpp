#include "kalshi/api.hpp"
#include "kalshi/models/market.hpp"
#include "kalshi/models/order.hpp"
#include "kalshi/websocket.hpp"

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

TEST(Models, DeriveOutcomeSide) {
	// buy-yes and sell-no both expose to YES
	ASSERT_EQ(kalshi::derive_outcome_side(kalshi::Side::Yes, kalshi::Action::Buy),
			  kalshi::OutcomeSide::Yes);
	ASSERT_EQ(kalshi::derive_outcome_side(kalshi::Side::No, kalshi::Action::Sell),
			  kalshi::OutcomeSide::Yes);
	// buy-no and sell-yes both expose to NO
	ASSERT_EQ(kalshi::derive_outcome_side(kalshi::Side::No, kalshi::Action::Buy),
			  kalshi::OutcomeSide::No);
	ASSERT_EQ(kalshi::derive_outcome_side(kalshi::Side::Yes, kalshi::Action::Sell),
			  kalshi::OutcomeSide::No);
}

TEST(Models, DeriveBookSide) {
	// Bid ≡ OutcomeSide::Yes
	ASSERT_EQ(kalshi::derive_book_side(kalshi::Side::Yes, kalshi::Action::Buy),
			  kalshi::BookSide::Bid);
	ASSERT_EQ(kalshi::derive_book_side(kalshi::Side::No, kalshi::Action::Sell),
			  kalshi::BookSide::Bid);
	// Ask ≡ OutcomeSide::No
	ASSERT_EQ(kalshi::derive_book_side(kalshi::Side::No, kalshi::Action::Buy),
			  kalshi::BookSide::Ask);
	ASSERT_EQ(kalshi::derive_book_side(kalshi::Side::Yes, kalshi::Action::Sell),
			  kalshi::BookSide::Ask);
}

TEST(Models, OutcomeSideAndBookSideEnumValues) {
	ASSERT_NE(kalshi::OutcomeSide::Yes, kalshi::OutcomeSide::No);
	ASSERT_NE(kalshi::BookSide::Bid, kalshi::BookSide::Ask);
}

TEST(Models, GetQuotesParamsDefaultsAndRfqUserFilter) {
	// 2026-05-07 upstream: optional `rfq_user_filter` on GET /quotes lets
	// callers restrict the response to quotes that responded to RFQs the
	// authenticated user created. Verify the field defaults to unset and
	// can hold a string when set.
	kalshi::GetQuotesParams params;
	ASSERT_FALSE(params.limit.has_value());
	ASSERT_FALSE(params.cursor.has_value());
	ASSERT_FALSE(params.rfq_id.has_value());
	ASSERT_FALSE(params.status.has_value());
	ASSERT_FALSE(params.rfq_user_filter.has_value());

	params.rfq_user_filter = "true";
	ASSERT_TRUE(params.rfq_user_filter.has_value());
	ASSERT_EQ(*params.rfq_user_filter, "true");
}

TEST(Models, CreateQuoteParamsDefaultsAndPostOnly) {
	// 2026-05-05 upstream: optional `post_only` on CreateQuoteParams.
	kalshi::CreateQuoteParams params;
	ASSERT_TRUE(params.rfq_id.empty());
	ASSERT_EQ(params.price, 0);
	ASSERT_EQ(params.count, 0);
	ASSERT_FALSE(params.expires_at.has_value());
	ASSERT_FALSE(params.post_only.has_value());

	params.post_only = true;
	ASSERT_TRUE(params.post_only.has_value());
	ASSERT_TRUE(*params.post_only);
}

TEST(Models, WsErrorCodeName) {
	// 1-22 are the original AsyncAPI codes; 25 was added 2026-05-12
	// for subscription buffer overflow. Spot-check the boundaries plus
	// the new addition and the unknown-fallback.
	EXPECT_EQ(kalshi::ws_error_code_name(1), "Unable to process message");
	EXPECT_EQ(kalshi::ws_error_code_name(9), "Authentication required");
	EXPECT_EQ(kalshi::ws_error_code_name(22), "shard_factor limit");
	EXPECT_EQ(kalshi::ws_error_code_name(25), "Subscription buffer overflow");
	// Currently-undefined upstream codes fall through.
	EXPECT_EQ(kalshi::ws_error_code_name(0), "Unknown error code");
	EXPECT_EQ(kalshi::ws_error_code_name(23), "Unknown error code");
	EXPECT_EQ(kalshi::ws_error_code_name(99), "Unknown error code");
}

TEST(Models, MarketLifecycleDefaultsAndYesSubTitle) {
	// 2026-05-11 upstream: the v2 market_lifecycle `metadata_updated`
	// sub-event now carries `yes_sub_title` when the yes-side subtitle
	// changes. The field is optional — most lifecycle frames (open / close
	// / determination / settlement) omit it.
	kalshi::MarketLifecycle lc;
	ASSERT_EQ(lc.sid, 0);
	ASSERT_TRUE(lc.market_ticker.empty());
	ASSERT_EQ(lc.open_ts, 0);
	ASSERT_EQ(lc.close_ts, 0);
	ASSERT_FALSE(lc.determination_ts.has_value());
	ASSERT_FALSE(lc.settled_ts.has_value());
	ASSERT_FALSE(lc.result.has_value());
	ASSERT_FALSE(lc.is_deactivated);
	ASSERT_FALSE(lc.yes_sub_title.has_value());

	lc.yes_sub_title = "above 65°F";
	ASSERT_TRUE(lc.yes_sub_title.has_value());
	ASSERT_EQ(*lc.yes_sub_title, "above 65°F");
}
