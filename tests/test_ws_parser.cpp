// Unit tests for the hand-rolled WebSocket JSON helpers.
//
// These cover the parser regression where Kalshi's v2 WS schema sends
// trade / orderbook numeric fields as JSON strings (``"yes_price":"47"``
// rather than ``"yes_price":47``), which the previously-strict digit
// loop interpreted as 0 — zeroing every production tick for days.

#include "kalshi/detail/ws_json.hpp"

#include <gtest/gtest.h>

using kalshi::detail::extract_int;
using kalshi::detail::extract_orderbook_entries;
using kalshi::detail::extract_string;
using kalshi::detail::PriceQty;

// --- extract_int: raw-number happy path -----------------------------

TEST(ExtractInt, RawNumber) {
	const std::string j = R"({"yes_price":47,"no_price":53,"count":10})";
	EXPECT_EQ(extract_int(j, "yes_price"), 47);
	EXPECT_EQ(extract_int(j, "no_price"), 53);
	EXPECT_EQ(extract_int(j, "count"), 10);
}

TEST(ExtractInt, RawNegative) {
	const std::string j = R"({"delta":-5,"price":99})";
	EXPECT_EQ(extract_int(j, "delta"), -5);
	EXPECT_EQ(extract_int(j, "price"), 99);
}

// --- extract_int: quoted-number regression coverage ----------------

TEST(ExtractInt, QuotedNumber) {
	// Kalshi production schema: price / count fields arrive as strings.
	const std::string j = R"({"yes_price":"47","no_price":"53","count":"10"})";
	EXPECT_EQ(extract_int(j, "yes_price"), 47);
	EXPECT_EQ(extract_int(j, "no_price"), 53);
	EXPECT_EQ(extract_int(j, "count"), 10);
}

TEST(ExtractInt, QuotedNegative) {
	const std::string j = R"({"delta":"-7"})";
	EXPECT_EQ(extract_int(j, "delta"), -7);
}

TEST(ExtractInt, MixedShapes) {
	// Top-level sid as raw int, msg-nested price as string — mirrors the
	// actual Kalshi production trade frames where top-level subscription
	// metadata is numeric but inner prices are string-encoded.
	const std::string j = R"({"type":"trade","sid":2,"seq":11,)"
						  R"("msg":{"market_ticker":"KXHIGHDEN","yes_price":"47",)"
						  R"("no_price":"53","count":"10","taker_side":"yes","ts":1776671655}})";
	EXPECT_EQ(extract_int(j, "sid"), 2);
	EXPECT_EQ(extract_int(j, "seq"), 11);
	EXPECT_EQ(extract_int(j, "yes_price"), 47);
	EXPECT_EQ(extract_int(j, "no_price"), 53);
	EXPECT_EQ(extract_int(j, "count"), 10);
	EXPECT_EQ(extract_int(j, "ts"), 1776671655);
}

// --- extract_int: edge cases ---------------------------------------

TEST(ExtractInt, MissingKeyReturnsZero) {
	const std::string j = R"({"yes_price":47})";
	EXPECT_EQ(extract_int(j, "no_price"), 0);
}

TEST(ExtractInt, EmptyStringValueReturnsZero) {
	const std::string j = R"({"x":""})";
	EXPECT_EQ(extract_int(j, "x"), 0);
}

TEST(ExtractInt, WhitespaceAfterColon) {
	const std::string j = R"({"x":   42})";
	EXPECT_EQ(extract_int(j, "x"), 42);
}

TEST(ExtractInt, QuotedValueWithWhitespace) {
	// The parser tolerates whitespace before the opening quote but not
	// inside it — Kalshi doesn't emit internal whitespace so this
	// matches the production contract.
	const std::string j = R"({"x":   "99"})";
	EXPECT_EQ(extract_int(j, "x"), 99);
}

// --- extract_string -------------------------------------------------

TEST(ExtractString, HappyPath) {
	const std::string j = R"({"market_ticker":"KXHIGHDEN","taker_side":"yes"})";
	EXPECT_EQ(extract_string(j, "market_ticker"), "KXHIGHDEN");
	EXPECT_EQ(extract_string(j, "taker_side"), "yes");
}

TEST(ExtractString, MissingKeyReturnsEmpty) {
	const std::string j = R"({"market_ticker":"KX"})";
	EXPECT_EQ(extract_string(j, "nope"), "");
}

// --- extract_orderbook_entries --------------------------------------

TEST(ExtractOrderbookEntries, RawNumbers) {
	const std::string j = R"({"yes":[[47,100],[48,200]]})";
	const auto entries = extract_orderbook_entries(j, "yes");
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].price, 47);
	EXPECT_EQ(entries[0].quantity, 100);
	EXPECT_EQ(entries[1].price, 48);
	EXPECT_EQ(entries[1].quantity, 200);
}

TEST(ExtractOrderbookEntries, QuotedNumbers) {
	// Kalshi production wire format for orderbook deltas.
	const std::string j = R"({"yes":[["47","100"],["48","200"]]})";
	const auto entries = extract_orderbook_entries(j, "yes");
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].price, 47);
	EXPECT_EQ(entries[0].quantity, 100);
	EXPECT_EQ(entries[1].price, 48);
	EXPECT_EQ(entries[1].quantity, 200);
}

TEST(ExtractOrderbookEntries, MixedShapes) {
	// Defensive: tolerate mixed raw + quoted inside one frame.
	const std::string j = R"({"no":[[47,"100"],["48",200]]})";
	const auto entries = extract_orderbook_entries(j, "no");
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].price, 47);
	EXPECT_EQ(entries[0].quantity, 100);
	EXPECT_EQ(entries[1].price, 48);
	EXPECT_EQ(entries[1].quantity, 200);
}

TEST(ExtractOrderbookEntries, MissingKeyReturnsEmpty) {
	const std::string j = R"({"yes":[[47,100]]})";
	const auto entries = extract_orderbook_entries(j, "no");
	EXPECT_TRUE(entries.empty());
}

TEST(ExtractOrderbookEntries, EmptyArray) {
	const std::string j = R"({"yes":[]})";
	const auto entries = extract_orderbook_entries(j, "yes");
	EXPECT_TRUE(entries.empty());
}
