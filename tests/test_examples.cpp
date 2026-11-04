#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "../examples/orderbook.hpp"

TEST(OrderbookExample, RejectsOverflowWithoutChangingTheBook) {
	example::Levels levels{{4200, (std::numeric_limits<std::int64_t>::max)()}};
	EXPECT_FALSE(example::apply_delta(levels, "0.4200", "0.01"));
	EXPECT_EQ(levels.at(4200), (std::numeric_limits<std::int64_t>::max)());
	EXPECT_TRUE(example::apply_delta(levels, "0.4200", "-92233720368547758.08"));
	EXPECT_TRUE(levels.empty());
}

TEST(OrderbookExample, MalformedOrUnrepresentableNumbersAreNeverZeroLevels) {
	example::Levels levels{{4200, 100}};
	for (const std::string& invalid :
		 {std::string{"bad"}, std::string{"0.00001"}, std::string{"9223372036854775808"}}) {
		EXPECT_FALSE(example::apply_delta(levels, invalid, "1.00"));
		EXPECT_FALSE(example::apply_delta(levels, "0.4200", invalid));
	}
	EXPECT_FALSE(example::apply_delta(levels, "1.0001", "1.00"));
	EXPECT_EQ(levels, (example::Levels{{4200, 100}}));
}

TEST(OrderbookExample, SnapshotsReplaceAtomicallyAndZeroCountsDisappear) {
	example::Levels levels{{4200, 100}};
	using Snapshot = std::vector<std::array<std::string, 2>>;
	EXPECT_FALSE(example::load(levels, Snapshot{{"0.5000", "1.00"}, {"bad", "1.00"}}));
	EXPECT_EQ(levels, (example::Levels{{4200, 100}}));
	EXPECT_TRUE(example::load(levels, Snapshot{{"0.5000", "1.00"}, {"0.4000", "0.00"}}));
	EXPECT_EQ(levels, (example::Levels{{5000, 100}}));
	EXPECT_TRUE(example::apply_delta(levels, "0.5000", "-1.00"));
	EXPECT_TRUE(levels.empty());
	EXPECT_TRUE(example::load(levels, std::nullopt));
}

TEST(OrderbookExample, InvalidUpdatesRequestRecoveryAndDeltasWaitForASnapshot) {
	example::Orderbooks books;
	kalshi::ws::Update<kalshi::ws::OrderbookSnapshot> snapshot{};
	snapshot.msg.market_ticker = "MARKET";
	snapshot.msg.yes_dollars_fp = std::vector<std::array<std::string, 2>>{{"0.4000", "1.00"}};
	example::Orderbooks::Change change = books.consume(snapshot);
	ASSERT_NE(change.book, nullptr);
	EXPECT_EQ(change.book->yes.at(4000), 100);
	kalshi::ws::Update<kalshi::ws::OrderbookDelta> delta{};
	delta.msg.market_ticker = "MARKET";
	delta.msg.side = kalshi::Side::Yes;
	delta.msg.price_dollars = "0.4000";
	delta.msg.delta_fp = "bad";
	change = books.consume(delta);
	EXPECT_TRUE(change.needs_snapshot);
	EXPECT_EQ(change.ticker, "MARKET");
	EXPECT_EQ(change.book, nullptr);
	delta.msg.delta_fp = "1.00";
	change = books.consume(delta);
	EXPECT_FALSE(change.needs_snapshot);
	EXPECT_EQ(change.book, nullptr);
	change = books.consume(snapshot);
	ASSERT_NE(change.book, nullptr);
	change = books.consume(delta);
	ASSERT_NE(change.book, nullptr);
	EXPECT_EQ(change.book->yes.at(4000), 200);
	snapshot.msg.no_dollars_fp = std::vector<std::array<std::string, 2>>{{"bad", "1.00"}};
	change = books.consume(snapshot);
	EXPECT_TRUE(change.needs_snapshot);
	EXPECT_EQ(change.book, nullptr);
}
