// WebSocketClient against a local server: the handshake, subscriptions,
// reconnects, and callbacks that tear the client down.

#include "kalshi/websocket.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "support/ws_test_server.hpp"
#include "test_signer_fixture.hpp"

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using namespace std::chrono_literals;
using kalshi::test::WsTestServer;

/// Collects what a client delivers so the test thread can wait for it.
/// Declare it before the client, which calls into it until destroyed.
class Recorder {
public:
	void attach(kalshi::WebSocketClient& ws) {
		ws.on_message([this](const kalshi::WsMessage& message) {
			const std::lock_guard lock(mutex_);
			messages_.push_back(message);
			changed_.notify_all();
		});
		ws.on_error([this](const kalshi::WsError& error) {
			const std::lock_guard lock(mutex_);
			errors_.push_back(error);
			changed_.notify_all();
		});
		ws.on_state_change([this](kalshi::WsState state) {
			const std::lock_guard lock(mutex_);
			states_.push_back(state);
			changed_.notify_all();
		});
	}

	/// Waits for the next message of type T and removes it.
	template <typename T>
	std::optional<T> next(std::chrono::milliseconds timeout = 5s) {
		std::unique_lock lock(mutex_);
		std::vector<kalshi::WsMessage>::iterator found;
		const bool seen = changed_.wait_for(lock, timeout, [&] {
			found =
				std::find_if(messages_.begin(), messages_.end(), [](const kalshi::WsMessage& m) {
					return std::holds_alternative<T>(m);
				});
			return found != messages_.end();
		});
		if (!seen) {
			return std::nullopt;
		}
		T value = std::get<T>(*found);
		messages_.erase(found);
		return value;
	}

	std::optional<kalshi::WsError> next_error(std::chrono::milliseconds timeout = 5s) {
		std::unique_lock lock(mutex_);
		if (!changed_.wait_for(lock, timeout, [&] { return !errors_.empty(); })) {
			return std::nullopt;
		}
		kalshi::WsError error = errors_.front();
		errors_.erase(errors_.begin());
		return error;
	}

	bool reached(kalshi::WsState state, std::chrono::milliseconds timeout = 10s) {
		std::unique_lock lock(mutex_);
		return changed_.wait_for(lock, timeout, [&] {
			return std::find(states_.begin(), states_.end(), state) != states_.end();
		});
	}

	void forget_states() {
		const std::lock_guard lock(mutex_);
		states_.clear();
	}

private:
	std::mutex mutex_;
	std::condition_variable changed_;
	std::vector<kalshi::WsMessage> messages_;
	std::vector<kalshi::WsError> errors_;
	std::vector<kalshi::WsState> states_;
};

kalshi::WsConfig local(const WsTestServer& server) {
	kalshi::WsConfig config;
	config.url = server.url();
	config.connect_timeout = 5s;
	config.reconnect_delay = 20ms;
	config.max_reconnect_delay = 100ms;
	return config;
}

// The command ID in a frame the client sent.
std::int64_t id_of(const std::string& command) {
	const std::size_t start = command.find("\"id\":");
	return start == std::string::npos ? -1 : std::stoll(command.substr(start + 5));
}

bool has(const std::string& text, std::string_view part) {
	return text.find(part) != std::string::npos;
}

std::string subscribed(std::int64_t id, std::string_view channel, std::int64_t sid) {
	return R"({"id":)" + std::to_string(id) + R"(,"type":"subscribed","msg":{"channel":")" +
		   std::string(channel) + R"(","sid":)" + std::to_string(sid) + "}}";
}

std::string delta(std::int64_t sid, std::int64_t seq) {
	return R"({"type":"orderbook_delta","sid":)" + std::to_string(sid) + R"(,"seq":)" +
		   std::to_string(seq) +
		   R"(,"msg":{"market_ticker":"KXA","market_id":"m","price_dollars":"0.5000","delta_fp":"1.00","side":"yes","ts_ms":1}})";
}

