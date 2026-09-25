#include "kalshi/websocket.hpp"

#include "kalshi/detail/http_path.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <libwebsockets.h>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "callback_slot.hpp"
#include "frames.hpp"
#include "subscriptions.hpp"
#include "ws_endpoint.hpp"

#ifndef _WIN32
#include <csignal>
#endif

namespace kalshi {

namespace {

// A namespace-scope object, so the noexcept config() accessor never
// allocates while initializing a static.
const WsConfig kMovedFromConfig{};

Error moved_from() {
	return Error::network("WebSocketClient was moved from");
}

// A connection that drops sooner than this counts as a failed attempt, so a
// server that accepts and then closes at once still backs off and gives up.
constexpr std::chrono::seconds kStableConnection{10};

std::uint16_t seconds_field(std::chrono::seconds value) {
	return static_cast<std::uint16_t>(std::clamp<std::int64_t>(value.count(), 1, 65535));
}

/// libwebsockets calls OPENSSL_cleanup() when the last context created with
/// LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT is destroyed (4.4 and later). OpenSSL
/// cannot start again after that, so every later TLS connection in the
/// process would fail, REST calls included. One idle context that is never
/// destroyed keeps that count above zero. The static pointer keeps it
/// reachable, so leak checkers do not report it.
void keep_openssl_initialized() {
	static lws_context* const keeper = [] {
		static const std::array<lws_protocols, 2> protocols{{
			{"kalshi-keeper", lws_callback_http_dummy, 0, 0, 0, nullptr, 0},
			{nullptr, nullptr, 0, 0, 0, nullptr, 0},
		}};
		lws_context_creation_info info{};
		info.port = CONTEXT_PORT_NO_LISTEN;
		info.protocols = protocols.data();
		info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
		return lws_create_context(&info);
	}();
	(void)keeper;
}

/// Network threads of clients destroyed inside their own callbacks. Such a
/// thread cannot join itself, so it finishes on its own; joining it here, at
/// the latest from this object's destructor at exit, keeps it from racing
/// process teardown. The destructor runs before OpenSSL's exit handler, which
/// was registered earlier.
class Orphans {
public:
	static Orphans& instance() {
		static Orphans orphans;
		return orphans;
	}

	Orphans(const Orphans&) = delete;
	Orphans& operator=(const Orphans&) = delete;

	~Orphans() {
		try {
			reap();
		} catch (...) { // NOLINT(bugprone-empty-catch): process exit has no one to report to
		}
	}

	void adopt(std::thread thread) {
		const std::lock_guard lock(mutex_);
		threads_.push_back(std::move(thread));
	}

	/// Joins every adopted thread except the calling one.
	void reap() {
		std::vector<std::thread> threads;
		{
			const std::lock_guard lock(mutex_);
			threads.swap(threads_);
		}
		for (std::thread& thread : threads) {
			if (thread.get_id() == std::this_thread::get_id()) {
				adopt(std::move(thread));
			} else if (thread.joinable()) {
				thread.join();
			}
		}
	}

private:
	Orphans() = default;

	std::mutex mutex_;
	std::vector<std::thread> threads_;
};

} // namespace

// Threads: connect() and disconnect() run on callers' threads; everything that
// touches libwebsockets after connect() runs on `thread`, the service thread.
// `mutex` guards the state both sides share. Service-thread-only members are
// marked below.
struct WebSocketClient::Impl : std::enable_shared_from_this<Impl> {
	// A libwebsockets timer whose owner can be recovered from its address.
	struct Timer {
		lws_sorted_usec_list_t sul;
		Impl* owner{nullptr};
	};

	Impl(Signer signer_in, WsConfig config_in)
		: signer(std::move(signer_in)), config(std::move(config_in)), rng(std::random_device{}()) {
		protocols[0] = lws_protocols{"kalshi-ws", &Impl::callback, 0, 65536, 0, nullptr, 0};
		protocols[1] = lws_protocols{nullptr, nullptr, 0, 0, 0, nullptr, 0};
		retry.secs_since_valid_ping = seconds_field(config.ping_interval);
		retry.secs_since_valid_hangup = seconds_field(config.idle_timeout);
		timer.owner = this;
	}

