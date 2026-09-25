// WebSocket command frames. Kalshi expects `id`, `cmd`, then `params`.

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "ws_cmd_bodies.hpp"

namespace {

std::string make_subscribe(std::int32_t id, const std::string& channel,
						   const std::vector<std::string>& market_tickers) {
	kalshi::ws_cmd::SubscribeCmd cmd;
	cmd.id = id;
	cmd.cmd = "subscribe";
	cmd.params.channels = {channel};
	if (!market_tickers.empty()) {
		cmd.params.market_tickers = market_tickers;
	}
	return kalshi::ws_cmd::render_cmd(cmd);
}

std::string make_unsubscribe(std::int32_t id, std::int32_t sid) {
	kalshi::ws_cmd::UnsubscribeCmd cmd;
	cmd.id = id;
	cmd.cmd = "unsubscribe";
	cmd.params.sids = {sid};
	return kalshi::ws_cmd::render_cmd(cmd);
}

std::string make_update(std::int32_t id, std::int32_t sid, const std::string& action,
						const std::string& channel,
						const std::vector<std::string>& market_tickers) {
	kalshi::ws_cmd::UpdateCmd cmd;
	cmd.id = id;
	cmd.cmd = "update_subscription";
	cmd.params.action = action;
	cmd.params.channel = channel;
	cmd.params.sids = {sid};
	cmd.params.market_tickers = market_tickers;
	return kalshi::ws_cmd::render_cmd(cmd);
}

} // namespace

TEST(WsCommands, WsSubscribeWithMarketTickers) {
	// Pre-migration nlohmann::ordered_json output:
	const std::string expected =
		R"({"id":42,"cmd":"subscribe","params":{"channels":["orderbook_delta"],"market_tickers":["KXHIGHDEN","KXHIGHLAX"]}})";
	EXPECT_EQ(make_subscribe(42, "orderbook_delta", {"KXHIGHDEN", "KXHIGHLAX"}), expected);
}

TEST(WsCommands, WsSubscribeWithoutMarketTickers) {
	// market_tickers key MUST be omitted (not emitted as []) when the
	// vector is empty — the pre-migration impl did
	//   `if (!market_tickers.empty()) params["market_tickers"] = market_tickers;`
	// so the key wasn't present in the serialized output.
	const std::string expected =
		R"({"id":1,"cmd":"subscribe","params":{"channels":["market_lifecycle_v2"]}})";
	EXPECT_EQ(make_subscribe(1, "market_lifecycle_v2", {}), expected);
}

TEST(WsCommands, WsUnsubscribe) {
	const std::string expected = R"({"id":7,"cmd":"unsubscribe","params":{"sids":[1234]}})";
	EXPECT_EQ(make_unsubscribe(7, 1234), expected);
}

TEST(WsCommands, WsUpdate) {
	const std::string expected =
		R"({"id":9,"cmd":"update_subscription","params":{"action":"add_markets","channel":"orderbook_delta","sids":[2222],"market_tickers":["KXHIGHNYC"]}})";
	EXPECT_EQ(make_update(9, 2222, "add_markets", "orderbook_delta", {"KXHIGHNYC"}), expected);
}
