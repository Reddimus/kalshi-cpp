#include "kalshi/websocket.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <libwebsockets.h>
#include <mutex>
#include <sstream>
#include <thread>

namespace kalshi {

// Forward declaration for the callback
struct WsImplData;
static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in,
					   size_t len);

namespace {

// JSON helpers for WebSocket messages
std::string build_subscribe_command(std::int32_t id, Channel channel,
									const std::vector<std::string>& market_tickers) {
	std::ostringstream ss;
	ss << "{\"id\":" << id << ",\"cmd\":\"subscribe\",\"params\":{\"channels\":[\"";

	switch (channel) {
		case Channel::OrderbookDelta:
			ss << "orderbook_delta";
			break;
		case Channel::Trade:
			ss << "trade";
			break;
		case Channel::Fill:
			ss << "fill";
			break;
		case Channel::MarketLifecycle:
			ss << "market_lifecycle";
			break;
	}
	ss << "\"]";

	if (!market_tickers.empty()) {
		ss << ",\"market_tickers\":[";
		for (size_t i = 0; i < market_tickers.size(); ++i) {
			if (i > 0)
				ss << ",";
			ss << "\"" << market_tickers[i] << "\"";
		}
		ss << "]";
	}

	ss << "}}";
	return ss.str();
}

std::string build_unsubscribe_command(std::int32_t id, std::int32_t sid) {
	std::ostringstream ss;
	ss << "{\"id\":" << id << ",\"cmd\":\"unsubscribe\",\"params\":{\"sids\":[" << sid << "]}}";
	return ss.str();
}

std::string build_update_command(std::int32_t id, std::int32_t sid, const std::string& action,
								 Channel channel, const std::vector<std::string>& market_tickers) {
	std::ostringstream ss;
	ss << "{\"id\":" << id << ",\"cmd\":\"update_subscription\",\"params\":{";
	ss << "\"action\":\"" << action << "\",";
	ss << "\"channel\":\"" << to_string(channel) << "\",";
	ss << "\"sids\":[" << sid << "],";
	ss << "\"market_tickers\":[";
	for (size_t i = 0; i < market_tickers.size(); ++i) {
		if (i > 0)
			ss << ",";
		ss << "\"" << market_tickers[i] << "\"";
	}
	ss << "]}}";
	return ss.str();
}

} // anonymous namespace

// Implementation data structure - exposed for callback
struct WsImplData {
	const Signer* signer;
	WsConfig config;

	std::atomic<bool> connected{false};
	std::atomic<bool> should_stop{false};

	WsMessageCallback message_callback;
	WsErrorCallback error_callback;
	WsStateCallback state_callback;

	std::mutex callback_mutex;
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

	void invoke_message_callback(const WsMessage& msg) {
		std::lock_guard lock(callback_mutex);
		if (message_callback) {
			message_callback(msg);
		}
	}

	void invoke_error_callback(const WsError& err) {
		std::lock_guard lock(callback_mutex);
		if (error_callback) {
			error_callback(err);
		}
	}

	void invoke_state_callback(bool connected_state) {
		std::lock_guard lock(callback_mutex);
		if (state_callback) {
			state_callback(connected_state);
		}
	}

	// Parse incoming JSON message and dispatch to appropriate callback
	void handle_message(const std::string& json);
};

struct WebSocketClient::Impl {
	std::unique_ptr<WsImplData> data;

	Impl(const Signer& s, WsConfig c) : data(std::make_unique<WsImplData>(s, std::move(c))) {}
};

