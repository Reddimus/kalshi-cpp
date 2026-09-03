#include "kalshi/websocket.hpp"

#include "kalshi/detail/callback_slot.hpp"
#include "kalshi/detail/ws_json.hpp"
#include "kalshi/detail/ws_message.hpp"
#include "kalshi/fixed_point.hpp"

#include "subscription_registry.hpp"

// IMPORTANT: include order below is load-bearing on Windows.
//
// ``ws_cmd_bodies.hpp`` pulls in ``<glaze/glaze.hpp>``, whose templated
// code (``glaze/core/buffer_traits.hpp``, ``glaze/util/fast_float.hpp``,
// ``glaze/json/read.hpp``) leans on ``std::numeric_limits<T>::max()`` /
// ``::min()``. ``<libwebsockets.h>`` transitively includes ``<windows.h>``
// on MSVC, which by default ``#define``s ``max`` and ``min`` as
// function-like macros — once those macros are live, every
// ``std::numeric_limits<T>::max()`` token gets clobbered and the build
// dies with a 100+ error cascade (C2589 / C3878 / C2760) in glaze
// headers. PR #19 first Windows CI run reproduced exactly that.
//
// We force the Glaze shim BEFORE ``<libwebsockets.h>`` via
// ``// clang-format off`` (the project's clang-format style otherwise
// regroups quoted includes after angle-bracket ones, which would
// undo the fix). We also belt-and-brace with ``NOMINMAX`` as a
// target-level compile definition in ``src/CMakeLists.txt`` so any
// future Windows header pulled in below this point stays safe.
// clang-format off
#include "ws_cmd_bodies.hpp"
// clang-format on

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <libwebsockets.h>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ===== Glaze serializers for outgoing WS commands =====
//
// Shim structs + glz::meta live in `src/ws/ws_cmd_bodies.hpp` (not
// installed). The WS server rejects frames whose top-level keys are
// not in the documented order; the meta there pins it. Byte-exact
// equivalence vs the pre-migration `nlohmann::ordered_json` output is
// pinned by `tests/test_json_serialize.cpp`.
//
// IMPORTANT: only the OUTGOING command builders use Glaze. The WS
// `handle_message` hot path below uses the hand-rolled scanners in
// `kalshi/detail/ws_json.hpp` and is deliberately untouched — see
// the `feedback_find_first_json_scanner` memory note.

