// The subscription registry: command frames, commands that wait for the
// server's confirmation, and resubscribing after a reconnect.

#include "kalshi/websocket.hpp"

#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "frames.hpp"
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
		registry
			.subscribe(Channel::OrderbookDelta,
					   {.market_tickers = {"A", "B"}, .use_yes_price = true}, true, frames)
			.value();
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_EQ(
		frames[0],
		R"({"id":2,"cmd":"subscribe","params":{"channels":["orderbook_delta"],"market_tickers":["A","B"],"use_yes_price":true}})");

	frames.clear();
	registry.on_subscribed({2, "orderbook_delta", 9});
	ASSERT_TRUE(registry
					.update(book,
							{.market_ticker = "C", .action = kalshi::ws::UpdateAction::AddMarkets},
							true, frames)
					.has_value());
	ASSERT_TRUE(registry.unsubscribe(book, frames).has_value());
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":3,"cmd":"update_subscription","params":{"sid":9,"market_ticker":"C","action":"add_markets"}})",
			R"({"id":4,"cmd":"unsubscribe","params":{"sids":[9]}})"}));
}

TEST(WsSubscriptions, NothingIsSentUntilConnected) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {}, false, frames).value();
	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					false, frames)
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
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	frames.clear();
	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					true, frames)
			.has_value());
	EXPECT_TRUE(frames.empty());

	const Reaction ack = registry.on_subscribed({2, "orderbook_delta", 55});
	ASSERT_EQ(ack.frames.size(), 1U);
	EXPECT_NE(ack.frames[0].find(R"("sid":55)"), std::string::npos) << ack.frames[0];
	ASSERT_TRUE(ack.message.has_value());
	EXPECT_EQ(std::get<kalshi::ws::Subscribed>(*ack.message).subscription, book);
}

TEST(WsSubscriptions, AnUnsubscribeBeforeTheAckIsSentAfterIt) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription trades =
		registry.subscribe(Channel::Trade, {}, true, frames).value();
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
		registry.subscribe(Channel::OrderbookDelta, {.market_tickers = {"A"}}, true, frames)
			.value();
	registry.on_subscribed({2, "orderbook_delta", 5});
	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					true, frames)
			.has_value());
	ASSERT_TRUE(
		registry
			.update(book,
					{.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::DeleteMarkets},
					true, frames)
			.has_value());
	registry.on_ok({3, 5});
	registry.on_ok({4, 5});
	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"C"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					true, frames)
			.has_value());

	registry.on_disconnected();
	kalshi::WsMessage stale = delta(5, 1);
	registry.on_data(stale, true);
	EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(stale).subscription, 0);

	// Confirmed changes are part of the resubscribe; the unconfirmed one follows it.
	frames.clear();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_EQ(
		frames[0],
		R"({"id":6,"cmd":"subscribe","params":{"channels":["orderbook_delta"],"market_tickers":["B"]}})");

	const Reaction ack = registry.on_subscribed({6, "orderbook_delta", 9});
	EXPECT_EQ(
		ack.frames,
		(std::vector<std::string>{
			R"({"id":7,"cmd":"update_subscription","params":{"sid":9,"market_tickers":["C"],"action":"add_markets"}})"}));
	kalshi::WsMessage fresh = delta(9, 1);
	registry.on_data(fresh, true);
	EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(fresh).subscription,
			  book.id);
}

TEST(WsSubscriptions, UpdateRepliesReplaceTheMarketList) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets}, true,
					frames)
			.has_value());

	const Reaction ok = registry.on_ok({3, 4, std::nullopt, R"({"market_tickers":["A","B","Z"]})"});
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
		registry.subscribe(Channel::OrderbookDelta, {.market_tickers = {"A"}}, true, frames)
			.value();
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
	EXPECT_NE(gap.frames[0].find(R"("sid":3,"market_tickers":["A"],"action":"get_snapshot")"),
			  std::string::npos)
		<< gap.frames[0];

	kalshi::ws::Update<kalshi::ws::OrderbookSnapshot> snapshot;
	snapshot.sid = 3;
	snapshot.seq = 5; // snapshots share the subscription's sequence
	kalshi::WsMessage baseline = snapshot;
	kalshi::WsMessage next = delta(3, 6);
	EXPECT_FALSE(registry.on_data(baseline, true).error.has_value());
	EXPECT_FALSE(registry.on_data(next, true).error.has_value());

	kalshi::WsMessage again = delta(3, 20);
	EXPECT_TRUE(registry.on_data(again, false).frames.empty()); // resync disabled
}

TEST(WsSubscriptions, ErrorsNameTheirSubscriptionAndEndFailedSubscribes) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription fills =
		registry.subscribe(Channel::Fill, {}, true, frames).value();

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

	const Reaction reply =
		registry.on_ok({id, std::nullopt, std::nullopt, R"([{"channel":"ticker","sid":2}])"});
	ASSERT_TRUE(reply.message.has_value());
	const kalshi::ws::SubscriptionList& list =
		std::get<kalshi::ws::SubscriptionList>(*reply.message);
	EXPECT_EQ(list.id, id);
	ASSERT_EQ(list.subscriptions.size(), 1U);
	EXPECT_EQ(list.subscriptions[0].sid, 2);
}

