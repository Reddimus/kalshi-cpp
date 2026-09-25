// WebSocketClient behavior that needs no server: URL validation, moved-from
// objects, and concurrent calls.

#include "kalshi/detail/http_path.hpp"
#include "kalshi/signer.hpp"
#include "kalshi/websocket.hpp"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <utility>
#include <vector>

#include "callback_slot.hpp"
#include "test_signer_fixture.hpp"
#include "ws_endpoint.hpp"

namespace {

kalshi::Signer make_test_signer() {
	return kalshi::test::make_signer();
}

} // namespace

TEST(WsLifecycle, DefaultIsDisconnected) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient ws(signer);
	EXPECT_FALSE(ws.is_connected());
}

TEST(WsLifecycle, ConfigAccessorReturnsConfig) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WsConfig cfg;
	cfg.url = "wss://example.test/ws";
	kalshi::WebSocketClient ws(signer, cfg);
	EXPECT_EQ(ws.config().url, "wss://example.test/ws");
}

TEST(WsLifecycle, InvalidUrlReturnsErrorWithoutNetworkOrExceptions) {
	kalshi::Signer signer = make_test_signer();

	for (const char* url :
		 {"https://example.test/ws", "wss://", "wss:///ws", "wss://user@example.test/ws",
		  "wss://example.test:/ws", "wss://example.test:not-a-port/ws", "wss://example.test:0/ws",
		  "wss://example.test:65536/ws", "wss://example.test:70000/ws", "wss://::1/ws",
		  "wss://[::1/ws", "wss://[]/ws", "wss://[::1]x/ws", "wss://exa mple.test/ws",
		  "wss://example.test/ws#fragment", "wss://example.test?token=value#fragment"}) {
		kalshi::WsConfig cfg;
		cfg.url = url;
		kalshi::WebSocketClient ws(signer, cfg);
		kalshi::Result<void> result;
		EXPECT_NO_THROW(result = ws.connect()) << url;
		ASSERT_FALSE(result.has_value()) << url;
		EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest) << url;
	}
}

TEST(WsLifecycle, UrlParserPreservesConnectionPathAndCustomPort) {
	const kalshi::Result<kalshi::detail::WsEndpoint> result =
		kalshi::detail::parse_ws_endpoint("wss://example.test:8443/ws/v2?token=value");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->host, "example.test");
	EXPECT_EQ(result->path, "/ws/v2?token=value");
	EXPECT_EQ(result->port, 8443);
	EXPECT_TRUE(result->use_ssl);
}

TEST(WsLifecycle, UrlParserSupportsBracketedIpv6AndQueryOnlyPath) {
	const kalshi::Result<kalshi::detail::WsEndpoint> result =
		kalshi::detail::parse_ws_endpoint("ws://[::1]?token=value");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->host, "::1");
	EXPECT_EQ(result->path, "/?token=value");
	EXPECT_EQ(result->port, 80);
	EXPECT_FALSE(result->use_ssl);
}

TEST(WsLifecycle, DefaultConfigUrlParsesToTheProductionEndpoint) {
	const kalshi::WsConfig defaults;
	const kalshi::Result<kalshi::detail::WsEndpoint> result =
		kalshi::detail::parse_ws_endpoint(defaults.url);
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->host, "external-api-ws.kalshi.com");
	EXPECT_EQ(result->path, "/trade-api/ws/v2");
	EXPECT_EQ(result->port, 443);
	EXPECT_TRUE(result->use_ssl);

	// The signature covers the path without its query string.
	EXPECT_EQ(kalshi::detail::request_signing_path("", result->path), "/trade-api/ws/v2");
}