namespace kalshi {

namespace {

std::int32_t exact_ws_integer(std::string_view wire, std::uint8_t scale) {
	const Result<FixedPoint> parsed = FixedPoint::parse(wire);
	if (!parsed)
		return 0;
	const Result<std::int64_t> value = parsed->scaled_integer(scale);
	if (!value || *value < std::numeric_limits<std::int32_t>::min() ||
		*value > std::numeric_limits<std::int32_t>::max())
		return 0;
	return static_cast<std::int32_t>(*value);
}

} // namespace

namespace detail {

struct LifecyclePriceRangeWire {
	std::string start;
	std::string end;
	std::string step;
};

struct LifecycleAdditionalMetadataWire {
	std::string name;
	std::string title;
	std::string yes_sub_title;
	std::string no_sub_title;
	std::string rules_primary;
	std::string rules_secondary;
	bool can_close_early{false};
	std::string event_ticker;
	std::int64_t expected_expiration_ts{0};
	std::string strike_type;
	std::optional<glz::raw_json> floor_strike;
	std::optional<glz::raw_json> cap_strike;
	std::optional<glz::raw_json> custom_strike;
};

struct LifecycleMessageWire {
	std::string event_type;
	std::string market_ticker;
	std::int32_t exchange_index{0};
	std::int64_t open_ts{0};
	std::int64_t close_ts{0};
	std::optional<std::int64_t> determination_ts;
	std::optional<std::int64_t> settled_ts;
	std::optional<std::string> result;
	std::string settlement_value;
	bool is_deactivated{false};
	std::string price_level_structure;
	std::vector<LifecyclePriceRangeWire> price_ranges;
	std::string strike_type;
	std::optional<glz::raw_json> floor_strike;
	std::optional<glz::raw_json> cap_strike;
	std::optional<glz::raw_json> custom_strike;
	std::optional<std::string> yes_sub_title;
	std::optional<LifecycleAdditionalMetadataWire> additional_metadata;
};

struct LifecycleEnvelopeWire {
	std::string type;
	std::int32_t sid{0};
	LifecycleMessageWire msg;
};

std::optional<WsMessage> parse_ws_data_message(std::string_view input) {
	const std::string json{input};
	const std::string type = extract_string(json, "type");
	const auto side = [&](std::string_view key) {
		return extract_string(json, std::string{key}) == "no" ? Side::No : Side::Yes;
	};
	const auto action = [&](std::string_view key) {
		return extract_string(json, std::string{key}) == "sell" ? Action::Sell : Action::Buy;
	};
	const auto outcome_side = [&](std::string_view key) {
		return extract_string(json, std::string{key}) == "no" ? OutcomeSide::No : OutcomeSide::Yes;
	};
	const auto book_side = [&](std::string_view key) {
		return extract_string(json, std::string{key}) == "ask" ? BookSide::Ask : BookSide::Bid;
	};
	const auto has_key = [&](std::string_view key) {
		return json.find('"' + std::string{key} + '"') != std::string::npos;
	};
	const auto orderbook_entries = [&](std::string_view current_key, std::string_view legacy_key) {
		const bool current = has_key(current_key);
		const std::vector<PriceQty> pairs =
			extract_orderbook_entries(json, std::string{current ? current_key : legacy_key});
		std::vector<OrderBookEntry> entries;
		entries.reserve(pairs.size());
		for (const PriceQty& pair : pairs) {
			OrderBookEntry entry{};
			entry.price_dollars = current ? pair.price_fp : std::string{};
			entry.quantity_fp = current ? pair.quantity_fp : std::string{};
			entry.price_cents = current ? exact_ws_integer(pair.price_fp, 2) : pair.price;
			entry.quantity = current ? exact_ws_integer(pair.quantity_fp, 0) : pair.quantity;
			entries.push_back(std::move(entry));
		}
		return entries;
	};

	if (type == "orderbook_snapshot") {
		OrderbookSnapshot snapshot;
		snapshot.sid = extract_int(json, "sid");
		snapshot.seq = extract_int(json, "seq");
		snapshot.market_ticker = extract_string(json, "market_ticker");
		snapshot.market_id = extract_string(json, "market_id");
		snapshot.yes = orderbook_entries("yes_dollars_fp", "yes");
		snapshot.no = orderbook_entries("no_dollars_fp", "no");
		return snapshot;
	}
	if (type == "orderbook_delta") {
		OrderbookDelta delta;
		delta.sid = extract_int(json, "sid");
		delta.seq = extract_int(json, "seq");
		delta.market_ticker = extract_string(json, "market_ticker");
		delta.market_id = extract_string(json, "market_id");
		delta.client_order_id = extract_string(json, "client_order_id");
		if (has_key("subaccount"))
			delta.subaccount = extract_int64(json, "subaccount");
		delta.price_dollars = extract_string(json, "price_dollars");
		delta.delta_fp = extract_string(json, "delta_fp");
		delta.price = exact_ws_integer(delta.price_dollars, 2);
		delta.delta = exact_ws_integer(delta.delta_fp, 0);
		delta.side = side("side");
		delta.timestamp_iso = extract_string(json, "ts");
		delta.timestamp_ms = extract_int64(json, "ts_ms");
		return delta;
	}
	if (type == "trade") {
		WsTrade trade;
		trade.sid = extract_int(json, "sid");
		trade.trade_id = extract_string(json, "trade_id");
		trade.market_ticker = extract_string(json, "market_ticker");
		trade.yes_price_dollars = extract_string(json, "yes_price_dollars");
		trade.no_price_dollars = extract_string(json, "no_price_dollars");
		trade.count_fp = extract_string(json, "count_fp");
		trade.yes_price = exact_ws_integer(trade.yes_price_dollars, 2);
		trade.no_price = exact_ws_integer(trade.no_price_dollars, 2);
		trade.count = exact_ws_integer(trade.count_fp, 0);
		trade.is_block_trade = extract_bool(json, "is_block_trade");
		trade.taker_side = side("taker_side");
		const std::string canonical_outcome = extract_string(json, "taker_outcome_side");
		trade.taker_outcome_side =
			canonical_outcome.empty()
				? (trade.taker_side == Side::No ? OutcomeSide::No : OutcomeSide::Yes)
				: outcome_side("taker_outcome_side");
		const std::string canonical_book = extract_string(json, "taker_book_side");
		trade.taker_book_side =
			canonical_book.empty()
				? (trade.taker_outcome_side == OutcomeSide::No ? BookSide::Ask : BookSide::Bid)
				: book_side("taker_book_side");
		trade.timestamp = extract_int64(json, "ts");
		trade.timestamp_ms = extract_int64(json, "ts_ms");
		return trade;
	}
	if (type == "fill") {
		WsFill fill;
		fill.sid = extract_int(json, "sid");
		fill.trade_id = extract_string(json, "trade_id");
		fill.order_id = extract_string(json, "order_id");
		fill.market_ticker = extract_string(json, "market_ticker");
		fill.exchange_index = extract_int(json, "exchange_index");
		fill.is_taker = extract_bool(json, "is_taker");
		fill.side = side("side");
		fill.yes_price_dollars = extract_string(json, "yes_price_dollars");
		fill.no_price_dollars = extract_string(json, "no_price_dollars");
		fill.count_fp = extract_string(json, "count_fp");
		fill.fee_cost = extract_string(json, "fee_cost");
		fill.yes_price = exact_ws_integer(fill.yes_price_dollars, 2);
		fill.no_price = exact_ws_integer(fill.no_price_dollars, 2);
		fill.count = exact_ws_integer(fill.count_fp, 0);
		fill.action = action("action");
		const std::string canonical_outcome = extract_string(json, "outcome_side");
		fill.outcome_side = canonical_outcome.empty() ? derive_outcome_side(fill.side, fill.action)
													  : outcome_side("outcome_side");
		const std::string canonical_book = extract_string(json, "book_side");
		fill.book_side = canonical_book.empty() ? derive_book_side(fill.side, fill.action)
												: book_side("book_side");
		fill.timestamp = extract_int64(json, "ts");
		fill.timestamp_ms = extract_int64(json, "ts_ms");
		fill.client_order_id = extract_string(json, "client_order_id");
		fill.post_position_fp = extract_string(json, "post_position_fp");
		fill.purchased_side = side("purchased_side");
		if (has_key("subaccount"))
			fill.subaccount = extract_int64(json, "subaccount");
		return fill;
	}
	if (type == "market_lifecycle" || type == "market_lifecycle_v2") {
		LifecycleEnvelopeWire wire;
		constexpr glz::opts read_options{.error_on_unknown_keys = false};
		if (glz::read<read_options>(wire, json))
			return std::nullopt;
		MarketLifecycle lifecycle;
		lifecycle.sid = wire.sid;
		lifecycle.event_type = std::move(wire.msg.event_type);
		lifecycle.market_ticker = std::move(wire.msg.market_ticker);
		lifecycle.exchange_index = wire.msg.exchange_index;
		lifecycle.open_ts = wire.msg.open_ts;
		lifecycle.close_ts = wire.msg.close_ts;
		lifecycle.determination_ts = wire.msg.determination_ts;
		lifecycle.settled_ts = wire.msg.settled_ts;
		lifecycle.result = std::move(wire.msg.result);
		lifecycle.is_deactivated = wire.msg.is_deactivated;
		lifecycle.yes_sub_title = std::move(wire.msg.yes_sub_title);
		lifecycle.settlement_value_dollars = std::move(wire.msg.settlement_value);
		lifecycle.price_level_structure = std::move(wire.msg.price_level_structure);
		lifecycle.price_ranges.reserve(wire.msg.price_ranges.size());
		for (LifecyclePriceRangeWire& range : wire.msg.price_ranges) {
			lifecycle.price_ranges.push_back(
				{std::move(range.start), std::move(range.end), std::move(range.step)});
		}
		lifecycle.strike_type = std::move(wire.msg.strike_type);
		if (wire.msg.floor_strike)
			lifecycle.floor_strike = std::move(wire.msg.floor_strike->str);
		if (wire.msg.cap_strike)
			lifecycle.cap_strike = std::move(wire.msg.cap_strike->str);
		if (wire.msg.custom_strike)
			lifecycle.custom_strike_json = std::move(wire.msg.custom_strike->str);
		if (wire.msg.additional_metadata) {
			LifecycleAdditionalMetadata metadata;
			metadata.name = std::move(wire.msg.additional_metadata->name);
			metadata.title = std::move(wire.msg.additional_metadata->title);
			metadata.yes_sub_title = std::move(wire.msg.additional_metadata->yes_sub_title);
			metadata.no_sub_title = std::move(wire.msg.additional_metadata->no_sub_title);
			metadata.rules_primary = std::move(wire.msg.additional_metadata->rules_primary);
			metadata.rules_secondary = std::move(wire.msg.additional_metadata->rules_secondary);
			metadata.can_close_early = wire.msg.additional_metadata->can_close_early;
			metadata.event_ticker = std::move(wire.msg.additional_metadata->event_ticker);
			metadata.expected_expiration_ts = wire.msg.additional_metadata->expected_expiration_ts;
			metadata.strike_type = std::move(wire.msg.additional_metadata->strike_type);
			if (wire.msg.additional_metadata->floor_strike)
				metadata.floor_strike = std::move(wire.msg.additional_metadata->floor_strike->str);
			if (wire.msg.additional_metadata->cap_strike)
				metadata.cap_strike = std::move(wire.msg.additional_metadata->cap_strike->str);
			if (wire.msg.additional_metadata->custom_strike)
				metadata.custom_strike_json =
					std::move(wire.msg.additional_metadata->custom_strike->str);
			lifecycle.additional_metadata = std::move(metadata);
		}
		return lifecycle;
	}
	return std::nullopt;
}

} // namespace detail

// Forward declaration for the callback
struct WsImplData;
static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in,
					   size_t len);