TEST(WsSubscriptions, MalformedListRepliesAreErrorsNotEmptyLists) {
	// The spec requires `msg` to be an array whose entries carry a channel and a
	// sid of at least 1. Anything else is not a list this SDK can vouch for.
	for (const std::string_view msg :
		 {std::string_view{}, std::string_view{"{}"}, std::string_view{"null"},
		  std::string_view{R"([{"channel":"ticker"}])"}, std::string_view{R"([{"sid":2}])"},
		  std::string_view{R"([{"channel":"ticker","sid":0}])"}}) {
		SubscriptionRegistry registry;
		std::vector<std::string> frames;
		const std::int64_t id = registry.list_subscriptions(frames);
		const Reaction reply =
			registry.on_ok({id, std::nullopt, std::nullopt, std::string{msg}, 1234});
		EXPECT_FALSE(reply.message.has_value()) << msg;
		ASSERT_TRUE(reply.error.has_value()) << msg;
		EXPECT_EQ(reply.error->id, id) << msg;
		EXPECT_EQ(reply.error->sending_ts_ms, 1234) << msg;
		EXPECT_TRUE(reply.frames.empty()) << msg;
	}

	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const std::int64_t id = registry.list_subscriptions(frames);
	const Reaction empty = registry.on_ok({id, std::nullopt, std::nullopt, "[]"});
	EXPECT_FALSE(empty.error.has_value());
	ASSERT_TRUE(empty.message.has_value());
	EXPECT_TRUE(std::get<kalshi::ws::SubscriptionList>(*empty.message).subscriptions.empty());
}

TEST(WsSubscriptions, RejectsUnknownHandlesAndMissingActions) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription stranger{99, Channel::Ticker};
	EXPECT_FALSE(registry.unsubscribe(stranger, frames).has_value());
	EXPECT_FALSE(
		registry.update(stranger, {.action = kalshi::ws::UpdateAction::GetSnapshot}, true, frames)
			.has_value());

	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {}, false, frames).value();
	const kalshi::Result<void> missing = registry.update(ticker, {}, true, frames);
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

TEST(WsSubscriptions, RejectedUpdatesAreNotResubscribed) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"BAD"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					true, frames)
			.has_value());
	const Reaction rejected = registry.on_error({3, 4, std::nullopt, 11, "Invalid parameter"});
	ASSERT_TRUE(rejected.error.has_value());
	EXPECT_EQ(rejected.error->subscription, ticker);
	EXPECT_EQ(registry.subscriptions().size(), 1U); // a failed update keeps the subscription

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(R"("market_tickers":["A"])"), std::string::npos) << frames[0];
}

TEST(WsSubscriptions, EmptiedSubscriptionsWaitForMarketsBeforeResubscribing) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::DeleteMarkets},
					true, frames)
			.has_value());
	registry.on_ok({3, 4, std::nullopt, R"({"market_tickers":[]})"});

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_TRUE(frames.empty()); // resubscribing without markets would stream all of them
	EXPECT_EQ(registry.subscriptions().size(), 1U);

	ASSERT_TRUE(
		registry
			.update(ticker,
					{.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets}, true,
					frames)
			.has_value());
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(
				  R"("cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["B"]})"),
			  std::string::npos)
		<< frames[0];
}

TEST(WsSubscriptions, SequencedRepliesAdvanceTheSequence) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_tickers = {"A"}}, true, frames)
			.value();
	registry.on_subscribed({2, "orderbook_delta", 3});
	kalshi::WsMessage first = delta(3, 5);
	registry.on_data(first, true);

	ASSERT_TRUE(
		registry
			.update(book, {.market_tickers = {"B"}, .action = kalshi::ws::UpdateAction::AddMarkets},
					true, frames)
			.has_value());
	registry.on_ok({3, 3, 6, R"({"market_tickers":["A","B"]})"});
	kalshi::WsMessage next = delta(3, 7);
	EXPECT_FALSE(registry.on_data(next, true).error.has_value());
}

TEST(WsSubscriptions, MaximumSequenceCannotOverflowGapDetection) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	ASSERT_TRUE(registry.subscribe(Channel::OrderbookDelta, {}, true, frames));
	registry.on_subscribed({2, "orderbook_delta", 9});
	kalshi::WsMessage first = delta(9, std::numeric_limits<std::int64_t>::max());
	registry.on_data(first, false);
	kalshi::WsMessage next = delta(9, 1);
	const Reaction reaction = registry.on_data(next, false);
	ASSERT_TRUE(reaction.error);
	EXPECT_NE(reaction.error->message.find("beyond INT64_MAX"), std::string::npos);
}

TEST(WsSubscriptions, RemovingAnAddedFilterDoesNotBroadenAfterReconnect) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {}, false, frames).value();
	ASSERT_TRUE(registry.update(
		ticker, {.market_ticker = "A", .action = kalshi::ws::UpdateAction::AddMarkets}, false,
		frames));
	ASSERT_TRUE(registry.update(
		ticker, {.market_ticker = "A", .action = kalshi::ws::UpdateAction::DeleteMarkets}, false,
		frames));
	registry.on_connected(frames);
	EXPECT_TRUE(frames.empty());
	EXPECT_EQ(registry.subscriptions().size(), 1U);
}

TEST(WsSubscriptions, RejectedUnsubscribeKeepsTheSubscriptionRetryable) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {}, true, frames).value();
	registry.on_subscribed({2, "ticker", 7});
	frames.clear();
	ASSERT_TRUE(registry.unsubscribe(ticker, frames));
	const Reaction failed = registry.on_error({3, 7, std::nullopt, 2, "invalid parameters"});
	ASSERT_TRUE(failed.error);
	EXPECT_EQ(failed.error->subscription, ticker);
	EXPECT_EQ(registry.subscriptions().size(), 1U);
	frames.clear();
	EXPECT_TRUE(registry.unsubscribe(ticker, frames));
	EXPECT_EQ(frames.size(), 1U);
}

