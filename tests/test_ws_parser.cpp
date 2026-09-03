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
#include "kalshi/detail/ws_message.hpp"

#include <gtest/gtest.h>

using kalshi::detail::extract_bool;
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

TEST(ExtractInt, OutOfRangeClampsInsteadOfOverflowing) {
	// Regression: a bare int32 accumulator silently overflowed (UB) on an
	// out-of-range numeric field. Values beyond int32 must saturate to the
	// nearest bound, not wrap.
	EXPECT_EQ(extract_int(R"({"seq":9999999999})", "seq"), INT32_MAX);
	EXPECT_EQ(extract_int(R"({"seq":-9999999999})", "seq"), INT32_MIN);
	EXPECT_EQ(extract_int(R"({"seq":2147483647})", "seq"), 2147483647); // exact max, unchanged
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

TEST(ExtractBool, HandlesBooleanTokensOnly) {
	EXPECT_TRUE(extract_bool(R"({"is_taker":true})", "is_taker"));
	EXPECT_FALSE(extract_bool(R"({"is_taker":false})", "is_taker"));
	EXPECT_FALSE(extract_bool(R"({"is_taker":"true"})", "is_taker"));
	EXPECT_FALSE(extract_bool(R"({"other":true})", "is_taker"));
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

TEST(WsParser, CurrentSupportedFramesPreserveCanonicalAndExactFields) {
	const std::optional<kalshi::WsMessage> snapshot = kalshi::detail::parse_ws_data_message(
		R"({"type":"orderbook_snapshot","sid":1,"seq":2,"msg":{"market_ticker":"KXTEST","market_id":"market-1","yes_dollars_fp":[["0.1250","1.50"]],"no_dollars_fp":[["0.8750","2.00"]]}})");
	ASSERT_TRUE(snapshot.has_value());
	const kalshi::OrderbookSnapshot& book = std::get<kalshi::OrderbookSnapshot>(*snapshot);
	ASSERT_EQ(book.yes.size(), 1U);
	EXPECT_EQ(book.yes[0].price_dollars, "0.1250");
	EXPECT_EQ(book.yes[0].quantity_fp, "1.50");

	const std::optional<kalshi::WsMessage> trade = kalshi::detail::parse_ws_data_message(
		R"({"type":"trade","sid":2,"msg":{"trade_id":"trade-1","market_ticker":"KXTEST","yes_price_dollars":"0.2500","no_price_dollars":"0.7500","count_fp":"1.50","taker_side":"no","taker_outcome_side":"yes","taker_book_side":"bid","is_block_trade":true,"ts":1788393600,"ts_ms":1788393600123}})");
	ASSERT_TRUE(trade.has_value());
	const kalshi::WsTrade& ws_trade = std::get<kalshi::WsTrade>(*trade);
	EXPECT_EQ(ws_trade.taker_outcome_side, kalshi::OutcomeSide::Yes);
	EXPECT_EQ(ws_trade.taker_book_side, kalshi::BookSide::Bid);
	EXPECT_EQ(ws_trade.timestamp_ms, 1788393600123);

	const std::optional<kalshi::WsMessage> fill = kalshi::detail::parse_ws_data_message(
		R"({"type":"fill","sid":3,"msg":{"trade_id":"trade-1","order_id":"order-1","market_ticker":"KXTEST","exchange_index":3,"is_taker":true,"side":"no","yes_price_dollars":"0.2500","count_fp":"1.50","fee_cost":"0.0100","action":"sell","outcome_side":"yes","book_side":"bid","ts":1788393600,"ts_ms":1788393600123,"client_order_id":"client-1","post_position_fp":"2.50","purchased_side":"yes","subaccount":7}})");
	ASSERT_TRUE(fill.has_value());
	const kalshi::WsFill& ws_fill = std::get<kalshi::WsFill>(*fill);
	EXPECT_EQ(ws_fill.outcome_side, kalshi::OutcomeSide::Yes);
	EXPECT_EQ(ws_fill.book_side, kalshi::BookSide::Bid);
	EXPECT_EQ(ws_fill.exchange_index, 3);
	EXPECT_EQ(ws_fill.fee_cost, "0.0100");
	EXPECT_EQ(ws_fill.timestamp_ms, 1788393600123);
	EXPECT_EQ(ws_fill.post_position_fp, "2.50");
	EXPECT_EQ(ws_fill.subaccount, 7);

	const std::optional<kalshi::WsMessage> lifecycle = kalshi::detail::parse_ws_data_message(
		R"({"type":"market_lifecycle_v2","sid":4,"msg":{"event_type":"price_level_structure_updated","market_ticker":"KXTEST","price_level_structure":"center_deci_edge_centi_cent","settlement_value":"0.5000","yes_sub_title":"At least 10"}})");
	ASSERT_TRUE(lifecycle.has_value());
	const kalshi::MarketLifecycle& life = std::get<kalshi::MarketLifecycle>(*lifecycle);
	EXPECT_EQ(life.event_type, "price_level_structure_updated");
	EXPECT_EQ(life.price_level_structure, "center_deci_edge_centi_cent");
	EXPECT_EQ(life.settlement_value_dollars, "0.5000");
	EXPECT_EQ(kalshi::classify_lifecycle_event(life),
			  kalshi::LifecycleEventType::PriceLevelStructureUpdated);

	const std::optional<kalshi::WsMessage> created = kalshi::detail::parse_ws_data_message(
		R"({"type":"market_lifecycle_v2","sid":5,"msg":{"event_type":"created","market_ticker":"KXCREATED","exchange_index":2,"open_ts":1788393600,"close_ts":1788480000,"price_level_structure":"deci_cent","price_ranges":[{"start":"0.0000","end":"0.1000","step":"0.0010"},{"start":"0.1000","end":"1.0000","step":"0.0100"}],"strike_type":"between","floor_strike":12.5,"cap_strike":20,"custom_strike":{"unit":"points"},"additional_metadata":{"name":"Created market","title":"Will it happen?","yes_sub_title":"Yes label","no_sub_title":"No label","rules_primary":"Primary","rules_secondary":"Secondary","can_close_early":true,"event_ticker":"KXEVENT","expected_expiration_ts":1788483600,"strike_type":"between","floor_strike":12.5,"cap_strike":20,"custom_strike":{"unit":"points"}}}})");
	ASSERT_TRUE(created.has_value());
	const kalshi::MarketLifecycle& created_lifecycle = std::get<kalshi::MarketLifecycle>(*created);
	ASSERT_EQ(created_lifecycle.price_ranges.size(), 2U);
	EXPECT_EQ(created_lifecycle.price_ranges[0].step, "0.0010");
	EXPECT_EQ(created_lifecycle.strike_type, "between");
	EXPECT_EQ(created_lifecycle.floor_strike, "12.5");
	EXPECT_EQ(created_lifecycle.cap_strike, "20");
	EXPECT_EQ(created_lifecycle.custom_strike_json, R"({"unit":"points"})");
	ASSERT_TRUE(created_lifecycle.additional_metadata.has_value());
	EXPECT_EQ(created_lifecycle.additional_metadata->event_ticker, "KXEVENT");
	EXPECT_TRUE(created_lifecycle.additional_metadata->can_close_early);
	EXPECT_EQ(created_lifecycle.additional_metadata->expected_expiration_ts, 1788483600);
	EXPECT_EQ(created_lifecycle.additional_metadata->yes_sub_title, "Yes label");
}

TEST(WsParser, LegacyDirectionsPopulateCanonicalCompatibilityViews) {
	const std::optional<kalshi::WsMessage> trade = kalshi::detail::parse_ws_data_message(
		R"({"type":"trade","sid":1,"msg":{"trade_id":"trade","market_ticker":"KXTEST","yes_price_dollars":"0.2500","no_price_dollars":"0.7500","count_fp":"1.00","taker_side":"no","ts":1788393600}})");
	ASSERT_TRUE(trade.has_value());
	const kalshi::WsTrade& ws_trade = std::get<kalshi::WsTrade>(*trade);
	EXPECT_EQ(ws_trade.taker_outcome_side, kalshi::OutcomeSide::No);
	EXPECT_EQ(ws_trade.taker_book_side, kalshi::BookSide::Ask);

	const std::optional<kalshi::WsMessage> fill = kalshi::detail::parse_ws_data_message(
		R"({"type":"fill","sid":2,"msg":{"trade_id":"trade","order_id":"order","market_ticker":"KXTEST","is_taker":false,"side":"yes","yes_price_dollars":"0.2500","count_fp":"1.00","action":"sell","ts":1788393600}})");
	ASSERT_TRUE(fill.has_value());
	const kalshi::WsFill& ws_fill = std::get<kalshi::WsFill>(*fill);
	EXPECT_EQ(ws_fill.outcome_side, kalshi::OutcomeSide::No);
	EXPECT_EQ(ws_fill.book_side, kalshi::BookSide::Ask);
}