namespace {

std::string channel_to_string(Channel channel) {
	switch (channel) {
		case Channel::OrderbookDelta:
			return "orderbook_delta";
		case Channel::Trade:
			return "trade";
		case Channel::Fill:
			return "fill";
		case Channel::MarketLifecycle:
			return "market_lifecycle_v2";
	}
	return "";
}

std::string build_subscribe_command(std::int32_t id, Channel channel,
									const std::vector<std::string>& market_tickers) {
	ws_cmd::SubscribeCmd cmd;
	cmd.id = id;
	cmd.cmd = "subscribe";
	cmd.params.channels = {channel_to_string(channel)};
	if (!market_tickers.empty()) {
		cmd.params.market_tickers = market_tickers;
	}
	return ws_cmd::render_cmd(cmd);
}

std::string build_unsubscribe_command(std::int32_t id, std::int32_t sid) {
	ws_cmd::UnsubscribeCmd cmd;
	cmd.id = id;
	cmd.cmd = "unsubscribe";
	cmd.params.sids = {sid};
	return ws_cmd::render_cmd(cmd);
}

std::string build_update_command(std::int32_t id, std::int32_t sid, const std::string& action,
								 Channel channel, const std::vector<std::string>& market_tickers) {
	ws_cmd::UpdateCmd cmd;
	cmd.id = id;
	cmd.cmd = "update_subscription";
	cmd.params.action = action;
	cmd.params.channel = std::string(to_string(channel));
	cmd.params.sids = {sid};
	cmd.params.market_tickers = market_tickers;
	return ws_cmd::render_cmd(cmd);
}

} // anonymous namespace

