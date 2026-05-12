// Copyright (c) 2026 PredictionMarketsAI
// SPDX-License-Identifier: MIT
//
// Byte-equivalence tests for the Glaze-backed outgoing JSON
// serializers, pinned against the pre-migration `nlohmann::ordered_json`
// output. Kalshi's API rejects unordered payloads on the
// order-management routes and the WS server rejects unordered
// subscribe frames, so any reordering or whitespace change in the
// emitted bytes is a production breakage — these tests are the
// regression gate.
//
// The expected strings below were captured from the pre-migration
// nlohmann impl (commit before this branch's HEAD) by running each
// `serialize_*` / `build_*_command` against the inputs in this file
// and copying `body.dump()`'s output verbatim. If you intentionally
// add a new field to a `glz::meta`, update the corresponding expected
// string here AND coordinate with Kalshi support — the field may
// need to be acknowledged on the server side first.

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

#include "../src/api/json_bodies.hpp"
#include "../src/ws/ws_cmd_bodies.hpp"

namespace {

// ===== WS command builders ===========================================
//
// These mirror the internal `build_*_command` helpers in
// `src/ws/websocket.cpp`, calling the shared `ws_cmd::render_cmd`
// through the same struct shapes. We don't expose the builders out of
// `websocket.cpp` itself because they're an implementation detail —
// reconstructing them here keeps the test self-contained without
// breaking the anonymous-namespace encapsulation.

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

// ===== WS subscribe / unsubscribe / update ===========================

TEST(JsonSerialize, WsSubscribeWithMarketTickers) {
	// Pre-migration nlohmann::ordered_json output:
	const std::string expected =
		R"({"id":42,"cmd":"subscribe","params":{"channels":["orderbook_delta"],"market_tickers":["KXHIGHDEN","KXHIGHLAX"]}})";
	EXPECT_EQ(make_subscribe(42, "orderbook_delta", {"KXHIGHDEN", "KXHIGHLAX"}), expected);
}

TEST(JsonSerialize, WsSubscribeWithoutMarketTickers) {
	// market_tickers key MUST be omitted (not emitted as []) when the
	// vector is empty — the pre-migration impl did
	//   `if (!market_tickers.empty()) params["market_tickers"] = market_tickers;`
	// so the key wasn't present in the serialized output.
	const std::string expected =
		R"({"id":1,"cmd":"subscribe","params":{"channels":["market_lifecycle"]}})";
	EXPECT_EQ(make_subscribe(1, "market_lifecycle", {}), expected);
}

TEST(JsonSerialize, WsUnsubscribe) {
	const std::string expected = R"({"id":7,"cmd":"unsubscribe","params":{"sids":[1234]}})";
	EXPECT_EQ(make_unsubscribe(7, 1234), expected);
}

TEST(JsonSerialize, WsUpdate) {
	const std::string expected =
		R"({"id":9,"cmd":"update_subscription","params":{"action":"add_markets","channel":"orderbook_delta","sids":[2222],"market_tickers":["KXHIGHNYC"]}})";
	EXPECT_EQ(make_update(9, 2222, "add_markets", "orderbook_delta", {"KXHIGHNYC"}), expected);
}

// ===== REST request body serializers =================================

TEST(JsonSerialize, CreateOrderMinimal) {
	kalshi::ser::CreateOrderBody body;
	body.ticker = "KXHIGHDEN-26MAY11-T80";
	body.side = "yes";
	body.action = "buy";
	body.type = "limit";
	body.count = 5;
	body.yes_price = 47;
	// All other optionals nullopt — must be omitted.

	const std::string expected =
		R"({"ticker":"KXHIGHDEN-26MAY11-T80","side":"yes","action":"buy","type":"limit","count":5,"yes_price":47})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, CreateOrderAllFields) {
	kalshi::ser::CreateOrderBody body;
	body.ticker = "KXHIGHLAX-26MAY11-T75";
	body.side = "no";
	body.action = "sell";
	body.type = "limit";
	body.count = 10;
	body.yes_price = 33;
	body.no_price = 67;
	body.client_order_id = "client-abc-123";
	body.expiration_ts = 1788000000;
	body.sell_position_floor = 0;
	body.buy_max_cost = 1000;

	const std::string expected =
		R"({"ticker":"KXHIGHLAX-26MAY11-T75","side":"no","action":"sell","type":"limit","count":10,"yes_price":33,"no_price":67,"client_order_id":"client-abc-123","expiration_ts":1788000000,"sell_position_floor":0,"buy_max_cost":1000})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, AmendOrderPartial) {
	kalshi::ser::AmendOrderBody body;
	body.count = 12;
	body.yes_price = 42;
	// no_price omitted.

	const std::string expected = R"({"count":12,"yes_price":42})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, AmendOrderAllNullopt) {
	// Pre-migration impl emitted `{}` (an empty object) when every
	// optional was nullopt — verify Glaze does the same.
	kalshi::ser::AmendOrderBody body;
	const std::string expected = R"({})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, DecreaseOrder) {
	kalshi::ser::DecreaseOrderBody body;
	body.reduce_by = 3;
	const std::string expected = R"({"reduce_by":3})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, BatchOrders) {
	kalshi::ser::CreateOrderBody o1;
	o1.ticker = "T1";
	o1.side = "yes";
	o1.action = "buy";
	o1.type = "limit";
	o1.count = 1;
	o1.yes_price = 50;

	kalshi::ser::CreateOrderBody o2;
	o2.ticker = "T2";
	o2.side = "no";
	o2.action = "buy";
	o2.type = "limit";
	o2.count = 2;
	o2.no_price = 40;

	kalshi::ser::BatchOrdersBody body;
	body.orders = {o1, o2};

	const std::string expected =
		R"({"orders":[)"
		R"({"ticker":"T1","side":"yes","action":"buy","type":"limit","count":1,"yes_price":50},)"
		R"({"ticker":"T2","side":"no","action":"buy","type":"limit","count":2,"no_price":40})"
		R"(]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, BatchCancel) {
	kalshi::ser::BatchCancelBody body;
	body.order_ids = {"oid-1", "oid-2"};
	const std::string expected = R"({"order_ids":["oid-1","oid-2"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, OrderGroup) {
	kalshi::ser::OrderGroupBody body;
	body.type = "oco";
	body.order_ids = {"oid-1", "oid-2"};
	const std::string expected = R"({"type":"oco","order_ids":["oid-1","oid-2"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, RfqMinimal) {
	kalshi::ser::RfqBody body;
	body.market_ticker = "KXHIGHDEN-26MAY11-T80";
	body.side = "yes";
	body.action = "buy";
	body.count = 100;

	const std::string expected =
		R"({"market_ticker":"KXHIGHDEN-26MAY11-T80","side":"yes","action":"buy","count":100})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, RfqWithExpiry) {
	kalshi::ser::RfqBody body;
	body.market_ticker = "T";
	body.side = "yes";
	body.action = "buy";
	body.count = 1;
	body.expires_at = 1788000000;

	const std::string expected =
		R"({"market_ticker":"T","side":"yes","action":"buy","count":1,"expires_at":1788000000})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, Quote) {
	kalshi::ser::QuoteBody body;
	body.rfq_id = "rfq-xyz";
	body.price = 55;
	body.count = 25;
	body.expires_at = 1788000000;

	const std::string expected =
		R"({"rfq_id":"rfq-xyz","price":55,"count":25,"expires_at":1788000000})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, ApiKey) {
	kalshi::ser::ApiKeyBody body;
	body.name = "test-key";
	body.scopes = {"read:markets", "write:orders"};

	const std::string expected = R"({"name":"test-key","scopes":["read:markets","write:orders"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, GenerateApiKeyWithScopes) {
	kalshi::ser::GenerateApiKeyBody body;
	body.name = "gen-key";
	body.scopes = std::vector<std::string>{"trade"};

	const std::string expected = R"({"name":"gen-key","scopes":["trade"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, GenerateApiKeyWithoutScopes) {
	// The pre-migration impl omitted the `scopes` key entirely (not as
	// `[]`) when the caller passed no scopes. Verify our optional<vec>
	// shim preserves that.
	kalshi::ser::GenerateApiKeyBody body;
	body.name = "gen-key";

	const std::string expected = R"({"name":"gen-key"})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, OrderIds) {
	kalshi::ser::OrderIdsBody body;
	body.order_ids = {"a", "b", "c"};
	const std::string expected = R"({"order_ids":["a","b","c"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, Tickers) {
	kalshi::ser::TickersBody body;
	body.tickers = {"KXHIGHDEN", "KXHIGHLAX"};
	const std::string expected = R"({"tickers":["KXHIGHDEN","KXHIGHLAX"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, MarketTickers) {
	kalshi::ser::MarketTickersBody body;
	body.market_tickers = {"M1"};
	const std::string expected = R"({"market_tickers":["M1"]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, SubaccountTransfer) {
	kalshi::ser::SubaccountTransferBody body;
	body.from_subaccount = 1;
	body.to_subaccount = 2;
	body.amount = 500;
	const std::string expected = R"({"from_subaccount":1,"to_subaccount":2,"amount":500})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, SubaccountNettingEnabled) {
	kalshi::ser::SubaccountNettingBody body;
	body.subaccount = 5;
	body.netting_enabled = true;
	const std::string expected = R"({"subaccount":5,"netting_enabled":true})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, SubaccountNettingDisabled) {
	kalshi::ser::SubaccountNettingBody body;
	body.subaccount = 5;
	body.netting_enabled = false;
	const std::string expected = R"({"subaccount":5,"netting_enabled":false})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}
