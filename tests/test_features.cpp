#include "kalshi/http_client.hpp"
#include "kalshi/websocket.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <string>

// --- WebSocket model tests ---

TEST(WebSocket, ChannelNamesMatchTheWire) {
	EXPECT_EQ(kalshi::ws::to_string(kalshi::ws::Channel::OrderbookDelta), "orderbook_delta");
	EXPECT_EQ(kalshi::ws::to_string(kalshi::ws::Channel::MarketLifecycleV2), "market_lifecycle_v2");
	EXPECT_EQ(kalshi::ws::to_string(kalshi::ws::Channel::CfbenchmarksValue5hz),
			  "cfbenchmarks_value_5hz");
	EXPECT_EQ(kalshi::ws::to_string(kalshi::ws::Channel::Unknown), "");
}

TEST(WebSocket, ErrorCodeNamesComeFromTheSpec) {
	EXPECT_EQ(kalshi::ws::error_code_name(7), "Unknown subscription ID");
	EXPECT_EQ(kalshi::ws::error_code_name(25), "Subscription buffer overflow");
	EXPECT_EQ(kalshi::ws::error_code_name(6), ""); // retired
}

TEST(WebSocket, WsConfigDefaults) {
	const kalshi::WsConfig config;
	EXPECT_EQ(config.url, "wss://external-api-ws.kalshi.com/trade-api/ws/v2");
	EXPECT_TRUE(config.auto_reconnect);
	EXPECT_EQ(config.max_reconnect_attempts, 0U);
	EXPECT_TRUE(config.resync_on_gap);
	EXPECT_EQ(kalshi::to_string(kalshi::WsState::Reconnecting), "reconnecting");
}

TEST(HttpClient, ClientConfigDefaultsToDedicatedTradeApiHost) {
	kalshi::ClientConfig config;
	ASSERT_EQ(config.base_url, std::string("https://external-api.kalshi.com/trade-api/v2"));
	ASSERT_TRUE(config.verify_ssl);
}

TEST(Environment, DemoPresetsUseDemoHosts) {
	EXPECT_EQ(kalshi::ClientConfig::for_environment(kalshi::Environment::Demo).base_url,
			  "https://external-api.demo.kalshi.co/trade-api/v2");
	EXPECT_EQ(kalshi::WsConfig::for_environment(kalshi::Environment::Demo).url,
			  "wss://external-api-ws.demo.kalshi.co/trade-api/ws/v2");
	EXPECT_EQ(kalshi::WsConfig::for_environment(kalshi::Environment::Production).url,
			  kalshi::WsConfig{}.url);
}
