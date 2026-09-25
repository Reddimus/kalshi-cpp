// The subscription registry: command frames, commands that wait for the
// server's confirmation, and resubscribing after a reconnect.

#include "kalshi/websocket.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "subscriptions.hpp"

namespace {

using kalshi::detail::Reaction;
using kalshi::detail::SubscriptionRegistry;
using kalshi::ws::Channel;

kalshi::WsMessage delta(std::int64_t sid, std::int64_t seq) {
	kalshi::ws::Update<kalshi::ws::OrderbookDelta> update;
	update.sid = sid;
	update.seq = seq;
	return update;
}

} // namespace

TEST(WsSubscriptions, CommandsKeepKalshisKeyOrderAndOmitUnsetOptions) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta,
						   {.market_tickers = {"A", "B"}, .use_yes_price = true}, true, frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_EQ(
		frames[0],
		R"({"id":2,"cmd":"subscribe","params":{"channels":["orderbook_delta"],"market_tickers":["A","B"],"use_yes_price":true}})");

	frames.clear();
	registry.on_subscribed({2, "orderbook_delta", 9});
	ASSERT_TRUE(registry
					.update(book,
							{.market_ticker = "C", .action = kalshi::ws::UpdateAction::AddMarkets},
							frames)
					.has_value());
	ASSERT_TRUE(registry.unsubscribe(book, frames).has_value());
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":3,"cmd":"update_subscription","params":{"sids":[9],"market_ticker":"C","action":"add_markets"}})",
			R"({"id":4,"cmd":"unsubscribe","params":{"sids":[9]}})"}));
}

TEST(WsSubscriptions, NothingIsSentUntilConnected) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker = registry.subscribe(Channel::Ticker, {}, false, frames);
	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					frames)
			.has_value());
	EXPECT_TRUE(frames.empty());

	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(R"("channels":["ticker"],"market_tickers":["A"])"), std::string::npos)
		<< frames[0];
}

TEST(WsSubscriptions, CommandsBeforeTheAckWaitForTheServersSid) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames);
	frames.clear();
	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					frames)
			.has_value());
	EXPECT_TRUE(frames.empty());

	const Reaction ack = registry.on_subscribed({2, "orderbook_delta", 55});
	ASSERT_EQ(ack.frames.size(), 1U);
	EXPECT_NE(ack.frames[0].find(R"("sids":[55])"), std::string::npos) << ack.frames[0];
	ASSERT_TRUE(ack.message.has_value());
	EXPECT_EQ(std::get<kalshi::ws::Subscribed>(*ack.message).subscription, book);
}

TEST(WsSubscriptions, AnUnsubscribeBeforeTheAckIsSentAfterIt) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription trades = registry.subscribe(Channel::Trade, {}, true, frames);
	frames.clear();
	ASSERT_TRUE(registry.unsubscribe(trades, frames).has_value());
	EXPECT_TRUE(frames.empty());
	EXPECT_TRUE(registry.subscriptions().empty());

	const Reaction ack = registry.on_subscribed({2, "trade", 7});
	EXPECT_FALSE(ack.message.has_value());
	ASSERT_EQ(ack.frames.size(), 1U);
	EXPECT_EQ(ack.frames[0], R"({"id":3,"cmd":"unsubscribe","params":{"sids":[7]}})");

	const Reaction done = registry.on_unsubscribed({3, 7});
	ASSERT_TRUE(done.message.has_value());
	EXPECT_EQ(std::get<kalshi::ws::Unsubscribed>(*done.message).subscription, trades);
}

TEST(WsSubscriptions, ReconnectingResubscribesWithTheCurrentMarkets) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_tickers = {"A"}}, true, frames);
	registry.on_subscribed({2, "orderbook_delta", 5});
	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					frames)
			.has_value());
	ASSERT_TRUE(
		registry
			.update(book,
					{.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::DeleteMarkets},
					frames)
			.has_value());
	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"C"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					frames)
			.has_value());

	registry.on_disconnected();
	kalshi::WsMessage stale = delta(5, 1);
	registry.on_data(stale, true);
	EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(stale).subscription, 0);

	frames.clear();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(R"("market_tickers":["B","C"])"), std::string::npos) << frames[0];

	const std::int64_t id = std::stoll(frames[0].substr(6));
	registry.on_subscribed({id, "orderbook_delta", 9});
	kalshi::WsMessage fresh = delta(9, 1);
	registry.on_data(fresh, true);
	EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(fresh).subscription,
			  book.id);
}