	const Signer signer;
	const WsConfig config;
	detail::CallbackSlot<void(const WsMessage&)> message_callback;
	detail::CallbackSlot<void(const WsError&)> error_callback;
	detail::CallbackSlot<void(WsState)> state_callback;

	std::mutex lifecycle; // serializes connect() and disconnect() on callers' threads

	mutable std::mutex mutex;
	std::condition_variable changed;
	WsState state{WsState::Disconnected};
	std::atomic<bool> stopping{false};
	std::optional<Error> connect_error; // why the first attempt failed
	bool established{false};			// this session connected at least once
	std::deque<std::string> outbox;
	detail::SubscriptionRegistry registry;
	lws_context* context{nullptr};
	std::thread thread;
	std::thread::id service_id;

	// Service thread only.
	lws* wsi{nullptr};
	std::string rx;
	std::vector<unsigned char> tx;
	AuthHeaders headers;
	detail::WsEndpoint endpoint;
	std::uint32_t failures{0};
	bool open{false};			 // the current connection finished its handshake
	bool failure_handled{false}; // on_failed() already dealt with this attempt
	std::chrono::steady_clock::time_point opened_at;
	std::minstd_rand rng;
	Timer timer{};
	lws_retry_bo_t retry{};
	std::array<lws_protocols, 2> protocols{};

	[[nodiscard]] bool on_service_thread() const {
		const std::lock_guard lock(mutex);
		return service_id == std::this_thread::get_id();
	}

	// ----- callbacks to the user --------------------------------------------

	void deliver(const WsMessage& message) const {
		if (!stopping) {
			message_callback.invoke(message);
		}
	}

	void deliver(const WsError& error) const {
		if (!stopping) {
			error_callback.invoke(error);
		}
	}

	void report(WsState next) const {
		if (!stopping || next == WsState::Disconnected) {
			state_callback.invoke(next);
		}
	}

	// ----- lifecycle ---------------------------------------------------------

	Result<void> connect() {
		if (on_service_thread()) {
			return std::unexpected(
				Error::network("connect() cannot run inside a WebSocket callback"));
		}
		const std::lock_guard lifecycle_lock(lifecycle);
		{
			const std::lock_guard lock(mutex);
			if (state == WsState::Connected) {
				return {};
			}
		}
		stop();
		reap(); // a session that dropped or gave up

		Result<detail::WsEndpoint> parsed = detail::parse_ws_endpoint(config.url);
		if (!parsed) {
			return std::unexpected(std::move(parsed.error()));
		}
		endpoint = std::move(*parsed);

		lws_context_creation_info info{};
		info.port = CONTEXT_PORT_NO_LISTEN;
		info.protocols = protocols.data();
		info.user = this;
		// Needed for client TLS; keep_openssl_initialized() stops its cleanup.
		keep_openssl_initialized();
		info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
		// Bounds each reconnect's handshake the way connect_timeout bounds the first.
		info.timeout_secs = static_cast<unsigned int>(std::max<std::int64_t>(
			1, std::chrono::ceil<std::chrono::seconds>(config.connect_timeout).count()));
		lws_context* created = lws_create_context(&info);
		if (created == nullptr) {
			return std::unexpected(Error::network("Failed to create the WebSocket context"));
		}
		{
			const std::lock_guard lock(mutex);
			context = created;
			stopping = false;
			state = WsState::Connecting;
			established = false;
			connect_error.reset();
			outbox.clear();
			failures = 0;
		}
		wsi = nullptr; // no service thread runs yet
		open = false;

		if (Result<void> started = attempt(); !started) {
			std::optional<Error> reason;
			{
				const std::lock_guard lock(mutex);
				reason = connect_error; // libwebsockets may have reported why
			}
			stop();
			{
				// Clear the shared pointer first so a concurrent stop() cannot wake
				// a destroyed context.
				const std::lock_guard lock(mutex);
				context = nullptr;
			}
			lws_context_destroy(created);
			return std::unexpected(reason ? std::move(*reason) : std::move(started.error()));
		}
		{
			const std::lock_guard lock(mutex);
			thread = std::thread([self = shared_from_this()] { self->run(); });
			service_id = thread.get_id();
		}

		std::unique_lock lock(mutex);
		const bool settled = changed.wait_for(lock, config.connect_timeout, [&] {
			return state == WsState::Connected || connect_error.has_value() || stopping ||
				   established;
		});
		// A connection that opened and dropped at once is reconnecting already.
		if (state == WsState::Connected || (established && !stopping && config.auto_reconnect)) {
			return {};
		}
		Error error =
			Error::network("Timed out after " + std::to_string(config.connect_timeout.count()) +
						   " ms waiting for the WebSocket handshake");
		if (connect_error) {
			error = *connect_error;
		} else if (established && !stopping) {
			error = Error::network("The WebSocket connection closed right after it opened");
		} else if (settled) {
			error = Error::network("Disconnected while connecting");
		}
		lock.unlock();
		stop();
		reap();
		return std::unexpected(std::move(error));
	}

