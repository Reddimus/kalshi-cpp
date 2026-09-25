#pragma once

// Glaze helpers the generated wire.hpp builds on.

#include "kalshi/websocket.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../json.hpp"
#include "../json_nulls.hpp"

namespace kalshi::detail {

inline constexpr glz::opts kWsReadOptions{.error_on_unknown_keys = false};

/// Reads a data frame into ws::Update<T>, retrying without null members when
/// Kalshi sends null for a field its spec marks non-null.
template <typename T>
[[nodiscard]] std::optional<WsMessage> read_update(const std::string& json) {
	ws::Update<T> update;
	if (!glz::read<kWsReadOptions>(update, json)) {
		return WsMessage{std::move(update)};
	}
	const std::string stripped = strip_null_members(json);
	update = ws::Update<T>{};
	if (!glz::read<kWsReadOptions>(update, stripped)) {
		return WsMessage{std::move(update)};
	}
	return std::nullopt;
}

inline constexpr glz::opts kWsPartialOptions{.error_on_unknown_keys = false, .partial_read = true};

struct TypeField {
	std::string_view type;
};

struct EventTypeField {
	struct Msg {
		std::string_view event_type;
	} msg;
};

/// A frame's `type`. Reading stops as soon as it is found.
[[nodiscard]] inline std::string_view envelope_type(const std::string& json) {
	TypeField field;
	return glz::read<kWsPartialOptions>(field, json) ? std::string_view{} : field.type;
}

/// `msg.event_type`, which tells apart messages that share a `type`.
[[nodiscard]] inline std::string_view msg_event_type(const std::string& json) {
	EventTypeField field;
	return glz::read<kWsPartialOptions>(field, json) ? std::string_view{} : field.msg.event_type;
}

template <typename Params>
struct Command {
	std::int64_t id{0};
	std::string cmd;
	Params params;
};

struct SidsWire {
	std::vector<std::int64_t> sids;
};

struct BareCommand {
	std::int64_t id{0};
	std::string cmd;
};

/// Serializes a command. Kalshi requires the keys in the order `id`, `cmd`,
/// `params`, which is the members' declaration order.
template <typename T>
[[nodiscard]] std::string render_command(const T& command) {
	std::string out;
	(void)glz::write<glz::opts{}>(command, out); // writing an aggregate cannot fail
	return out;
}

} // namespace kalshi::detail