// Implementation data structure - exposed for callback
struct WsImplData {
	const Signer* signer;
	WsConfig config;

	std::atomic<bool> connected{false};
	std::atomic<bool> should_stop{false};

	detail::CallbackSlot<void(const WsMessage&)> message_callback;
	detail::CallbackSlot<void(const WsError&)> error_callback;
	detail::CallbackSlot<void(bool)> state_callback;
	std::atomic<std::int32_t> next_command_id{1};
	std::atomic<std::uint16_t> reconnect_attempts{0}; ///< Current reconnect attempts (0-65535)

	// libwebsockets context and connection
	lws_context* context{nullptr};
	lws* wsi{nullptr};
	std::thread service_thread;

	// Send queue - deque provides contiguous storage and efficient front removal
	std::mutex send_mutex;
	std::deque<std::string> send_queue;
	std::string current_send_buffer;

	// Receive buffer
	std::string recv_buffer;

	// Track server-assigned subscription IDs: client command id -> server sid.
	ws_detail::SubscriptionRegistry subscriptions;

	// Auth headers for handshake
	AuthHeaders auth_headers;

	WsImplData(const Signer& s, WsConfig c) : signer(&s), config(std::move(c)) {}

	~WsImplData() {
		if (context) {
			lws_context_destroy(context);
		}
	}