	/// Ends the session without waiting for the service thread, and returns
	/// whether one was running. Subscriptions stay registered for the next
	/// connect() unless `forget` is set. Calls no user code, so it is safe
	/// under `lifecycle`.
	bool stop(bool forget = false) {
		bool was_up = false;
		{
			const std::lock_guard lock(mutex);
			was_up = state != WsState::Disconnected;
			stopping = true;
			state = WsState::Disconnected;
			if (forget) {
				registry.clear();
			} else {
				registry.on_disconnected();
			}
			outbox.clear();
			if (context != nullptr) {
				lws_cancel_service(context);
			}
		}
		changed.notify_all();
		return was_up;
	}

	/// Joins a stopped service thread. Never called on that thread.
	void reap() {
		std::thread worker;
		{
			const std::lock_guard lock(mutex);
			worker = std::move(thread);
		}
		if (worker.joinable()) {
			worker.join();
		}
		const std::lock_guard lock(mutex);
		service_id = {};
	}

	void disconnect() {
		bool was_up = stop(true); // also interrupts a connect() in progress
		if (!on_service_thread()) {
			const std::lock_guard lifecycle_lock(lifecycle);
			// A connect() may have started a new session before we got the lock.
			was_up = stop(true) || was_up;
			reap();
		} // else the thread exits once this callback returns; connect() or ~Impl reaps it
		if (was_up) {
			report(WsState::Disconnected);
		}
	}

	/// Tears everything down for the destructor. Failures, such as a thread
	/// that cannot be joined, have no caller to report to.
	void destroy() noexcept {
		try {
			teardown();
		} catch (...) { // NOLINT(bugprone-empty-catch): see above
		}
	}

	void teardown() {
		message_callback.set(nullptr);
		error_callback.set(nullptr);
		state_callback.set(nullptr);
		stop(true);
		if (on_service_thread()) {
			// The thread holds its own reference and finishes after this callback.
			std::thread self;
			{
				const std::lock_guard lock(mutex);
				self = std::move(thread);
			}
			if (self.joinable()) {
				Orphans::instance().adopt(std::move(self));
			}
			return;
		}
		const std::lock_guard lifecycle_lock(lifecycle);
		stop(true); // in case a connect() started a session before we got the lock
		reap();
		Orphans::instance().reap();
	}

	void run() {
#ifndef _WIN32
		// OpenSSL sends with write(), which raises SIGPIPE once the peer has reset the
		// connection. libwebsockets ignores SIGPIPE process-wide, but the app may restore
		// the default, which ends the process, so block it on this thread.
		sigset_t pipe{};
		sigemptyset(&pipe);
		sigaddset(&pipe, SIGPIPE);
		pthread_sigmask(SIG_BLOCK, &pipe, nullptr);
#endif
		lws_context* active = nullptr;
		{
			const std::lock_guard lock(mutex);
			active = context;
		}
		while (!stopping) {
			if (lws_service(active, 0) < 0 && stop()) {
				report(WsState::Disconnected);
			}
		}
		{
			const std::lock_guard lock(mutex);
			context = nullptr;
		}
		// The timer lives in this object, not the context; unlink it first so a
		// later connect() never touches the destroyed context's timer list.
		lws_sul_cancel(&timer.sul);
		lws_context_destroy(active);
		timer.sul = lws_sorted_usec_list_t{};
		wsi = nullptr;
		open = false;
	}

