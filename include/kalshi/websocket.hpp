#pragma once

#include "kalshi/error.hpp"
#include "kalshi/models/market.hpp"
#include "kalshi/signer.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kalshi {

/// WebSocket channels available for subscription
enum class Channel : std::uint8_t { OrderbookDelta, Trade, Fill, MarketLifecycle };

/// Convert channel to string for API
[[nodiscard]] constexpr std::string_view to_string(Channel ch) noexcept {
	switch (ch) {
		case Channel::OrderbookDelta:
			return "orderbook_delta";
		case Channel::Trade:
			return "trade";
		case Channel::Fill:
			return "fill";
		case Channel::MarketLifecycle:
			return "market_lifecycle";
	}
	return "orderbook_delta";
}

/// Orderbook snapshot message
struct OrderbookSnapshot {
	std::int32_t sid{0};
	std::int32_t seq{0};
	std::string market_ticker;
	std::vector<OrderBookEntry> yes;
	std::vector<OrderBookEntry> no;
};

/// Orderbook delta message
struct OrderbookDelta {
	std::int32_t sid{0};
	std::int32_t seq{0};
	std::string market_ticker;
	std::int32_t price{0};
	std::int32_t delta{0};
	Side side{Side::Yes};
};

/// Trade message from WebSocket
struct WsTrade {
	std::int32_t sid{0};
	std::string trade_id;
	std::string market_ticker;
	std::int32_t yes_price{0};
	std::int32_t no_price{0};
	std::int32_t count{0};
	Side taker_side{Side::Yes};
	std::int64_t timestamp{0};
};

/// Fill message (user's order was filled)
struct WsFill {
	std::int32_t sid{0};
	std::string trade_id;
	std::string order_id;
	std::string market_ticker;
	bool is_taker{false};
	Side side{Side::Yes};
	std::int32_t yes_price{0};
	std::int32_t no_price{0};
	std::int32_t count{0};
	Action action{Action::Buy};
	std::int64_t timestamp{0};
};

/// Market lifecycle message
struct MarketLifecycle {
	std::int32_t sid{0};
	std::string market_ticker;
	std::int64_t open_ts{0};
	std::int64_t close_ts{0};
	std::optional<std::int64_t> determination_ts;
	std::optional<std::int64_t> settled_ts;
	std::optional<std::string> result;
	bool is_deactivated{false};
};

/// Union of all possible WebSocket data messages
using WsMessage = std::variant<OrderbookSnapshot, OrderbookDelta, WsTrade, WsFill, MarketLifecycle>;

/// Subscription ID returned when subscribing
struct SubscriptionId {
	std::int32_t sid{0};
	Channel channel{Channel::OrderbookDelta};
};

/// WebSocket error
struct WsError {
	std::int32_t code{0};
	std::string message;
};

/// Callback for WebSocket messages
using WsMessageCallback = std::function<void(const WsMessage&)>;

/// Callback for WebSocket errors
using WsErrorCallback = std::function<void(const WsError&)>;

/// Callback for connection state changes
using WsStateCallback = std::function<void(bool connected)>;

/// WebSocket client configuration
struct WsConfig {
	std::string url{"wss://api.elections.kalshi.com/trade-api/ws/v2"};
	std::chrono::seconds reconnect_delay{5};
	std::uint16_t max_reconnect_attempts{10}; ///< Max reconnect attempts (0-65535, default 10)
	bool auto_reconnect{true};
};

/// WebSocket streaming client for Kalshi
///
/// Provides real-time market data via WebSocket connection.
/// Based on the TypeScript SDK's KalshiStream implementation.
class WebSocketClient {
public:
	/// Create a WebSocket client with authentication
	WebSocketClient(const Signer& signer, WsConfig config = {});
	~WebSocketClient();

	WebSocketClient(WebSocketClient&&) noexcept;
	WebSocketClient& operator=(WebSocketClient&&) noexcept;

	// Non-copyable
	WebSocketClient(const WebSocketClient&) = delete;
	WebSocketClient& operator=(const WebSocketClient&) = delete;

	/// Connect to the WebSocket server
	[[nodiscard]] Result<void> connect();

	/// Disconnect from the server
	void disconnect();

	/// Check if connected
	[[nodiscard]] bool is_connected() const noexcept;

	/// Subscribe to orderbook updates for specific markets
	[[nodiscard]] Result<SubscriptionId>
	subscribe_orderbook(const std::vector<std::string>& market_tickers);

	/// Subscribe to trades (optionally filtered by markets)
	[[nodiscard]] Result<SubscriptionId>
	subscribe_trades(const std::vector<std::string>& market_tickers = {});

	/// Subscribe to fills for the authenticated user
	[[nodiscard]] Result<SubscriptionId>
	subscribe_fills(const std::vector<std::string>& market_tickers = {});

	/// Subscribe to market lifecycle events
	[[nodiscard]] Result<SubscriptionId> subscribe_lifecycle();

	/// Unsubscribe from a subscription
	[[nodiscard]] Result<void> unsubscribe(SubscriptionId sub_id);

	/// Add markets to an existing subscription
	[[nodiscard]] Result<void> add_markets(SubscriptionId sub_id,
										   const std::vector<std::string>& market_tickers);

	/// Remove markets from an existing subscription
	[[nodiscard]] Result<void> remove_markets(SubscriptionId sub_id,
											  const std::vector<std::string>& market_tickers);

	/// Set callback for incoming messages
	void on_message(WsMessageCallback callback);

	/// Set callback for errors
	void on_error(WsErrorCallback callback);

	/// Set callback for connection state changes
	void on_state_change(WsStateCallback callback);

	/// Get the configuration
	[[nodiscard]] const WsConfig& config() const noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace kalshi