TEST(WsSubscriptions, OrderbookHandleStaysReservedUntilUnsubscribeConfirmation) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	frames.clear();
	EXPECT_FALSE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames));
	EXPECT_TRUE(frames.empty());
	registry.on_subscribed({2, "orderbook_delta", 9});
	EXPECT_FALSE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames));
	kalshi::WsMessage update = delta(9, 1);
	registry.on_data(update, false);
	EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(update).subscription,
			  book.id);
	ASSERT_TRUE(registry.unsubscribe(book, frames));
	frames.clear();
	EXPECT_FALSE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames));
	EXPECT_TRUE(frames.empty());
	registry.on_unsubscribed({3, 9});
	EXPECT_TRUE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames));
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_EQ(registry.subscriptions().size(), 1U);
}

TEST(WsSubscriptions, RejectedOrderbookSubscribeCanBeRetried) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	ASSERT_TRUE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames));
	registry.on_error({.id = 2, .code = 11, .message = "rejected"});
	frames.clear();
	EXPECT_TRUE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames));
	ASSERT_EQ(frames.size(), 1U);
}

TEST(WsSubscriptions, UpdatesRejectClosingHandlesAndCanRetryAfterUnsubscribeRejection) {
	for (const bool acknowledged : {false, true}) {
		SCOPED_TRACE(acknowledged);
		SubscriptionRegistry registry;
		std::vector<std::string> frames;
		const kalshi::ws::Subscription book =
			registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames)
				.value();
		if (acknowledged) {
			registry.on_subscribed({2, "orderbook_delta", 9});
		}
		ASSERT_TRUE(registry.unsubscribe(book, frames));
		frames.clear();
		const kalshi::Result<void> update = registry.update(
			book, {.market_ticker = "B", .action = kalshi::ws::UpdateAction::AddMarkets}, true,
			frames);
		ASSERT_FALSE(update);
		EXPECT_EQ(update.error().code, kalshi::ErrorCode::InvalidRequest);
		EXPECT_TRUE(frames.empty());
		if (!acknowledged) {
			const Reaction ack = registry.on_subscribed({2, "orderbook_delta", 9});
			ASSERT_EQ(ack.frames.size(), 1U);
		}
		const Reaction failed = registry.on_error({3, 9, std::nullopt, 2, "invalid parameters"});
		EXPECT_TRUE(failed.frames.empty());
		ASSERT_TRUE(registry.update(
			book, {.market_ticker = "B", .action = kalshi::ws::UpdateAction::AddMarkets}, true,
			frames));
		ASSERT_EQ(frames.size(), 1U);
		EXPECT_EQ(
			frames[0],
			R"({"id":4,"cmd":"update_subscription","params":{"sid":9,"market_ticker":"B","action":"add_markets"}})");
	}
}

TEST(WsSubscriptions, TerminalChannelErrorsAllowOrderbookReplacement) {
	for (const std::int64_t code : {10, 25}) {
		for (const int command_kind : {0, 1, 2}) {
			SCOPED_TRACE(code);
			SCOPED_TRACE(command_kind);
			SubscriptionRegistry registry;
			std::vector<std::string> frames;
			const kalshi::ws::Subscription book =
				registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames)
					.value();
			registry.on_subscribed({2, "orderbook_delta", 9});
			ASSERT_TRUE(registry.update(
				book, {.market_ticker = "B", .action = kalshi::ws::UpdateAction::AddMarkets}, true,
				frames));
			std::optional<std::int64_t> command;
			if (command_kind == 1) {
				command = 3;
			} else if (command_kind == 2) {
				ASSERT_TRUE(registry.unsubscribe(book, frames));
				command = 4;
			}
			frames.clear();
			const Reaction failed = registry.on_error({command, 9, std::nullopt, code, ""});
			ASSERT_TRUE(failed.error);
			EXPECT_EQ(failed.error->subscription, book);
			EXPECT_TRUE(registry.subscriptions().empty());
			EXPECT_FALSE(registry.update(
				book, {.market_ticker = "B", .action = kalshi::ws::UpdateAction::AddMarkets}, true,
				frames));
			EXPECT_FALSE(registry.on_ok({3, 9, 1}).message);
			kalshi::WsMessage stale = delta(9, 2);
			registry.on_data(stale, false);
			EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(stale).subscription,
					  0);
			registry.on_connected(frames);
			EXPECT_TRUE(frames.empty());
			EXPECT_TRUE(
				registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "C"}, true, frames));
			EXPECT_EQ(frames.size(), 1U);
		}
	}
}

TEST(WsSubscriptions, NonterminalChannelErrorsKeepTheActiveHandle) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "orderbook_delta", 9});
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {}, true, frames).value();
	registry.on_subscribed({4, "ticker", 10});
	const Reaction failed =
		registry.on_error({std::nullopt, 9, std::nullopt, 11, "invalid parameter"});
	ASSERT_TRUE(failed.error);
	EXPECT_EQ(failed.error->subscription, book);
	EXPECT_EQ(registry.subscriptions(), (std::vector<kalshi::ws::Subscription>{book, ticker}));
	registry.on_error({std::nullopt, 9, std::nullopt, 10, "channel stopped"});
	EXPECT_EQ(registry.subscriptions(), (std::vector<kalshi::ws::Subscription>{ticker}));
	kalshi::ws::Update<kalshi::ws::Ticker> update;
	update.sid = 10;
	kalshi::WsMessage message = update;
	registry.on_data(message, false);
	EXPECT_EQ(std::get<kalshi::ws::Update<kalshi::ws::Ticker>>(message).subscription, ticker.id);
}

