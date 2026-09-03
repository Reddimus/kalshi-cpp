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
		R"({"id":1,"cmd":"subscribe","params":{"channels":["market_lifecycle_v2"]}})";
	EXPECT_EQ(make_subscribe(1, "market_lifecycle_v2", {}), expected);
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
	body.side = "bid";
	body.count = "5.00";
	body.price = "0.4700";
	body.time_in_force = "good_till_canceled";
	body.self_trade_prevention_type = "taker_at_cross";
	// All other optionals nullopt — must be omitted.

	const std::string expected =
		R"({"ticker":"KXHIGHDEN-26MAY11-T80","side":"bid","count":"5.00","price":"0.4700","time_in_force":"good_till_canceled","self_trade_prevention_type":"taker_at_cross"})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, CreateOrderAllFields) {
	kalshi::ser::CreateOrderBody body;
	body.ticker = "KXHIGHLAX-26MAY11-T75";
	body.side = "ask";
	body.count = "10.00";
	body.price = "0.6700";
	body.time_in_force = "fill_or_kill";
	body.self_trade_prevention_type = "taker_at_cross";
	body.client_order_id = "client-abc-123";
	body.expiration_time = 1788000000;
	body.post_only = true;
	body.reduce_only = true;
	body.order_group_id = "group-abc";
	body.cancel_order_on_pause = true;
	body.subaccount = 7;
	body.exchange_index = 0;

	const std::string expected =
		R"({"ticker":"KXHIGHLAX-26MAY11-T75","side":"ask","count":"10.00","price":"0.6700","time_in_force":"fill_or_kill","self_trade_prevention_type":"taker_at_cross","client_order_id":"client-abc-123","expiration_time":1788000000,"post_only":true,"reduce_only":true,"order_group_id":"group-abc","cancel_order_on_pause":true,"subaccount":7,"exchange_index":0})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, AmendOrderPartial) {
	kalshi::ser::AmendOrderBody body;
	body.ticker = "T1";
	body.side = "bid";
	body.price = "0.4200";
	body.count = "12.00";

	const std::string expected = R"({"ticker":"T1","side":"bid","price":"0.4200","count":"12.00"})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, DecreaseOrder) {
	kalshi::ser::DecreaseOrderBody body;
	body.reduce_by = "3.00";
	const std::string expected = R"({"reduce_by":"3.00"})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, BatchOrders) {
	kalshi::ser::CreateOrderBody o1;
	o1.ticker = "T1";
	o1.side = "bid";
	o1.count = "1.00";
	o1.price = "0.5000";
	o1.time_in_force = "good_till_canceled";
	o1.self_trade_prevention_type = "taker_at_cross";

	kalshi::ser::CreateOrderBody o2;
	o2.ticker = "T2";
	o2.side = "ask";
	o2.count = "2.00";
	o2.price = "0.4000";
	o2.time_in_force = "immediate_or_cancel";
	o2.self_trade_prevention_type = "maker";

	kalshi::ser::BatchOrdersBody body;
	body.orders = {o1, o2};

	const std::string expected =
		R"({"orders":[)"
		R"({"ticker":"T1","side":"bid","count":"1.00","price":"0.5000","time_in_force":"good_till_canceled","self_trade_prevention_type":"taker_at_cross"},)"
		R"({"ticker":"T2","side":"ask","count":"2.00","price":"0.4000","time_in_force":"immediate_or_cancel","self_trade_prevention_type":"maker"})"
		R"(]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, BatchCancel) {
	kalshi::ser::BatchCancelBody body;
	body.orders = {
		kalshi::ser::BatchCancelOrderBody{.order_id = "oid-1"},
		kalshi::ser::BatchCancelOrderBody{
			.order_id = "oid-2",
			.subaccount = 7,
			.exchange_index = 1,
		},
	};
	const std::string expected =
		R"({"orders":[{"order_id":"oid-1"},{"order_id":"oid-2","subaccount":7,"exchange_index":1}]})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, OrderGroup) {
	kalshi::ser::OrderGroupBody body;
	body.subaccount = 7;
	body.contracts_limit_fp = "10.25";
	body.exchange_index = 3;
	const std::string expected =
		R"({"subaccount":7,"contracts_limit_fp":"10.25","exchange_index":3})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, RfqMinimal) {
	kalshi::ser::RfqBody body;
	body.market_ticker = "KXHIGHDEN-26MAY11-T80";
	body.contracts_fp = "100.00";
	body.rest_remainder = false;

	const std::string expected =
		R"({"market_ticker":"KXHIGHDEN-26MAY11-T80","contracts_fp":"100.00","rest_remainder":false})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, RfqAllFields) {
	kalshi::ser::RfqBody body;
	body.market_ticker = "T";
	body.contracts_fp = "1.25";
	body.target_cost_dollars = "0.625000";
	body.rest_remainder = true;
	body.replace_existing = true;
	body.subtrader_id = "user_trader";
	body.subaccount = 7;

	const std::string expected =
		R"({"market_ticker":"T","contracts_fp":"1.25","target_cost_dollars":"0.625000","rest_remainder":true,"replace_existing":true,"subtrader_id":"user_trader","subaccount":7})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, Quote) {
	kalshi::ser::QuoteBody body;
	body.rfq_id = "rfq-xyz";
	body.yes_bid = "0.550000";
	body.no_bid = "0.450000";
	body.rest_remainder = false;

	const std::string expected =
		R"({"rfq_id":"rfq-xyz","yes_bid":"0.550000","no_bid":"0.450000","rest_remainder":false})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, QuotePostOnly) {
	// 2026-05-05 upstream: opt-in flag preventing taker matches/fees.
	// Verifies post_only renders after expires_at (the field order pinned by
	// glz::meta<QuoteBody>) and is included when set.
	kalshi::ser::QuoteBody body;
	body.rfq_id = "rfq-xyz";
	body.yes_bid = "0.550000";
	body.no_bid = "0.450000";
	body.rest_remainder = true;
	body.post_only = true;
	body.subaccount = 7;

	const std::string expected =
		R"({"rfq_id":"rfq-xyz","yes_bid":"0.550000","no_bid":"0.450000","rest_remainder":true,"post_only":true,"subaccount":7})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, ApiKey) {
	kalshi::ser::ApiKeyBody body;
	body.name = "test-key";
	body.public_key = "pem";
	body.scopes = {"read:markets", "write:orders"};

	const std::string expected =
		R"({"name":"test-key","public_key":"pem","scopes":["read:markets","write:orders"]})";
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
	body.client_transfer_id = "transfer-1";
	body.from_subaccount = 1;
	body.to_subaccount = 2;
	body.amount_cents = 500;
	body.exchange_index = 3;
	const std::string expected =
		R"({"client_transfer_id":"transfer-1","from_subaccount":1,"to_subaccount":2,"amount_cents":500,"exchange_index":3})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, SubaccountNettingEnabled) {
	kalshi::ser::SubaccountNettingBody body;
	body.subaccount_number = 5;
	body.enabled = true;
	const std::string expected = R"({"subaccount_number":5,"enabled":true})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}

TEST(JsonSerialize, SubaccountNettingDisabled) {
	kalshi::ser::SubaccountNettingBody body;
	body.subaccount_number = 5;
	body.enabled = false;
	const std::string expected = R"({"subaccount_number":5,"enabled":false})";
	EXPECT_EQ(kalshi::ser::render_body(body), expected);
}
