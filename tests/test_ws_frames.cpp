// Frame parsing beyond the spec's examples: control frames, message types that
// share a `type`, and values the spec does not promise.

#include "kalshi/websocket.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "frames.hpp"

namespace {

template <typename T>
const T* as(const kalshi::detail::Frame& frame) {
	const kalshi::WsMessage* message = std::get_if<kalshi::WsMessage>(&frame);
	return message == nullptr ? nullptr : std::get_if<T>(message);
}

} // namespace

TEST(WsFrames, ControlFramesParse) {
	const kalshi::detail::Frame subscribed = kalshi::detail::parse_frame(
		R"({"id":1,"type":"subscribed","msg":{"channel":"orderbook_delta","sid":7}})");
	const kalshi::detail::SubscribedFrame* ack =
		std::get_if<kalshi::detail::SubscribedFrame>(&subscribed);
	ASSERT_NE(ack, nullptr);
	EXPECT_EQ(ack->id, 1);
	EXPECT_EQ(ack->channel, "orderbook_delta");
	EXPECT_EQ(ack->sid, 7);

	const kalshi::detail::Frame unsubscribed =
		kalshi::detail::parse_frame(R"({"id":102,"sid":2,"seq":7,"type":"unsubscribed"})");
	ASSERT_TRUE(std::holds_alternative<kalshi::detail::UnsubscribedFrame>(unsubscribed));
	EXPECT_EQ(std::get<kalshi::detail::UnsubscribedFrame>(unsubscribed).sid, 2);

	const kalshi::detail::Frame error = kalshi::detail::parse_frame(
		R"({"id":123,"type":"error","msg":{"code":7,"msg":"Unknown subscription ID"}})");
	const kalshi::detail::ErrorFrame* failure = std::get_if<kalshi::detail::ErrorFrame>(&error);
	ASSERT_NE(failure, nullptr);
	EXPECT_EQ(failure->code, 7);
	EXPECT_EQ(failure->message, "Unknown subscription ID");
}

TEST(WsFrames, OkRepliesKeepTheirPayloadForTheCommandThatAskedForIt) {
	const kalshi::detail::Frame update = kalshi::detail::parse_frame(
		R"({"id":123,"sid":456,"seq":222,"type":"ok","msg":{"market_tickers":["A","B"]}})");
	const kalshi::detail::OkFrame* ok = std::get_if<kalshi::detail::OkFrame>(&update);
	ASSERT_NE(ok, nullptr);
	EXPECT_EQ(kalshi::detail::parse_updated(ok->msg).market_tickers,
			  (std::vector<std::string>{"A", "B"}));

	const kalshi::detail::Frame list = kalshi::detail::parse_frame(
		R"({"id":3,"type":"ok","msg":[{"channel":"orderbook_delta","sid":1},{"channel":"ticker","sid":2}]})");
	const std::vector<kalshi::ws::ListedSubscription> subscriptions =
		kalshi::detail::parse_subscription_list(std::get<kalshi::detail::OkFrame>(list).msg);
	ASSERT_EQ(subscriptions.size(), 2U);
	EXPECT_EQ(subscriptions[1].channel, "ticker");
	EXPECT_EQ(subscriptions[1].sid, 2);
}

TEST(WsFrames, UnknownTypesAreIgnoredAndMalformedOnesFlagged) {
	EXPECT_TRUE(std::holds_alternative<std::monostate>(
		kalshi::detail::parse_frame(R"({"type":"brand_new","sid":1,"msg":{}})")));
	EXPECT_TRUE(std::holds_alternative<std::monostate>(kalshi::detail::parse_frame("not json")));
	EXPECT_TRUE(std::holds_alternative<std::monostate>(kalshi::detail::parse_frame("")));
	const kalshi::detail::Frame malformed =
		kalshi::detail::parse_frame(R"({"type":"trade","sid":"not a number"})");
	ASSERT_TRUE(std::holds_alternative<kalshi::detail::MalformedFrame>(malformed));
	EXPECT_EQ(std::get<kalshi::detail::MalformedFrame>(malformed).type, "trade");
}