// Kalshi selects markets by ID only on the ticker channel; elsewhere an ID
// filter is acknowledged but selects nothing, so an orderbook never resyncs.
TEST(WsSubscriptions, MarketIdsAreRejectedOutsideTheTickerChannel) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	for (const Channel channel :
		 {Channel::OrderbookDelta, Channel::Trade, Channel::MarketPositions}) {
		const kalshi::Result<kalshi::ws::Subscription> ids =
			registry.subscribe(channel, {.market_ids = {"M1"}}, true, frames);
		ASSERT_FALSE(ids.has_value());
		EXPECT_EQ(ids.error().code, kalshi::ErrorCode::InvalidRequest);
		EXPECT_FALSE(registry.subscribe(channel, {.market_id = "M1"}, true, frames).has_value());
	}
	EXPECT_TRUE(frames.empty());
	EXPECT_TRUE(registry.subscriptions().empty());

	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "orderbook_delta", 3});
	frames.clear();
	EXPECT_FALSE(registry
					 .update(book,
							 {.market_ids = {"M2"}, .action = kalshi::ws::UpdateAction::AddMarkets},
							 true, frames)
					 .has_value());
	EXPECT_FALSE(registry
					 .update(book,
							 {.market_id = "M2", .action = kalshi::ws::UpdateAction::GetSnapshot},
							 true, frames)
					 .has_value());
	EXPECT_TRUE(frames.empty());

	ASSERT_TRUE(registry.subscribe(Channel::Ticker, {.market_ids = {"M1"}}, true, frames));
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(R"("channels":["ticker"],"market_ids":["M1"])"), std::string::npos)
		<< frames[0];
}

// Kalshi answers a market change on a ticker subscription made without markets
// with error 13 and keeps streaming every market.
TEST(WsSubscriptions, AnUnfilteredTickerSubscriptionCannotChangeMarkets) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription all =
		registry.subscribe(Channel::Ticker, {}, true, frames).value();
	const kalshi::ws::UpdateSubscriptionParams add{.market_tickers = {"A"},
												   .action = kalshi::ws::UpdateAction::AddMarkets};
	const kalshi::ws::UpdateSubscriptionParams remove{
		.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::DeleteMarkets};
	EXPECT_FALSE(registry.update(all, add, true, frames).has_value()); // pending
	registry.on_subscribed({2, "ticker", 4});
	EXPECT_FALSE(registry.update(all, add, true, frames).has_value());
	EXPECT_FALSE(registry.update(all, remove, true, frames).has_value());
	EXPECT_EQ(frames.size(), 1U); // only the subscribe

	SubscriptionRegistry filtered_registry;
	std::vector<std::string> filtered_frames;
	const kalshi::ws::Subscription filtered =
		filtered_registry.subscribe(Channel::Ticker, {.market_ticker = "B"}, true, filtered_frames)
			.value();
	filtered_registry.on_subscribed({2, "ticker", 5});
	filtered_frames.clear();
	ASSERT_TRUE(filtered_registry.update(filtered, add, true, filtered_frames).has_value());
	EXPECT_EQ(filtered_frames.size(), 1U);
}

// A subscribe carries no match IDs, so matches follow every confirmation.
TEST(WsSubscriptions, EbattleMatchesAreAddedAgainAfterEachConfirmation) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription scores =
		registry.subscribe(Channel::EbattleScores, {}, true, frames).value();
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_EQ(frames[0], R"({"id":2,"cmd":"subscribe","params":{"channels":["ebattle_scores"]}})");
	frames.clear();
	ASSERT_TRUE(registry
					.update(scores,
							{.action = kalshi::ws::UpdateAction::SubscribeMatches,
							 .match_ids = {"m1", "m2"}},
							true, frames)
					.has_value());
	EXPECT_TRUE(frames.empty()); // waits for the sid

	const Reaction ack = registry.on_subscribed({2, "ebattle_scores", 6});
	ASSERT_EQ(ack.frames.size(), 1U);
	EXPECT_EQ(
		ack.frames[0],
		R"({"id":3,"cmd":"update_subscription","params":{"sid":6,"action":"subscribe_matches","match_ids":["m1","m2"]}})");
	registry.on_ok({3, 6});
	ASSERT_TRUE(
		registry
			.update(scores,
					{.action = kalshi::ws::UpdateAction::UnsubscribeMatches, .match_ids = {"m1"}},
					true, frames)
			.has_value());
	registry.on_ok({4, 6});

	frames.clear();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_EQ(frames[0], R"({"id":5,"cmd":"subscribe","params":{"channels":["ebattle_scores"]}})");
	const Reaction again = registry.on_subscribed({5, "ebattle_scores", 8});
	ASSERT_EQ(again.frames.size(), 1U);
	EXPECT_EQ(
		again.frames[0],
		R"({"id":6,"cmd":"update_subscription","params":{"sid":8,"action":"subscribe_matches","match_ids":["m2"]}})");
}