	std::int32_t get_next_id() { return next_command_id.fetch_add(1); }

	void queue_send(const std::string& msg) {
		std::lock_guard lock(send_mutex);
		send_queue.push_back(msg);
		if (wsi) {
			lws_callback_on_writable(wsi);
		}
	}

	void invoke_message_callback(const WsMessage& msg) noexcept { message_callback.invoke(msg); }

	void invoke_error_callback(const WsError& err) noexcept { error_callback.invoke(err); }

	void invoke_state_callback(bool connected_state) noexcept {
		state_callback.invoke(connected_state);
	}

	// Parse incoming JSON message and dispatch to appropriate callback
	void handle_message(const std::string& json);
};

struct WebSocketClient::Impl {
	std::unique_ptr<WsImplData> data;

	Impl(const Signer& s, WsConfig c) : data(std::make_unique<WsImplData>(s, std::move(c))) {}
};

// libwebsockets fixes this callback ABI, including the adjacent opaque pointers.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in,
					   size_t len) {
	(void)user;
	WsImplData* impl = static_cast<WsImplData*>(lws_context_user(lws_get_context(wsi)));

	if (!impl)
		return 0;

	switch (reason) {
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
			impl->connected = true;
			impl->reconnect_attempts = 0;
			impl->subscriptions.clear();
			impl->invoke_state_callback(true);
			break;

		case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
			impl->connected = false;
			if (in) {
				impl->invoke_error_callback({0, std::string(static_cast<char*>(in), len)});
			}
			impl->invoke_state_callback(false);
			break;

		case LWS_CALLBACK_CLIENT_CLOSED:
			impl->connected = false;
			impl->subscriptions.clear();
			impl->invoke_state_callback(false);
			break;

		case LWS_CALLBACK_CLIENT_RECEIVE:
			if (in && len > 0) {
				impl->recv_buffer.append(static_cast<char*>(in), len);

				// Check if this is the final fragment
				if (lws_is_final_fragment(wsi)) {
					impl->handle_message(impl->recv_buffer);
					impl->recv_buffer.clear();
				}
			}
			break;

		case LWS_CALLBACK_CLIENT_WRITEABLE: {
			std::lock_guard lock(impl->send_mutex);
			if (!impl->send_queue.empty()) {
				std::string msg = impl->send_queue.front();
				impl->send_queue.pop_front();

				// Allocate buffer with LWS_PRE padding
				std::vector<unsigned char> buf(LWS_PRE + msg.size());
				std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());

				int written = lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
				if (written < static_cast<int>(msg.size())) {
					impl->invoke_error_callback({-1, "Failed to write to WebSocket"});
				}

				// If there are more messages, request another callback
				if (!impl->send_queue.empty()) {
					lws_callback_on_writable(wsi);
				}
			}
			break;
		}

		case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
			// Add authentication headers to the WebSocket upgrade request
			unsigned char** p = reinterpret_cast<unsigned char**>(in);
			unsigned char* end = (*p) + len;

			// Helper to add a header. libwebsockets expects header names to include ':'.
			auto add_header = [&](const char* name, const std::string& value) -> bool {
				if (lws_add_http_header_by_name(
						wsi, reinterpret_cast<const unsigned char*>(name),
						reinterpret_cast<const unsigned char*>(value.c_str()),
						static_cast<int>(value.length()), p, end) != 0) {
					return false;
				}
				return true;
			};

			if (!add_header("KALSHI-ACCESS-KEY:", impl->auth_headers.access_key) ||
				!add_header("KALSHI-ACCESS-SIGNATURE:", impl->auth_headers.signature) ||
				!add_header("KALSHI-ACCESS-TIMESTAMP:", impl->auth_headers.timestamp)) {
				return -1; // Header buffer overflow
			}
			break;
		}

		default:
			break;
	}

	return 0;
}