TEST(WsFrames, LifecycleFramesAreToldApartByEventType) {
	const kalshi::detail::Frame metadata = kalshi::detail::parse_frame(
		R"({"type":"market_lifecycle_v2","sid":1,"seq":4,"msg":{"event_type":"metadata_updated","market_ticker":"KXA","floor_strike":70.5,"yes_sub_title":"70.5 or above"}})");
	const kalshi::ws::Update<kalshi::ws::MarketMetadataUpdated>* updated =
		as<kalshi::ws::Update<kalshi::ws::MarketMetadataUpdated>>(metadata);
	ASSERT_NE(updated, nullptr);
	EXPECT_EQ(updated->msg.floor_strike, 70.5);
	EXPECT_EQ(updated->msg.yes_sub_title, "70.5 or above");

	const kalshi::detail::Frame settled = kalshi::detail::parse_frame(
		R"({"type":"market_lifecycle_v2","sid":1,"seq":5,"msg":{"event_type":"settled","market_ticker":"KXA","settled_ts":1700000000,"result":"yes"}})");
	const kalshi::ws::Update<kalshi::ws::MarketLifecycleV2>* lifecycle =
		as<kalshi::ws::Update<kalshi::ws::MarketLifecycleV2>>(settled);
	ASSERT_NE(lifecycle, nullptr);
	EXPECT_EQ(lifecycle->msg.event_type, kalshi::ws::MarketLifecycleV2EventType::Settled);
	EXPECT_EQ(lifecycle->seq, 5);
}

TEST(WsFrames, EnumValuesTheSpecAddsLaterReadAsUnknown) {
	const kalshi::detail::Frame fill = kalshi::detail::parse_frame(
		R"({"type":"fill","sid":13,"msg":{"trade_id":"t","side":"maybe","action":"sell_to_close","book_side":"ask"}})");
	const kalshi::ws::Update<kalshi::ws::Fill>* update =
		as<kalshi::ws::Update<kalshi::ws::Fill>>(fill);
	ASSERT_NE(update, nullptr);
	EXPECT_EQ(update->msg.side, kalshi::Side::Unknown);
	EXPECT_EQ(update->msg.action, kalshi::ws::OrderAction::SellToClose);
	EXPECT_EQ(update->msg.book_side, kalshi::BookSide::Ask);

	const kalshi::detail::Frame order = kalshi::detail::parse_frame(
		R"({"type":"user_order","sid":22,"msg":{"order_id":"o","status":"unknown"}})");
	const kalshi::ws::Update<kalshi::ws::UserOrder>* user_order =
		as<kalshi::ws::Update<kalshi::ws::UserOrder>>(order);
	ASSERT_NE(user_order, nullptr);
	EXPECT_EQ(user_order->msg.status, kalshi::OrderStatus::Unknown);
}

TEST(WsFrames, NullsForNonNullableFieldsReadAsEmpty) {
	const kalshi::detail::Frame trade = kalshi::detail::parse_frame(
		R"({"type":"trade","sid":11,"seq":2,"msg":{"trade_id":null,"market_ticker":"KXA","count_fp":"1.00"}})");
	const kalshi::ws::Update<kalshi::ws::Trade>* update =
		as<kalshi::ws::Update<kalshi::ws::Trade>>(trade);
	ASSERT_NE(update, nullptr);
	EXPECT_TRUE(update->msg.trade_id.empty());
	EXPECT_EQ(update->msg.market_ticker, "KXA");
}

TEST(WsFrames, OrderbookLevelsArePriceAndCountPairs) {
	const kalshi::detail::Frame snapshot = kalshi::detail::parse_frame(
		R"({"type":"orderbook_snapshot","sid":2,"seq":1,"msg":{"market_ticker":"KXA","market_id":"m","yes_dollars_fp":[["0.4500","10.00"],["0.4400","3.00"]]}})");
	const kalshi::ws::Update<kalshi::ws::OrderbookSnapshot>* update =
		as<kalshi::ws::Update<kalshi::ws::OrderbookSnapshot>>(snapshot);
	ASSERT_NE(update, nullptr);
	ASSERT_TRUE(update->msg.yes_dollars_fp.has_value());
	ASSERT_EQ(update->msg.yes_dollars_fp->size(), 2U);
	EXPECT_EQ((*update->msg.yes_dollars_fp)[0][0], "0.4500");
	EXPECT_EQ((*update->msg.yes_dollars_fp)[1][1], "3.00");
	EXPECT_FALSE(update->msg.no_dollars_fp.has_value());
}
