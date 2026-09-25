#pragma once

#include "kalshi/environment.hpp"
#include "kalshi/error.hpp"
#include "kalshi/signer.hpp"
#include "kalshi/ws_models.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kalshi {

/// Everything `on_message` delivers: channel data as `ws::Update<T>`, and
/// subscription events. docs/channels.md lists the types per channel.
using WsMessage = ws::Message;

/// An error frame from the server, or a problem the client found itself.
struct WsError {
	/// Kalshi's error code (see `ws::error_code_name`), or 0 for a client error.
	std::int64_t code{0};
	std::string message;
	/// The command that failed, when the server names one.
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	/// The subscription the error concerns, when known.
	std::optional<ws::Subscription> subscription;
};

enum class WsState : std::uint8_t { Disconnected, Connecting, Connected, Reconnecting };

[[nodiscard]] constexpr std::string_view to_string(WsState state) noexcept {
	switch (state) {
		case WsState::Disconnected:
			return "disconnected";
		case WsState::Connecting:
			return "connecting";
		case WsState::Connected:
			return "connected";
		case WsState::Reconnecting:
			return "reconnecting";
	}
	return "";
}

struct WsConfig {
	std::string url{websocket_url(Environment::Production)};
	/// How long `connect()` waits for the handshake.
	std::chrono::milliseconds connect_timeout{std::chrono::seconds{10}};
	/// Reconnect and resubscribe after the connection drops.
	bool auto_reconnect{true};
	/// The first reconnect waits about this long; each failure doubles the
	/// wait, with jitter, up to `max_reconnect_delay`.
	std::chrono::milliseconds reconnect_delay{std::chrono::milliseconds{500}};
	std::chrono::milliseconds max_reconnect_delay{std::chrono::seconds{30}};
	/// Stop after this many failed attempts in a row; 0 keeps trying. A
	/// connection that drops within 10 seconds of opening counts as failed.
	std::uint32_t max_reconnect_attempts{0};
	/// Ping after this much silence, and drop the connection after
	/// `idle_timeout` without a reply. Kalshi pings every 10 seconds.
	std::chrono::seconds ping_interval{std::chrono::seconds{15}};
	std::chrono::seconds idle_timeout{std::chrono::seconds{30}};
	/// Request fresh order book snapshots when an orderbook_delta sequence
	/// number is skipped. The gap is reported to `on_error` either way.
	bool resync_on_gap{true};

	[[nodiscard]] static WsConfig for_environment(Environment environment) {
		WsConfig config;
		config.url = std::string(websocket_url(environment));
		return config;
	}
};

/// Streams Kalshi's WebSocket channels.
///
/// Subscriptions survive reconnects: the client resubscribes with the same
/// parameters and keeps each `ws::Subscription` handle, while the server's
/// `sid` changes. Callbacks run on the client's network thread, which blocks
/// SIGPIPE, except the Disconnected state change from `disconnect()`, which
/// runs on the caller's.
/// They may call any method, including destroying the client, but should
/// return quickly. Every method is thread-safe.
class WebSocketClient {
public:
	/// Creates a client that authenticates with a copy of `signer`.
	explicit WebSocketClient(Signer signer, WsConfig config = {});
	~WebSocketClient();

	WebSocketClient(WebSocketClient&&) noexcept;
	WebSocketClient& operator=(WebSocketClient&&) noexcept;
	WebSocketClient(const WebSocketClient&) = delete;
	WebSocketClient& operator=(const WebSocketClient&) = delete;

	/// Opens the connection and waits up to `connect_timeout` for the
	/// handshake. Subscriptions made before the call are sent once connected.
	[[nodiscard]] Result<void> connect();

	/// Closes the connection and forgets every subscription. `connect()` can
	/// open a new session afterwards.
	void disconnect();

	[[nodiscard]] WsState state() const noexcept;
	[[nodiscard]] bool is_connected() const noexcept;

	/// Subscribes to one channel. The handle is usable at once: updates and
	/// unsubscribes made before the server confirms are sent after it does.
	[[nodiscard]] Result<ws::Subscription> subscribe(ws::Channel channel,
													 ws::SubscribeParams params = {});
	[[nodiscard]] Result<void> unsubscribe(ws::Subscription subscription);
	[[nodiscard]] Result<void> update_subscription(ws::Subscription subscription,
												   const ws::UpdateSubscriptionParams& params);
	[[nodiscard]] Result<void> add_markets(ws::Subscription subscription,
										   std::vector<std::string> market_tickers);
	[[nodiscard]] Result<void> remove_markets(ws::Subscription subscription,
											  std::vector<std::string> market_tickers);
	/// Asks for order book snapshots of markets in an orderbook_delta
	/// subscription, without changing it.
	[[nodiscard]] Result<void> request_snapshot(ws::Subscription subscription,
												std::vector<std::string> market_tickers);
	/// Asks the server for its view of this connection's subscriptions. The
	/// reply arrives as `ws::SubscriptionList` carrying the returned ID.
	[[nodiscard]] Result<std::int64_t> list_subscriptions();
	/// The subscriptions this client holds.
	[[nodiscard]] std::vector<ws::Subscription> subscriptions() const;

	void on_message(std::function<void(const WsMessage&)> callback);
	void on_error(std::function<void(const WsError&)> callback);
	void on_state_change(std::function<void(WsState)> callback);

	[[nodiscard]] const WsConfig& config() const noexcept;

private:
	struct Impl;
	std::shared_ptr<Impl> impl_;
};

} // namespace kalshi
