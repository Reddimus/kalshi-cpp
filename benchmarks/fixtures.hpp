#pragma once

// Synthetic payloads for the benchmarks, sized like live Kalshi data. Length
// matters because std::string keeps short strings inline, up to 15 characters
// in libstdc++ and 22 in libc++, so fixtures with short strings hide
// allocations that real data causes. Each market is about 2 KB, the median of
// a live 1000-market page, and IDs are 36-character UUIDs.

#include "kalshi/kalshi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixtures {

/// A deterministic version 4 UUID.
inline std::string uuid(std::uint64_t seed) {
	// splitmix64, for well-spread hex digits.
	const auto mix = [](std::uint64_t x) {
		x += 0x9E3779B97F4A7C15ULL;
		x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
		x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
		return x ^ (x >> 31U);
	};
	const std::array<std::uint64_t, 2> bits{mix(seed), mix(~seed)};
	constexpr std::string_view digits = "0123456789abcdef";
	std::string out;
	for (std::size_t nibble = 0; nibble < 32; ++nibble) {
		if (nibble == 8 || nibble == 12 || nibble == 16 || nibble == 20) {
			out += '-';
		}
		const std::uint64_t word = bits[nibble / 16];
		out += digits[static_cast<std::size_t>((word >> (60 - 4 * (nibble % 16))) & 0xFU)];
	}
	out[14] = '4';
	out[19] = digits[8 + static_cast<std::size_t>(bits[1] & 0x3U)];
	return out;
}

/// An RFC 3339 time with microseconds, like REST `created_time` values.
inline std::string timestamp(int index) {
	std::array<char, 64> text{};
	std::snprintf(text.data(), text.size(), "2026-09-25T%02d:%02d:%02d.%06dZ",
				  14 + index / 3600 % 10, index / 60 % 60, index % 60, index * 7919 % 1000000);
	return text.data();
}

/// A 50-character page cursor like Kalshi's.
inline std::string page_cursor(int page) {
	std::array<char, 64> text{};
	std::snprintf(text.data(), text.size(), "CgwIpLGb1QYQ4NX0ixISG0tYTkNBQUZTUFJFQUQtMjZTRVAy%02d",
				  page % 100);
	return text.data();
}

/// One market as GET /markets returns it, keys in the API's alphabetical
/// order. `null_expiration_value` sends `expiration_value`, a string the spec
/// marks non-null, as null.
inline std::string market_json(int index, bool null_expiration_value = false) {
	const std::string spread = std::to_string(index) + ".5";
	std::string json =
		R"({"can_close_early":true,"close_time":"2026-09-30T19:00:00Z","created_time":")";
	json += timestamp(index);
	json += R"(","custom_strike":{"football_team":")";
	json += uuid(static_cast<std::uint64_t>(index) + 500000);
	json += R"("},"early_close_condition":)"
			R"("This market will close and expire after a winner is declared.",)"
			R"("event_ticker":"KXNCAAFSPREAD-26SEP26NORHIL","exchange_index":0,)"
			R"("expected_expiration_time":"2026-09-27T02:30:00Z",)"
			R"("expiration_time":"2026-10-11T02:30:00Z","expiration_value":)";
	json += null_expiration_value ? "null" : R"("")";
	json += R"(,"floor_strike":)";
	json += spread;
	json += R"(,"last_price_dollars":"0.4300","latest_expiration_time":"2026-10-11T02:30:00Z",)"
			R"("liquidity_dollars":"0.0000","market_type":"binary","no_ask_dollars":"0.5800",)"
			R"("no_bid_dollars":"0.5600","no_sub_title":"Northfield by )";
	json += spread;
	json += R"(+","notional_value_dollars":"1.0000","occurrence_datetime":"2026-09-27T02:30:00Z",)"
			R"("open_interest_fp":"1813.66","open_time":"2026-09-25T14:00:00Z",)"
			R"("previous_price_dollars":"0.4100","previous_yes_ask_dollars":"0.4200",)"
			R"("previous_yes_bid_dollars":"0.4000","price_level_structure":"linear_cent",)"
			R"("price_ranges":[{"end":"1.0000","start":"0.0000","step":"0.0100"}],"result":"",)"
			R"("rules_primary":"If Northfield wins by more than )";
	json += spread;
	json += R"( points in the Northfield vs Hillcrest college football game )"
			R"(originally scheduled for Sep 26, 2026, then the market resolves to Yes. )"
			R"(Overtime counts toward the final score.",)"
			R"("rules_secondary":"The outcome is determined by the final score reported by )"
			R"(the league's official statistics provider. If the game is postponed and not )"
			R"(completed within 14 days of its original date, the market resolves to No. )"
			R"(Kalshi is not affiliated with, endorsed by, or connected to either school, )"
			R"(their conference, or the league. All trademarks belong to their owners.",)"
			R"("settlement_timer_seconds":300,"status":"active","strike_type":"greater",)"
			R"("ticker":"KXNCAAFSPREAD-26SEP26NORHIL-NOR)";
	json += std::to_string(index);
	json += R"(","title":"Northfield wins by more than )";
	json += spread;
	json += R"( points?","updated_time":")";
	json += timestamp(index + 17);
	json += R"(","volume_24h_fp":"56.00","volume_fp":"2249.06","yes_ask_dollars":"0.4400",)"
			R"("yes_ask_size_fp":"30.00","yes_bid_dollars":"0.4200","yes_bid_size_fp":"313.00",)"
			R"("yes_sub_title":"Northfield by )";
	json += spread;
	json += R"(+"})";
	return json;
}

