// Contract tests for the operations applications use most: the exact request
// each one sends and how its documented response parses. Fixtures are
// synthetic and follow Kalshi Predictions OpenAPI 3.34.0.

#include "kalshi/api.hpp"
#include "kalshi/helpers.hpp"
#include "kalshi/pagination.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "support/recording_transport.hpp"

namespace {

using kalshi::test::RecordingTransport;

struct Fixture {
	std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client{transport};
};

kalshi::CreateOrderV2Request limit_order() {
	kalshi::CreateOrderV2Request order;
	order.ticker = "KXHIGHNY-26SEP25-T70";
	order.client_order_id = "client-1";
	order.side = kalshi::BookSide::Bid;
	order.count = "10.00";
	order.price = "0.5600";
	order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
	order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;
	order.exchange_index = 2;
	return order;
}

} // namespace

TEST(Operations, CreateOrderSendsTheV2BodyAndReturnsTheFill) {
	Fixture f;
	f.transport->status = 201;
	f.transport->response_body =
		R"({"order_id":"o-1","client_order_id":"client-1","fill_count":"2.00","remaining_count":"8.00",)"
		R"("average_fill_price":"0.5500","average_fee_paid":"0.0100","ts_ms":1788000000123})";

	const kalshi::Result<kalshi::CreateOrderV2Response> result =
		f.client.create_order(limit_order());

	ASSERT_TRUE(result.has_value()) << result.error().message;
	EXPECT_EQ(f.transport->method, kalshi::HttpMethod::POST);
	EXPECT_EQ(f.transport->target, "/portfolio/events/orders");
	EXPECT_EQ(f.transport->body,
			  R"({"ticker":"KXHIGHNY-26SEP25-T70","client_order_id":"client-1","side":"bid",)"
			  R"("count":"10.00","price":"0.5600","time_in_force":"good_till_canceled",)"
			  R"("self_trade_prevention_type":"taker_at_cross","exchange_index":2})");
	EXPECT_EQ(result->order_id, "o-1");
	EXPECT_EQ(result->fill_count, "2.00");
	EXPECT_EQ(result->remaining_count, "8.00");
	EXPECT_EQ(result->average_fill_price, "0.5500");
	EXPECT_EQ(result->ts_ms, 1788000000123);
}

TEST(Operations, InvalidOrdersNeverReachTheExchange) {
	Fixture f;
	kalshi::CreateOrderV2Request order = limit_order();
	order.price = "56 cents";

	const kalshi::Result<kalshi::CreateOrderV2Response> result = f.client.create_order(order);

	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_EQ(f.transport->calls, 0);
}

TEST(Operations, AmendAndCancelRouteByOrderAndSelectors) {
	Fixture f;
	kalshi::AmendOrderV2Request amend;
	amend.ticker = "KXTEST";
	amend.side = kalshi::BookSide::Ask;
	amend.price = "0.4000";
	amend.count = "3.00";
	f.transport->response_body = R"({"order_id":"o-1","remaining_count":"3.00","ts_ms":5})";
	ASSERT_TRUE(f.client.amend_order("o-1", amend, {.subaccount = 4}).has_value());
	EXPECT_EQ(f.transport->target, "/portfolio/events/orders/o-1/amend?subaccount=4");
	EXPECT_EQ(f.transport->body,
			  R"({"ticker":"KXTEST","side":"ask","price":"0.4000","count":"3.00"})");

	f.transport->response_body = R"({"order_id":"o-1","reduced_by":"3.00","ts_ms":7})";
	const kalshi::Result<kalshi::CancelOrderV2Response> canceled = f.client.cancel_order(
		"o-1", {.subaccount = 4, .exchange_index = 3, .market_ticker = "KXTEST"});
	ASSERT_TRUE(canceled.has_value());
	EXPECT_EQ(f.transport->method, kalshi::HttpMethod::DEL);
	EXPECT_EQ(f.transport->target,
			  "/portfolio/events/orders/o-1?subaccount=4&exchange_index=3&market_ticker=KXTEST");
	EXPECT_EQ(canceled->reduced_by, "3.00");
}

