#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kalshi {

/// Side of a position or order (yes/no for binary markets)
enum class Side : std::uint8_t { Yes, No };

/// Order action (buy or sell)
enum class Action : std::uint8_t { Buy, Sell };

/// Normalized outcome exposure. Yes = buy-yes ∨ sell-no; No = buy-no ∨ sell-yes.
/// Added to Kalshi v2 API on 2026-05-06 — eventually intended to replace the
/// (side, action) pair, but for now they coexist.
enum class OutcomeSide : std::uint8_t { Yes, No };

/// Normalized orderbook side. Bid = OutcomeSide::Yes; Ask = OutcomeSide::No.
/// Companion to OutcomeSide, also new on the v2 response surface 2026-05-06.
enum class BookSide : std::uint8_t { Bid, Ask };

/// Derive OutcomeSide from the (side, action) pair (offline; useful when the
/// server response predates the 2026-05-06 fields or when constructing an
/// outgoing request).
[[nodiscard]] constexpr OutcomeSide derive_outcome_side(Side side, Action action) noexcept {
	const bool yes_exposure = (side == Side::Yes && action == Action::Buy) ||
							  (side == Side::No && action == Action::Sell);
	return yes_exposure ? OutcomeSide::Yes : OutcomeSide::No;
}

/// Derive BookSide from the (side, action) pair. Bid ≡ OutcomeSide::Yes.
[[nodiscard]] constexpr BookSide derive_book_side(Side side, Action action) noexcept {
	return derive_outcome_side(side, action) == OutcomeSide::Yes ? BookSide::Bid : BookSide::Ask;
}

/// Market status
enum class MarketStatus : std::uint8_t { Open, Closed, Settled, Unopened, Paused };

/// Price-quantity pair in an order book
struct OrderBookEntry {
	std::int32_t price_cents; // 1-99 for binary markets
	std::int32_t quantity;
	std::string price_dollars;
	std::string quantity_fp;
};

/// Order book for a market
struct OrderBook {
	std::string market_ticker;
	std::vector<OrderBookEntry> yes_bids;
	std::vector<OrderBookEntry> no_bids;
};

/// Market information
/// Memory layout optimized: 8-byte fields first, then 4-byte, then 1-byte, strings last
struct Market {
	// 8-byte aligned fields
	std::int64_t open_time{0};
	std::int64_t close_time{0};
	std::optional<std::int64_t> expected_expiration_time;
	std::optional<std::int64_t> expiration_time;
	std::optional<std::int64_t> latest_expiration_time;
	std::optional<std::int64_t> settlement_ts;

	// 4-byte fields grouped together
	std::int32_t yes_bid{0};
	std::int32_t yes_ask{0};
	std::int32_t no_bid{0};
	std::int32_t no_ask{0};
	std::int32_t volume{0};
	std::int32_t open_interest{0};
	std::optional<std::int32_t> settlement_timer_seconds;
	std::optional<std::int32_t> settlement_value_cents;
	std::int32_t exchange_index{0};

	// 1-byte enum
	MarketStatus status{MarketStatus::Open};

	// Strings last (have internal pointers, variable size)
	std::string ticker;
	std::string event_ticker;
	std::string market_type;
	std::string title;
	std::string subtitle;
	std::string yes_sub_title;
	std::string no_sub_title;
	std::string yes_bid_dollars;
	std::string yes_ask_dollars;
	std::string no_bid_dollars;
	std::string no_ask_dollars;
	std::string last_price_dollars;
	std::string volume_fp;
	std::string volume_24h_fp;
	std::string open_interest_fp;
	std::string notional_value_dollars;
	std::string previous_yes_bid_dollars;
	std::string previous_yes_ask_dollars;
	std::string previous_price_dollars;
	std::string settlement_value_dollars;
	std::string price_level_structure;
	std::string rules_primary;
	std::string rules_secondary;
	std::optional<std::string> expiration_value;
	std::optional<std::string> result; // "yes", "no", or nullopt if not settled
};

/// User position in a market
struct Position {
	std::string market_ticker;
	std::string position_fp;
	std::string total_traded_dollars;
	std::string market_exposure_dollars;
	std::string realized_pnl_dollars;
	std::string fees_paid_dollars;
	std::int32_t exchange_index{0};
	std::int32_t yes_contracts{0};
	std::int32_t no_contracts{0};
	std::int32_t total_cost_cents{0};
};

} // namespace kalshi
