#include "frames.hpp"

#include <string_view>
#include <utility>

#include "wire.hpp"

namespace kalshi::detail {

// Named, not anonymous: Glaze reflection needs types with linkage.
namespace frame_wire {

struct SubscribedWire {
	std::optional<std::int64_t> id;
	struct Msg {
		std::string channel;
		std::int64_t sid{0};
	} msg;
};

struct OkWire {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	glz::raw_json msg{"{}"};
};

struct ErrorWire {
	std::optional<std::int64_t> id;
	std::optional<std::int64_t> sid;
	std::optional<std::int64_t> seq;
	struct Msg {
		std::int64_t code{0};
		std::string msg;
	} msg;
};

struct UpdatedWire {
	std::vector<std::string> market_tickers;
	std::vector<std::string> market_ids;
	std::vector<std::string> index_ids;
	std::vector<std::string> underlying_tickers;
};

} // namespace frame_wire

using namespace frame_wire;

Frame parse_frame(const std::string& json) {
	const std::string_view type = envelope_type(json);
	if (type == "subscribed") {
		SubscribedWire wire;
		if (glz::read<kWsReadOptions>(wire, json)) {
			return MalformedFrame{std::string(type)};
		}
		return SubscribedFrame{wire.id, std::move(wire.msg.channel), wire.msg.sid};
	}
	if (type == "unsubscribed") {
		UnsubscribedFrame frame;
		if (glz::read<kWsReadOptions>(frame, json)) {
			return MalformedFrame{std::string(type)};
		}
		return frame;
	}
	if (type == "ok") {
		OkWire wire;
		if (glz::read<kWsReadOptions>(wire, json)) {
			return MalformedFrame{std::string(type)};
		}
		return OkFrame{wire.id, wire.sid, wire.seq, std::move(wire.msg.str)};
	}
	if (type == "error") {
		ErrorWire wire;
		if (glz::read<kWsReadOptions>(wire, json)) {
			return MalformedFrame{std::string(type)};
		}
		return ErrorFrame{wire.id, wire.sid, wire.seq, wire.msg.code, std::move(wire.msg.msg)};
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

std::vector<ws::ListedSubscription> parse_subscription_list(const std::string& msg) {
	std::vector<ws::ListedSubscription> list;
	if (glz::read<kWsReadOptions>(list, msg)) {
		return {};
	}
	return list;
}

} // namespace kalshi::detail
