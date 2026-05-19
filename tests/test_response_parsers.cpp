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

TEST(ResponseParsers, OrderbookParsesLegacyCentSchema) {
	const std::string body = R"json({
		"orderbook": {
			"market_ticker": "KXRAINNYC-26MAY18-T0",
			"yes": [[47, 100], [46, 25]],
			"no": [[53, 200]]
		}
	})json";

	const kalshi::OrderBook book = kalshi::api_detail::parse_orderbook_response(body);

	EXPECT_EQ(book.market_ticker, "KXRAINNYC-26MAY18-T0");
	ASSERT_EQ(book.yes_bids.size(), 2U);
	EXPECT_EQ(book.yes_bids[0].price_cents, 47);
	EXPECT_EQ(book.yes_bids[0].quantity, 100);
	EXPECT_EQ(book.yes_bids[1].price_cents, 46);
	EXPECT_EQ(book.yes_bids[1].quantity, 25);
	ASSERT_EQ(book.no_bids.size(), 1U);
	EXPECT_EQ(book.no_bids[0].price_cents, 53);
	EXPECT_EQ(book.no_bids[0].quantity, 200);
}

TEST(ResponseParsers, OrderbookParsesCurrentFixedPointDollarSchema) {
	const std::string body = R"json({
		"ticker": "KXRAINSEAM-26MAY",
		"orderbook_fp": {
			"yes_dollars": [["0.0100", "788.03"], ["0.1250", "3.50"]],
			"no_dollars": [["0.9900", "10.00"]]
		}
	})json";

	const kalshi::OrderBook book = kalshi::api_detail::parse_orderbook_response(body);

	EXPECT_EQ(book.market_ticker, "KXRAINSEAM-26MAY");
	ASSERT_EQ(book.yes_bids.size(), 2U);
	EXPECT_EQ(book.yes_bids[0].price_cents, 1);
	EXPECT_EQ(book.yes_bids[0].quantity, 788);
	EXPECT_EQ(book.yes_bids[1].price_cents, 13);
	EXPECT_EQ(book.yes_bids[1].quantity, 4);
	ASSERT_EQ(book.no_bids.size(), 1U);
	EXPECT_EQ(book.no_bids[0].price_cents, 99);
	EXPECT_EQ(book.no_bids[0].quantity, 10);
}

TEST(ResponseParsers, OrderbooksParseBatchedResponse) {
	const std::string body = R"json({
		"orderbooks": [
			{
				"ticker": "KXRAINNYC-26MAY18-T0",
				"orderbook_fp": {
					"yes_dollars": [["0.2200", "12.00"]],
					"no_dollars": [["0.7800", "8.00"]]
				}
			},
			{
				"ticker": "KXHIGHAUS-26MAY18-B75",
				"orderbook_fp": {
					"yes_dollars": [],
					"no_dollars": [["0.5100", "3.00"]]
				}
			}
		]
	})json";

	const std::vector<kalshi::OrderBook> books =
		kalshi::api_detail::parse_orderbooks_response(body);

	ASSERT_EQ(books.size(), 2U);
	EXPECT_EQ(books[0].market_ticker, "KXRAINNYC-26MAY18-T0");
	ASSERT_EQ(books[0].yes_bids.size(), 1U);
	EXPECT_EQ(books[0].yes_bids[0].price_cents, 22);
	EXPECT_EQ(books[0].yes_bids[0].quantity, 12);
	ASSERT_EQ(books[0].no_bids.size(), 1U);
	EXPECT_EQ(books[0].no_bids[0].price_cents, 78);
	EXPECT_EQ(books[1].market_ticker, "KXHIGHAUS-26MAY18-B75");
	EXPECT_TRUE(books[1].yes_bids.empty());
	ASSERT_EQ(books[1].no_bids.size(), 1U);
	EXPECT_EQ(books[1].no_bids[0].price_cents, 51);
	EXPECT_EQ(books[1].no_bids[0].quantity, 3);
}

TEST(ResponseParsers, DepositsParseFinalizedAndPending) {
	const std::string body = R"json({
		"deposits": [
			{
				"id": "dep_01",
				"status": "applied",
				"type": "wire",
				"amount_cents": 50000,
				"fee_cents": 0,
				"created_ts": 1715000000,
				"finalized_ts": 1715000600
			},
			{
				"id": "dep_02",
				"status": "pending",
				"type": "ach",
				"amount_cents": 25000,
				"fee_cents": 100,
				"created_ts": 1715100000,
				"finalized_ts": null
			}
		],
		"cursor": "abc"
	})json";

	const std::vector<kalshi::Deposit> deposits = kalshi::api_detail::parse_deposits_response(body);

	ASSERT_EQ(deposits.size(), 2U);
	EXPECT_EQ(deposits[0].id, "dep_01");
	EXPECT_EQ(deposits[0].status, "applied");
	EXPECT_EQ(deposits[0].type, "wire");
	EXPECT_EQ(deposits[0].amount_cents, 50000);
	EXPECT_EQ(deposits[0].fee_cents, 0);
	EXPECT_EQ(deposits[0].created_ts, 1715000000);
	ASSERT_TRUE(deposits[0].finalized_ts.has_value());
	EXPECT_EQ(*deposits[0].finalized_ts, 1715000600);

	EXPECT_EQ(deposits[1].id, "dep_02");
	EXPECT_EQ(deposits[1].status, "pending");
	EXPECT_EQ(deposits[1].type, "ach");
	EXPECT_EQ(deposits[1].amount_cents, 25000);
	EXPECT_EQ(deposits[1].fee_cents, 100);
	EXPECT_FALSE(deposits[1].finalized_ts.has_value());
}

