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
			return "market_lifecycle_v2";
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
	/// Set when the upstream `metadata_updated` lifecycle event reports a
	/// yes-side subtitle change. Added to the v2 channel 2026-05-11.
	/// Nullopt when the frame omits the field (most lifecycle frames).
	std::optional<std::string> yes_sub_title;
};

/// Kalshi's `market_lifecycle_v2` channel multiplexes several sub-event
/// types onto one flat frame shape (no explicit `event_type` field on the
/// wire as of 2026-05-14). This enum mirrors the documented sub-events
/// from docs.kalshi.com/changelog so consumers can classify a frame by
/// which fields are populated. Returned by `classify_lifecycle_event`.
enum class LifecycleEventType : std::uint8_t {
	/// Default / not yet classified — fields all carry their defaults.
	Unknown,
	/// `settled_ts` is set — positions have been finalized.
	Settled,
	/// `determination_ts` is set (but not yet settled).
	Determined,
	/// `is_deactivated` is true — market stopped accepting orders.
	Deactivated,
	/// `yes_sub_title` is set — yes-side subtitle changed (e.g. floor
	/// strike rolled forward on a temperature contract). Added 2026-05-11.
	MetadataUpdated,
	/// `open_ts` or `close_ts` is non-zero — market created / activated.
	/// Covers both the `created` and `activated` upstream sub-events; the
	/// wire format doesn't distinguish them flat-encoded.
	OpenOrCreated,
};

/// Classify a flat MarketLifecycle frame by inspecting which fields are
/// populated. Resolves the upstream sub-event ambiguity by precedence:
/// Settled > Determined > Deactivated > MetadataUpdated > OpenOrCreated
/// > Unknown. The precedence reflects the lifecycle progression — once a
/// market settles, prior fields are kept for reference but the frame is
/// principally a settle event.
[[nodiscard]] constexpr LifecycleEventType
classify_lifecycle_event(const MarketLifecycle& lc) noexcept {
	if (lc.settled_ts.has_value()) {
		return LifecycleEventType::Settled;
	}
	if (lc.determination_ts.has_value()) {
		return LifecycleEventType::Determined;
	}
	if (lc.is_deactivated) {
		return LifecycleEventType::Deactivated;
	}
	if (lc.yes_sub_title.has_value()) {
		return LifecycleEventType::MetadataUpdated;
	}
	if (lc.open_ts != 0 || lc.close_ts != 0) {
		return LifecycleEventType::OpenOrCreated;
	}
	return LifecycleEventType::Unknown;
}

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

/// Map a Kalshi WebSocket error code to the canonical name documented at
/// https://docs.kalshi.com/websockets/websocket-connection#error-messages.
/// Returns "Unknown error code" for codes outside the documented range.
/// Names mirror Kalshi's AsyncAPI spec snapshot 2026-05-12 (codes 1-22
/// from the original v2 spec + code 25 added 2026-05-12 for subscription
/// buffer overflow). Codes 23, 24, 26+ are currently undefined upstream.
[[nodiscard]] constexpr std::string_view ws_error_code_name(std::int32_t code) noexcept {
	switch (code) {
		case 1:
			return "Unable to process message";
		case 2:
			return "Params required";
		case 3:
			return "Channels required";
		case 4:
			return "Subscription IDs required";
		case 5:
			return "Unknown command";
		case 6:
			return "Already subscribed";
		case 7:
			return "Unknown subscription ID";
		case 8:
			return "Unknown channel name";
		case 9:
			return "Authentication required";
		case 10:
			return "Channel error";
		case 11:
			return "Invalid parameter";
		case 12:
			return "Exactly one subscription ID required";
		case 13:
			return "Unsupported action";
		case 14:
			return "Market Ticker required";
		case 15:
			return "Action required";
		case 16:
			return "Market not found";
		case 17:
			return "Internal error";
		case 18:
			return "Command timeout";
		case 19:
			return "shard_factor validation";
		case 20:
			return "shard_factor dependency";
		case 21:
			return "shard_key validation";
		case 22:
			return "shard_factor limit";
		case 25:
			return "Subscription buffer overflow";
		default:
			return "Unknown error code";
	}
}

/// Callback for WebSocket messages
using WsMessageCallback = std::function<void(const WsMessage&)>;

/// Callback for WebSocket errors
using WsErrorCallback = std::function<void(const WsError&)>;

/// Callback for connection state changes
using WsStateCallback = std::function<void(bool connected)>;

/// WebSocket client configuration
struct WsConfig {
	std::string url{"wss://external-api-ws.kalshi.com/trade-api/ws/v2"};
	/// Verify WSS server certificates and hostnames. Defaults to ``true``;
	/// set to ``false`` only for self-signed test servers.
	bool verify_ssl{true};
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