	// ----- connection attempts (service thread, or before it starts) ---------

	Result<void> attempt() {
		Result<AuthHeaders> signed_headers =
			signer.sign("GET", detail::request_signing_path("", endpoint.path));
		if (!signed_headers) {
			return std::unexpected(std::move(signed_headers.error()));
		}
		headers = std::move(*signed_headers);

		lws_context* active = nullptr;
		{
			const std::lock_guard lock(mutex);
			active = context;
		}
		lws_client_connect_info info{};
		info.context = active;
		info.address = endpoint.host.c_str();
		info.port = endpoint.port;
		info.path = endpoint.path.c_str();
		info.host = endpoint.host.c_str();
		// Kalshi rejects upgrades that carry an Origin header, so leave it unset.
		info.origin = nullptr;
		info.ssl_connection = endpoint.use_ssl ? LCCSCF_USE_SSL : 0;
		info.retry_and_idle_policy = &retry;
		open = false;
		failure_handled = false;
		wsi = lws_client_connect_via_info(&info);
		if (wsi == nullptr) {
			return std::unexpected(Error::network("Failed to start the WebSocket connection"));
		}
		return {};
	}

	std::chrono::milliseconds backoff() {
		const double doubled =
			static_cast<double>(config.reconnect_delay.count()) *
			std::pow(2.0, static_cast<double>(std::min<std::uint32_t>(failures, 20)));
		const double capped =
			std::min(doubled, static_cast<double>(config.max_reconnect_delay.count()));
		std::uniform_real_distribution<double> jitter(0.5, 1.0);
		return std::chrono::milliseconds{
			static_cast<std::chrono::milliseconds::rep>(std::llround(capped * jitter(rng)))};
	}

	void schedule_reconnect() {
		if (stopping) {
			return;
		}
		if (config.max_reconnect_attempts != 0 && failures >= config.max_reconnect_attempts) {
			{
				const std::lock_guard lock(mutex);
				state = WsState::Disconnected;
			}
			report(WsState::Disconnected);
			deliver(WsError{0,
							"Gave up reconnecting after " + std::to_string(failures) + " attempts",
							std::nullopt, std::nullopt, std::nullopt, std::nullopt});
			return;
		}
		const std::chrono::milliseconds delay = backoff();
		++failures;
		lws_context* active = nullptr;
		{
			const std::lock_guard lock(mutex);
			active = context;
		}
		lws_sul_schedule(active, 0, &timer.sul, &Impl::on_timer,
						 std::chrono::duration_cast<std::chrono::microseconds>(delay).count());
	}

	static void on_timer(lws_sorted_usec_list_t* sul) {
		Impl* self = reinterpret_cast<Timer*>(sul)->owner;
		if (self->stopping) {
			return;
		}
		// A synchronous failure may already have gone through on_failed(), which
		// reported it and scheduled the next attempt or gave up.
		if (Result<void> started = self->attempt(); !started && !self->failure_handled) {
			self->deliver(WsError{0, started.error().message, std::nullopt, std::nullopt,
								  std::nullopt, std::nullopt});
			self->schedule_reconnect();
		}
	}

	// ----- libwebsockets events (service thread) -----------------------------

	// libwebsockets fixes this callback ABI, including the adjacent opaque pointers.
	// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
	static int callback(lws* connection, lws_callback_reasons reason, void* /*user*/, void* in,
						std::size_t len) {
		lws_context* owner = lws_get_context(connection);
		Impl* self = owner == nullptr ? nullptr : static_cast<Impl*>(lws_context_user(owner));
		return self == nullptr ? 0 : self->on_event(connection, reason, in, len);
	}