TEST(WsSubscriptions, EbattleMatchesChosenBeforeConnectingFollowTheFirstConfirmation) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription scores =
		registry.subscribe(Channel::EbattleScores, {}, false, frames).value();
	ASSERT_TRUE(
		registry
			.update(scores,
					{.action = kalshi::ws::UpdateAction::SubscribeMatches, .match_ids = {"all"}},
					false, frames)
			.has_value());
	EXPECT_TRUE(frames.empty());

	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	const Reaction ack = registry.on_subscribed({2, "ebattle_scores", 3});
	ASSERT_EQ(ack.frames.size(), 1U);
	EXPECT_NE(ack.frames[0].find(R"("sid":3,"action":"subscribe_matches","match_ids":["all"])"),
			  std::string::npos)
		<< ack.frames[0];
}

namespace {

kalshi::WsMessage snapshot(std::int64_t sid, std::int64_t seq,
						   std::optional<std::int64_t> id = {}) {
	kalshi::ws::Update<kalshi::ws::OrderbookSnapshot> update;
	update.sid = sid;
	update.seq = seq;
	update.id = id;
	return update;
}

} // namespace

// A delta for one market can go missing just before another market's snapshot
// or an `ok`; both carry the subscription's next seq.
TEST(WsSubscriptions, SnapshotsAndOkFramesRevealGaps) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	ASSERT_TRUE(
		registry.subscribe(Channel::OrderbookDelta, {.market_tickers = {"A", "B"}}, true, frames));
	registry.on_subscribed({2, "orderbook_delta", 3});
	kalshi::WsMessage first = delta(3, 1);
	EXPECT_FALSE(registry.on_data(first, true).error.has_value());
	kalshi::WsMessage after_gap = snapshot(3, 3);
	const Reaction hidden = registry.on_data(after_gap, true);
	ASSERT_TRUE(hidden.error.has_value());
	ASSERT_EQ(hidden.frames.size(), 1U);
	EXPECT_NE(hidden.frames[0].find(R"("action":"get_snapshot")"), std::string::npos);

	const Reaction ok = registry.on_ok({std::nullopt, 3, 7}, true);
	ASSERT_TRUE(ok.error.has_value());
	EXPECT_EQ(ok.error->seq, 7);
	EXPECT_EQ(ok.frames.size(), 1U);
	kalshi::WsMessage next = delta(3, 8);
	EXPECT_FALSE(registry.on_data(next, true).error.has_value());
}

// A get_snapshot reply is a snapshot carrying the command's id, not an `ok`.
TEST(WsSubscriptions, SnapshotRepliesCompleteTheirCommand) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "orderbook_delta", 3});
	for (std::int64_t i = 0; i < 3; ++i) {
		ASSERT_TRUE(
			registry
				.update(book,
						{.market_tickers = {"A"}, .action = kalshi::ws::UpdateAction::GetSnapshot},
						true, frames)
				.has_value());
	}
	// Commands 3, 4, and 5 are in flight; replies to 3 and 4 arrive.
	kalshi::WsMessage reply = snapshot(3, 1, 3);
	registry.on_data(reply, true);
	kalshi::WsMessage second = snapshot(3, 2, 4);
	registry.on_data(second, true);
	// Only command 5 is left, so an `ok` naming 3 or 4 matches nothing.
	EXPECT_FALSE(registry.on_ok({3, 3}).message.has_value());
	EXPECT_FALSE(registry.on_ok({4, 3}).message.has_value());
	EXPECT_TRUE(registry.on_ok({5, 3}).message.has_value());
}

TEST(WsSubscriptions, AReplyIdNeverCompletesAPendingSubscribe) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	kalshi::WsMessage early = snapshot(3, 1, 2); // names the subscribe command
	registry.on_data(early, true);
	const Reaction ack = registry.on_subscribed({2, "orderbook_delta", 3});
	ASSERT_TRUE(ack.message.has_value());
	EXPECT_EQ(std::get<kalshi::ws::Subscribed>(*ack.message).subscription, book);
}

// Error 7 means the server no longer knows the subscription, so it is gone.
TEST(WsSubscriptions, UnknownSubscriptionIdCompletesAnUnsubscribe) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "orderbook_delta", 9});
	ASSERT_TRUE(registry.unsubscribe(book, frames).has_value());
	const Reaction unknown = registry.on_error({3, 9, std::nullopt, 7, "Unknown subscription ID"});
	ASSERT_TRUE(unknown.message.has_value());
	EXPECT_EQ(std::get<kalshi::ws::Unsubscribed>(*unknown.message).subscription, book);
	EXPECT_TRUE(registry.subscriptions().empty());
	frames.clear();
	registry.on_connected(frames);
	EXPECT_TRUE(frames.empty());
	EXPECT_TRUE(registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames)
					.has_value());
}

// The spec lists `seq` as required in `unsubscribed`, but a confirmation
// without one must still end the subscription, or the orderbook handle stays
// reserved until the next reconnect.
TEST(WsSubscriptions, UnsubscribedWithoutASequenceCompletesTheUnsubscribe) {
	for (const std::string_view json : {
			 std::string_view{R"({"type":"unsubscribed","id":3,"sid":9})"},
			 // A server-initiated unsubscribe carries no command id.
			 std::string_view{R"({"type":"unsubscribed","sid":9})"},
		 }) {
		SCOPED_TRACE(json);
		SubscriptionRegistry registry;
		std::vector<std::string> frames;
		const kalshi::ws::Subscription book =
			registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames)
				.value();
		registry.on_subscribed({2, "orderbook_delta", 9});
		ASSERT_TRUE(registry.unsubscribe(book, frames).has_value());

		const kalshi::detail::Frame frame = kalshi::detail::parse_frame(std::string{json});
		const kalshi::detail::UnsubscribedFrame* unsubscribed =
			std::get_if<kalshi::detail::UnsubscribedFrame>(&frame);
		ASSERT_NE(unsubscribed, nullptr);
		const Reaction done = registry.on_unsubscribed(*unsubscribed);
		ASSERT_TRUE(done.message.has_value());
		EXPECT_EQ(std::get<kalshi::ws::Unsubscribed>(*done.message).subscription, book);
		EXPECT_TRUE(registry.subscriptions().empty());
		frames.clear();
		EXPECT_TRUE(
			registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "B"}, true, frames)
				.has_value());
		EXPECT_EQ(frames.size(), 1U);
	}
}

