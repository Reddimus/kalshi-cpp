#pragma once

// Parsing of every frame Kalshi's WebSocket server sends.

#include "kalshi/websocket.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace kalshi::detail {

struct SubscribedFrame {
	std::optional<std::int64_t> id;
	std::string channel;
	std::int64_t sid{0};
};

struct UnsubscribedFrame {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
};

/// An `ok` reply. `msg` stays raw because its shape depends on the command:
/// market lists for update_subscription, an array for list_subscriptions.
struct OkFrame {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	std::string msg;
};

struct ErrorFrame {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	std::int64_t code{0};
	std::string message;
};

/// A frame of a known type that did not match its schema.
struct MalformedFrame {
	std::string type;
};

using Frame = std::variant<std::monostate, WsMessage, SubscribedFrame, UnsubscribedFrame, OkFrame,
						   ErrorFrame, MalformedFrame>;

/// Parses one frame. std::monostate means the frame had no type this SDK
/// version knows; MalformedFrame means a known type failed to parse.
[[nodiscard]] Frame parse_frame(const std::string& json);

/// Parses a data frame, for tests and benchmarks.
[[nodiscard]] std::optional<WsMessage> parse_message(const std::string& json);

/// Reads the lists in an update_subscription `ok` reply.
[[nodiscard]] ws::Updated parse_updated(const std::string& msg);

/// Reads the array in a list_subscriptions reply.
[[nodiscard]] std::vector<ws::ListedSubscription> parse_subscription_list(const std::string& msg);

} // namespace kalshi::detail
