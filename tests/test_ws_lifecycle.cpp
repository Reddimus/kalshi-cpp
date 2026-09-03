/// @file test_ws_lifecycle.cpp
/// @brief Lifecycle/move-from regression tests for kalshi::WebSocketClient.
///
/// kalshi-cpp has shipped two SIGSEGV fixes in WebSocketClient already
/// (v0.0.9 reaped a leftover service thread/lws context before
/// reconnect — see commit ``49b2634``). This file pins the **third**
/// crash mode: a moved-from instance whose impl_ is nullptr. The
/// defaulted move ctor leaves the source object's unique_ptr null;
/// when the source then runs ~WebSocketClient (which calls
/// disconnect()), the previous code dereferenced impl_->data and
/// segfaulted in the implicit destructor. The fix is a single
/// ``if (!impl_) return;`` guard at the top of every method that
/// touches impl_, mirroring the polymarket-cpp clob::WebSocketClient
/// and polymarket::us::ws::Subscriber impls.
///
/// Live WS smoke tests against the Kalshi Trade API are out of
/// scope here (no creds, no exchange round-trip on CI). These tests
/// don't connect, so they don't need network access.

#include <future>
#include <gtest/gtest.h>
#include <kalshi/detail/callback_slot.hpp>
#include <kalshi/signer.hpp>
#include <kalshi/websocket.hpp>
#include <utility>

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

	for (const std::string& url :
		 {"https://example.test/ws", "wss:///ws", "wss://user@example.test/ws",
		  "wss://example.test:/ws", "wss://example.test:not-a-port/ws",
		  "wss://example.test:70000/ws", "wss://::1/ws", "wss://example.test/ws#fragment"}) {
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

TEST(WsLifecycle, MoveConstructLeavesMovedFromSafe) {
	// Regression: defaulted move ctor leaves moved-from impl_ as
	// nullptr. Pre-fix, calling is_connected() on the moved-from
	// object dereferenced through nullptr (impl_->data->connected)
	// and segfaulted. The implicit destructor on the moved-from
	// object then called disconnect(), which had the same deref.
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);

	kalshi::WebSocketClient b(std::move(a));
	EXPECT_FALSE(b.is_connected());
	// a is moved-from — accessors must remain safe.
	EXPECT_FALSE(a.is_connected());
	// Implicit ~WebSocketClient on a, b follows — must not crash.
}

TEST(WsLifecycle, MoveAssignLeavesMovedFromSafe) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(signer);

	b = std::move(a);
	EXPECT_FALSE(b.is_connected());
	EXPECT_FALSE(a.is_connected());
	// Both go out of scope here; no segfault.
}

TEST(WsLifecycle, MovedFromSubscribeReturnsNetworkError) {
	// Mutator path — subscribe_orderbook on the moved-from instance
	// should surface a clean error rather than deref the nullptr.
	// (subscribe_* checks ``data->connected`` before doing anything,
	// so the null-guard in the impl prevents the deref.)
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(std::move(a));

	kalshi::Result<kalshi::SubscriptionId> rc = a.subscribe_orderbook({"DUMMY-MARKET-TICKER"});
	EXPECT_FALSE(rc.has_value());
}

TEST(WsLifecycle, MovedFromConfigReturnsEmpty) {
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(std::move(a));
	// Accessor must not crash on moved-from. Returns the static
	// empty WsConfig sentinel — the contents are unspecified beyond
	// "default-constructed", but the call is safe.
	const kalshi::WsConfig& cfg = a.config();
	(void)cfg;
	SUCCEED();
}

TEST(WsLifecycle, MovedFromCallbackSetterDoesNotCrash) {
	// on_message / on_error / on_state_change setters were
	// previously unguarded; passing through impl_->data->callback_mutex
	// crashed on a moved-from instance.
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(std::move(a));

	a.on_message([](const kalshi::WsMessage&) {});
	a.on_error([](const kalshi::WsError&) {});
	a.on_state_change([](bool) {});
	SUCCEED();
}

TEST(WsLifecycle, DefaultIsConnectedFalseAfterMove) {
	// Belt-and-braces: chained move (a -> b -> c) followed by all
	// three going out of scope. Pin that ~WebSocketClient is safe
	// for both moved-from AND moved-into instances regardless of
	// whether disconnect() was ever called explicitly.
	kalshi::Signer signer = make_test_signer();
	kalshi::WebSocketClient a(signer);
	kalshi::WebSocketClient b(std::move(a));
	kalshi::WebSocketClient c(std::move(b));
	EXPECT_FALSE(c.is_connected());
	EXPECT_FALSE(b.is_connected());
	EXPECT_FALSE(a.is_connected());
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

TEST(WsLifecycle, ServiceThreadNeverJoinsItself) {
	std::promise<void> assigned;
	std::shared_future<void> may_check = assigned.get_future().share();
	std::promise<bool> result;
	std::thread service_thread;
	service_thread = std::thread([&] {
		may_check.wait();
		result.set_value(kalshi::detail::join_thread_unless_current(service_thread));
	});
	assigned.set_value();
	EXPECT_FALSE(result.get_future().get());
	ASSERT_TRUE(service_thread.joinable());
	service_thread.join();
}