// The server drops subscriptions with the connection, so a pending unsubscribe is done.
TEST(WsSubscriptions, DisconnectingEndsPendingUnsubscribes) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	const kalshi::ws::Subscription trades =
		registry.subscribe(Channel::Trade, {}, true, frames).value();
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {}, true, frames).value();
	registry.on_subscribed({2, "orderbook_delta", 9});
	ASSERT_TRUE(registry.unsubscribe(book, frames).has_value());   // sent
	ASSERT_TRUE(registry.unsubscribe(trades, frames).has_value()); // held until confirmed
	EXPECT_EQ(registry.on_disconnected(), (std::vector<kalshi::ws::Subscription>{book, trades}));
	EXPECT_EQ(registry.subscriptions(), (std::vector<kalshi::ws::Subscription>{ticker}));
}

namespace {

using kalshi::ws::UpdateAction;

void expect_invalid(const kalshi::Result<kalshi::ws::Subscription>& result) {
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest);
}

void expect_invalid(const kalshi::Result<void>& result) {
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest);
}

} // namespace

// The spec makes market_ticker, market_tickers, market_id, and market_ids
// mutually exclusive in a subscribe (#100).
TEST(WsSubscriptions, ContradictoryMarketSelectorsAreRejected) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	for (const kalshi::ws::SubscribeParams& params : {
			 kalshi::ws::SubscribeParams{.market_ticker = "KXHIGHNY-26", .market_id = "uuid-1"},
			 kalshi::ws::SubscribeParams{.market_tickers = {"A"}, .market_ids = {"uuid-1"}},
			 kalshi::ws::SubscribeParams{.market_id = "uuid-1", .market_ids = {"uuid-2"}},
			 kalshi::ws::SubscribeParams{.market_ticker = "A", .market_tickers = {"B"}},
			 kalshi::ws::SubscribeParams{.market_ticker = "A", .market_ids = {"uuid-1"}},
			 kalshi::ws::SubscribeParams{.market_tickers = {"A"}, .market_id = "uuid-1"},
		 }) {
		expect_invalid(registry.subscribe(Channel::Ticker, params, true, frames));
	}
	expect_invalid(registry.subscribe(
		Channel::OrderbookDelta, {.market_ticker = "A", .market_tickers = {"B"}}, true, frames));
	EXPECT_TRUE(frames.empty());
	EXPECT_TRUE(registry.subscriptions().empty());

	ASSERT_TRUE(registry.subscribe(Channel::Ticker, {.market_id = "uuid-1"}, true, frames));
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":2,"cmd":"subscribe","params":{"channels":["ticker"],"market_id":"uuid-1"}})"}));
}

// An empty ticker or ID can never select a market (#100).
TEST(WsSubscriptions, EmptyMarketSelectorsAreRejected) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	for (const kalshi::ws::SubscribeParams& params : {
			 kalshi::ws::SubscribeParams{.market_ticker = ""},
			 kalshi::ws::SubscribeParams{.market_tickers = {"A", ""}},
			 kalshi::ws::SubscribeParams{.market_id = ""},
			 kalshi::ws::SubscribeParams{.market_ids = {"uuid-1", ""}},
		 }) {
		expect_invalid(registry.subscribe(Channel::Ticker, params, true, frames));
	}
	expect_invalid(
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = ""}, true, frames));
	EXPECT_TRUE(frames.empty());

	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	frames.clear();
	expect_invalid(registry.update(
		ticker, {.market_ticker = "", .action = UpdateAction::AddMarkets}, true, frames));
	expect_invalid(registry.update(
		ticker, {.market_tickers = {"B", ""}, .action = UpdateAction::DeleteMarkets}, true,
		frames));
	EXPECT_TRUE(frames.empty());
}

// Error codes 19 to 22 in the spec bound the communications shards.
TEST(WsSubscriptions, ShardParametersFollowTheSpecsBounds) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	for (const kalshi::ws::SubscribeParams& params : {
			 kalshi::ws::SubscribeParams{.shard_key = 3},
			 kalshi::ws::SubscribeParams{.shard_factor = 0},
			 kalshi::ws::SubscribeParams{.shard_factor = -1},
			 kalshi::ws::SubscribeParams{.shard_factor = 101},
			 kalshi::ws::SubscribeParams{.shard_factor = 4, .shard_key = 4},
			 kalshi::ws::SubscribeParams{.shard_factor = 4, .shard_key = -1},
		 }) {
		expect_invalid(registry.subscribe(Channel::Communications, params, true, frames));
	}
	EXPECT_TRUE(frames.empty());

	ASSERT_TRUE(registry.subscribe(Channel::Communications, {.shard_factor = 4, .shard_key = 3},
								   true, frames));
	ASSERT_TRUE(registry.subscribe(Channel::Communications, {.shard_factor = 100}, true, frames));
	ASSERT_TRUE(registry.subscribe(Channel::Communications, {.shard_factor = 1, .shard_key = 0},
								   true, frames));
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":2,"cmd":"subscribe","params":{"channels":["communications"],"shard_factor":4,"shard_key":3}})",
			R"({"id":4,"cmd":"subscribe","params":{"channels":["communications"],"shard_factor":100}})",
			R"({"id":6,"cmd":"subscribe","params":{"channels":["communications"],"shard_factor":1,"shard_key":0}})"}));
}

