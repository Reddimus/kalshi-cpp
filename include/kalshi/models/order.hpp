#pragma once

#include "kalshi/models/market.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace kalshi {

/// Order type
enum class OrderType : std::uint8_t { Limit, Market };

/// Time in force for orders
enum class TimeInForce : std::uint8_t {
	GTC, // Good til cancelled
	IOC, // Immediate or cancel
	FOK	 // Fill or kill
};

/// Order status
enum class OrderStatus : std::uint8_t { Pending, Open, Filled, Cancelled, PartiallyFilled };

/// Order request (for creating new orders)
struct OrderRequest {
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	OrderType type{OrderType::Limit};
	std::int32_t count{0};			   // Number of contracts
	std::optional<std::int32_t> price; // Price in cents (required for limit orders)
	TimeInForce tif{TimeInForce::GTC};
	std::optional<std::int64_t> expiration_ts; // Unix ms for GTC orders
};

/// Order (existing order from API)
/// Memory layout optimized: 8-byte fields first, then 4-byte, then 1-byte enums, strings last
struct Order {
	// 8-byte aligned fields
	std::int64_t created_time{0};
	std::optional<std::int64_t> expiration_ts;

	// 4-byte fields grouped together
	std::int32_t initial_count{0};
	std::int32_t remaining_count{0};
	std::int32_t filled_count{0};
	std::int32_t price{0};

	// 1-byte enums packed together (4 bytes total with alignment)
	Side side{Side::Yes};
	Action action{Action::Buy};
	OrderType type{OrderType::Limit};
	OrderStatus status{OrderStatus::Pending};

	// Strings last (have internal pointers, variable size)
	std::string order_id;
	std::string market_ticker;
};

/// Trade execution
/// Memory layout optimized: 8-byte fields first, then 4-byte, then 1-byte, strings last
struct Trade {
	// 8-byte aligned field
	std::int64_t timestamp{0};

	// 4-byte fields grouped together
	std::int32_t count{0};
	std::int32_t price{0};

	// 1-byte fields packed together
	Side side{Side::Yes};
	Action action{Action::Buy};
	bool is_taker{false};

	// Strings last (have internal pointers, variable size)
	std::string trade_id;
	std::string order_id;
	std::string market_ticker;
};

} // namespace kalshi
