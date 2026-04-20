// Unit tests for the WebSocket JSON helpers.
//
// Covers two distinct historical regressions:
//
// 1) Kalshi's v2 WS schema renamed the numeric fields to use the
//    ``_dollars`` / ``_fp`` suffixes and encoded them as decimal
//    *strings*, not integers:
//       "yes_price_dollars": "0.3200"  (32 cents)
//       "count_fp":          "40.00"   (40 contracts)
//       "delta_fp":          "-30.87"  (-31 rounded)
//       "price_dollars":     "0.4200"  (42 cents, orderbook deltas)
//    The previous strict integer parser returned 0 for every tick for
//    four-plus days of live traffic.
//
// 2) Orderbook delta timestamps are ISO-8601 strings
//    ("2026-04-20T08:19:13.898402Z"), not unix-second ints.

#include "kalshi/detail/ws_json.hpp"

#include <gtest/gtest.h>

using kalshi::detail::extract_dollar_cents;
using kalshi::detail::extract_fp_int;
using kalshi::detail::extract_int;
using kalshi::detail::extract_iso8601_millis;
using kalshi::detail::extract_orderbook_entries;
using kalshi::detail::extract_string;

// --- extract_int: raw-number path (still used for sid/seq/ts) -------

TEST(ExtractInt, RawNumber) {
	const std::string j = R"({"sid":2,"seq":501,"ts":1776673036})";
	EXPECT_EQ(extract_int(j, "sid"), 2);
	EXPECT_EQ(extract_int(j, "seq"), 501);
	EXPECT_EQ(extract_int(j, "ts"), 1776673036);
}

TEST(ExtractInt, RawNegative) {
	EXPECT_EQ(extract_int(R"({"delta":-5})", "delta"), -5);
}

TEST(ExtractInt, MissingKeyReturnsZero) {
	EXPECT_EQ(extract_int(R"({"a":1})", "b"), 0);
}

// --- extract_string -------------------------------------------------

TEST(ExtractString, HappyPath) {
	const std::string j = R"({"market_ticker":"KXHIGHDEN","taker_side":"yes"})";
	EXPECT_EQ(extract_string(j, "market_ticker"), "KXHIGHDEN");
	EXPECT_EQ(extract_string(j, "taker_side"), "yes");
}

TEST(ExtractString, MissingKeyReturnsEmpty) {
	EXPECT_EQ(extract_string(R"({"a":"b"})", "c"), "");
}

// --- extract_dollar_cents: regression coverage ---------------------

TEST(ExtractDollarCents, TypicalPrices) {
	const std::string j = R"({"yes_price_dollars":"0.3200","no_price_dollars":"0.6800"})";
	EXPECT_EQ(extract_dollar_cents(j, "yes_price_dollars"), 32);
	EXPECT_EQ(extract_dollar_cents(j, "no_price_dollars"), 68);
}

TEST(ExtractDollarCents, BoundaryPrices) {
	EXPECT_EQ(extract_dollar_cents(R"({"p":"0.0100"})", "p"), 1);
	EXPECT_EQ(extract_dollar_cents(R"({"p":"0.9900"})", "p"), 99);
	EXPECT_EQ(extract_dollar_cents(R"({"p":"1.0000"})", "p"), 100);
	EXPECT_EQ(extract_dollar_cents(R"({"p":"0.0000"})", "p"), 0);
}

TEST(ExtractDollarCents, RoundingHalfUp) {
	// 0.125 → 13 (not 12) with round-half-up on the 3rd fractional digit.
	EXPECT_EQ(extract_dollar_cents(R"({"p":"0.125"})", "p"), 13);
	EXPECT_EQ(extract_dollar_cents(R"({"p":"0.124"})", "p"), 12);
	EXPECT_EQ(extract_dollar_cents(R"({"p":"0.129"})", "p"), 13);
}

TEST(ExtractDollarCents, NegativeValues) {
	// Prices don't go negative in production, but the parser handles it.
	EXPECT_EQ(extract_dollar_cents(R"({"p":"-0.3200"})", "p"), -32);
}

TEST(ExtractDollarCents, MissingKeyReturnsZero) {
	EXPECT_EQ(extract_dollar_cents(R"({"a":"0.5"})", "b"), 0);
}

TEST(ExtractDollarCents, EmptyStringReturnsZero) {
	EXPECT_EQ(extract_dollar_cents(R"({"p":""})", "p"), 0);
}

// --- extract_fp_int: count_fp + delta_fp ---------------------------

TEST(ExtractFpInt, PositiveFloat) {
	EXPECT_EQ(extract_fp_int(R"({"count_fp":"40.00"})", "count_fp"), 40);
	EXPECT_EQ(extract_fp_int(R"({"count_fp":"111.00"})", "count_fp"), 111);
}

TEST(ExtractFpInt, NegativeFloat) {
	EXPECT_EQ(extract_fp_int(R"({"delta_fp":"-30.87"})", "delta_fp"), -31);
	EXPECT_EQ(extract_fp_int(R"({"delta_fp":"-1.00"})", "delta_fp"), -1);
}