TEST(WsSubscriptions, UpdatesRejectContradictorySelectors) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "ticker", 9});
	frames.clear();
	expect_invalid(registry.update(
		ticker, {.market_ticker = "B", .market_id = "uuid-9", .action = UpdateAction::AddMarkets},
		true, frames));
	expect_invalid(registry.update(
		ticker, {.market_ticker = "B", .market_tickers = {"C"}, .action = UpdateAction::AddMarkets},
		true, frames));
	expect_invalid(registry.update(
		ticker,
		{.market_id = "uuid-1", .market_ids = {"uuid-2"}, .action = UpdateAction::DeleteMarkets},
		true, frames));
	EXPECT_TRUE(frames.empty());

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":3,"cmd":"subscribe","params":{"channels":["ticker"],"market_ticker":"A"}})"}));
}

// A subscription keeps the selector it was created with, so a resubscribe
// never names markets both by ticker and by ID.
TEST(WsSubscriptions, UpdatesKeepTheSubscriptionsSelectorKind) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription by_ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	const kalshi::ws::Subscription by_id =
		registry.subscribe(Channel::Ticker, {.market_id = "uuid-1"}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	registry.on_subscribed({4, "ticker", 5});
	frames.clear();
	expect_invalid(registry.update(
		by_ticker, {.market_ids = {"uuid-2"}, .action = UpdateAction::AddMarkets}, true, frames));
	expect_invalid(registry.update(
		by_id, {.market_ticker = "B", .action = UpdateAction::AddMarkets}, true, frames));
	EXPECT_TRUE(frames.empty());
	ASSERT_TRUE(registry.update(
		by_id, {.market_ids = {"uuid-2"}, .action = UpdateAction::AddMarkets}, true, frames));
	registry.on_ok({5, 5});

	// An unfiltered subscription not yet sent takes its selector from its first markets.
	const kalshi::ws::Subscription later =
		registry.subscribe(Channel::Ticker, {}, false, frames).value();
	ASSERT_TRUE(registry.update(
		later, {.market_ids = {"uuid-3"}, .action = UpdateAction::AddMarkets}, false, frames));
	expect_invalid(registry.update(
		later, {.market_tickers = {"C"}, .action = UpdateAction::AddMarkets}, false, frames));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":7,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["A"]}})",
			R"({"id":8,"cmd":"subscribe","params":{"channels":["ticker"],"market_ids":["uuid-1","uuid-2"]}})",
			R"({"id":9,"cmd":"subscribe","params":{"channels":["ticker"],"market_ids":["uuid-3"]}})"}));
}

// An `ok` reply may list markets both by ticker and by ID. Only the list in
// the subscription's own selector replaces its markets.
TEST(WsSubscriptions, UpdateRepliesReplaceOnlyTheSubscriptionsSelectorKind) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription by_ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	const kalshi::ws::Subscription by_id =
		registry.subscribe(Channel::Ticker, {.market_ids = {"uuid-1"}}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	registry.on_subscribed({4, "ticker", 5});
	ASSERT_TRUE(registry.update(
		by_ticker, {.market_tickers = {"B"}, .action = UpdateAction::AddMarkets}, true, frames));
	ASSERT_TRUE(registry.update(
		by_id, {.market_ids = {"uuid-2"}, .action = UpdateAction::AddMarkets}, true, frames));
	ASSERT_TRUE(registry.update(
		by_id, {.market_ids = {"uuid-3"}, .action = UpdateAction::AddMarkets}, true, frames));

	const Reaction both = registry.on_ok(
		{5, 4, std::nullopt, R"({"market_tickers":["A","B"],"market_ids":["uuid-a","uuid-b"]})"});
	ASSERT_TRUE(both.message.has_value());
	EXPECT_EQ(std::get<kalshi::ws::Updated>(*both.message).market_ids,
			  (std::vector<std::string>{"uuid-a", "uuid-b"})); // reported as sent
	registry.on_ok({6, 5, std::nullopt, R"({"market_tickers":["X","Y"]})"});
	registry.on_ok(
		{7, 5, std::nullopt,
		 R"({"market_tickers":["X","Y","Z"],"market_ids":["uuid-1","uuid-2","uuid-3"]})"});

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":8,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["A","B"]}})",
			R"({"id":9,"cmd":"subscribe","params":{"channels":["ticker"],"market_ids":["uuid-1","uuid-2","uuid-3"]}})"}));
}

// The server's subscriptions end with the connection, so an update it had not
// answered is sent again on its own. Folded into the resubscribe, a rejected
// change would end the whole subscription.
TEST(WsSubscriptions, UnconfirmedUpdatesAreResentAfterTheNewConfirmation) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"GOOD", "BAD"}, .action = UpdateAction::AddMarkets}, true,
		frames));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":4,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["A"]}})"}));
	const Reaction ack = registry.on_subscribed({4, "ticker", 8});
	EXPECT_EQ(
		ack.frames,
		(std::vector<std::string>{
			R"({"id":5,"cmd":"update_subscription","params":{"sid":8,"market_tickers":["GOOD","BAD"],"action":"add_markets"}})"}));

	const Reaction rejected = registry.on_error({5, 8, std::nullopt, 11, "Invalid parameter"});
	ASSERT_TRUE(rejected.error.has_value());
	EXPECT_EQ(rejected.error->subscription, ticker);
	EXPECT_EQ(registry.subscriptions(), (std::vector<kalshi::ws::Subscription>{ticker}));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":6,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["A"]}})"}));
	EXPECT_TRUE(registry.on_subscribed({6, "ticker", 9}).frames.empty());
}

