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

inline constexpr JsonReadOptions kWsReadOptions{{.error_on_unknown_keys = false}};
inline constexpr JsonReadOptions kWsEnvelopeReadOptions{
	{.error_on_unknown_keys = false, .error_on_missing_keys = true}};
inline constexpr glz::opts kWsPartialOptions{.error_on_unknown_keys = false, .partial_read = true};

struct DataEnvelope {
	std::int64_t sid{0};
	glz::raw_json_view msg;
};

struct PartialEnvelope {
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
};

/// Checks that a data frame carries a positive `sid` and a JSON object `msg`
/// before lenient body parsing or null-member stripping runs.
[[nodiscard]] inline bool has_valid_data_envelope(const std::string& json) {
	DataEnvelope envelope;
	if (glz::read<kWsEnvelopeReadOptions>(envelope, json) || envelope.sid < 1) {
		return false;
	}
	const std::string_view raw = envelope.msg.str;
	const std::size_t first = raw.find_first_not_of(" \t\n\r");
	return first != std::string_view::npos && raw[first] == '{';
}

/// Best-effort extraction of `sid` (>= 1) and `seq` from a malformed data frame
/// so the registry can attach the error and self-heal sequenced streams.
[[nodiscard]] inline PartialEnvelope partial_envelope(const std::string& json) {
	PartialEnvelope envelope;
	(void)glz::read<kWsPartialOptions>(envelope, json);
	if (envelope.sid && *envelope.sid < 1) {
		envelope.sid.reset();
	}
	return envelope;
}

/// Reads a data frame into ws::Update<T>, retrying without null members when
/// Kalshi sends null for a field its spec marks non-null.
template <typename T>
[[nodiscard]] std::optional<WsMessage> read_update(const std::string& json) {
	if (!has_valid_data_envelope(json)) {
		return std::nullopt;
	}
	ws::Update<T> update;
	if (!glz::read<kWsReadOptions>(update, json)) {
		return WsMessage{std::move(update)};
	}
	if (glz::validate_json(json)) {
		return std::nullopt;
	}
	const std::string stripped = strip_null_members(json);
	update = ws::Update<T>{};
	if (!glz::read<kWsReadOptions>(update, stripped)) {
		return WsMessage{std::move(update)};
	}
	return std::nullopt;
}

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