TEST(ExtractFpInt, RoundsHalfUp) {
	EXPECT_EQ(extract_fp_int(R"({"x":"3.5"})", "x"), 4);
	EXPECT_EQ(extract_fp_int(R"({"x":"3.4"})", "x"), 3);
}

TEST(ExtractFpInt, MissingKeyReturnsZero) {
	EXPECT_EQ(extract_fp_int(R"({"a":"5"})", "b"), 0);
}

// --- extract_iso8601_millis ----------------------------------------

TEST(ExtractIso8601, HappyPath) {
	// 2026-04-20T08:19:13.898Z → matches this in ms: compute reference
	// via Python: datetime(2026,4,20,8,19,13,898000).timestamp()*1000.
	// Expected: 1776673153898
	const std::string j = R"({"ts":"2026-04-20T08:19:13.898Z"})";
	EXPECT_EQ(extract_iso8601_millis(j, "ts"), 1776673153898LL);
}

TEST(ExtractIso8601, TruncatesSubMillisecond) {
	// 2026-04-20T08:19:13.898402Z still yields 898 milliseconds.
	const std::string j = R"({"ts":"2026-04-20T08:19:13.898402Z"})";
	EXPECT_EQ(extract_iso8601_millis(j, "ts"), 1776673153898LL);
}

TEST(ExtractIso8601, NoFractionalPart) {
	const std::string j = R"({"ts":"2026-04-20T08:19:13Z"})";
	EXPECT_EQ(extract_iso8601_millis(j, "ts"), 1776673153000LL);
}

TEST(ExtractIso8601, MissingOrMalformedReturnsZero) {
	EXPECT_EQ(extract_iso8601_millis(R"({"a":"2026-04-20T08:19:13Z"})", "b"), 0);
	EXPECT_EQ(extract_iso8601_millis(R"({"ts":"garbage"})", "ts"), 0);
}

// --- End-to-end trade frame shape ---------------------------------

TEST(WsParser, EndToEndTradeFrame) {
	// Exact shape observed in production on 2026-04-20.
	const std::string j =
		R"({"type":"trade","sid":2,"seq":1,)"
		R"("msg":{"trade_id":"5ac29930","market_ticker":"KXHIGHCHI-26APR20-B55.5",)"
		R"("yes_price_dollars":"0.3200","no_price_dollars":"0.6800",)"
		R"("count_fp":"40.00","taker_side":"yes","ts":1776673036}})";
	EXPECT_EQ(extract_int(j, "sid"), 2);
	EXPECT_EQ(extract_int(j, "seq"), 1);
	EXPECT_EQ(extract_string(j, "market_ticker"), "KXHIGHCHI-26APR20-B55.5");
	EXPECT_EQ(extract_dollar_cents(j, "yes_price_dollars"), 32);
	EXPECT_EQ(extract_dollar_cents(j, "no_price_dollars"), 68);
	EXPECT_EQ(extract_fp_int(j, "count_fp"), 40);
	EXPECT_EQ(extract_string(j, "taker_side"), "yes");
	EXPECT_EQ(extract_int(j, "ts"), 1776673036);
}

// --- End-to-end orderbook_delta frame shape ------------------------

TEST(WsParser, EndToEndOrderbookDeltaFrame) {
	// Exact shape observed in production on 2026-04-20.
	const std::string j = R"({"type":"orderbook_delta","sid":1,"seq":501,)"
						  R"("msg":{"market_ticker":"KXHIGHAUS-26APR20-T64",)"
						  R"("market_id":"1ae3c605-1e84-4865-abf3-a9c11985fa71",)"
						  R"("price_dollars":"0.4200","delta_fp":"-30.87","side":"yes",)"
						  R"("ts":"2026-04-20T08:19:13.898402Z"}})";
	EXPECT_EQ(extract_int(j, "sid"), 1);
	EXPECT_EQ(extract_int(j, "seq"), 501);
	EXPECT_EQ(extract_string(j, "market_ticker"), "KXHIGHAUS-26APR20-T64");
	EXPECT_EQ(extract_dollar_cents(j, "price_dollars"), 42);
	EXPECT_EQ(extract_fp_int(j, "delta_fp"), -31);
	EXPECT_EQ(extract_string(j, "side"), "yes");
	EXPECT_EQ(extract_iso8601_millis(j, "ts"), 1776673153898LL);
}

// --- Orderbook-entries array parser (unchanged, still covered) -----

TEST(ExtractOrderbookEntries, RawNumbers) {
	const auto entries = extract_orderbook_entries(R"({"yes":[[47,100]]})", "yes");
	ASSERT_EQ(entries.size(), 1u);
	EXPECT_EQ(entries[0].price, 47);
	EXPECT_EQ(entries[0].quantity, 100);
}

TEST(ExtractOrderbookEntries, EmptyArray) {
	const auto entries = extract_orderbook_entries(R"({"yes":[]})", "yes");
	EXPECT_TRUE(entries.empty());
}

TEST(ExtractOrderbookEntries, MissingKeyReturnsEmpty) {
	const auto entries = extract_orderbook_entries(R"({"yes":[[47,100]]})", "no");
	EXPECT_TRUE(entries.empty());
}
