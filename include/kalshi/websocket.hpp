#pragma once

#include "kalshi/environment.hpp"
#include "kalshi/error.hpp"
#include "kalshi/signer.hpp"
#include "kalshi/ws_models.hpp"

#include <chrono>
#include <cstddef>
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
	std::optional<std::int64_t> sending_ts_ms;
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
	/// A `wss://` server's certificate must name the URL's host and chain to
	/// an authority in OpenSSL's default verify paths, which `SSL_CERT_FILE`
	/// and `SSL_CERT_DIR` override, or on Windows in the system's root store.
	/// REST calls trust what libcurl trusts, which can differ.
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
	/// Ping when no pong has arrived for `ping_interval`, and drop the
	/// connection when none arrives for `idle_timeout`, which must be longer;
	/// `connect()` rejects other values. The check runs about every five
	/// seconds, so a drop can come a few seconds late. Kalshi sends its own
	/// pings only after 10 seconds without a data message.
	std::chrono::seconds ping_interval{std::chrono::seconds{15}};
	std::chrono::seconds idle_timeout{std::chrono::seconds{30}};
	/// Request fresh order book snapshots when an orderbook_delta sequence
	/// number is skipped. The gap is reported to `on_error` either way.
	bool resync_on_gap{true};
	/// Maximum received message size; zero disables the limit.
	std::size_t max_message_bytes{std::size_t{64} * 1024 * 1024};

	[[nodiscard]] static WsConfig for_environment(Environment environment) {
		WsConfig config;
		config.url = std::string(websocket_url(environment));
		return config;
	}
};

/// Streams Kalshi's WebSocket channels.
///
/// Internal failures stop the session and clear subscriptions.
/// Subscriptions survive reconnects: the client resubscribes with the same
/// parameters and keeps each `ws::Subscription` handle, while the server's
/// `sid` changes. The resubscribe carries the updates the server confirmed.
/// Updates still unanswered or unsent when the connection dropped follow it,
/// each on its own once the new subscription is confirmed, so a rejected one
/// is reported in `on_error` and the subscription stays. Snapshot and list
/// requests are not sent again. Callbacks run on the client's network thread, which blocks
/// SIGPIPE on POSIX systems, except the Disconnected state change from
/// `disconnect()`, which runs on the caller's.
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
	/// Keep one OrderbookDelta handle per client; extend it with add_markets.
	/// A second handle is rejected while the first remains registered.
	/// `market_id` and `market_ids` are accepted only on the ticker channel.
	/// Name markets with only one of `market_ticker`, `market_tickers`,
	/// `market_id`, and `market_ids`; none may be empty, and neither may an
	/// index ID or underlying ticker. `shard_key` needs `shard_factor`, from
	/// 1 to 100, and must be below it. Anything else returns `InvalidRequest`
	/// without sending.
	/// Matches added to an ebattle_scores subscription are added again after
	/// a reconnect.
	[[nodiscard]] Result<ws::Subscription> subscribe(ws::Channel channel,
													 ws::SubscribeParams params = {});
	[[nodiscard]] Result<void> unsubscribe(ws::Subscription subscription);
	/// Rejects updates while unsubscription is pending, and market changes to a
	/// ticker subscription sent without markets, which Kalshi does not allow.
	/// Markets follow the subscribe's rules and must be named the way the
	/// subscription names them, by ticker or by ID. A subscription made
	/// without markets takes the way its first added markets use.
	/// Market actions need a market, and the index, underlying, and match
	/// actions need their lists, with no empty IDs. Each action works only on the channels the
	/// spec gives it: `get_snapshot` on orderbook_delta, the index actions on
	/// the CF Benchmarks channels, the underlying actions on pyth_value, and
	/// the match actions on ebattle_scores; those four take no market changes.
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
	/// reply arrives as `ws::SubscriptionList` carrying the returned ID, or as
	/// a `WsError` carrying it when the reply is not a valid list.
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
