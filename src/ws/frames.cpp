#include "frames.hpp"

#include <string_view>
#include <utility>

#include "wire.hpp"

namespace kalshi::detail {

// Named, not anonymous: Glaze reflection needs types with linkage.
namespace frame_wire {

// Control frames are read with kControlReadOptions, which makes every member
// that is not std::optional required. The split follows the `required` lists
// of the response payloads in spec/asyncapi.yaml, except where noted.

struct SubscribedWire {
	std::optional<std::int64_t> id;
	struct Msg {
		std::string channel;
		std::int64_t sid{0};
	} msg;
	std::optional<std::int64_t> sending_ts_ms;
};

struct UnsubscribedWire {
	std::optional<std::int64_t> id;
	std::int64_t sid{0};
	// The spec requires `seq`, but nothing reads it here. Requiring it would
	// drop a confirmation without one, leaving the unsubscribe unfinished.
	std::optional<std::int64_t> seq;
	std::optional<std::int64_t> sending_ts_ms;
};

// The replies that share the type `ok` require different members, and only the
// command they answer tells them apart, so nothing but `type` is required here.
struct OkWire {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	std::optional<glz::raw_json> msg;
	std::optional<std::int64_t> sending_ts_ms;
};

struct ErrorWire {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	struct Msg {
		std::int64_t code{0};
		std::string msg;
	} msg;
	std::optional<std::int64_t> sending_ts_ms;
};

struct UpdatedWire {
	std::vector<std::string> market_tickers;
	std::vector<std::string> market_ids;
	std::vector<std::string> index_ids;
	std::vector<std::string> underlying_tickers;
};

} // namespace frame_wire

using namespace frame_wire;

namespace {

/// Fails a read when a member the wire struct requires is absent, instead of
/// leaving that member at its default. Data frames keep kWsReadOptions.
constexpr JsonReadOptions kControlReadOptions{
	{.error_on_unknown_keys = false, .error_on_missing_keys = true}};

/// The spec's `subscriptionId`: the server numbers subscriptions from 1. The
/// registry keeps 0 for a subscription the server has not confirmed.
constexpr bool is_sid(std::int64_t sid) noexcept {
	return sid >= 1;
}

constexpr bool is_sid_or_absent(const std::optional<std::int64_t>& sid) noexcept {
	return !sid || is_sid(*sid);
}

} // namespace

Frame parse_frame(const std::string& json) {
	const std::string_view type = envelope_type(json);
	if (type == "subscribed") {
		SubscribedWire wire;
		if (glz::read<kControlReadOptions>(wire, json) || !is_sid(wire.msg.sid)) {
			return MalformedFrame{std::string(type)};
		}
		return SubscribedFrame{wire.id, std::move(wire.msg.channel), wire.msg.sid,
							   wire.sending_ts_ms};
	}
	if (type == "unsubscribed") {
		UnsubscribedWire wire;
		if (glz::read<kControlReadOptions>(wire, json) || !is_sid(wire.sid)) {
			return MalformedFrame{std::string(type)};
		}
		return UnsubscribedFrame{wire.id, wire.sid, wire.seq, wire.sending_ts_ms};
	}
	if (type == "ok") {
		OkWire wire;
		if (glz::read<kControlReadOptions>(wire, json) || !is_sid_or_absent(wire.sid)) {
			return MalformedFrame{std::string(type)};
		}
		return OkFrame{wire.id, wire.sid, wire.seq,
					   wire.msg ? std::move(wire.msg->str) : std::string{}, wire.sending_ts_ms};
	}
	if (type == "error") {
		ErrorWire wire;
		if (glz::read<kControlReadOptions>(wire, json) || !is_sid_or_absent(wire.sid)) {
			return MalformedFrame{std::string(type)};
		}
		return ErrorFrame{
			wire.id,		   wire.sid, wire.seq, wire.msg.code, std::move(wire.msg.msg),
			wire.sending_ts_ms};
	}
	if (std::optional<WsMessage> message = parse_data_frame(type, json)) {
		return std::move(*message);
	}
	if (is_data_type(type)) {
		return MalformedFrame{std::string(type)};
	}
	return {};
}

std::optional<WsMessage> parse_message(const std::string& json) {
	return parse_data_frame(envelope_type(json), json);
}

ws::Updated parse_updated(const std::string& msg) {
	UpdatedWire wire;
	if (glz::read<kWsReadOptions>(wire, msg)) {
		return {};
	}
	return ws::Updated{{},
					   std::move(wire.market_tickers),
					   std::move(wire.market_ids),
					   std::move(wire.index_ids),
					   std::move(wire.underlying_tickers)};
}

std::optional<std::vector<ws::ListedSubscription>> parse_subscription_list(const std::string& msg) {
	std::vector<ws::ListedSubscription> list;
	if (glz::read<kControlReadOptions>(list, msg)) {
		return std::nullopt;
	}
	for (const ws::ListedSubscription& subscription : list) {
		if (!is_sid(subscription.sid)) {
			return std::nullopt;
		}
	}
	return list;
}

} // namespace kalshi::detail
