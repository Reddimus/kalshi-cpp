// Sustained allocation failure must not escape libwebsockets' C callback.
#include "kalshi/websocket.hpp"

#include <chrono>
#include <cstdint>
#include <future>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <variant>

#include "support/allocation_counter.hpp"
#include "support/ws_test_server.hpp"
#include "test_signer_fixture.hpp"

namespace {

using namespace std::chrono_literals;

TEST(WsAllocationFailure, DisconnectWithPendingUpdateStopsWithoutAllocatingAgain) {
	kalshi::test::WsTestServer server;
	ASSERT_TRUE(server.running());
	std::promise<void> acknowledged;
	std::promise<void> armed;
	std::promise<std::uint64_t> disconnected;
	std::future<void> ack = acknowledged.get_future();
	std::future<void> ready = armed.get_future();
	std::future<std::uint64_t> stopped = disconnected.get_future();
	kalshi::WsConfig config;
	config.url = server.url();
	config.connect_timeout = 5s;
	kalshi::WebSocketClient ws(kalshi::test::make_signer(), config);
	ws.on_message([&](const kalshi::WsMessage& message) {
		if (std::holds_alternative<kalshi::ws::Subscribed>(message)) {
			acknowledged.set_value();
		} else if (std::holds_alternative<kalshi::ws::Update<kalshi::ws::OrderbookDelta>>(
					   message)) {
			armed.set_value();
			kalshi::test::set_allocation_failure(true);
		}
	});
	ws.on_state_change([&](kalshi::WsState state) {
		if (state == kalshi::WsState::Disconnected) {
			kalshi::test::set_allocation_failure(false);
			disconnected.set_value(kalshi::test::allocation_failures());
		}
	});
	ASSERT_TRUE(ws.connect().has_value());
	ASSERT_TRUE(server.wait_for_connections(1));
	const kalshi::Result<kalshi::ws::Subscription> subscription =
		ws.subscribe(kalshi::ws::Channel::OrderbookDelta, {.market_tickers = {"KXA"}});
	ASSERT_TRUE(subscription.has_value());
	const std::optional<std::string> command = server.next_command();
	ASSERT_TRUE(command.has_value());
	const std::size_t id = command->find("\"id\":");
	ASSERT_NE(id, std::string::npos);
	server.send("{\"id\":" + std::to_string(std::stoll(command->substr(id + 5))) +
				R"(,"type":"subscribed","msg":{"channel":"orderbook_delta","sid":1}})");
	ASSERT_EQ(ack.wait_for(5s), std::future_status::ready);
	// No acknowledgement: disconnect must merge this pending update, allocating
	// a larger market list. Recovery must clear it without repeating that merge.
	ASSERT_TRUE(ws.add_markets(*subscription, {"KXB"}).has_value());
	ASSERT_TRUE(server.next_command().has_value());
	server.send(
		R"({"type":"orderbook_delta","sid":1,"seq":1,"msg":{"market_ticker":"KXA","market_id":"m","price_dollars":"0.5000","delta_fp":"1.00","side":"yes","ts_ms":1}})");
	ASSERT_EQ(ready.wait_for(5s), std::future_status::ready);
	server.drop();
	ASSERT_EQ(stopped.wait_for(5s), std::future_status::ready);
	EXPECT_GT(stopped.get(), 0u);
	EXPECT_FALSE(ws.is_connected());
	EXPECT_TRUE(ws.subscriptions().empty());
	// The stopped worker can be reaped and the same client used again.
	ASSERT_TRUE(ws.connect().has_value());
	EXPECT_TRUE(ws.is_connected());
	ws.on_state_change(nullptr);
	ws.disconnect();
}

} // namespace