// Updates waiting for a confirmation stay separate too, and updates made while
// disconnected queue behind them, in order.
TEST(WsSubscriptions, HeldUpdatesStaySeparateAndInOrderAcrossReconnects) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"BAD"}, .action = UpdateAction::AddMarkets}, true, frames));
	registry.on_disconnected();
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"BAD"}, .action = UpdateAction::DeleteMarkets}, false, frames));
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"C"}, .action = UpdateAction::AddMarkets}, false, frames));
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"A"}, .action = UpdateAction::GetSnapshot}, false, frames));

	frames.clear();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":3,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["A"]}})"}));
	const Reaction ack = registry.on_subscribed({3, "ticker", 5});
	EXPECT_EQ(
		ack.frames,
		(std::vector<std::string>{
			R"({"id":4,"cmd":"update_subscription","params":{"sid":5,"market_tickers":["BAD"],"action":"add_markets"}})",
			R"({"id":5,"cmd":"update_subscription","params":{"sid":5,"market_tickers":["BAD"],"action":"delete_markets"}})",
			R"({"id":6,"cmd":"update_subscription","params":{"sid":5,"market_tickers":["C"],"action":"add_markets"}})"}));
	registry.on_error({4, 5, std::nullopt, 11, "Invalid parameter"});
	registry.on_error({5, 5, std::nullopt, 11, "Invalid parameter"});
	registry.on_ok({6, 5});
	EXPECT_EQ(registry.subscriptions(), (std::vector<kalshi::ws::Subscription>{ticker}));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":7,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["A","C"]}})"}));
}

// The new subscription sends fresh snapshots, so a snapshot request from the
// old connection is not sent again.
TEST(WsSubscriptions, SnapshotRequestsAreNotResentAfterAReconnect) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription book =
		registry.subscribe(Channel::OrderbookDelta, {.market_ticker = "A"}, true, frames).value();
	registry.on_subscribed({2, "orderbook_delta", 3});
	ASSERT_TRUE(registry.update(
		book, {.market_tickers = {"A"}, .action = UpdateAction::GetSnapshot}, true, frames));
	ASSERT_TRUE(registry.update(book, {.market_tickers = {"B"}, .action = UpdateAction::AddMarkets},
								true, frames));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	EXPECT_NE(frames[0].find(R"("market_ticker":"A")"), std::string::npos) << frames[0];
	const Reaction ack = registry.on_subscribed({5, "orderbook_delta", 7});
	EXPECT_EQ(
		ack.frames,
		(std::vector<std::string>{
			R"({"id":6,"cmd":"update_subscription","params":{"sid":7,"market_tickers":["B"],"action":"add_markets"}})"}));
}

// A subscription whose markets were all removed is not resubscribed, so
// markets added on the old connection become its next subscribe.
TEST(WsSubscriptions, AnEmptiedSubscriptionSubscribesWithItsUnconfirmedMarkets) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription ticker =
		registry.subscribe(Channel::Ticker, {.market_tickers = {"A"}}, true, frames).value();
	registry.on_subscribed({2, "ticker", 4});
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"A"}, .action = UpdateAction::DeleteMarkets}, true, frames));
	registry.on_ok({3, 4, std::nullopt, R"({"market_tickers":[]})"});
	ASSERT_TRUE(registry.update(
		ticker, {.market_tickers = {"B"}, .action = UpdateAction::AddMarkets}, true, frames));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	EXPECT_EQ(
		frames,
		(std::vector<std::string>{
			R"({"id":5,"cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["B"]}})"}));
	EXPECT_TRUE(registry.on_subscribed({5, "ticker", 6}).frames.empty());
}

TEST(WsSubscriptions, UnconfirmedMatchesFollowTheConfirmedOnes) {
	SubscriptionRegistry registry;
	std::vector<std::string> frames;
	const kalshi::ws::Subscription scores =
		registry.subscribe(Channel::EbattleScores, {}, true, frames).value();
	registry.on_subscribed({2, "ebattle_scores", 6});
	ASSERT_TRUE(registry.update(
		scores, {.action = UpdateAction::SubscribeMatches, .match_ids = {"m1"}}, true, frames));
	registry.on_ok({3, 6});
	ASSERT_TRUE(registry.update(
		scores, {.action = UpdateAction::SubscribeMatches, .match_ids = {"m2"}}, true, frames));

	frames.clear();
	registry.on_disconnected();
	registry.on_connected(frames);
	ASSERT_EQ(frames.size(), 1U);
	const Reaction ack = registry.on_subscribed({5, "ebattle_scores", 8});
	EXPECT_EQ(
		ack.frames,
		(std::vector<std::string>{
			R"({"id":6,"cmd":"update_subscription","params":{"sid":8,"action":"subscribe_matches","match_ids":["m1"]}})",
			R"({"id":7,"cmd":"update_subscription","params":{"sid":8,"action":"subscribe_matches","match_ids":["m2"]}})"}));
}
