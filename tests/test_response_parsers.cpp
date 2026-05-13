#include <gtest/gtest.h>
#include <string>

#include "response_parsers.hpp"

namespace {

TEST(ResponseParsers, CandlesticksParseCurrentDollarSchema) {
	const std::string body = R"json({
		"market_candlesticks": [
			{
				"end_period_ts": 1775656800,
				"price": {
					"open_dollars": "0.4200",
					"high_dollars": "0.5700",
					"low_dollars": "0.3900",
					"close_dollars": "0.5100"
				},
				"volume_fp": "5406.00"
			}
		]
	})json";

	const std::vector<kalshi::Candlestick> candles =
		kalshi::api_detail::parse_candlesticks_response(body);

	ASSERT_EQ(candles.size(), 1U);
	EXPECT_EQ(candles[0].timestamp, 1775656800);
	EXPECT_EQ(candles[0].open_price, 42);
	EXPECT_EQ(candles[0].high_price, 57);
	EXPECT_EQ(candles[0].low_price, 39);
	EXPECT_EQ(candles[0].close_price, 51);
	EXPECT_EQ(candles[0].volume, 5406);
}

TEST(ResponseParsers, CandlesticksParseLegacyCentSchema) {
	const std::string body = R"json({
		"market_candlesticks": [
			{
				"end_period_ts": 1775656860,
				"price": {
					"open": 7,
					"high": 13,
					"low": 6,
					"close": 12
				},
				"volume": 84
			}
		]
	})json";

	const std::vector<kalshi::Candlestick> candles =
		kalshi::api_detail::parse_candlesticks_response(body);

	ASSERT_EQ(candles.size(), 1U);
	EXPECT_EQ(candles[0].timestamp, 1775656860);
	EXPECT_EQ(candles[0].open_price, 7);
	EXPECT_EQ(candles[0].high_price, 13);
	EXPECT_EQ(candles[0].low_price, 6);
	EXPECT_EQ(candles[0].close_price, 12);
	EXPECT_EQ(candles[0].volume, 84);
}

TEST(ResponseParsers, CandlesticksAcceptAlternateArrayKey) {
	const std::string body = R"json({
		"candlesticks": [
			{
				"end_period_ts": 1775656920,
				"price": {
					"open_dollars": "0.0100",
					"high_dollars": "0.0300",
					"low_dollars": "0.0100",
					"close_dollars": "0.0200"
				},
				"volume_fp": "7.00"
			}
		]
	})json";

	const std::vector<kalshi::Candlestick> candles =
		kalshi::api_detail::parse_candlesticks_response(body);

	ASSERT_EQ(candles.size(), 1U);
	EXPECT_EQ(candles[0].open_price, 1);
	EXPECT_EQ(candles[0].high_price, 3);
	EXPECT_EQ(candles[0].low_price, 1);
	EXPECT_EQ(candles[0].close_price, 2);
	EXPECT_EQ(candles[0].volume, 7);
}

} // namespace