/// Subscribes and acknowledges with `sid`, returning the handle.
kalshi::ws::Subscription subscribe_acked(kalshi::WebSocketClient& ws, WsTestServer& server,
										 Recorder& recorder, kalshi::ws::Channel channel,
										 kalshi::ws::SubscribeParams params, std::int64_t sid) {
	const kalshi::Result<kalshi::ws::Subscription> subscription =
		ws.subscribe(channel, std::move(params));
	EXPECT_TRUE(subscription.has_value());
	const std::optional<std::string> command = server.next_command();
	EXPECT_TRUE(command.has_value());
	server.send(subscribed(id_of(command.value_or("")), kalshi::ws::to_string(channel), sid));
	EXPECT_TRUE(recorder.next<kalshi::ws::Subscribed>().has_value());
	return subscription.value_or(kalshi::ws::Subscription{});
}

class WsClient : public ::testing::Test {
protected:
	void SetUp() override {
		if (!server.running()) {
			GTEST_SKIP() << "could not start a local WebSocket server";
		}
	}

	WsTestServer server;
};

} // namespace

TEST_F(WsClient, HandshakeCarriesSignedHeadersAndNoOrigin) {
	kalshi::WebSocketClient ws(kalshi::test::make_signer("key-123"), local(server));
	ASSERT_TRUE(ws.connect().has_value());
	EXPECT_EQ(ws.state(), kalshi::WsState::Connected);
	// The client can see the 101 before the server thread records the handshake.
	ASSERT_TRUE(server.wait_for_connections(1));

	std::map<std::string, std::string> headers = server.headers();
	EXPECT_EQ(headers["path"], "/trade-api/ws/v2");
	if (headers["kalshi-access-key:"].empty()) {
		GTEST_SKIP() << "libwebsockets was built without custom header support";
	}
	EXPECT_EQ(headers["kalshi-access-key:"], "key-123");
	EXPECT_FALSE(headers["kalshi-access-signature:"].empty());
	EXPECT_EQ(headers["kalshi-access-timestamp:"].size(), 13U); // milliseconds
	EXPECT_TRUE(headers["origin:"].empty());
}

TEST_F(WsClient, RefusedHandshakeFailsFastWithTheReason) {
	server.reject(true);
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	const kalshi::Result<void> result = ws.connect();
	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::NetworkError);
	EXPECT_TRUE(has(result.error().message, "connection failed")) << result.error().message;
	EXPECT_LT(std::chrono::steady_clock::now() - start, 4s);
	EXPECT_EQ(ws.state(), kalshi::WsState::Disconnected);

	server.reject(false);
	EXPECT_TRUE(ws.connect().has_value()); // a failed connect leaves the client reusable
}

#ifndef _WIN32
TEST_F(WsClient, ConnectTimesOutWhenTheHandshakeNeverFinishes) {
	// A listening socket that never accepts completes TCP but never upgrades.
	const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
	ASSERT_GE(listener, 0);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	socklen_t length = sizeof(address);
	ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&address), length), 0);
	ASSERT_EQ(::listen(listener, 1), 0);
	ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length), 0);

	kalshi::WsConfig config;
	config.url = "ws://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/ws";
	config.connect_timeout = 300ms;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), config);
	const kalshi::Result<void> result = ws.connect();
	::close(listener);
	ASSERT_FALSE(result.has_value());
	EXPECT_TRUE(has(result.error().message, "Timed out")) << result.error().message;
}
#endif

TEST_F(WsClient, OpenSslStillWorksAfterASessionEnds) {
	{
		kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
		ASSERT_TRUE(ws.connect().has_value());
		ws.disconnect();
	}
	// libwebsockets 4.4+ shuts OpenSSL down when its last TLS context goes away.
	const kalshi::Result<kalshi::Signer> signer =
		kalshi::Signer::from_pem("key", kalshi::test::ed25519_private_key_pem());
	ASSERT_TRUE(signer.has_value()) << signer.error().message;
	EXPECT_TRUE(signer->sign("GET", "/trade-api/v2/portfolio/balance").has_value());
}