// libwebsockets callback
static int ws_callback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in,
					   size_t len) {
	WsImplData* impl = static_cast<WsImplData*>(lws_context_user(lws_get_context(wsi)));

	if (!impl)
		return 0;

	switch (reason) {
		case LWS_CALLBACK_CLIENT_ESTABLISHED:
			impl->connected = true;
			impl->reconnect_attempts = 0;
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

	// Extract common fields
	auto extract_int = [&](const std::string& key) -> std::int32_t {
		std::string search = "\"" + key + "\"";
		size_t pos = json.find(search);
		if (pos == std::string::npos)
			return 0;
		pos = json.find(':', pos);
		if (pos == std::string::npos)
			return 0;
		pos++;
		while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
			pos++;
		std::int32_t val = 0;
		while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
			val = val * 10 + (json[pos] - '0');
			pos++;
		}
		return val;
	};

	auto extract_string = [&](const std::string& key) -> std::string {
		std::string search = "\"" + key + "\"";
		size_t pos = json.find(search);
		if (pos == std::string::npos)
			return "";
		pos = json.find(':', pos);
		if (pos == std::string::npos)
			return "";
		pos = json.find('"', pos);
		if (pos == std::string::npos)
			return "";
		size_t start = pos + 1;
		size_t end = json.find('"', start);
		if (end == std::string::npos)
			return "";
		return json.substr(start, end - start);
	};

	if (msg_type == "error") {
		WsError err;
		// Look for nested msg object
		size_t msg_pos = json.find("\"msg\"");
		if (msg_pos != std::string::npos) {
			err.code = extract_int("code");
			err.message = extract_string("message");
		}
		invoke_error_callback(err);
	} else if (msg_type == "orderbook_snapshot") {
		OrderbookSnapshot snap;
		snap.sid = extract_int("sid");
		snap.seq = extract_int("seq");
		snap.market_ticker = extract_string("market_ticker");
		// Note: Full implementation would parse yes/no arrays
		invoke_message_callback(snap);
	} else if (msg_type == "orderbook_delta") {
		OrderbookDelta delta;
		delta.sid = extract_int("sid");
		delta.seq = extract_int("seq");
		delta.market_ticker = extract_string("market_ticker");
		delta.price = extract_int("price");
		delta.delta = extract_int("delta");
		std::string side_str = extract_string("side");
		delta.side = (side_str == "yes") ? Side::Yes : Side::No;
		invoke_message_callback(delta);
	} else if (msg_type == "trade") {
		WsTrade trade;
		trade.sid = extract_int("sid");
		trade.trade_id = extract_string("trade_id");
		trade.market_ticker = extract_string("market_ticker");
		trade.yes_price = extract_int("yes_price");
		trade.no_price = extract_int("no_price");
		trade.count = extract_int("count");
		std::string side_str = extract_string("taker_side");
		trade.taker_side = (side_str == "yes") ? Side::Yes : Side::No;
		trade.timestamp = extract_int("ts");
		invoke_message_callback(trade);
	} else if (msg_type == "fill") {
		WsFill fill;
		fill.sid = extract_int("sid");
		fill.trade_id = extract_string("trade_id");
		fill.order_id = extract_string("order_id");
		fill.market_ticker = extract_string("market_ticker");
		fill.is_taker = (extract_string("is_taker") == "true");
		std::string side_str = extract_string("side");
		fill.side = (side_str == "yes") ? Side::Yes : Side::No;
		fill.yes_price = extract_int("yes_price");
		fill.no_price = extract_int("no_price");
		fill.count = extract_int("count");
		std::string action_str = extract_string("action");
		fill.action = (action_str == "buy") ? Action::Buy : Action::Sell;
		fill.timestamp = extract_int("ts");
		invoke_message_callback(fill);
	} else if (msg_type == "market_lifecycle") {
		MarketLifecycle lc;
		lc.sid = extract_int("sid");
		lc.market_ticker = extract_string("market_ticker");
		lc.open_ts = extract_int("open_ts");
		lc.close_ts = extract_int("close_ts");
		std::int64_t det = extract_int("determination_ts");
		if (det > 0)
			lc.determination_ts = det;
		std::int64_t set = extract_int("settled_ts");
		if (set > 0)
			lc.settled_ts = set;
		std::string result = extract_string("result");
		if (!result.empty())
			lc.result = result;
		lc.is_deactivated = (extract_string("is_deactivated") == "true");
		invoke_message_callback(lc);
	}
}

static const struct lws_protocols protocols[] = {{"kalshi-ws", ws_callback, 0, 65536},
												 LWS_PROTOCOL_LIST_TERM};

WebSocketClient::WebSocketClient(const Signer& signer, WsConfig config)
	: impl_(std::make_unique<Impl>(signer, std::move(config))) {}

WebSocketClient::~WebSocketClient() {
	disconnect();
}

