/// @file live_market_view.hpp
/// @brief Helper for maintaining a live view of market data from WebSocket streams
///
/// Maintains per-ticker state for:
/// - Best bid/ask (from orderbook snapshots + deltas)
/// - Last trade (from trade channel)

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <kalshi/websocket.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kalshi {

/// State for a single market's live view
struct LiveMarketState {
	std::string ticker;

	// Best bid/ask (top-of-book) for YES side
	std::optional<std::int32_t> best_bid_price; // Highest bid price (cents)
	std::optional<std::int32_t> best_bid_size;	// Size at best bid
	std::optional<std::int32_t> best_ask_price; // Lowest ask price (cents)
	std::optional<std::int32_t> best_ask_size;	// Size at best ask

	// Last trade info
	std::optional<std::int32_t> last_trade_price; // yes_price of last trade (cents)
	std::optional<std::int32_t> last_trade_size;  // count of last trade
	std::optional<Side> last_trade_taker_side;	  // Taker side of last trade
	std::optional<std::int64_t> last_trade_ts;	  // Timestamp of last trade

	// Sequence tracking for orderbook
	std::int32_t last_seq{0};

	// Internal orderbook depth (yes side only for simplicity)
	std::map<std::int32_t, std::int32_t> yes_bids; // price -> quantity
	std::map<std::int32_t, std::int32_t> yes_asks; // price -> quantity (derived from no bids)
};

/// Manages live market state for multiple tickers
class LiveMarketView {
public:
	LiveMarketView() = default;

	/// Process a WebSocket message and update state
	void process_message(const WsMessage& msg) {
		std::visit(
			[this](auto&& m) {
				using T = std::decay_t<decltype(m)>;
				if constexpr (std::is_same_v<T, OrderbookSnapshot>) {
					handle_snapshot(m);
				} else if constexpr (std::is_same_v<T, OrderbookDelta>) {
					handle_delta(m);
				} else if constexpr (std::is_same_v<T, WsTrade>) {
					handle_trade(m);
				}
			},
			msg);
	}

	/// Get state for a specific ticker (returns nullopt if not yet received)
	[[nodiscard]] std::optional<LiveMarketState> get_state(const std::string& ticker) const {
		std::lock_guard lock(mutex_);
		auto it = states_.find(ticker);
		if (it == states_.end())
			return std::nullopt;
		return it->second;
	}

	/// Get all market states
	[[nodiscard]] std::map<std::string, LiveMarketState> get_all_states() const {
		std::lock_guard lock(mutex_);
		return states_;
	}

	/// Register a ticker to track (initializes empty state)
	void register_ticker(const std::string& ticker) {
		std::lock_guard lock(mutex_);
		if (states_.find(ticker) == states_.end()) {
			LiveMarketState state;
			state.ticker = ticker;
			states_[ticker] = std::move(state);
		}
	}

	/// Print a summary line for a single market
	static void print_market_line(const LiveMarketState& state, std::ostream& os = std::cout) {
		os << "  " << state.ticker << ": ";
		if (state.best_bid_price && state.best_ask_price) {
			os << *state.best_bid_price << "c/" << *state.best_ask_price << "c";
			if (state.best_bid_size && state.best_ask_size) {
				os << " (" << *state.best_bid_size << "x" << *state.best_ask_size << ")";
			}
		} else {
			os << "bid/ask: --";
		}
		if (state.last_trade_price) {
			os << " | Last: " << *state.last_trade_price << "c";
			if (state.last_trade_size) {
				os << " x" << *state.last_trade_size;
			}
			if (state.last_trade_taker_side) {
				os << " [" << (*state.last_trade_taker_side == Side::Yes ? "YES" : "NO") << "]";
			}
		}
		os << "\n";
	}

	/// Print all market states
	void print_all(std::ostream& os = std::cout) const {
		std::lock_guard lock(mutex_);
		for (const auto& [ticker, state] : states_) {
			print_market_line(state, os);
		}
	}

private:
	mutable std::mutex mutex_;
	std::map<std::string, LiveMarketState> states_;

	void handle_snapshot(const OrderbookSnapshot& snap) {
		std::lock_guard lock(mutex_);
		auto& state = states_[snap.market_ticker];
		state.ticker = snap.market_ticker;
		state.last_seq = snap.seq;

		// Clear and rebuild orderbook
		state.yes_bids.clear();
		state.yes_asks.clear();

		// YES bids are direct bids on the YES side
		for (const auto& entry : snap.yes) {
			if (entry.quantity > 0) {
				state.yes_bids[entry.price_cents] = entry.quantity;
			}
		}

		// NO bids at price P translate to YES asks at price (100 - P)
		for (const auto& entry : snap.no) {
			if (entry.quantity > 0) {
				std::int32_t yes_ask_price = 100 - entry.price_cents;
				state.yes_asks[yes_ask_price] = entry.quantity;
			}
		}

		update_best_bid_ask(state);
	}

	void handle_delta(const OrderbookDelta& delta) {
		std::lock_guard lock(mutex_);
		auto& state = states_[delta.market_ticker];
		state.ticker = delta.market_ticker;
		state.last_seq = delta.seq;

		if (delta.side == Side::Yes) {
			// Delta on YES side affects YES bids
			auto& book = state.yes_bids;
			book[delta.price] += delta.delta;
			if (book[delta.price] <= 0) {
				book.erase(delta.price);
			}
		} else {
			// Delta on NO side affects YES asks (100 - price)
			std::int32_t yes_ask_price = 100 - delta.price;
			auto& book = state.yes_asks;
			book[yes_ask_price] += delta.delta;
			if (book[yes_ask_price] <= 0) {
				book.erase(yes_ask_price);
			}
		}

		update_best_bid_ask(state);
	}

	void handle_trade(const WsTrade& trade) {
		std::lock_guard lock(mutex_);
		auto& state = states_[trade.market_ticker];
		state.ticker = trade.market_ticker;
		state.last_trade_price = trade.yes_price;
		state.last_trade_size = trade.count;
		state.last_trade_taker_side = trade.taker_side;
		state.last_trade_ts = trade.timestamp;
	}

	void update_best_bid_ask(LiveMarketState& state) {
		// Best bid = highest price with quantity > 0
		if (!state.yes_bids.empty()) {
			auto it = state.yes_bids.rbegin(); // highest price
			state.best_bid_price = it->first;
			state.best_bid_size = it->second;
		} else {
			state.best_bid_price = std::nullopt;
			state.best_bid_size = std::nullopt;
		}

		// Best ask = lowest price with quantity > 0
		if (!state.yes_asks.empty()) {
			auto it = state.yes_asks.begin(); // lowest price
			state.best_ask_price = it->first;
			state.best_ask_size = it->second;
		} else {
			state.best_ask_price = std::nullopt;
			state.best_ask_size = std::nullopt;
		}
	}
};

} // namespace kalshi