TEST_F(WsClient, DataCarriesTheSubscriptionThatProducedIt) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());

	const kalshi::Result<kalshi::ws::Subscription> ticker =
		ws.subscribe(kalshi::ws::Channel::Ticker, {.market_tickers = {"KXA"}});
	ASSERT_TRUE(ticker.has_value());
	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	EXPECT_TRUE(has(
		*command, R"("cmd":"subscribe","params":{"channels":["ticker"],"market_tickers":["KXA"]})"))
		<< *command;

	server.send(subscribed(id_of(*command), "ticker", 42));
	const std::optional<kalshi::ws::Subscribed> ack = recorder.next<kalshi::ws::Subscribed>();
	ASSERT_TRUE(ack.has_value());
	EXPECT_EQ(ack->subscription, *ticker);
	EXPECT_EQ(ack->sid, 42);

	server.send(
		R"({"type":"ticker","sid":42,"msg":{"market_ticker":"KXA","price_dollars":"0.5000"}})");
	const std::optional<kalshi::ws::Update<kalshi::ws::Ticker>> update =
		recorder.next<kalshi::ws::Update<kalshi::ws::Ticker>>();
	ASSERT_TRUE(update.has_value());
	EXPECT_EQ(update->subscription, ticker->id);
	EXPECT_EQ(update->msg.price_dollars, "0.5000");
}

TEST_F(WsClient, SubscriptionsMadeBeforeConnectAreSentOnceConnected) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	const kalshi::Result<kalshi::ws::Subscription> ticker =
		ws.subscribe(kalshi::ws::Channel::Ticker, {.market_tickers = {"KXA"}});
	ASSERT_TRUE(ticker.has_value());
	ASSERT_TRUE(ws.connect().has_value());

	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	EXPECT_TRUE(has(*command, R"("channels":["ticker"],"market_tickers":["KXA"])")) << *command;
	server.send(subscribed(id_of(*command), "ticker", 11));
	const std::optional<kalshi::ws::Subscribed> ack = recorder.next<kalshi::ws::Subscribed>();
	ASSERT_TRUE(ack.has_value());
	EXPECT_EQ(ack->subscription, *ticker);

	ws.disconnect(); // forgets them
	EXPECT_TRUE(ws.subscriptions().empty());
}

TEST_F(WsClient, UnsubscribeBeforeTheAckWaitsForTheServersSid) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());

	const kalshi::Result<kalshi::ws::Subscription> trades =
		ws.subscribe(kalshi::ws::Channel::Trade);
	ASSERT_TRUE(trades.has_value());
	const std::optional<std::string> subscribe = server.next_command();
	ASSERT_TRUE(subscribe.has_value());
	ASSERT_TRUE(ws.unsubscribe(*trades).has_value());
	EXPECT_FALSE(server.next_command(200ms).has_value()); // nothing to name yet

	server.send(subscribed(id_of(*subscribe), "trade", 7));
	const std::optional<std::string> unsubscribe = server.next_command();
	ASSERT_TRUE(unsubscribe.has_value());
	EXPECT_TRUE(has(*unsubscribe, R"("cmd":"unsubscribe","params":{"sids":[7]})")) << *unsubscribe;

	server.send(R"({"id":)" + std::to_string(id_of(*unsubscribe)) +
				R"(,"sid":7,"seq":1,"type":"unsubscribed"})");
	const std::optional<kalshi::ws::Unsubscribed> done = recorder.next<kalshi::ws::Unsubscribed>();
	ASSERT_TRUE(done.has_value());
	EXPECT_EQ(done->subscription, *trades);
	EXPECT_TRUE(ws.subscriptions().empty());
}