TEST(Operations, BatchCreateReportsPerOrderErrors) {
	Fixture f;
	f.transport->status = 201;
	f.transport->response_body =
		R"({"orders":[{"order_id":"o-1","fill_count":"0.00","remaining_count":"10.00","ts_ms":1},)"
		R"({"error":{"code":"insufficient_balance","message":"Insufficient balance"}}]})";
	kalshi::BatchCreateOrdersV2Request batch;
	batch.orders = {limit_order(), limit_order()};

	const kalshi::Result<std::vector<kalshi::BatchCreateOrdersV2ResponseOrdersItem>> result =
		f.client.batch_create_orders(batch);

	ASSERT_TRUE(result.has_value()) << result.error().message;
	ASSERT_EQ(result->size(), 2U);
	EXPECT_EQ((*result)[0].order_id, "o-1");
	ASSERT_TRUE((*result)[1].error.has_value());
	EXPECT_EQ((*result)[1].error->code, "insufficient_balance");
	EXPECT_EQ(f.transport->target, "/portfolio/events/orders/batched");
}

TEST(Operations, MarketsDecodeEscapesEnumsAndCursors) {
	Fixture f;
	f.transport->response_body =
		R"({"cursor":"next page","markets":[{"ticker":"KXHIGHNY-26SEP25-T70",)"
		R"("event_ticker":"KXHIGHNY-26SEP25","market_type":"binary","status":"active",)"
		R"("yes_sub_title":"70° or above","no_sub_title":"Below \"70\"",)"
		R"("yes_bid_dollars":"0.4200","yes_ask_dollars":"0.4400","volume_fp":"12.50",)"
		R"("custom_strike":{"city":"NYC"},"exchange_index":1}]})";
	kalshi::GetMarketsParams params;
	params.limit = 2;
	params.status = kalshi::GetMarketsStatus::Open;
	params.series_ticker = "KXHIGHNY";

	const kalshi::Result<kalshi::GetMarketsResponse> page = f.client.get_markets(params);

	ASSERT_TRUE(page.has_value()) << page.error().message;
	EXPECT_EQ(f.transport->target, "/markets?limit=2&series_ticker=KXHIGHNY&status=open");
	EXPECT_EQ(page->cursor, "next page");
	ASSERT_EQ(page->markets.size(), 1U);
	const kalshi::Market& market = page->markets[0];
	EXPECT_EQ(market.yes_sub_title, "70° or above");
	EXPECT_EQ(market.no_sub_title, "Below \"70\"");
	EXPECT_EQ(market.status, kalshi::MarketStatus::Active);
	EXPECT_EQ(market.market_type, kalshi::MarketType::Binary);
	EXPECT_EQ(market.yes_bid_dollars, "0.4200");
	EXPECT_EQ(kalshi::to_cents(market.yes_ask_dollars).value(), 44);
	ASSERT_TRUE(market.custom_strike.has_value());
	EXPECT_EQ(market.custom_strike->text, R"({"city":"NYC"})");
}

TEST(Operations, SingleFieldResponsesAreUnwrapped) {
	Fixture f;
	f.transport->response_body =
		R"({"orderbook_fp":{"yes_dollars":[["0.4200","100.00"],["0.4100","5.50"]],"no_dollars":[]}})";

	const kalshi::Result<kalshi::OrderbookCountFp> book =
		f.client.get_market_orderbook("KXTEST", {.depth = 10});

	ASSERT_TRUE(book.has_value()) << book.error().message;
	EXPECT_EQ(f.transport->target, "/markets/KXTEST/orderbook?depth=10");
	ASSERT_EQ(book->yes_dollars.size(), 2U);
	EXPECT_EQ(book->yes_dollars[1][0], "0.4100");
	EXPECT_EQ(book->yes_dollars[1][1], "5.50");
	EXPECT_TRUE(book->no_dollars.empty());
}

TEST(Operations, BalanceAndPositionsParsePortfolioShards) {
	Fixture f;
	f.transport->response_body =
		R"({"balance":12345,"balance_dollars":"123.4500","portfolio_value":500,"updated_ts":1788000000,)"
		R"("balance_breakdown":[{"exchange_index":0,"balance":"100.0000"},{"exchange_index":1,"balance":"23.4500"}]})";

	const kalshi::Result<kalshi::GetBalanceResponse> balance =
		f.client.get_balance({.subaccount = 1, .exchange_index = 1});

	ASSERT_TRUE(balance.has_value()) << balance.error().message;
	EXPECT_EQ(f.transport->target, "/portfolio/balance?subaccount=1&exchange_index=1");
	EXPECT_EQ(balance->balance_dollars, "123.4500");
	ASSERT_TRUE(balance->balance_breakdown.has_value());
	EXPECT_EQ(balance->balance_breakdown->at(1).balance, "23.4500");
}