	int on_event(lws* connection, lws_callback_reasons reason, void* in, std::size_t len) {
		switch (reason) {
			case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
				return append_headers(connection, in, len);
			case LWS_CALLBACK_CLIENT_ESTABLISHED:
				on_established(connection);
				return 0;
			case LWS_CALLBACK_CLIENT_RECEIVE:
				if (connection != wsi || stopping) {
					return stopping ? -1 : 0;
				}
				rx.append(static_cast<const char*>(in), len);
				if (lws_is_final_fragment(connection) != 0 &&
					lws_remaining_packet_payload(connection) == 0) {
					on_frame(connection);
					rx.clear();
				}
				return 0;
			case LWS_CALLBACK_CLIENT_WRITEABLE:
				return connection == wsi ? write_next(connection) : 0;
			case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
				on_failed(connection, in == nullptr
										  ? std::string{"connection failed"}
										  : std::string(static_cast<const char*>(in), len));
				return 0;
			case LWS_CALLBACK_CLIENT_CLOSED:
				on_closed(connection);
				return 0;
			case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
				if (wsi != nullptr && !stopping) {
					const std::lock_guard lock(mutex);
					if (!outbox.empty()) {
						lws_callback_on_writable(wsi);
					}
				}
				return 0;
			default:
				return 0;
		}
	}

	int append_headers(lws* connection, void* in, std::size_t len) const {
		unsigned char** cursor = static_cast<unsigned char**>(in);
		unsigned char* end = *cursor + len;
		const std::array<std::pair<const char*, const std::string*>, 3> fields{{
			{"KALSHI-ACCESS-KEY:", &headers.access_key},
			{"KALSHI-ACCESS-SIGNATURE:", &headers.signature},
			{"KALSHI-ACCESS-TIMESTAMP:", &headers.timestamp},
		}};
		for (const std::pair<const char*, const std::string*>& field : fields) {
			if (lws_add_http_header_by_name(
					connection, reinterpret_cast<const unsigned char*>(field.first),
					reinterpret_cast<const unsigned char*>(field.second->data()),
					static_cast<int>(field.second->size()), cursor, end) != 0) {
				return -1;
			}
		}
		return 0;
	}

	void on_established(lws* connection) {
		std::vector<std::string> frames;
		{
			const std::lock_guard lock(mutex);
			if (stopping) {
				return;
			}
			wsi = connection;
			state = WsState::Connected;
			established = true;
			registry.on_connected(frames);
			outbox.assign(std::make_move_iterator(frames.begin()),
						  std::make_move_iterator(frames.end()));
		}
		rx.clear();
		open = true;
		opened_at = std::chrono::steady_clock::now();
		changed.notify_all();
		if (!frames.empty()) {
			lws_callback_on_writable(connection);
		}
		report(WsState::Connected);
	}

	void on_failed(lws* connection, const std::string& reason) {
		// libwebsockets may report one failed attempt more than once.
		if ((connection != wsi && wsi != nullptr) || failure_handled) {
			return;
		}
		failure_handled = true;
		wsi = nullptr;
		bool first = false;
		{
			const std::lock_guard lock(mutex);
			first = !established;
			if (first) {
				connect_error = Error::network("WebSocket connection failed: " + reason);
			}
		}
		if (first) {
			changed.notify_all();
			return;
		}
		deliver(WsError{0, "Reconnect failed: " + reason, std::nullopt, std::nullopt, std::nullopt,
						std::nullopt});
		schedule_reconnect();
	}

	void on_closed(lws* connection) {
		if (connection != wsi) {
			return;
		}
		if (!open) {
			// Some platforms report a refused upgrade as a close, not an error.
			on_failed(connection, "the server closed the connection during the handshake");
			return;
		}
		open = false;
		wsi = nullptr;
		const bool stable = std::chrono::steady_clock::now() - opened_at >= kStableConnection;
		WsState next = WsState::Disconnected;
		{
			const std::lock_guard lock(mutex);
			registry.on_disconnected();
			outbox.clear();
			if (stopping) {
				return;
			}
			next = config.auto_reconnect ? WsState::Reconnecting : WsState::Disconnected;
			state = next;
		}
		changed.notify_all();
		report(next);
		if (next == WsState::Reconnecting) {
			if (stable) {
				failures = 0;
			}
			schedule_reconnect();
		}
	}