/// Which markets in a page carry a null where the spec forbids one.
enum class Nulls { None, Last, All };

/// A GET /markets body holding markets `first` to `first + count - 1`.
inline std::string markets_body(int first, int count, std::string_view cursor, Nulls nulls) {
	std::string body = R"({"cursor":")";
	body += cursor;
	body += R"(","markets":[)";
	for (int i = first; i < first + count; ++i) {
		const bool null_field =
			nulls == Nulls::All || (nulls == Nulls::Last && i == first + count - 1);
		body += i == first ? "" : ",";
		body += market_json(i, null_field);
	}
	return body + "]}";
}

inline std::string markets_page(int count, Nulls nulls = Nulls::None) {
	return markets_body(0, count, page_cursor(1), nulls);
}

/// A page body and the cursor that requests it, empty for the first page.
struct Page {
	std::string cursor;
	std::string body;
};

/// `pages` linked pages of `per_page` markets; the last has an empty cursor.
inline std::vector<Page> market_pages(int pages, int per_page) {
	std::vector<Page> out;
	for (int page = 0; page < pages; ++page) {
		const std::string next = page + 1 < pages ? page_cursor(page + 1) : "";
		out.push_back({page == 0 ? "" : page_cursor(page),
					   markets_body(page * per_page, per_page, next, Nulls::None)});
	}
	return out;
}

/// Weather tickers of 20 to 23 characters, either side of libc++'s inline limit.
inline std::string order_ticker(int index) {
	constexpr std::array<std::string_view, 4> tickers{
		"KXHIGHNY-26OCT02-T64", "KXHIGHCHI-26OCT02-B71.5", "KXHIGHAUS-26OCT02-T93",
		"KXHIGHMIA-26OCT02-B86.5"};
	return std::string{tickers[static_cast<std::size_t>(index) % tickers.size()]};
}

/// One order as GET /portfolio/orders returns it, cycling through resting,
/// canceled, and executed orders on both sides.
inline std::string order_json(int index) {
	const bool yes = index % 2 == 0;
	const int cents = 30 + index % 40;
	const std::string yes_price = "0." + std::to_string(cents) + "00";
	const std::string no_price = "0." + std::to_string(100 - cents) + "00";
	std::string_view status = "resting";
	std::string_view filled = "2.00";
	std::string_view remaining = "8.00";
	if (index % 4 == 1) {
		status = "canceled";
		filled = "0.00";
		remaining = "0.00";
	} else if (index % 4 == 2) {
		status = "executed";
		filled = "10.00";
		remaining = "0.00";
	}
	std::string json = R"({"action":"buy","book_side":")";
	json += yes ? "bid" : "ask";
	json += R"(","cancel_order_on_pause":false,"client_order_id":")";
	json += uuid(static_cast<std::uint64_t>(index) + 1000000);
	json += R"(","created_time":")";
	json += timestamp(index);
	json += R"(","exchange_index":0,"expiration_time":null,"fill_count_fp":")";
	json += filled;
	json += R"(","initial_count_fp":"10.00","last_update_time":")";
	json += timestamp(index + 90);
	json += R"(","maker_fees_dollars":"0.0000","maker_fill_cost_dollars":"0.8400",)"
			R"("no_price_dollars":")";
	json += no_price;
	json += R"(","order_group_id":null,"order_id":")";
	json += uuid(static_cast<std::uint64_t>(index));
	json += R"(","outcome_side":")";
	json += yes ? "yes" : "no";
	json += R"(","remaining_count_fp":")";
	json += remaining;
	json += R"(","self_trade_prevention_type":"taker_at_cross","side":")";
	json += yes ? "yes" : "no";
	json += R"(","status":")";
	json += status;
	json += R"(","subaccount_number":0,"taker_fees_dollars":"0.0100",)"
			R"("taker_fill_cost_dollars":"0.0000","ticker":")";
	json += order_ticker(index);
	json += R"(","type":"limit","user_id":")";
	json += uuid(0xACC0U); // one account
	json += R"(","yes_price_dollars":")";
	json += yes_price;
	json += R"("})";
	return json;
}

inline std::string orders_page(int count) {
	std::string body = R"({"cursor":")" + page_cursor(1) + R"(","orders":[)";
	for (int i = 0; i < count; ++i) {
		body += i == 0 ? "" : ",";
		body += order_json(i);
	}
	return body + "]}";
}