TEST(Operations, GeneratedKeysRequestEd25519) {
	Fixture f;
	f.transport->status = 201;
	f.transport->response_body =
		R"({"api_key_id":"k-1","private_key":"-----BEGIN PRIVATE KEY-----\nabc\n-----END PRIVATE KEY-----\n","key_type":"ed25519"})";
	kalshi::GenerateApiKeyRequest request;
	request.name = "bot";
	request.key_type = kalshi::ApiKeyType::Ed25519;
	request.scopes = std::vector<kalshi::ApiKeyScope>{kalshi::ApiKeyScope::Read};

	const kalshi::Result<kalshi::GenerateApiKeyResponse> key = f.client.generate_api_key(request);

	ASSERT_TRUE(key.has_value()) << key.error().message;
	EXPECT_EQ(f.transport->body, R"({"name":"bot","key_type":"ed25519","scopes":["read"]})");
	EXPECT_EQ(key->api_key_id, "k-1");
	EXPECT_EQ(key->private_key, "-----BEGIN PRIVATE KEY-----\nabc\n-----END PRIVATE KEY-----\n");
}

TEST(Operations, ServerErrorsSurfaceAsTypedErrors) {
	Fixture f;
	f.transport->status = 404;
	f.transport->response_body =
		R"({"error":{"code":"market_not_found","message":"Market not found"}})";

	const kalshi::Result<kalshi::Market> market = f.client.get_market("KXNOPE");

	ASSERT_FALSE(market.has_value());
	EXPECT_EQ(market.error().code, kalshi::ErrorCode::NotFound);
	EXPECT_EQ(market.error().api_code, "market_not_found");
	EXPECT_EQ(market.error().http_status, 404);
}

