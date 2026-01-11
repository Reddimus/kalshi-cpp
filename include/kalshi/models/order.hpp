#pragma once

#include "kalshi/models/market.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace kalshi {

/// Order type
enum class OrderType { Limit, Market };

/// Time in force for orders
enum class TimeInForce {
	GTC, // Good til cancelled
	IOC, // Immediate or cancel
	FOK  // Fill or kill
};

/// Order status
enum class OrderStatus { Pending, Open, Filled, Cancelled, PartiallyFilled };

/// Order request (for creating new orders)
struct OrderRequest {
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	OrderType type{OrderType::Limit};
	std::int32_t count{0};             // Number of contracts
	std::optional<std::int32_t> price; // Price in cents (required for limit orders)
	TimeInForce tif{TimeInForce::GTC};
	std::optional<std::int64_t> expiration_ts; // Unix ms for GTC orders
};

/// Order (existing order from API)
struct Order {
	std::string order_id;
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	OrderType type{OrderType::Limit};
	OrderStatus status{OrderStatus::Pending};
	std::int32_t initial_count{0};
	std::int32_t remaining_count{0};
	std::int32_t filled_count{0};
	std::int32_t price{0};
	std::int64_t created_time{0};
	std::optional<std::int64_t> expiration_ts;
};

/// Trade execution
struct Trade {
	std::string trade_id;
	std::string order_id;
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	std::int32_t count{0};
	std::int32_t price{0};
	std::int64_t timestamp{0};
	bool is_taker{false};
};

} // namespace kalshi