/// A batch create request with a client order ID on every order, as bots send them.
inline kalshi::BatchCreateOrdersV2Request batch_create_request(int count) {
	kalshi::BatchCreateOrdersV2Request payload;
	payload.orders.reserve(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i) {
		kalshi::CreateOrderV2Request order;
		order.ticker = order_ticker(i);
		order.client_order_id = uuid(static_cast<std::uint64_t>(i) + 2000000);
		order.side = i % 2 == 0 ? kalshi::BookSide::Bid : kalshi::BookSide::Ask;
		order.count = std::to_string(1 + i % 10) + ".00";
		order.price = "0." + std::to_string(30 + i % 40) + "00";
		order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
		order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
		payload.orders.push_back(std::move(order));
	}
	return payload;
}

/// Endpoint costs that differ from the default, from the rate-limit notes in
/// spec/openapi.yaml.
inline kalshi::RateLimitConfig rate_limits() {
	using kalshi::HttpMethod;
	kalshi::RateLimitConfig config;
	config.costs = {
		{HttpMethod::GET, "/trade-api/v2/portfolio/orders/{order_id}", 2.0},
		{HttpMethod::DEL, "/trade-api/v2/portfolio/events/orders", 2.0},
		{HttpMethod::DEL, "/trade-api/v2/portfolio/events/orders/batched", 2.0},
		{HttpMethod::DEL, "/trade-api/v2/portfolio/events/orders/{order_id}", 2.0},
		{HttpMethod::GET, "/trade-api/v2/communications/rfqs/{rfq_id}/quotes/{quote_id}", 2.0},
		{HttpMethod::DEL, "/trade-api/v2/communications/rfqs/{rfq_id}/quotes/{quote_id}", 2.0},
		{HttpMethod::POST, "/trade-api/v2/communications/quotes", 2.0},
		{HttpMethod::GET, "/trade-api/v2/communications/quotes/{quote_id}", 2.0},
		{HttpMethod::DEL, "/trade-api/v2/communications/quotes/{quote_id}", 2.0},
		{HttpMethod::POST, "/trade-api/v2/account/api_usage_level/upgrade", 30.0},
	};
	return config;
}

// WebSocket data frames, modeled on the AsyncAPI examples with live ticker
// lengths.

inline constexpr std::string_view kTradeFrame =
	R"({"type":"trade","sid":11,"seq":2,"msg":{"trade_id":"5f0c2a91-7d3e-4b8a-a6f2-1c9e0b7d4a63",)"
	R"("market_ticker":"KXNCAAFGAME-26SEP26NORHIL-NOR","yes_price_dollars":"0.4400",)"
	R"("no_price_dollars":"0.5600","count_fp":"43.98","taker_side":"yes",)"
	R"("taker_outcome_side":"yes","taker_book_side":"bid","is_block_trade":false,)"
	R"("ts":1790363780,"ts_ms":1790363780921}})";

inline constexpr std::string_view kDeltaFrame =
	R"({"type":"orderbook_delta","sid":2,"seq":501,"msg":{)"
	R"("market_ticker":"KXBTCD-26SEP2517-T114999.99",)"
	R"("market_id":"9b0f6b43-5b68-4f9f-9f02-9a2d1b8ac1a1","price_dollars":"0.4200",)"
	R"("delta_fp":"-30.00","side":"yes","ts":"2026-09-25T19:16:20.921884Z",)"
	R"("ts_ms":1790363780921}})";

inline constexpr std::string_view kFillFrame =
	R"({"type":"fill","sid":13,"msg":{"trade_id":"d91bc706-ee49-470d-82d8-11418bda6fed",)"
	R"("order_id":"ee587a1c-8b87-4dcf-b721-9f6f790619fa",)"
	R"("client_order_id":"3c1f9a52-6d0e-4b7a-9c85-0e2d4f6a7b18",)"
	R"("market_ticker":"KXHIGHNY-26OCT02-T64","exchange_index":0,"is_taker":true,)"
	R"("side":"yes","yes_price_dollars":"0.4200","count_fp":"3.00","fee_cost":"0.0200",)"
	R"("action":"buy","ts":1790363780,"ts_ms":1790363780921,"post_position_fp":"10.00",)"
	R"("purchased_side":"yes","outcome_side":"yes","book_side":"bid","subaccount":0}})";

inline constexpr std::string_view kTickerFrame =
	R"({"type":"ticker","sid":11,"msg":{"market_id":"9b0f6b43-5b68-4f9f-9f02-9a2d1b8ac1a1",)"
	R"("market_ticker":"KXBTCD-26SEP2517-T114999.99","price_dollars":"0.4800",)"
	R"("yes_bid_dollars":"0.4500","yes_ask_dollars":"0.5300","volume_fp":"33896.00",)"
	R"("open_interest_fp":"20422.00","dollar_volume":16948,"dollar_open_interest":10211,)"
	R"("yes_bid_size_fp":"300.00","yes_ask_size_fp":"150.00","last_trade_size_fp":"25.00",)"
	R"("ts":1790363780,"ts_ms":1790363780921,"time":"2026-09-25T19:16:20Z"}})";

} // namespace fixtures