TEST_F(WsClient, ReconnectsAndResubscribesWithTheCurrentMarkets) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());
	const kalshi::ws::Subscription book = subscribe_acked(
		ws, server, recorder, kalshi::ws::Channel::OrderbookDelta, {.market_tickers = {"KXA"}}, 1);

	ASSERT_TRUE(ws.add_markets(book, {"KXB"}).has_value());
	const std::optional<std::string> update = server.next_command();
	ASSERT_TRUE(update.has_value());
	EXPECT_TRUE(
		has(*update, R"("params":{"sids":[1],"market_tickers":["KXB"],"action":"add_markets"})"))
		<< *update;

	recorder.forget_states();
	server.drop();
	ASSERT_TRUE(recorder.reached(kalshi::WsState::Reconnecting));
	ASSERT_TRUE(recorder.reached(kalshi::WsState::Connected));
	const std::optional<std::string> resubscribe = server.next_command();
	ASSERT_TRUE(resubscribe.has_value());
	EXPECT_TRUE(
		has(*resubscribe, R"("channels":["orderbook_delta"],"market_tickers":["KXA","KXB"])"))
		<< *resubscribe;

	server.send(subscribed(id_of(*resubscribe), "orderbook_delta", 2));
	const std::optional<kalshi::ws::Subscribed> ack = recorder.next<kalshi::ws::Subscribed>();
	ASSERT_TRUE(ack.has_value());
	EXPECT_EQ(ack->subscription, book);
	EXPECT_EQ(ack->sid, 2);

	server.send(delta(2, 1));
	const std::optional<kalshi::ws::Update<kalshi::ws::OrderbookDelta>> data =
		recorder.next<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>();
	ASSERT_TRUE(data.has_value());
	EXPECT_EQ(data->subscription, book.id);
	EXPECT_EQ(server.connections(), 2);
}

TEST_F(WsClient, SequenceGapsAreReportedAndResynced) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());
	const kalshi::ws::Subscription book = subscribe_acked(
		ws, server, recorder, kalshi::ws::Channel::OrderbookDelta, {.market_tickers = {"KXA"}}, 3);

	server.send(delta(3, 1));
	server.send(delta(3, 2));
	server.send(delta(3, 5));
	const std::optional<kalshi::WsError> gap = recorder.next_error();
	ASSERT_TRUE(gap.has_value());
	EXPECT_EQ(gap->code, 0);
	EXPECT_EQ(gap->seq, 5);
	EXPECT_EQ(gap->subscription, book);
	EXPECT_TRUE(has(gap->message, "expected seq 3, got 5")) << gap->message;

	const std::optional<std::string> resync = server.next_command();
	ASSERT_TRUE(resync.has_value());
	EXPECT_TRUE(has(*resync, R"("sids":[3],"market_tickers":["KXA"],"action":"get_snapshot")"))
		<< *resync;
}

TEST_F(WsClient, ServerErrorsNameTheirCommandAndSubscription) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());

	const kalshi::Result<kalshi::ws::Subscription> fills = ws.subscribe(kalshi::ws::Channel::Fill);
	ASSERT_TRUE(fills.has_value());
	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	server.send(R"({"id":)" + std::to_string(id_of(*command)) +
				R"(,"type":"error","msg":{"code":9,"msg":"Authentication required"}})");

	const std::optional<kalshi::WsError> error = recorder.next_error();
	ASSERT_TRUE(error.has_value());
	EXPECT_EQ(error->code, 9);
	EXPECT_EQ(error->message, "Authentication required");
	EXPECT_EQ(error->id, id_of(*command));
	EXPECT_EQ(error->subscription, *fills);
	EXPECT_TRUE(ws.subscriptions().empty());
}