void WsImplData::handle_message(const std::string& json) {
	// Simple JSON type detection
	size_t type_pos = json.find("\"type\"");
	if (type_pos == std::string::npos)
		return;

	size_t colon = json.find(':', type_pos);
	if (colon == std::string::npos)
		return;

	size_t quote1 = json.find('"', colon);
	if (quote1 == std::string::npos)
		return;

	size_t quote2 = json.find('"', quote1 + 1);
	if (quote2 == std::string::npos)
		return;

	std::string msg_type = json.substr(quote1 + 1, quote2 - quote1 - 1);

	// Extract helpers live in detail/ws_json.hpp so the unit tests can
	// exercise them directly — the original in-lambda versions were
	// private to this translation unit.
	auto extract_int = [&](const std::string& key) { return detail::extract_int(json, key); };

	auto extract_string = [&](const std::string& key) { return detail::extract_string(json, key); };

	if (msg_type == "error") {
		WsError err;
		// Look for nested msg object
		size_t msg_pos = json.find("\"msg\"");
		if (msg_pos != std::string::npos) {
			err.code = extract_int("code");
			err.message = extract_string("message");
			if (err.message.empty()) {
				// No explicit message field — fall back to the documented
				// error-code name (e.g. code 7 → "Unknown subscription ID").
				// The pre-fix fallback `extract_string("msg")` returned the
				// first quoted token inside the `msg` object, which is the
				// `"code"` key name itself, surfacing as message="code" in
				// consumer logs. The find-first scanner anti-pattern caught
				// in 2026-05-15 production logs against kalshi-websocket.
				err.message = std::string{ws_error_code_name(err.code)};
			}
		}
		invoke_error_callback(err);
	} else if (msg_type == "subscribed") {
		// Track server subscription ID: {"type":"subscribed","id":1,"msg":{"sid":12345,...}}
		std::int32_t client_id = extract_int("id");
		std::int32_t server_sid = extract_int("sid");
		if (client_id > 0 && server_sid > 0) {
			subscriptions.register_ack(client_id, server_sid);
		}
	} else if (std::optional<WsMessage> message = detail::parse_ws_data_message(json)) {
		invoke_message_callback(*message);
	}
}

static const struct lws_protocols protocols[] = {{.name = "kalshi-ws",
												  .callback = ws_callback,
												  .per_session_data_size = 0,
												  .rx_buffer_size = 65536},
												 LWS_PROTOCOL_LIST_TERM};

WebSocketClient::WebSocketClient(const Signer& signer, WsConfig config)
	: impl_(std::make_unique<Impl>(signer, std::move(config))) {}

WebSocketClient::~WebSocketClient() {
	disconnect();
}

WebSocketClient::WebSocketClient(WebSocketClient&&) noexcept = default;

WebSocketClient& WebSocketClient::operator=(WebSocketClient&&) noexcept = default;

