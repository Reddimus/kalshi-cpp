#pragma once

// Parsing of every frame Kalshi's WebSocket server sends.

#include "kalshi/websocket.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kalshi::detail {

struct SubscribedFrame {
	std::optional<std::int64_t> id;
	std::string channel;
	std::int64_t sid{0};
	std::optional<std::int64_t> sending_ts_ms;
};

struct UnsubscribedFrame {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	std::optional<std::int64_t> sending_ts_ms;
};

/// An `ok` reply. `msg` stays raw because its shape depends on the command:
/// market lists for update_subscription, an array for list_subscriptions. It
/// is empty when the reply carries none.
struct OkFrame {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	std::string msg;
	std::optional<std::int64_t> sending_ts_ms;
};

struct ErrorFrame {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	std::int64_t code{0};
	std::string message;
	std::optional<std::int64_t> sending_ts_ms;
};

/// A frame of a known type that did not match its schema, including a control
/// frame without a member its schema requires.
struct MalformedFrame {
	std::string type;
};

using Frame = std::variant<std::monostate, WsMessage, SubscribedFrame, UnsubscribedFrame, OkFrame,
						   ErrorFrame, MalformedFrame>;

/// Parses one frame. std::monostate means the frame had no type this SDK
/// version knows; MalformedFrame means a known type failed to parse.
///
/// `subscribed`, `unsubscribed`, and `error` frames must carry the members
/// spec/asyncapi.yaml requires, except an `unsubscribed` frame's `seq`. An `ok`
/// frame needs only its type: the members it requires depend on the command it
/// answers, so the reply to update_subscription is not checked, and only
/// parse_subscription_list() checks the reply to list_subscriptions. Any `sid`
/// in a control frame must be at least 1. No member of a parsed control frame
/// is a default standing in for one the server left out.
[[nodiscard]] Frame parse_frame(const std::string& json);

/// Parses a data frame, for tests and benchmarks.
[[nodiscard]] std::optional<WsMessage> parse_message(const std::string& json);

/// Reads the lists in an update_subscription `ok` reply. A reply without a
/// readable `msg` gives empty lists, not an error.
[[nodiscard]] ws::Updated parse_updated(const std::string& msg);

/// Reads the array in a list_subscriptions reply. std::nullopt unless `msg` is
/// an array whose entries each carry a channel and a `sid` of at least 1.
[[nodiscard]] std::optional<std::vector<ws::ListedSubscription>>
parse_subscription_list(const std::string& msg);

} // namespace kalshi::detail
