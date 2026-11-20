#include "kalshi/fixed_point.hpp"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>

TEST(FixedPoint, PreservesWireRepresentation) {
	const kalshi::Result<kalshi::FixedPoint> value = kalshi::FixedPoint::parse("0.5600");
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(value->wire(), "0.5600");
}

TEST(FixedPoint, ConvertsOnlyAtAnExactRequestedScale) {
	const kalshi::Result<kalshi::FixedPoint> dollars = kalshi::FixedPoint::parse("0.5600");
	ASSERT_TRUE(dollars.has_value());
	const kalshi::Result<std::int64_t> cents = dollars->scaled_integer(2);
	ASSERT_TRUE(cents.has_value());
	EXPECT_EQ(*cents, 56);

	const kalshi::Result<kalshi::FixedPoint> subcent = kalshi::FixedPoint::parse("0.560001");
	ASSERT_TRUE(subcent.has_value());
	const kalshi::Result<std::int64_t> lossy = subcent->scaled_integer(2);
	ASSERT_FALSE(lossy.has_value());
	EXPECT_EQ(lossy.error().code, kalshi::ErrorCode::InvalidRequest);
}

TEST(FixedPoint, HandlesSignedCountsAndRejectsInvalidOrOverflowingValues) {
	const kalshi::Result<kalshi::FixedPoint> count = kalshi::FixedPoint::parse("-2.50");
	ASSERT_TRUE(count.has_value());
	const kalshi::Result<std::int64_t> hundredths = count->scaled_integer(2);
	ASSERT_TRUE(hundredths.has_value());
	EXPECT_EQ(*hundredths, -250);
	EXPECT_FALSE(count->scaled_integer(0).has_value());

	EXPECT_FALSE(kalshi::FixedPoint::parse("1e-2").has_value());
	EXPECT_FALSE(kalshi::FixedPoint::parse("1.").has_value());
	const kalshi::Result<kalshi::FixedPoint> huge =
		kalshi::FixedPoint::parse("999999999999999999999999.00");
	ASSERT_TRUE(huge.has_value());
	EXPECT_FALSE(huge->scaled_integer(2).has_value());
}

TEST(FixedPoint, AMovedFromValueFailsToConvert) {
	kalshi::FixedPoint value = kalshi::FixedPoint::parse("12345678901234567890.5").value();
	const kalshi::FixedPoint moved = std::move(value);
	EXPECT_EQ(moved.wire(), "12345678901234567890.5");
	// A moved-from heap string is empty in libstdc++, libc++, and MSVC's STL.
	// NOLINTBEGIN(bugprone-use-after-move): moved-from must stay safe
	ASSERT_TRUE(value.wire().empty());
	const kalshi::Result<std::int64_t> converted = value.scaled_integer(2);
	// NOLINTEND(bugprone-use-after-move)
	ASSERT_FALSE(converted.has_value());
	EXPECT_EQ(converted.error().code, kalshi::ErrorCode::InvalidRequest);
}