TEST(Operations, EmptyPathParametersFailBeforeTransport) {
	Fixture f;
	const kalshi::Result<kalshi::Market> market = f.client.get_market("");
	ASSERT_FALSE(market.has_value());
	EXPECT_EQ(market.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_EQ(f.transport->calls, 0);
}

TEST(Operations, CollectPagesFollowsCursors) {
	class Pages final : public kalshi::HttpTransport {
	public:
		mutable std::vector<std::string> targets;
		[[nodiscard]] kalshi::Result<kalshi::HttpResponse>
		request(kalshi::HttpMethod, std::string_view target, std::string_view) const override {
			targets.emplace_back(target);
			const bool first = targets.size() == 1;
			return kalshi::HttpResponse{
				200,
				first ? R"({"cursor":"c2","markets":[{"ticker":"A"},{"ticker":"B"}]})"
					  : R"({"cursor":"","markets":[{"ticker":"C"}]})",
				{}};
		}
	};
	const std::shared_ptr<Pages> transport = std::make_shared<Pages>();
	kalshi::KalshiClient client(transport);
	kalshi::GetMarketsParams params;
	params.limit = 2;

	const kalshi::Result<std::vector<kalshi::Market>> markets = kalshi::collect_pages(
		[&](std::string_view cursor) {
			params.cursor = cursor.empty() ? std::nullopt : std::optional<std::string>(cursor);
			return client.get_markets(params);
		},
		&kalshi::GetMarketsResponse::markets);

	ASSERT_TRUE(markets.has_value());
	ASSERT_EQ(markets->size(), 3U);
	EXPECT_EQ(markets->back().ticker, "C");
	EXPECT_EQ(transport->targets,
			  (std::vector<std::string>{"/markets?limit=2", "/markets?limit=2&cursor=c2"}));
}

TEST(Operations, AcceptRfqBatchPreservesPriceProtectionAndQuotes) {
	Fixture f;
	f.transport->response_body =
		R"({"accepted_side":"yes","quotes":[{"id":"q-1","rfq_id":"r-1","market_ticker":"KXTEST","contracts_fp":"2.00","yes_bid_dollars":"0.4000","no_bid_dollars":"0.5000","status":"accepted"}]})";
	kalshi::AcceptRFQBatchRequest request;
	request.accepted_side = kalshi::Side::Yes;
	request.min_bids = {{"r-1", "0.3900"}};
	request.client_order_id = "client-1";
	const kalshi::Result<kalshi::AcceptRFQBatchResponse> result =
		f.client.accept_rfq_batch("batch/1", request);
	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(f.transport->method, kalshi::HttpMethod::PUT);
	EXPECT_EQ(f.transport->target, "/communications/rfq_batches/batch%2F1/accept");
	EXPECT_EQ(
		f.transport->body,
		R"({"accepted_side":"yes","min_bids":[{"rfq_id":"r-1","bid_dollars":"0.3900"}],"client_order_id":"client-1"})");
	EXPECT_EQ(result->accepted_side, kalshi::Side::Yes);
	ASSERT_EQ(result->quotes.size(), 1U);
	EXPECT_EQ(result->quotes[0].id, "q-1");
	EXPECT_EQ(result->quotes[0].contracts_fp, "2.00");
	EXPECT_EQ(result->quotes[0].yes_bid_dollars, "0.4000");
	EXPECT_EQ(result->quotes[0].status, kalshi::QuoteStatus::Accepted);
}

TEST(Operations, FcmPositionTransferSendsCenticountsAndReturnsItsId) {
	Fixture f;
	f.transport->status = 201;
	f.transport->response_body = R"({"position_transfer_id":"transfer-1"})";
	const kalshi::CreateFCMPositionTransferRequest request{"source_1", "error_2", "KXTEST", 200,
														   5600};
	const kalshi::Result<std::string> result = f.client.create_fcm_position_transfer(request);
	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(f.transport->method, kalshi::HttpMethod::POST);
	EXPECT_EQ(f.transport->target, "/fcm/positions/transfers");
	EXPECT_EQ(
		f.transport->body,
		R"({"source_subtrader_id":"source_1","target_subtrader_id":"error_2","market_ticker":"KXTEST","position_centicount":200,"price_centicents":5600})");
	EXPECT_EQ(*result, "transfer-1");
}

TEST(Operations, RfqBatchCreationPreservesChildrenAndNestedResponse) {
	Fixture f;
	f.transport->status = 201;
	f.transport->response_body =
		R"({"rfq_batch":{"id":"batch-1","status":"open","client_batch_id":"client-batch","rfqs":[{"id":"r-1","market_ticker":"KXA","contracts_fp":"2.00","status":"open"},{"id":"r-2","market_ticker":"KXB","contracts_fp":"2.00","status":"open"}]}})";
	kalshi::CreateRFQBatchRequest request;
	request.children = {{.market_ticker = "KXA"}, {.market_ticker = "KXB"}};
	request.contracts_fp = "2.00";
	request.rest_remainder = false;
	request.client_batch_id = "client-batch";
	const kalshi::Result<kalshi::RFQBatch> result = f.client.create_rfq_batch(request);
	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(f.transport->method, kalshi::HttpMethod::POST);
	EXPECT_EQ(f.transport->target, "/communications/rfq_batches");
	EXPECT_EQ(
		f.transport->body,
		R"({"children":[{"market_ticker":"KXA"},{"market_ticker":"KXB"}],"contracts_fp":"2.00","rest_remainder":false,"client_batch_id":"client-batch"})");
	EXPECT_EQ(result->id, "batch-1");
	EXPECT_EQ(result->status, kalshi::RFQBatchStatus::Open);
	EXPECT_EQ(result->client_batch_id, "client-batch");
	ASSERT_EQ(result->rfqs.size(), 2U);
	EXPECT_EQ(result->rfqs[1].market_ticker, "KXB");
	EXPECT_EQ(result->rfqs[1].contracts_fp, "2.00");
}

TEST(Operations, RfqBatchQuoteCreationSendsPricesAndReturnsId) {
	Fixture f;
	f.transport->status = 201;
	f.transport->response_body = R"({"id":"batch-quote-1"})";
	kalshi::CreateRFQBatchQuoteRequest request;
	request.quotes = {{"r-1", "0.4000", "0.5000"}, {"r-2", "0.3000", "0.6000"}};
	request.rest_remainder = false;
	request.post_only = true;
	request.subaccount = 2;
	const kalshi::Result<std::string> result = f.client.create_rfq_batch_quote("batch-1", request);
	ASSERT_TRUE(result) << result.error().message;
	EXPECT_EQ(f.transport->method, kalshi::HttpMethod::POST);
	EXPECT_EQ(f.transport->target, "/communications/rfq_batches/batch-1/quotes");
	EXPECT_EQ(
		f.transport->body,
		R"({"quotes":[{"rfq_id":"r-1","yes_bid":"0.4000","no_bid":"0.5000"},{"rfq_id":"r-2","yes_bid":"0.3000","no_bid":"0.6000"}],"rest_remainder":false,"post_only":true,"subaccount":2})");
	EXPECT_EQ(*result, "batch-quote-1");
}
