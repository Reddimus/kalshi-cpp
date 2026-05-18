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

#include <gtest/gtest.h>
#include <kalshi/signer.hpp>
#include <kalshi/websocket.hpp>
#include <utility>

namespace {

/// Throwaway 2048-bit RSA key generated specifically for these tests.
/// Used to make ``Signer::from_pem`` succeed so we can construct a
/// ``WebSocketClient`` and exercise its move + destruct paths. Never
/// signs live traffic.
const char* TEST_RSA_KEY = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA3ZNc4DAP9V2Rq8H1zNNb2X7x7hxQrCtWt6D7EnuFXsOLMJ0B
2WPDUVlvVlsF5EofNM0Tejf3BP6x1qtPxY+LC1oGcvVPJhQp3AiCRjt1fpZeEqaR
H9/pSatURcb+E/M4kL4F4IK1vn6RnqA/VqO2WQiKCMfQgL9h+XCNi12wX9l+sH+8
6mIX2yan0UeKGKvLBgNjdqxaD9QElo0lz+APvNF9bTSHe4zSycr8MQvWVmY+/CG9
6JzIid/SFChYJjZ5btDRZy8/iHftf99lo1pJ80I/NKiHk/wlsiQpJpAVmBYBfOxp
4YS3Uy3uEnWvTvbGl1zT4aZAa1jOmGGvZiHUUQIDAQABAoIBAC7/w09NepEX9h55
36bA/WZawjv41xbSAYylUaRXvZA+h5956kq/mc4/W3m0iIEmRMzJJDTEOrodQUEw
6NSV0E9J2vzW8mE4HTHuPx3hHljJ0e4AVV+uudf1xsQfQ8UdDfZLzEjVSPI9fCtq
v8yjoLntcQQQSDaLAd/sYyW464DE50pUwgt3wGyIHx19m+TF8ntULzZv/e0P++hW
ox8L1Ytfd5h1augQ0K3rD27i9QSOauesLN4cc2eZZ8ow1rr6pR38++NHyrYO/NaR
nm6qU6W0EqUgyZdRaApD89UK1lGvt9rq++Yg2kAtw3MPsFoK2lFTYjEyGphyadBN
dG9jym0CgYEA8PF7m6C+LcPdKk1BoN2b3mt9FvV0AJu/3yIijHd2RCSmr2OVV2py
SZ0J9xWQi95eSU0y5RUiA3k82AFHlPJSsCpLqfeAS8fJcYHo09mMM6+xKv8E6KEW
Laxt6GsIeaJRLkcn4pO2ZAq4aTk79lhNHXc7rg0kKNokgvMBamPIN60CgYEA62wL
MKpGfUp6Hl1jOszbXt867XbTSX2eoK1HQ8mkeGlxJgBI7UrIKIaArbU1Ov/gAfu5
gDWiLcxZtl8gHRjlEaSl01wKM6AXbEl+3jU1lOATscdsRyeyJ48dk5IWPZygnVUo
+amtQyet07Bht90Q3ULsDPaFXVtZAJDxj0vHM7UCgYEAlsg4d6M3gMJjBNcGLBqj
MaUIyjZfGwZdI9Fj143nGCvrmDT0v5jg3sqE8viu1akaTjsej5gTCiNz/SWH22Fu
d8pwQXSe+E2V9g+7WeB5ydq4P9UKCF7O11RiD6Hz0tLOhOyIvFV+PcsrrsXfjYGi
+L6mPX0B1QL2+HAEwcSiBp0CgYA82hSaY6kMwa+HIcSAcmtRvonQz6IVoO7bwW5m
SzzEEx04IWK4U1ghgYLJY8l6kqEoYhS02ygshmG6DiSS4Nh1EwX5+BR6+6qSRv0Q
GtjavoDYtx951Pzr1MZkWqJ9EntBr72DqyQp85uu2CyqBe5SAvZY82/NjcsXpl+K
FqBK8QKBgQDeSgsq3ljKeIuLBl9yYzc9rEGnMZJl33wcVgAG6jTr+qZ5iEToiRTu
TSBYZgwgMJTq7i4blpsYJCoHcQynRJgSmIvSLuCw/ovyBHmHvJDDjrFsCutQYXA9
Y2lRUoypd9DhH64Hki9kaqKd23817XEXR9kwu7QdrzWYWST5l+cTAg==
-----END RSA PRIVATE KEY-----
)";

kalshi::Signer make_test_signer() {
	kalshi::Result<kalshi::Signer> result =
		kalshi::Signer::from_pem("test-api-key-id", TEST_RSA_KEY);
	// If this skips it means the OpenSSL build can't parse the embedded
	// PEM — the lifecycle tests below have nothing meaningful to assert
	// without a valid signer. We don't want to silently pass.
	if (!result.has_value()) {
		ADD_FAILURE() << "Embedded test RSA key failed to parse — "
						 "regenerate via openssl genrsa -traditional 2048";
	}
	return std::move(result.value());
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