TEST(WsLifecycle, ConcurrentConnectAttemptsAreSafe) {
	// Run under ThreadSanitizer, this checks connect() and disconnect()
	// share their state safely.
	kalshi::Signer signer = make_test_signer();
	kalshi::WsConfig cfg;
	cfg.url = "not-a-websocket-url";
	kalshi::WebSocketClient ws(signer, cfg);

	std::atomic<int> unexpected_successes{0};
	std::vector<std::thread> threads;
	threads.reserve(8);
	for (int worker = 0; worker < 8; ++worker) {
		threads.emplace_back([&ws, &unexpected_successes]() {
			for (int attempt = 0; attempt < 200; ++attempt) {
				if (ws.connect().has_value()) {
					unexpected_successes.fetch_add(1);
				}
				ws.disconnect();
			}
		});
	}
	for (std::thread& thread : threads) {
		thread.join();
	}

	EXPECT_EQ(unexpected_successes.load(), 0);
	EXPECT_FALSE(ws.is_connected());
}

TEST(WsLifecycle, MoveConstructLeavesMovedFromSafe) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);

	kalshi::WebSocketClient b(std::move(a));
	EXPECT_FALSE(b.is_connected());
	EXPECT_FALSE(a.is_connected()); // NOLINT(bugprone-use-after-move): moved-from must stay safe
}

TEST(WsLifecycle, MoveAssignLeavesMovedFromSafe) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(signer);

	b = std::move(a);
	EXPECT_FALSE(b.is_connected());
	EXPECT_FALSE(a.is_connected()); // NOLINT(bugprone-use-after-move)
}

TEST(WsLifecycle, MovedFromCommandsReturnErrors) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(std::move(a));

	// NOLINTBEGIN(bugprone-use-after-move)
	EXPECT_FALSE(a.subscribe(kalshi::ws::Channel::Ticker).has_value());
	EXPECT_FALSE(a.unsubscribe({1, kalshi::ws::Channel::Ticker}).has_value());
	EXPECT_FALSE(a.list_subscriptions().has_value());
	EXPECT_FALSE(a.connect().has_value());
	EXPECT_TRUE(a.subscriptions().empty());
	EXPECT_EQ(a.state(), kalshi::WsState::Disconnected);
	// NOLINTEND(bugprone-use-after-move)
}

TEST(WsLifecycle, MovedFromAccessorsAndSettersAreSafe) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(std::move(a));

	// NOLINTBEGIN(bugprone-use-after-move)
	EXPECT_EQ(a.config().url, kalshi::WsConfig{}.url);
	a.on_message([](const kalshi::WsMessage&) {});
	a.on_error([](const kalshi::WsError&) {});
	a.on_state_change([](kalshi::WsState) {});
	a.disconnect();
	// NOLINTEND(bugprone-use-after-move)
}

TEST(WsLifecycle, SubscriptionsWaitForAConnection) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient ws(signer);
	const kalshi::Result<kalshi::ws::Subscription> ticker = ws.subscribe(
		kalshi::ws::Channel::Ticker, kalshi::ws::SubscribeParams{.market_tickers = {"A"}});
	ASSERT_TRUE(ticker.has_value());
	EXPECT_EQ(ws.subscriptions(), (std::vector<kalshi::ws::Subscription>{*ticker}));
	EXPECT_TRUE(ws.add_markets(*ticker, {"B"}).has_value());
	EXPECT_FALSE(ws.list_subscriptions().has_value()); // needs a connection
	EXPECT_FALSE(ws.subscribe(kalshi::ws::Channel::Unknown).has_value());

	EXPECT_TRUE(ws.unsubscribe(*ticker).has_value());
	EXPECT_TRUE(ws.subscriptions().empty());
	EXPECT_FALSE(ws.unsubscribe(*ticker).has_value());
}

TEST(WsLifecycle, CallbackMayReplaceItselfWithoutDeadlocking) {
	kalshi::detail::CallbackSlot<void(int)> callback;
	int observed = 0;
	callback.set([&](int value) {
		observed = value;
		callback.set([](int) {});
	});
	callback.invoke(42);
	EXPECT_EQ(observed, 42);
}