WebSocketClient::WebSocketClient(WebSocketClient&&) noexcept = default;

WebSocketClient& WebSocketClient::operator=(WebSocketClient&&) noexcept = default;

Result<void> WebSocketClient::connect() {
	auto& data = impl_->data;

	if (data->connected) {
		return {};
	}

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
	auto auth_result = data->signer->sign("GET", path);
	if (!auth_result) {
		return std::unexpected(auth_result.error());
	}

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
	conn_info.origin = host.c_str();
	conn_info.protocol = protocols[0].name;

	if (use_ssl) {
		conn_info.ssl_connection =
			LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
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
	auto start = std::chrono::steady_clock::now();
	while (!data->connected && !data->should_stop) {
		auto elapsed = std::chrono::steady_clock::now() - start;
		if (elapsed > std::chrono::seconds(10)) {
			disconnect();
			return std::unexpected(Error::network("Connection timeout"));
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return {};
}

void WebSocketClient::disconnect() {
	auto& data = impl_->data;

	if (!data->context) {
		return;
	}

	data->should_stop = true;
	data->connected = false;

	if (data->service_thread.joinable()) {
		data->service_thread.join();
	}

	if (data->context) {
		lws_context_destroy(data->context);
		data->context = nullptr;
	}
	data->wsi = nullptr;

	data->invoke_state_callback(false);
}

bool WebSocketClient::is_connected() const noexcept {
	return impl_->data->connected;
}

Result<SubscriptionId>
WebSocketClient::subscribe_orderbook(const std::vector<std::string>& market_tickers) {
	auto& data = impl_->data;

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
	auto& data = impl_->data;

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
	auto& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_subscribe_command(id, Channel::Fill, market_tickers);
	data->queue_send(cmd);

	return SubscriptionId{.sid = id, .channel = Channel::Fill};
}

Result<SubscriptionId> WebSocketClient::subscribe_lifecycle() {
	auto& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_subscribe_command(id, Channel::MarketLifecycle, {});
	data->queue_send(cmd);

	return SubscriptionId{.sid = id, .channel = Channel::MarketLifecycle};
}

Result<void> WebSocketClient::unsubscribe(SubscriptionId sub_id) {
	auto& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	std::int32_t id = data->get_next_id();
	std::string cmd = build_unsubscribe_command(id, sub_id.sid);
	data->queue_send(cmd);

	return {};
}

Result<void> WebSocketClient::add_markets(SubscriptionId sub_id,
										  const std::vector<std::string>& market_tickers) {
	auto& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	if (market_tickers.empty()) {
		return std::unexpected(Error{ErrorCode::InvalidRequest, "market_tickers required"});
	}

	std::int32_t id = data->get_next_id();
	std::string cmd =
		build_update_command(id, sub_id.sid, "add_markets", sub_id.channel, market_tickers);
	data->queue_send(cmd);

	return {};
}

Result<void> WebSocketClient::remove_markets(SubscriptionId sub_id,
											 const std::vector<std::string>& market_tickers) {
	auto& data = impl_->data;

	if (!data->connected) {
		return std::unexpected(Error::network("Not connected"));
	}

	if (market_tickers.empty()) {
		return std::unexpected(Error{ErrorCode::InvalidRequest, "market_tickers required"});
	}

	std::int32_t id = data->get_next_id();
	std::string cmd =
		build_update_command(id, sub_id.sid, "delete_markets", sub_id.channel, market_tickers);
	data->queue_send(cmd);

	return {};
}

void WebSocketClient::on_message(WsMessageCallback callback) {
	std::lock_guard lock(impl_->data->callback_mutex);
	impl_->data->message_callback = std::move(callback);
}

void WebSocketClient::on_error(WsErrorCallback callback) {
	std::lock_guard lock(impl_->data->callback_mutex);
	impl_->data->error_callback = std::move(callback);
}

void WebSocketClient::on_state_change(WsStateCallback callback) {
	std::lock_guard lock(impl_->data->callback_mutex);
	impl_->data->state_callback = std::move(callback);
}

const WsConfig& WebSocketClient::config() const noexcept {
	return impl_->data->config;
}

} // namespace kalshi
