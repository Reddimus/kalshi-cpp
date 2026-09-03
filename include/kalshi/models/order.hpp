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
	/// Matching-engine wall-clock timestamp (Unix epoch ms) at which the
	/// mutation that produced this response was processed. Populated by
	/// V2 order-mutating endpoints (create / amend / decrease / batch_create
	/// / batch_cancel) starting 2026-05-05. Nullopt when the response
	/// doesn't carry it (e.g. `get_order` / `get_orders` reads, or pre-
	/// 2026-05-05 servers).
	std::optional<std::int64_t> mutation_ts_ms;

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
	std::int32_t exchange_index{0};
	std::string client_order_id;
	std::string yes_price_dollars;
	std::string no_price_dollars;
	std::string fill_count_fp;
	std::string remaining_count_fp;
	std::string initial_count_fp;
	std::string taker_fill_cost_dollars;
	std::string maker_fill_cost_dollars;
	std::string taker_fees_dollars;
	std::string maker_fees_dollars;
	OutcomeSide outcome_side{OutcomeSide::Yes};
	BookSide book_side{BookSide::Bid};
	std::string user_id;
	std::string created_time_iso;
	std::string expiration_time;
	std::string last_update_time;
	std::string self_trade_prevention_type;
	std::string order_group_id;
	std::string average_fill_price;
	std::string average_fee_paid;
	/// True when the canonical direction came from the response or mutation request.
	bool has_canonical_direction{false};
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