TEST(ResponseParsers, DepositsEmptyArray) {
	const std::string body = R"json({"deposits": [], "cursor": ""})json";
	const std::vector<kalshi::Deposit> deposits = kalshi::api_detail::parse_deposits_response(body);
	EXPECT_TRUE(deposits.empty());
}

TEST(ResponseParsers, WithdrawalsParseMixedStatuses) {
	const std::string body = R"json({
		"withdrawals": [
			{
				"id": "wd_01",
				"status": "applied",
				"type": "crypto",
				"amount_cents": 100000,
				"fee_cents": 250,
				"created_ts": 1716000000,
				"finalized_ts": 1716000900
			},
			{
				"id": "wd_02",
				"status": "failed",
				"type": "debit",
				"amount_cents": 75000,
				"fee_cents": 0,
				"created_ts": 1716100000,
				"finalized_ts": null
			}
		]
	})json";

	const std::vector<kalshi::Withdrawal> withdrawals =
		kalshi::api_detail::parse_withdrawals_response(body);

	ASSERT_EQ(withdrawals.size(), 2U);
	EXPECT_EQ(withdrawals[0].id, "wd_01");
	EXPECT_EQ(withdrawals[0].status, "applied");
	EXPECT_EQ(withdrawals[0].type, "crypto");
	EXPECT_EQ(withdrawals[0].amount_cents, 100000);
	EXPECT_EQ(withdrawals[0].fee_cents, 250);
	ASSERT_TRUE(withdrawals[0].finalized_ts.has_value());
	EXPECT_EQ(*withdrawals[0].finalized_ts, 1716000900);

	EXPECT_EQ(withdrawals[1].id, "wd_02");
	EXPECT_EQ(withdrawals[1].status, "failed");
	EXPECT_FALSE(withdrawals[1].finalized_ts.has_value());
}

TEST(ResponseParsers, WithdrawalsEmptyArrayWhenKeyMissing) {
	// Defensive: an unrelated body shouldn't populate the vector.
	const std::string body = R"json({"deposits": [{"id":"x"}]})json";
	const std::vector<kalshi::Withdrawal> withdrawals =
		kalshi::api_detail::parse_withdrawals_response(body);
	EXPECT_TRUE(withdrawals.empty());
}

TEST(ResponseParsers, OrderCancelResultParsesV2Response) {
	const std::string body = R"json({
		"order_id": "order-1",
		"reduced_by": "10.00",
		"ts_ms": 1779148800123,
		"client_order_id": "client-1"
	})json";

	const kalshi::OrderCancelResult result =
		kalshi::api_detail::parse_order_cancel_result_response(body);

	EXPECT_EQ(result.order_id, "order-1");
	EXPECT_EQ(result.reduced_by, "10.00");
	EXPECT_EQ(result.ts_ms, 1779148800123);
	EXPECT_EQ(result.client_order_id, "client-1");
	EXPECT_FALSE(result.error.has_value());
}

TEST(ResponseParsers, BatchOrderCancelResultParsesV2Errors) {
	const std::string body = R"json({
		"orders": [
			{
				"order_id": "order-1",
				"reduced_by": "10.00",
				"client_order_id": "client-1",
				"ts_ms": 1779148800123
			},
			{
				"order_id": "order-2",
				"reduced_by": "0.00",
				"client_order_id": "client-2",
				"ts_ms": 1779148800456,
				"error": {
					"code": "not_found",
					"message": "order not found",
					"details": "missing order",
					"service": "matching_engine"
				}
			}
		]
	})json";

	const std::vector<kalshi::OrderCancelResult> results =
		kalshi::api_detail::parse_batch_order_cancel_result_response(body);

	ASSERT_EQ(results.size(), 2U);
	EXPECT_EQ(results[0].order_id, "order-1");
	EXPECT_EQ(results[0].reduced_by, "10.00");
	EXPECT_FALSE(results[0].error.has_value());
	EXPECT_EQ(results[1].order_id, "order-2");
	EXPECT_EQ(results[1].ts_ms, 1779148800456);
	ASSERT_TRUE(results[1].error.has_value());
	EXPECT_EQ(results[1].error->code, "not_found");
	EXPECT_EQ(results[1].error->message, "order not found");
	EXPECT_EQ(results[1].error->details, "missing order");
	EXPECT_EQ(results[1].error->service, "matching_engine");
}

} // namespace
