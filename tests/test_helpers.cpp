#include "kalshi/helpers.hpp"

#include <chrono>
#include <gtest/gtest.h>

TEST(Helpers, CentsAndContractsConvertOnlyWhenExact) {
	EXPECT_EQ(kalshi::to_cents("0.5600").value(), 56);
	EXPECT_EQ(kalshi::to_cents("1").value(), 100);
	EXPECT_FALSE(kalshi::to_cents("0.5650").has_value()); // sub-cent tick
	EXPECT_FALSE(kalshi::to_cents("abc").has_value());
	EXPECT_EQ(kalshi::to_contracts("10.00").value(), 10);
	EXPECT_FALSE(kalshi::to_contracts("2.50").has_value());
}

TEST(Helpers, TimestampsParseUtcFractionsAndOffsets) {
	using std::chrono::milliseconds;
	const kalshi::Result<kalshi::Timestamp> utc = kalshi::parse_timestamp("2026-09-25T14:00:00Z");
	ASSERT_TRUE(utc.has_value());
	EXPECT_EQ(utc->time_since_epoch(), milliseconds{1790344800000});

	const kalshi::Result<kalshi::Timestamp> micros =
		kalshi::parse_timestamp("2026-09-25T14:00:00.123456Z");
	ASSERT_TRUE(micros.has_value());
	EXPECT_EQ(micros->time_since_epoch(), milliseconds{1790344800123});

	const kalshi::Result<kalshi::Timestamp> offset =
		kalshi::parse_timestamp("2026-09-25T10:00:00.5-04:00");
	ASSERT_TRUE(offset.has_value());
	EXPECT_EQ(offset->time_since_epoch(), milliseconds{1790344800500});
}

TEST(Helpers, MalformedTimestampsFail) {
	for (const char* text : {"", "2026-09-25", "2026-13-01T00:00:00Z", "2026-02-30T00:00:00Z",
							 "2026-09-25T24:00:00Z", "2026-09-25T14:00:00", "2026-09-25T14:00:00.Z",
							 "2026-09-25T14:00:00+0400", "2026-09-25T14:00:00Zjunk"}) {
		EXPECT_FALSE(kalshi::parse_timestamp(text).has_value()) << text;
	}
}

TEST(Helpers, DirectionsFollowExposure) {
	EXPECT_EQ(kalshi::outcome_side(kalshi::Side::Yes, kalshi::Action::Buy),
			  kalshi::OutcomeSide::Yes);
	EXPECT_EQ(kalshi::outcome_side(kalshi::Side::No, kalshi::Action::Sell),
			  kalshi::OutcomeSide::Yes);
	EXPECT_EQ(kalshi::outcome_side(kalshi::Side::No, kalshi::Action::Buy), kalshi::OutcomeSide::No);
	EXPECT_EQ(kalshi::book_side(kalshi::Side::Yes, kalshi::Action::Sell), kalshi::BookSide::Ask);
	EXPECT_EQ(kalshi::book_side(kalshi::Side::Yes, kalshi::Action::Buy), kalshi::BookSide::Bid);
}