TEST(WsSubscriptions, UpdateRepliesReplaceTheMarketList) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_ticker = "A"}, true, frames);
	registry.on_subscribed({2, "ticker", 4});
	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					frames)
			.has_value());

	const Reaction ok = registry.on_ok({3, 4, R"({"market_tickers":["A","B","Z"]})"});
	ASSERT_TRUE(ok.message.has_value());
	const kalshi::ws::Updated& updated = std::get<kalshi::ws::Updated>(*ok.message);
	EXPECT_EQ(updated.subscription, ticker);
	EXPECT_EQ(updated.market_tickers, (std::vector<std::string>{"A", "B", "Z"}));

	frames.clear();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(R"("market_tickers":["A","B","Z"])"), std::string::npos) << frames[0];
	EXPECT_EQ(frames[0].find("market_ticker\":"), std::string::npos) << frames[0];
}

TEST(WsSubscriptions, GapsAreReportedAndOrderbookGapsRequestSnapshots) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_tickers = {"A"}}, true, frames);
	registry.on_subscribed({2, "orderbook_delta", 3});

	kalshi::WsMessage first = delta(3, 1);
	kalshi::WsMessage second = delta(3, 2);
	kalshi::WsMessage skipped = delta(3, 4);
	EXPECT_FALSE(registry.on_data(first, true).error.has_value());
	EXPECT_FALSE(registry.on_data(second, true).error.has_value());
	const Reaction gap = registry.on_data(skipped, true);
	ASSERT_TRUE(gap.error.has_value());
	EXPECT_EQ(gap.error->subscription, book);
	ASSERT_EQ(gap.frames.size(), 1U);
	EXPECT_NE(gap.frames[0].find(R"("sids":[3],"market_tickers":["A"],"action":"get_snapshot")"),
			  std::string::npos)
		<< gap.frames[0];

	kalshi::ws::Update<kalshi::ws::OrderbookSnapshot> snapshot;
	snapshot.sid = 3;
	snapshot.seq = 9; // a snapshot starts a new baseline
	kalshi::WsMessage baseline = snapshot;
	kalshi::WsMessage next = delta(3, 10);
	EXPECT_FALSE(registry.on_data(baseline, true).error.has_value());
	EXPECT_FALSE(registry.on_data(next, true).error.has_value());

	kalshi::WsMessage again = delta(3, 20);
	EXPECT_TRUE(registry.on_data(again, false).frames.empty()); // resync disabled
}

TEST(WsSubscriptions, ErrorsNameTheirSubscriptionAndEndFailedSubscribes) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription fills = registry.subscribe(Channel::Fill, {}, true, frames);

	const Reaction failed = registry.on_error({2, std::nullopt, std::nullopt, 9, ""});
	ASSERT_TRUE(failed.error.has_value());
	EXPECT_EQ(failed.error->code, 9);
	EXPECT_EQ(failed.error->message, "Authentication required"); // named from the spec
	EXPECT_EQ(failed.error->subscription, fills);
	EXPECT_TRUE(registry.subscriptions().empty());
}

TEST(WsSubscriptions, ListRepliesCarryTheCommandId) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const std::int64_t id = registry.list_subscriptions(frames);
	EXPECT_EQ(frames, (std::vector<std::string>{R"({"id":1,"cmd":"list_subscriptions"})"}));

	const Reaction reply = registry.on_ok({id, std::nullopt, R"([{"channel":"ticker","sid":2}])"});
	ASSERT_TRUE(reply.message.has_value());
	const kalshi::ws::SubscriptionList& list =
		std::get<kalshi::ws::SubscriptionList>(*reply.message);
	EXPECT_EQ(list.id, id);
	ASSERT_EQ(list.subscriptions.size(), 1U);
	EXPECT_EQ(list.subscriptions[0].sid, 2);
}

TEST(WsSubscriptions, RejectsUnknownHandlesAndMissingActions) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription stranger{99, Channel::Ticker};
	EXPECT_FALSE(registry.unsubscribe(stranger, frames).has_value());
	EXPECT_FALSE(
		registry.update(stranger, {.action = kalshi::ws::UpdateAction::GetSnapshot}, frames)
			.has_value());

	const kalshi::ws::Subscription ticker = registry.subscribe(Channel::Ticker, {}, false, frames);
	const kalshi::Result<void> missing = registry.update(ticker, {}, frames);
	ASSERT_FALSE(missing.has_value());
	EXPECT_EQ(missing.error().code, kalshi::ErrorCode::InvalidRequest);
}

TEST(WsSubscriptions, AnAckNobodyWantsIsUnsubscribed) {
	SubscriptionRegistry registry;
	const Reaction reply = registry.on_subscribed({42, "ticker", 8});
	EXPECT_FALSE(reply.message.has_value());
	ASSERT_EQ(reply.frames.size(), 1U);
	EXPECT_NE(reply.frames[0].find(R"("cmd":"unsubscribe","params":{"sids":[8]})"),
			  std::string::npos);
}
