#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kalshi {

/// Side of a position or order (yes/no for binary markets)
enum class Side { Yes, No };

/// Order action (buy or sell)
enum class Action { Buy, Sell };

/// Market status
enum class MarketStatus { Open, Closed, Settled };

/// Price-quantity pair in an order book
struct OrderBookEntry {
	std::int32_t price_cents; // 1-99 for binary markets
	std::int32_t quantity;
};

/// Order book for a market
struct OrderBook {
	std::string market_ticker;
	std::vector<OrderBookEntry> yes_bids;
	std::vector<OrderBookEntry> no_bids;
};

/// Market information
struct Market {
	std::string ticker;
	std::string title;
	std::string subtitle;
	MarketStatus status{MarketStatus::Open};
	std::int64_t open_time{0};
	std::int64_t close_time{0};
	std::optional<std::int64_t> expiration_time;
	std::optional<std::string> result; // "yes", "no", or nullopt if not settled
	std::int32_t yes_bid{0};
	std::int32_t yes_ask{0};
	std::int32_t no_bid{0};
	std::int32_t no_ask{0};
	std::int32_t volume{0};
	std::int32_t open_interest{0};
};

/// User position in a market
struct Position {
	std::string market_ticker;
	std::int32_t yes_contracts{0};
	std::int32_t no_contracts{0};
	std::int32_t total_cost_cents{0};
};

} // namespace kalshi