TEST_F(WsClient, ListSubscriptionsRepliesWithTheCommandId) {
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());

	const kalshi::Result<std::int64_t> id = ws.list_subscriptions();
	ASSERT_TRUE(id.has_value());
	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	EXPECT_EQ(*command, R"({"id":)" + std::to_string(*id) + R"(,"cmd":"list_subscriptions"})");

	server.send(R"({"id":)" + std::to_string(*id) +
				R"(,"type":"ok","msg":[{"channel":"ticker","sid":2},{"channel":"fill","sid":3}]})");
	const std::optional<kalshi::ws::SubscriptionList> list =
		recorder.next<kalshi::ws::SubscriptionList>();
	ASSERT_TRUE(list.has_value());
	EXPECT_EQ(list->id, *id);
	ASSERT_EQ(list->subscriptions.size(), 2U);
	EXPECT_EQ(list->subscriptions[1].channel, "fill");
	EXPECT_EQ(list->subscriptions[1].sid, 3);
}

TEST_F(WsClient, GivesUpAfterMaxReconnectAttempts) {
	kalshi::WsConfig config = local(server);
	config.max_reconnect_attempts = 2;
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), config);
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());
	ASSERT_TRUE(server.wait_for_connections(1));

	recorder.forget_states();
	server.reject(true);
	server.drop();
	ASSERT_TRUE(recorder.reached(kalshi::WsState::Disconnected));
	std::optional<kalshi::WsError> error;
	while ((error = recorder.next_error(2s)) && !has(error->message, "Gave up")) {
	}
	ASSERT_TRUE(error.has_value());
	EXPECT_TRUE(has(error->message, "after 2 attempts")) << error->message;
	EXPECT_EQ(ws.state(), kalshi::WsState::Disconnected);
}

TEST_F(WsClient, ConnectionsThatDropAtOnceCountAsFailedAttempts) {
	kalshi::WsConfig config = local(server);
	config.max_reconnect_attempts = 2;
	Recorder recorder;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), config);
	recorder.attach(ws);
	ASSERT_TRUE(ws.connect().has_value());
	ASSERT_TRUE(server.wait_for_connections(1));

	recorder.forget_states();
	server.drop_on_connect(true); // accepted, then closed: no backoff reset
	server.drop();
	ASSERT_TRUE(recorder.reached(kalshi::WsState::Disconnected));
	EXPECT_EQ(server.connections(), 3); // the first connection and two attempts
}

TEST_F(WsClient, DestroyingTheClientInsideItsCallbackIsSafe) {
	std::promise<void> destroyed;
	std::unique_ptr<kalshi::WebSocketClient> ws =
		std::make_unique<kalshi::WebSocketClient>(kalshi::test::make_signer(), local(server));
	ASSERT_TRUE(ws->connect().has_value());
	ws->on_message([&](const kalshi::WsMessage&) {
		ws.reset();
		destroyed.set_value();
	});
	const kalshi::Result<kalshi::ws::Subscription> trades =
		ws->subscribe(kalshi::ws::Channel::Trade);
	ASSERT_TRUE(trades.has_value());
	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	server.send(subscribed(id_of(*command), "trade", 5));
	EXPECT_EQ(destroyed.get_future().wait_for(5s), std::future_status::ready);
}

TEST_F(WsClient, DisconnectInsideACallbackThenReconnect) {
	std::promise<void> disconnected;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), local(server));
	ASSERT_TRUE(ws.connect().has_value());
	ws.on_message([&](const kalshi::WsMessage&) {
		ws.disconnect();
		EXPECT_FALSE(ws.connect().has_value()); // not from the network thread
		disconnected.set_value();
	});
	const kalshi::Result<kalshi::ws::Subscription> trades =
		ws.subscribe(kalshi::ws::Channel::Trade);
	ASSERT_TRUE(trades.has_value());
	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	server.send(subscribed(id_of(*command), "trade", 5));
	ASSERT_EQ(disconnected.get_future().wait_for(5s), std::future_status::ready);

	ws.on_message(nullptr);
	EXPECT_TRUE(ws.connect().has_value());
	EXPECT_TRUE(server.wait_for_connections(2));
}