	int write_next(lws* connection) {
		std::string frame;
		bool more = false;
		{
			const std::lock_guard lock(mutex);
			if (outbox.empty() || stopping) {
				return stopping ? -1 : 0;
			}
			frame = std::move(outbox.front());
			outbox.pop_front();
			more = !outbox.empty();
		}
		tx.resize(LWS_PRE + frame.size());
		std::memcpy(tx.data() + LWS_PRE, frame.data(), frame.size());
		const int written =
			lws_write(connection, tx.data() + LWS_PRE, frame.size(), LWS_WRITE_TEXT);
		if (written < static_cast<int>(frame.size())) {
			deliver(WsError{0, "Failed to write to the WebSocket", std::nullopt, std::nullopt,
							std::nullopt, std::nullopt});
			return -1; // drop the connection; reconnecting resends the subscriptions
		}
		if (more) {
			lws_callback_on_writable(connection);
		}
		return 0;
	}

	void on_frame(lws* connection) {
		detail::Frame frame = detail::parse_frame(rx);
		detail::Reaction reaction;
		std::optional<WsMessage> data;
		{
			const std::lock_guard lock(mutex);
			if (WsMessage* message = std::get_if<WsMessage>(&frame)) {
				reaction = registry.on_data(*message, config.resync_on_gap);
				data = std::move(*message);
			} else if (const detail::SubscribedFrame* subscribed =
						   std::get_if<detail::SubscribedFrame>(&frame)) {
				reaction = registry.on_subscribed(*subscribed);
			} else if (const detail::UnsubscribedFrame* unsubscribed =
						   std::get_if<detail::UnsubscribedFrame>(&frame)) {
				reaction = registry.on_unsubscribed(*unsubscribed);
			} else if (const detail::OkFrame* ok = std::get_if<detail::OkFrame>(&frame)) {
				reaction = registry.on_ok(*ok);
			} else if (const detail::ErrorFrame* error = std::get_if<detail::ErrorFrame>(&frame)) {
				reaction = registry.on_error(*error);
			} else if (const detail::MalformedFrame* malformed =
						   std::get_if<detail::MalformedFrame>(&frame)) {
				reaction.error = WsError{0,
										 "Could not parse a " + malformed->type + " frame",
										 std::nullopt,
										 std::nullopt,
										 std::nullopt,
										 std::nullopt};
			}
			for (std::string& command : reaction.frames) {
				outbox.push_back(std::move(command));
			}
		}
		if (!reaction.frames.empty()) {
			lws_callback_on_writable(connection);
		}
		// A gap is reported before the frame that reveals it.
		if (reaction.error) {
			deliver(*reaction.error);
		}
		if (data) {
			deliver(*data);
		}
		if (reaction.message) {
			deliver(*reaction.message);
		}
	}

	// ----- commands from callers' threads ------------------------------------

