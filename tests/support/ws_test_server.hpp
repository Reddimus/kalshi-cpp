#pragma once

// A local libwebsockets server that plays Kalshi's side of the WebSocket
// protocol for tests. It records handshake headers and the commands a client
// sends, and sends or drops frames when a test asks.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <libwebsockets.h>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace kalshi::test {

class WsTestServer {
public:
	WsTestServer() {
		lws_set_log_level(LLL_ERR, nullptr);
		protocols_[0] =
			lws_protocols{"kalshi-test", &WsTestServer::callback, 0, 65536, 0, nullptr, 0};
		protocols_[1] = lws_protocols{nullptr, nullptr, 0, 0, 0, nullptr, 0};
		lws_context_creation_info info{};
		info.port = 0; // the kernel picks a free port
		info.iface = "127.0.0.1";
		info.protocols = protocols_.data();
		info.user = this;
		context_ = lws_create_context(&info);
		if (context_ != nullptr) {
			port_ = lws_get_vhost_listen_port(lws_get_vhost_by_name(context_, "default"));
			thread_ = std::thread([this] {
				while (!stop_) {
					lws_service(context_, 0);
				}
			});
		}
	}

	~WsTestServer() {
		stop_ = true;
		if (context_ != nullptr) {
			lws_cancel_service(context_);
			thread_.join();
			lws_context_destroy(context_);
		}
	}

	WsTestServer(const WsTestServer&) = delete;
	WsTestServer& operator=(const WsTestServer&) = delete;

	[[nodiscard]] bool running() const { return context_ != nullptr && port_ > 0; }

	[[nodiscard]] std::string url() const {
		return "ws://127.0.0.1:" + std::to_string(port_) + "/trade-api/ws/v2";
	}

	/// The next command a client sent, waiting up to `timeout`.
	std::optional<std::string>
	next_command(std::chrono::milliseconds timeout = std::chrono::seconds{5}) {
		std::unique_lock lock(mutex_);
		if (!changed_.wait_for(lock, timeout, [&] { return !received_.empty(); })) {
			return std::nullopt;
		}
		std::string command = std::move(received_.front());
		received_.pop_front();
		return command;
	}

	bool wait_for_connections(int count,
							  std::chrono::milliseconds timeout = std::chrono::seconds{10}) {
		std::unique_lock lock(mutex_);
		return changed_.wait_for(lock, timeout,
								 [&] { return connections_ >= count && client_ != nullptr; });
	}

	/// Sends a frame to the current connection.
	void send(std::string frame) {
		const std::lock_guard lock(mutex_);
		outgoing_.push_back(std::move(frame));
		lws_cancel_service(context_);
	}

	/// Closes the current connection, as a server restart would.
	void drop() {
		const std::lock_guard lock(mutex_);
		drop_ = true;
		lws_cancel_service(context_);
	}

	/// Refuses new handshakes when true.
	void reject(bool value) {
		const std::lock_guard lock(mutex_);
		reject_ = value;
	}

	/// Accepts new connections and closes them right away when true.
	void drop_on_connect(bool value) {
		const std::lock_guard lock(mutex_);
		drop_on_connect_ = value;
	}

	[[nodiscard]] std::map<std::string, std::string> headers() const {
		const std::lock_guard lock(mutex_);
		return headers_;
	}

	[[nodiscard]] int connections() const {
		const std::lock_guard lock(mutex_);
		return connections_;
	}

private:
	// Empty when libwebsockets was built without LWS_WITH_CUSTOM_HEADERS.
	static std::string header([[maybe_unused]] lws* wsi, [[maybe_unused]] const char* name) {
#if !defined(LWS_WITH_CUSTOM_HEADERS)
		return {};
#else
		const int length = lws_hdr_custom_length(wsi, name, static_cast<int>(std::strlen(name)));
		if (length <= 0) {
			return {};
		}
		std::vector<char> buffer(static_cast<std::size_t>(length) + 1);
		if (lws_hdr_custom_copy(wsi, buffer.data(), static_cast<int>(buffer.size()), name,
								static_cast<int>(std::strlen(name))) < 0) {
			return {};
		}
		return std::string(buffer.data(), static_cast<std::size_t>(length));
#endif
	}

	// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): libwebsockets' callback ABI
	static int callback(lws* wsi, lws_callback_reasons reason, void* /*user*/, void* in,
						std::size_t len) {
		lws_context* context = lws_get_context(wsi);
		WsTestServer* self =
			context == nullptr ? nullptr : static_cast<WsTestServer*>(lws_context_user(context));
		return self == nullptr ? 0 : self->on_event(wsi, reason, in, len);
	}

	int on_event(lws* wsi, lws_callback_reasons reason, void* in, std::size_t len) {
		switch (reason) {
			case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
				const std::lock_guard lock(mutex_);
				return reject_ ? 1 : 0;
			}
			case LWS_CALLBACK_ESTABLISHED: {
				std::map<std::string, std::string> seen;
				for (const char* name : {"kalshi-access-key:", "kalshi-access-signature:",
										 "kalshi-access-timestamp:", "origin:"}) {
					seen[name] = header(wsi, name);
				}
				std::array<char, 256> uri{};
				if (lws_hdr_copy(wsi, uri.data(), static_cast<int>(uri.size()), WSI_TOKEN_GET_URI) >
					0) {
					seen["path"] = uri.data();
				}
				bool drop_now = false;
				{
					const std::lock_guard lock(mutex_);
					headers_ = std::move(seen);
					client_ = wsi;
					++connections_;
					outgoing_.clear();
					drop_ = drop_on_connect_;
					drop_now = drop_on_connect_;
				}
				changed_.notify_all();
				if (drop_now) {
					lws_callback_on_writable(wsi);
				}
				return 0;
			}
			case LWS_CALLBACK_RECEIVE: {
				const std::lock_guard lock(mutex_);
				partial_.append(static_cast<const char*>(in), len);
				if (lws_is_final_fragment(wsi) != 0 && lws_remaining_packet_payload(wsi) == 0) {
					received_.push_back(std::move(partial_));
					partial_.clear();
				}
			}
				changed_.notify_all();
				return 0;
			case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
				const std::lock_guard lock(mutex_);
				if (client_ != nullptr && (drop_ || !outgoing_.empty())) {
					lws_callback_on_writable(client_);
				}
				return 0;
			}
			case LWS_CALLBACK_SERVER_WRITEABLE: {
				std::string frame;
				bool more = false;
				{
					const std::lock_guard lock(mutex_);
					if (wsi != client_) {
						return 0;
					}
					if (drop_) {
						drop_ = false;
						client_ = nullptr;
						return -1;
					}
					if (outgoing_.empty()) {
						return 0;
					}
					frame = std::move(outgoing_.front());
					outgoing_.pop_front();
					more = !outgoing_.empty();
				}
				std::vector<unsigned char> buffer(LWS_PRE + frame.size());
				std::memcpy(buffer.data() + LWS_PRE, frame.data(), frame.size());
				if (lws_write(wsi, buffer.data() + LWS_PRE, frame.size(), LWS_WRITE_TEXT) <
					static_cast<int>(frame.size())) {
					return -1;
				}
				if (more) {
					lws_callback_on_writable(wsi);
				}
				return 0;
			}
			case LWS_CALLBACK_CLOSED: {
				const std::lock_guard lock(mutex_);
				if (wsi == client_) {
					client_ = nullptr;
				}
				return 0;
			}
			default:
				return 0;
		}
	}

	std::array<lws_protocols, 2> protocols_{};
	lws_context* context_{nullptr};
	int port_{0};
	std::thread thread_;
	std::atomic<bool> stop_{false};

	mutable std::mutex mutex_;
	std::condition_variable changed_;
	std::deque<std::string> received_;
	std::deque<std::string> outgoing_;
	std::string partial_;
	std::map<std::string, std::string> headers_;
	lws* client_{nullptr};
	int connections_{0};
	bool drop_{false};
	bool reject_{false};
	bool drop_on_connect_{false};
};

} // namespace kalshi::test