Result<void> WebSocketClient::connect() {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (data->connected) {
		return {};
	}

	// Reap any leftover state from a partial previous connection. A
	// LWS_CALLBACK_CLIENT_CONNECTION_ERROR sets ``connected = false`` but
	// does NOT join the service thread or destroy the lws context — the
	// caller's reconnect-on-error loop then calls connect() again. Without
	// this reap, the std::thread move-assignment on the new
	// ``data->service_thread = std::thread(...)`` below hits a still-
	// joinable thread, which std::terminate's the process with the
	// classic ``terminate called without an active exception`` followed
	// by SIGSEGV (exit 139). Production seen ~5 times/day on
	// kalshi-websocket before this fix.
	if (data->service_thread.joinable()) {
		data->should_stop = true;
		if (!detail::join_thread_unless_current(data->service_thread)) {
			return std::unexpected(
				Error::network("Cannot reconnect from the WebSocket service callback"));
		}
	}
	if (data->context) {
		lws_context_destroy(data->context);
		data->context = nullptr;
	}
	data->wsi = nullptr;
	data->should_stop = false;

	// Parse URL
	std::string url = data->config.url;
	bool use_ssl = (url.substr(0, 3) == "wss");
	std::string host;
	std::string path = "/";
	int port = use_ssl ? 443 : 80;

	// Extract host and path from URL
	size_t host_start = url.find("://");
	if (host_start != std::string::npos) {
		host_start += 3;
	} else {
		host_start = 0;
	}

	size_t path_start = url.find('/', host_start);
	if (path_start != std::string::npos) {
		host = url.substr(host_start, path_start - host_start);
		path = url.substr(path_start);
	} else {
		host = url.substr(host_start);
	}

	// Check for port in host
	size_t port_pos = host.find(':');
	if (port_pos != std::string::npos) {
		port = std::stoi(host.substr(port_pos + 1));
		host = host.substr(0, port_pos);
	}

	// Generate auth headers
	Result<AuthHeaders> auth_result = data->signer->sign("GET", path);
	if (!auth_result) {
		return std::unexpected(auth_result.error());
	}
	data->auth_headers = *auth_result;

	// Create context
	struct lws_context_creation_info ctx_info {};
	std::memset(&ctx_info, 0, sizeof(ctx_info));
	ctx_info.port = CONTEXT_PORT_NO_LISTEN;
	ctx_info.protocols = protocols;
	ctx_info.user = data.get();
	ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

	data->context = lws_create_context(&ctx_info);
	if (!data->context) {
		return std::unexpected(Error::network("Failed to create WebSocket context"));
	}

	// Create connection
	struct lws_client_connect_info conn_info {};
	std::memset(&conn_info, 0, sizeof(conn_info));
	conn_info.context = data->context;
	conn_info.address = host.c_str();
	conn_info.port = port;
	conn_info.path = path.c_str();
	conn_info.host = host.c_str();
	// Kalshi rejects websocket upgrades that include an Origin header
	// (403), while the documented Python websockets example sends no
	// Origin and succeeds. Leave this unset so libwebsockets omits it.
	conn_info.origin = nullptr;

	if (use_ssl) {
		conn_info.ssl_connection = LCCSCF_USE_SSL;
	}

	data->wsi = lws_client_connect_via_info(&conn_info);
	if (!data->wsi) {
		lws_context_destroy(data->context);
		data->context = nullptr;
		return std::unexpected(Error::network("Failed to initiate WebSocket connection"));
	}

	// Start service thread
	data->service_thread = std::thread([&data = this->impl_->data]() {
		while (!data->should_stop && data->context) {
			lws_service(data->context, 50);
		}
	});

	// Wait briefly for connection (with timeout)
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	while (!data->connected && !data->should_stop) {
		std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - start;
		if (elapsed > std::chrono::seconds(10)) {
			disconnect();
			return std::unexpected(Error::network("Connection timeout"));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return {};
}

void WebSocketClient::disconnect() {
	// The defaulted move ctor / move-assignment leave the moved-from
	// object's impl_ as nullptr. ~WebSocketClient unconditionally
	// calls disconnect(), so without this guard the implicit
	// destructor on a moved-from instance dereferences the nullptr
	// below (auto& data = impl_->data) and segfaults. The same
	// pattern exists in polymarket-cpp's clob::WebSocketClient and
	// polymarket::us::ws::Subscriber — pinning the contract here.
	if (!impl_) {
		return;
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->context) {
		return;
	}

	data->should_stop = true;
	data->connected = false;
	data->subscriptions.clear();

	if (data->service_thread.joinable()) {
		if (!detail::join_thread_unless_current(data->service_thread)) {
			data->invoke_state_callback(false);
			return;
		}
	}

	if (data->context) {
		lws_context_destroy(data->context);
		data->context = nullptr;
	}
	data->wsi = nullptr;

	data->invoke_state_callback(false);
}

bool WebSocketClient::is_connected() const noexcept {
	if (!impl_) {
		return false;
	}
	return impl_->data->connected;
}

Result<SubscriptionId>
WebSocketClient::subscribe_orderbook(const std::vector<std::string>& market_tickers) {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	if (market_tickers.empty()) {
		return std::unexpected(Error{ErrorCode::InvalidRequest, "market_tickers required"});
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_subscribe_command(id, Channel::OrderbookDelta, market_tickers);
	data->queue_send(cmd);

	return SubscriptionId{.sid = id, .channel = Channel::OrderbookDelta};
}

Result<SubscriptionId>
WebSocketClient::subscribe_trades(const std::vector<std::string>& market_tickers) {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_subscribe_command(id, Channel::Trade, market_tickers);
	data->queue_send(cmd);

	return SubscriptionId{.sid = id, .channel = Channel::Trade};
}

Result<SubscriptionId>
WebSocketClient::subscribe_fills(const std::vector<std::string>& market_tickers) {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_subscribe_command(id, Channel::Fill, market_tickers);
	data->queue_send(cmd);

	return SubscriptionId{.sid = id, .channel = Channel::Fill};
}

Result<SubscriptionId> WebSocketClient::subscribe_lifecycle() {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_subscribe_command(id, Channel::MarketLifecycle, {});
	data->queue_send(cmd);

	return SubscriptionId{.sid = id, .channel = Channel::MarketLifecycle};
}

Result<void> WebSocketClient::unsubscribe(SubscriptionId sub_id) {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_unsubscribe_command(id, data->subscriptions.resolve(sub_id.sid));
	data->subscriptions.erase(sub_id.sid);
	data->queue_send(cmd);

	return {};
}

Result<void> WebSocketClient::add_markets(SubscriptionId sub_id,
										  const std::vector<std::string>& market_tickers) {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	if (market_tickers.empty()) {
		return std::unexpected(Error{ErrorCode::InvalidRequest, "market_tickers required"});
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_update_command(id, data->subscriptions.resolve(sub_id.sid),
										   "add_markets", sub_id.channel, market_tickers);
	data->queue_send(cmd);

	return {};
}

Result<void> WebSocketClient::remove_markets(SubscriptionId sub_id,
											 const std::vector<std::string>& market_tickers) {
	if (!impl_) {
		return std::unexpected(Error::network("Client moved-from"));
	}
	std::unique_ptr<WsImplData>& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	if (market_tickers.empty()) {
		return std::unexpected(Error{ErrorCode::InvalidRequest, "market_tickers required"});
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_update_command(id, data->subscriptions.resolve(sub_id.sid),
										   "delete_markets", sub_id.channel, market_tickers);
	data->queue_send(cmd);

	return {};
}

void WebSocketClient::on_message(WsMessageCallback callback) {
	if (!impl_) {
		return;
	}
	impl_->data->message_callback.set(std::move(callback));
}

void WebSocketClient::on_error(WsErrorCallback callback) {
	if (!impl_) {
		return;
	}
	impl_->data->error_callback.set(std::move(callback));
}

void WebSocketClient::on_state_change(WsStateCallback callback) {
	if (!impl_) {
		return;
	}
	impl_->data->state_callback.set(std::move(callback));
}

const WsConfig& WebSocketClient::config() const noexcept { // NOLINT(bugprone-exception-escape)
	// Returning a reference into a nullptr would crash; surface a
	// static empty config so accessors stay safe on moved-from
	// instances (matches the null-guard pattern in disconnect() /
	// is_connected()).
	if (!impl_) {
		// NOLINTNEXTLINE(bugprone-exception-escape)
		static const WsConfig kEmpty{};
		return kEmpty;
	}
	return impl_->data->config;
}

} // namespace kalshi