	/// Queues frames and wakes the service thread. Requires `mutex`.
	void queue(std::vector<std::string>& frames) {
		if (frames.empty()) {
			return;
		}
		for (std::string& frame : frames) {
			outbox.push_back(std::move(frame));
		}
		if (context != nullptr) {
			lws_cancel_service(context);
		}
	}
};

WebSocketClient::WebSocketClient(Signer signer, WsConfig config)
	: impl_(std::make_shared<Impl>(std::move(signer), std::move(config))) {}

WebSocketClient::~WebSocketClient() {
	if (impl_) {
		impl_->destroy();
	}
}

WebSocketClient::WebSocketClient(WebSocketClient&& other) noexcept = default;

WebSocketClient& WebSocketClient::operator=(WebSocketClient&& other) noexcept {
	if (this != &other) {
		if (impl_) {
			impl_->destroy();
		}
		impl_ = std::move(other.impl_);
	}
	return *this;
}

Result<void> WebSocketClient::connect() {
	return impl_ ? impl_->connect() : std::unexpected(moved_from());
}

void WebSocketClient::disconnect() {
	if (impl_) {
		impl_->disconnect();
	}
}

WsState WebSocketClient::state() const noexcept {
	if (!impl_) {
		return WsState::Disconnected;
	}
	const std::lock_guard lock(impl_->mutex);
	return impl_->state;
}

bool WebSocketClient::is_connected() const noexcept {
	return state() == WsState::Connected;
}

Result<ws::Subscription> WebSocketClient::subscribe(ws::Channel channel,
													ws::SubscribeParams params) {
	if (!impl_) {
		return std::unexpected(moved_from());
	}
	if (channel == ws::Channel::Unknown) {
		return std::unexpected(Error{ErrorCode::InvalidRequest, "A channel is required", 0, {}});
	}
	std::vector<std::string> frames;
	const std::lock_guard lock(impl_->mutex);
	const ws::Subscription subscription = impl_->registry.subscribe(
		channel, std::move(params), impl_->state == WsState::Connected, frames);
	impl_->queue(frames);
	return subscription;
}

Result<void> WebSocketClient::unsubscribe(ws::Subscription subscription) {
	if (!impl_) {
		return std::unexpected(moved_from());
	}
	std::vector<std::string> frames;
	const std::lock_guard lock(impl_->mutex);
	Result<void> result = impl_->registry.unsubscribe(subscription, frames);
	impl_->queue(frames);
	return result;
}

Result<void> WebSocketClient::update_subscription(ws::Subscription subscription,
												  const ws::UpdateSubscriptionParams& params) {
	if (!impl_) {
		return std::unexpected(moved_from());
	}
	std::vector<std::string> frames;
	const std::lock_guard lock(impl_->mutex);
	Result<void> result =
		impl_->registry.update(subscription, params, impl_->state == WsState::Connected, frames);
	impl_->queue(frames);
	return result;
}

Result<void> WebSocketClient::add_markets(ws::Subscription subscription,
										  std::vector<std::string> market_tickers) {
	ws::UpdateSubscriptionParams params;
	params.action = ws::UpdateAction::AddMarkets;
	params.market_tickers = std::move(market_tickers);
	return update_subscription(subscription, params);
}

Result<void> WebSocketClient::remove_markets(ws::Subscription subscription,
											 std::vector<std::string> market_tickers) {
	ws::UpdateSubscriptionParams params;
	params.action = ws::UpdateAction::DeleteMarkets;
	params.market_tickers = std::move(market_tickers);
	return update_subscription(subscription, params);
}

Result<void> WebSocketClient::request_snapshot(ws::Subscription subscription,
											   std::vector<std::string> market_tickers) {
	ws::UpdateSubscriptionParams params;
	params.action = ws::UpdateAction::GetSnapshot;
	params.market_tickers = std::move(market_tickers);
	return update_subscription(subscription, params);
}

Result<std::int64_t> WebSocketClient::list_subscriptions() {
	if (!impl_) {
		return std::unexpected(moved_from());
	}
	std::vector<std::string> frames;
	const std::lock_guard lock(impl_->mutex);
	if (impl_->state != WsState::Connected) {
		return std::unexpected(Error::network("Not connected"));
	}
	const std::int64_t id = impl_->registry.list_subscriptions(frames);
	impl_->queue(frames);
	return id;
}

std::vector<ws::Subscription> WebSocketClient::subscriptions() const {
	if (!impl_) {
		return {};
	}
	const std::lock_guard lock(impl_->mutex);
	return impl_->registry.subscriptions();
}

void WebSocketClient::on_message(std::function<void(const WsMessage&)> callback) {
	if (impl_) {
		impl_->message_callback.set(std::move(callback));
	}
}

void WebSocketClient::on_error(std::function<void(const WsError&)> callback) {
	if (impl_) {
		impl_->error_callback.set(std::move(callback));
	}
}

void WebSocketClient::on_state_change(std::function<void(WsState)> callback) {
	if (impl_) {
		impl_->state_callback.set(std::move(callback));
	}
}

const WsConfig& WebSocketClient::config() const noexcept {
	return impl_ ? impl_->config : kMovedFromConfig;
}

} // namespace kalshi
