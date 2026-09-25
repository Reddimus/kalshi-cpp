#include "kalshi/http_client.hpp"
#include "kalshi/websocket.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <string>

// --- WebSocket model tests ---

TEST(WebSocket, ChannelToString) {
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::OrderbookDelta),
			  std::string_view("orderbook_delta"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::Trade), std::string_view("trade"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::Fill), std::string_view("fill"));
	ASSERT_EQ(kalshi::to_string(kalshi::Channel::MarketLifecycle),
			  std::string_view("market_lifecycle_v2"));
}

TEST(WebSocket, OrderbookSnapshotDefault) {
	kalshi::OrderbookSnapshot snap;
	ASSERT_EQ(snap.sid, 0);
	ASSERT_EQ(snap.seq, 0);
	ASSERT_TRUE(snap.market_ticker.empty());
	ASSERT_TRUE(snap.yes.empty());
	ASSERT_TRUE(snap.no.empty());
}

TEST(WebSocket, OrderbookDeltaDefault) {
	kalshi::OrderbookDelta delta;
	ASSERT_EQ(delta.sid, 0);
	ASSERT_EQ(delta.seq, 0);
	ASSERT_TRUE(delta.market_ticker.empty());
	ASSERT_EQ(delta.price, 0);
	ASSERT_EQ(delta.delta, 0);
}

TEST(WebSocket, TradeDefault) {
	kalshi::WsTrade trade;
	ASSERT_EQ(trade.sid, 0);
	ASSERT_TRUE(trade.trade_id.empty());
	ASSERT_TRUE(trade.market_ticker.empty());
	ASSERT_EQ(trade.yes_price, 0);
	ASSERT_EQ(trade.count, 0);
}

TEST(WebSocket, SubscriptionId) {
	kalshi::SubscriptionId sub;
	sub.sid = 42;
	sub.channel = kalshi::Channel::OrderbookDelta;
	ASSERT_EQ(sub.sid, 42);
	ASSERT_EQ(sub.channel, kalshi::Channel::OrderbookDelta);
}

TEST(WebSocket, WsErrorDefault) {
	kalshi::WsError err;
	ASSERT_EQ(err.code, 0);
	ASSERT_TRUE(err.message.empty());
}

TEST(WebSocket, WsConfigDefaults) {
	kalshi::WsConfig config;
	ASSERT_EQ(config.url, std::string("wss://external-api-ws.kalshi.com/trade-api/ws/v2"));
	ASSERT_TRUE(config.auto_reconnect);
	ASSERT_EQ(config.max_reconnect_attempts, 10);
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
