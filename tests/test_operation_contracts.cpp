#include "kalshi/api.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

namespace {

class RecordingTransport final : public kalshi::HttpTransport {
public:
	std::string response_body{"{}"};
	kalshi::HttpMethod method{kalshi::HttpMethod::GET};
	std::string path;
	std::string body;

	[[nodiscard]] kalshi::Result<kalshi::HttpResponse>
	request(kalshi::HttpMethod request_method, std::string_view request_path,
			std::string_view request_body) const override {
		RecordingTransport* self = const_cast<RecordingTransport*>(this);
		self->method = request_method;
		self->path = request_path;
		self->body = request_body;
		return kalshi::HttpResponse{200, response_body, {}};
	}
};

TEST(OperationContracts, ExchangeStatusUsesDocumentedRoute) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body = R"({"trading_active":true,"exchange_active":true})";
	kalshi::KalshiClient client(transport);

	const kalshi::Result<kalshi::ExchangeStatus> result = client.get_exchange_status();

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::GET);
	EXPECT_EQ(transport->path, "/exchange/status");
	EXPECT_TRUE(transport->body.empty());
}

TEST(OperationContracts, ExchangeSchedulePreservesEffectiveRangesAndDailySessions) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body =
		R"({"schedule":{"standard_hours":[{"start_time":"2026-01-01T00:00:00Z","end_time":"2026-12-31T23:59:59Z","monday":[{"open_time":"08:00","close_time":"23:59"}],"tuesday":[],"wednesday":[],"thursday":[],"friday":[],"saturday":[],"sunday":[]}],"maintenance_windows":[{"start_datetime":"2026-09-04T07:00:00Z","end_datetime":"2026-09-04T08:00:00Z"}]}})";
	kalshi::KalshiClient client(transport);

	const kalshi::Result<kalshi::Schedule> schedule = client.get_exchange_schedule();

	ASSERT_TRUE(schedule.has_value());
	ASSERT_EQ(schedule->standard_hours.size(), 1U);
	EXPECT_EQ(schedule->standard_hours[0].start_time, "2026-01-01T00:00:00Z");
	ASSERT_EQ(schedule->standard_hours[0].monday.size(), 1U);
	EXPECT_EQ(schedule->standard_hours[0].monday[0].open_time, "08:00");
	ASSERT_EQ(schedule->maintenance_windows.size(), 1U);
	EXPECT_EQ(schedule->maintenance_windows[0].start_datetime, "2026-09-04T07:00:00Z");
}

TEST(OperationContracts, InjectedTransportIsInspectableWithoutUndefinedBehavior) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	EXPECT_EQ(&client.transport(), transport.get());
	EXPECT_THROW(static_cast<void>(client.http_client()), std::logic_error);
}

TEST(OperationContracts, MarketPreservesCurrentFixedPointAndShardFields) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body = R"({"market":{"ticker":"KXTEST","status":"active",
		"yes_bid_dollars":"0.125000","yes_ask_dollars":"0.135000",
		"no_bid_dollars":"0.865000","no_ask_dollars":"0.875000",
		"last_price_dollars":"0.125000","volume_fp":"12.50",
		"volume_24h_fp":"2.25","open_interest_fp":"8.75",
		"price_level_structure":"center_deci_edge_centi_cent","exchange_index":3}})";
	kalshi::KalshiClient client(transport);

	const kalshi::Result<kalshi::Market> result = client.get_market("KXTEST");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->yes_bid_dollars, "0.125000");
	EXPECT_EQ(result->volume_fp, "12.50");
	EXPECT_EQ(result->open_interest_fp, "8.75");
	EXPECT_EQ(result->price_level_structure, "center_deci_edge_centi_cent");
	EXPECT_EQ(result->exchange_index, 3);
}

TEST(OperationContracts, PortfolioFiltersIncludeShardAndSubaccount) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body = R"({"orders":[],"cursor":""})";
	kalshi::KalshiClient client(transport);
	kalshi::GetOrdersParams params;
	params.market_ticker = "KX A/B";
	params.subaccount = 7;
	params.exchange_index = 3;

	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Order>> result =
		client.get_orders(params);

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(transport->path, "/portfolio/orders?ticker=KX%20A%2FB&subaccount=7&exchange_index=3");
}

TEST(OperationContracts, PortfolioResponsesPreserveExactFixedPointFields) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"balance":1234,"balance_dollars":"12.340000","portfolio_value":4567,"updated_ts":1770000000})";
	const kalshi::Result<kalshi::Balance> balance = client.get_balance();
	ASSERT_TRUE(balance.has_value());
	EXPECT_EQ(balance->balance, 1234);
	EXPECT_EQ(balance->balance_dollars, "12.340000");
	EXPECT_EQ(balance->portfolio_value, 4567);
	EXPECT_EQ(balance->updated_ts, 1770000000);

	transport->response_body =
		R"({"market_positions":[{"ticker":"KXTEST","position_fp":"1.25","total_traded_dollars":"4.125000","market_exposure_dollars":"1.250000","realized_pnl_dollars":"0.375000","fees_paid_dollars":"0.010000","exchange_index":3}],"cursor":""})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Position>> positions =
		client.get_positions();
	ASSERT_TRUE(positions.has_value());
	ASSERT_EQ(positions->items.size(), 1U);
	EXPECT_EQ(positions->items[0].market_ticker, "KXTEST");
	EXPECT_EQ(positions->items[0].position_fp, "1.25");
	EXPECT_EQ(positions->items[0].yes_contracts, 0);
	EXPECT_EQ(positions->items[0].exchange_index, 3);

	transport->response_body =
		R"({"orders":[{"order_id":"o1","ticker":"KXTEST","status":"resting","side":"yes","action":"buy","client_order_id":"c1","yes_price_dollars":"0.125000","no_price_dollars":"0.875000","initial_count_fp":"2.25","remaining_count_fp":"1.25","fill_count_fp":"1.00","taker_fees_dollars":"0.010000","exchange_index":3}],"cursor":""})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Order>> orders = client.get_orders();
	ASSERT_TRUE(orders.has_value());
	ASSERT_EQ(orders->items.size(), 1U);
	EXPECT_EQ(orders->items[0].yes_price_dollars, "0.125000");
	EXPECT_EQ(orders->items[0].initial_count_fp, "2.25");
	EXPECT_EQ(orders->items[0].initial_count, 0);
	EXPECT_EQ(orders->items[0].fill_count_fp, "1.00");
	EXPECT_EQ(orders->items[0].exchange_index, 3);

	transport->response_body =
		R"({"fills":[{"fill_id":"f1","trade_id":"t1","order_id":"o1","ticker":"KXTEST","side":"yes","action":"buy","count_fp":"1.25","yes_price_dollars":"0.125000","no_price_dollars":"0.875000","fee_cost":"0.010000","exchange_index":3}],"cursor":""})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Fill>> fills = client.get_fills();
	ASSERT_TRUE(fills.has_value());
	ASSERT_EQ(fills->items.size(), 1U);
	EXPECT_EQ(fills->items[0].fill_id, "f1");
	EXPECT_EQ(fills->items[0].count_fp, "1.25");
	EXPECT_EQ(fills->items[0].count, 0);
	EXPECT_EQ(fills->items[0].yes_price, 0);
	EXPECT_EQ(fills->items[0].exchange_index, 3);

	transport->response_body =
		R"({"settlements":[{"ticker":"KXTEST","event_ticker":"KXEVENT","result":"yes","yes_count_fp":"1.25","no_count_fp":"0.00","yes_total_cost_dollars":"0.625000","fee_cost":"0.010000","exchange_index":3}],"cursor":""})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Settlement>> settlements =
		client.get_settlements();
	ASSERT_TRUE(settlements.has_value());
	ASSERT_EQ(settlements->items.size(), 1U);
	EXPECT_EQ(settlements->items[0].market_ticker, "KXTEST");
	EXPECT_EQ(settlements->items[0].yes_count_fp, "1.25");
	EXPECT_EQ(settlements->items[0].yes_count, 0);
	EXPECT_EQ(settlements->items[0].exchange_index, 3);
}

TEST(OperationContracts, OrdersPreferCanonicalDirectionAndIsoTimestamps) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body =
		R"({"order":{"order_id":"order-1","user_id":"user-1","client_order_id":"client-1","ticker":"KXTEST","outcome_side":"no","book_side":"ask","type":"limit","status":"resting","yes_price_dollars":"0.2500","no_price_dollars":"0.7500","fill_count_fp":"0.00","remaining_count_fp":"1.50","initial_count_fp":"1.50","taker_fees_dollars":"0.0000","maker_fees_dollars":"0.0000","taker_fill_cost_dollars":"0.0000","maker_fill_cost_dollars":"0.0000","created_time":"2026-09-03T00:00:00Z","expiration_time":"2026-09-04T00:00:00Z","last_update_time":"2026-09-03T00:01:00Z","self_trade_prevention_type":"maker","order_group_id":"group-1","exchange_index":3}})";
	kalshi::KalshiClient client(transport);

	const kalshi::Result<kalshi::Order> result = client.get_order("order-1");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->outcome_side, kalshi::OutcomeSide::No);
	EXPECT_EQ(result->book_side, kalshi::BookSide::Ask);
	EXPECT_EQ(result->user_id, "user-1");
	EXPECT_EQ(result->created_time_iso, "2026-09-03T00:00:00Z");
	EXPECT_EQ(result->expiration_time, "2026-09-04T00:00:00Z");
	EXPECT_EQ(result->last_update_time, "2026-09-03T00:01:00Z");
	EXPECT_EQ(result->self_trade_prevention_type, "maker");
	EXPECT_EQ(result->order_group_id, "group-1");
}

TEST(OperationContracts, FillsSettlementsAndQueuePositionsParseCurrentSchemas) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"fills":[{"fill_id":"fill-1","exchange_index":3,"trade_id":"trade-1","order_id":"order-1","ticker":"KXTEST","market_ticker":"KXTEST","outcome_side":"no","book_side":"ask","count_fp":"1.50","yes_price_dollars":"0.2500","no_price_dollars":"0.7500","is_taker":true,"created_time":"2026-09-03T00:00:00Z","fee_cost":"0.0100","subaccount_number":7,"ts":1788393600}]})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Fill>> fills = client.get_fills();
	ASSERT_TRUE(fills.has_value());
	ASSERT_EQ(fills->items.size(), 1U);
	EXPECT_EQ(fills->items[0].outcome_side, kalshi::OutcomeSide::No);
	EXPECT_EQ(fills->items[0].book_side, kalshi::BookSide::Ask);
	EXPECT_EQ(fills->items[0].created_time_iso, "2026-09-03T00:00:00Z");
	EXPECT_EQ(fills->items[0].subaccount_number, 7);
	EXPECT_EQ(fills->items[0].timestamp, 1788393600);

	transport->response_body =
		R"({"settlements":[{"ticker":"KXTEST","exchange_index":3,"event_ticker":"EV1","market_result":"no","yes_count_fp":"0.00","yes_total_cost_dollars":"0.0000","no_count_fp":"1.00","no_total_cost_dollars":"0.7500","revenue":100,"settled_time":"2026-09-03T00:00:00Z","fee_cost":"0.0100","value":0}]})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Settlement>> settlements =
		client.get_settlements();
	ASSERT_TRUE(settlements.has_value());
	ASSERT_EQ(settlements->items.size(), 1U);
	EXPECT_EQ(settlements->items[0].result, "no");
	EXPECT_EQ(settlements->items[0].settled_time_iso, "2026-09-03T00:00:00Z");
	ASSERT_TRUE(settlements->items[0].value.has_value());
	EXPECT_EQ(*settlements->items[0].value, 0);

	transport->response_body =
		R"({"queue_positions":[{"order_id":"order-1","market_ticker":"KXTEST","queue_position_fp":"1.50"}]})";
	const kalshi::Result<std::vector<kalshi::OrderQueuePosition>> queue =
		client.get_queue_positions(kalshi::GetQueuePositionsParams{});
	ASSERT_TRUE(queue.has_value());
	ASSERT_EQ(queue->size(), 1U);
	EXPECT_EQ((*queue)[0].market_ticker, "KXTEST");
	EXPECT_EQ((*queue)[0].queue_position_fp, "1.50");
	EXPECT_EQ((*queue)[0].position, 0);
}

TEST(OperationContracts, BalanceAndOrderMutationsRouteBySubaccountAndShard) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"balance":2500,"balance_dollars":"25.0000","portfolio_value":3000,"updated_ts":1788393600,"balance_breakdown":[{"exchange_index":3,"balance":"20.0000"},{"exchange_index":4,"balance":"5.0000"}]})";
	const kalshi::Result<kalshi::Balance> balance =
		client.get_balance({.subaccount = 7, .exchange_index = -1});
	ASSERT_TRUE(balance.has_value());
	EXPECT_EQ(transport->path, "/portfolio/balance?subaccount=7&exchange_index=-1");
	ASSERT_EQ(balance->balance_breakdown.size(), 2U);
	EXPECT_EQ(balance->balance_breakdown[0].exchange_index, 3);
	EXPECT_EQ(balance->balance_breakdown[0].balance_dollars, "20.0000");

	transport->response_body = R"({"order":{"order_id":"order-1"}})";
	kalshi::AmendOrderParams amend;
	amend.order_id = "order-1";
	amend.ticker = "KXTEST";
	amend.book_side = kalshi::BookSide::Bid;
	amend.price_dollars = "0.2500";
	amend.count_fp = "1.00";
	amend.subaccount = 7;
	ASSERT_TRUE(client.amend_order(amend).has_value());
	EXPECT_EQ(transport->path, "/portfolio/events/orders/order-1/amend?subaccount=7");

	kalshi::DecreaseOrderParams decrease;
	decrease.order_id = "order-1";
	decrease.reduce_by_fp = "1.00";
	decrease.subaccount = 7;
	ASSERT_TRUE(client.decrease_order(decrease).has_value());
	EXPECT_EQ(transport->path, "/portfolio/events/orders/order-1/decrease?subaccount=7");
}

TEST(OperationContracts, OrderGroupSelectorsAndNestedEventMarketsArePreserved) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = R"({"id":"group-1","contracts_limit_fp":"10.00"})";
	ASSERT_TRUE(client.get_order_group("group-1", {.subaccount = 7}).has_value());
	EXPECT_EQ(transport->path, "/portfolio/order_groups/group-1?subaccount=7");
	ASSERT_TRUE(
		client.delete_order_group("group-1", {.subaccount = 7, .exchange_index = 3}).has_value());
	EXPECT_EQ(transport->path, "/portfolio/order_groups/group-1?subaccount=7&exchange_index=3");
	ASSERT_TRUE(
		client.reset_order_group("group-1", {.subaccount = 7, .exchange_index = 3}).has_value());
	EXPECT_EQ(transport->path,
			  "/portfolio/order_groups/group-1/reset?subaccount=7&exchange_index=3");

	transport->response_body =
		R"({"events":[{"event_ticker":"EV1","series_ticker":"SERIES","title":"Event","markets":[{"ticker":"KXTEST","event_ticker":"EV1","status":"active","yes_bid_dollars":"0.2500","exchange_index":3}]}],"cursor":""})";
	kalshi::GetEventsParams params;
	params.with_nested_markets = true;
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::Event>> events =
		client.get_events(params);
	ASSERT_TRUE(events.has_value());
	ASSERT_EQ(events->items.size(), 1U);
	ASSERT_EQ(events->items[0].markets.size(), 1U);
	EXPECT_EQ(events->items[0].markets[0].ticker, "KXTEST");
	EXPECT_EQ(events->items[0].markets[0].yes_bid_dollars, "0.2500");
}

TEST(OperationContracts, PortfolioReadFiltersUseCurrentNames) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = R"({"market_positions":[],"event_positions":[],"cursor":""})";
	kalshi::GetPositionsParams positions;
	positions.market_ticker = "KXTEST";
	positions.count_filter = "position";
	positions.subaccount = 7;
	positions.exchange_index = 3;
	ASSERT_TRUE(client.get_positions(positions).has_value());
	EXPECT_EQ(
		transport->path,
		"/portfolio/positions?ticker=KXTEST&count_filter=position&subaccount=7&exchange_index=3");

	transport->response_body = R"({"fills":[],"cursor":""})";
	kalshi::GetFillsParams fills;
	fills.market_ticker = "KXTEST";
	fills.subaccount = 7;
	fills.exchange_index = 3;
	ASSERT_TRUE(client.get_fills(fills).has_value());
	EXPECT_EQ(transport->path, "/portfolio/fills?ticker=KXTEST&subaccount=7&exchange_index=3");
}

TEST(OperationContracts, BatchOrderbooksUseOneCommaSeparatedTickerParameter) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body = R"({"orderbooks":[]})";
	kalshi::KalshiClient client(transport);

	ASSERT_TRUE(client.get_market_orderbooks({"KX A", "KX/B"}).has_value());
	EXPECT_EQ(transport->path, "/markets/orderbooks?tickers=KX%20A%2CKX%2FB");
}

TEST(OperationContracts, CandlesticksRequireCurrentRangeAndSeriesContract) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);
	kalshi::GetCandlesticksParams missing_range;
	missing_range.series_ticker = "SERIES";
	missing_range.ticker = "MARKET";
	EXPECT_FALSE(client.get_market_candlesticks(missing_range).has_value());
	EXPECT_TRUE(transport->path.empty());

	transport->response_body = R"({"candlesticks":[]})";
	kalshi::GetCandlesticksParams current;
	current.series_ticker = "SERIES";
	current.ticker = "MARKET";
	current.start_ts = 1760000000;
	current.end_ts = 1770000000;
	current.period_interval = 60;
	current.include_latest_before_start = true;
	ASSERT_TRUE(client.get_market_candlesticks(current).has_value());
	EXPECT_EQ(transport->path, "/series/SERIES/markets/MARKET/"
							   "candlesticks?period_interval=60&start_ts=1760000000&end_ts="
							   "1770000000&include_latest_before_start=true");
}

TEST(OperationContracts, CurrentSeriesAndEventFiltersAreEncoded) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = R"({"series":[]})";
	kalshi::GetSeriesParams series;
	series.category = "Sports & games";
	series.tags = "football,nfl";
	series.include_product_metadata = true;
	series.include_volume = true;
	series.min_updated_ts = 1770000000;
	ASSERT_TRUE(client.get_series_list(series).has_value());
	EXPECT_EQ(transport->path,
			  "/series?category=Sports%20%26%20games&tags=football%2Cnfl&include_product_metadata="
			  "true&include_volume=true&min_updated_ts=1770000000");

	transport->response_body = R"({"events":[],"cursor":""})";
	kalshi::GetEventsParams events;
	events.with_nested_markets = true;
	events.with_milestones = true;
	events.event_tickers = "E1,E2";
	events.min_close_ts = 1760000000;
	events.min_updated_ts = 1770000000;
	ASSERT_TRUE(client.get_events(events).has_value());
	EXPECT_EQ(transport->path, "/events?with_nested_markets=true&with_milestones=true&event_"
							   "tickers=E1%2CE2&min_close_ts=1760000000&min_updated_ts=1770000000");
}

TEST(OperationContracts, CurrentMarketPortfolioAndDiscoveryFiltersAreEncoded) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = R"({"markets":[],"cursor":""})";
	kalshi::GetMarketsParams markets;
	markets.min_created_ts = 1;
	markets.max_close_ts = 2;
	markets.mve_filter = "exclude";
	ASSERT_TRUE(client.get_markets(markets).has_value());
	EXPECT_EQ(transport->path, "/markets?min_created_ts=1&max_close_ts=2&mve_filter=exclude");

	transport->response_body = R"({"orders":[],"cursor":""})";
	kalshi::GetOrdersParams orders;
	orders.event_ticker = "E1,E2";
	orders.min_ts = 10;
	orders.max_ts = 20;
	ASSERT_TRUE(client.get_orders(orders).has_value());
	EXPECT_EQ(transport->path, "/portfolio/orders?event_ticker=E1%2CE2&min_ts=10&max_ts=20");

	transport->response_body = R"({"settlements":[],"cursor":""})";
	kalshi::GetSettlementsParams settlements;
	settlements.event_ticker = "E1";
	settlements.min_ts = 10;
	settlements.max_ts = 20;
	settlements.subaccount = 7;
	ASSERT_TRUE(client.get_settlements(settlements).has_value());
	EXPECT_EQ(transport->path,
			  "/portfolio/settlements?event_ticker=E1&min_ts=10&max_ts=20&subaccount=7");

	transport->response_body = R"({"milestones":[],"cursor":""})";
	kalshi::GetMilestonesParams milestones;
	milestones.minimum_start_date = "2026-09-03";
	milestones.related_event_ticker = "E1";
	milestones.min_updated_ts = 30;
	ASSERT_TRUE(client.get_milestones(milestones).has_value());
	EXPECT_EQ(
		transport->path,
		"/milestones?minimum_start_date=2026-09-03&related_event_ticker=E1&min_updated_ts=30");

	transport->response_body = R"({"structured_targets":[],"cursor":""})";
	kalshi::GetStructuredTargetsParams targets;
	targets.page_size = 25;
	targets.ids = "one,two";
	targets.type = "team";
	ASSERT_TRUE(client.get_structured_targets(targets).has_value());
	EXPECT_EQ(transport->path, "/structured_targets?page_size=25&ids=one%2Ctwo&type=team");
}

TEST(OperationContracts, UserTimestampQueuePositionsAndCancelAllUseCurrentContracts) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = R"({"as_of_time":"2026-09-03T06:00:00Z"})";
	const kalshi::Result<kalshi::UserDataTimestamp> timestamp = client.get_user_data_timestamp();
	ASSERT_TRUE(timestamp.has_value());
	EXPECT_EQ(timestamp->as_of_time, "2026-09-03T06:00:00Z");
	EXPECT_EQ(transport->path, "/exchange/user_data_timestamp");

	transport->response_body = R"({"queue_positions":[]})";
	kalshi::GetQueuePositionsParams queue;
	queue.market_tickers = "KX1,KX2";
	queue.event_ticker = "EV1";
	queue.subaccount = 7;
	ASSERT_TRUE(client.get_queue_positions(queue).has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::GET);
	EXPECT_EQ(
		transport->path,
		"/portfolio/orders/queue_positions?market_tickers=KX1%2CKX2&event_ticker=EV1&subaccount=7");

	transport->response_body.clear();
	ASSERT_TRUE(client.cancel_all_orders(7).has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::DEL);
	EXPECT_EQ(transport->path, "/portfolio/events/orders?subaccount=7");
}

TEST(OperationContracts, SubaccountsUseCurrentIdempotentAndNettingSchemas) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = "{}";
	kalshi::SubaccountTransfer transfer;
	transfer.client_transfer_id = "11111111-1111-4111-8111-111111111111";
	transfer.from_subaccount = 0;
	transfer.to_subaccount = 7;
	transfer.amount_cents = 2500;
	transfer.exchange_index = 3;
	ASSERT_TRUE(client.transfer_subaccount(transfer).has_value());
	EXPECT_EQ(
		transport->body,
		R"({"client_transfer_id":"11111111-1111-4111-8111-111111111111","from_subaccount":0,"to_subaccount":7,"amount_cents":2500,"exchange_index":3})");

	transport->response_body =
		R"({"subaccount_balances":[{"subaccount_number":7,"exchange_index":3,"balance":"25.000000","updated_ts":1770000000}]})";
	const kalshi::Result<kalshi::SubaccountBalances> balances = client.get_subaccount_balances();
	ASSERT_TRUE(balances.has_value());
	ASSERT_EQ(balances->balances.size(), 1U);
	EXPECT_EQ(balances->balances[0].balance_dollars, "25.000000");
	EXPECT_EQ(balances->balances[0].exchange_index, 3);

	transport->response_body = "{}";
	ASSERT_TRUE(client.update_subaccount_netting(7, true).has_value());
	EXPECT_EQ(transport->body, R"({"subaccount_number":7,"enabled":true})");

	transport->response_body =
		R"({"netting_configs":[{"subaccount_number":7,"enabled":true,"exchange_index":3}]})";
	const kalshi::Result<kalshi::SubaccountNettingList> configs = client.get_subaccount_netting();
	ASSERT_TRUE(configs.has_value());
	ASSERT_EQ(configs->netting_settings.size(), 1U);
	EXPECT_EQ(configs->netting_settings[0].subaccount, 7);
	EXPECT_TRUE(configs->netting_settings[0].netting_enabled);
	EXPECT_EQ(configs->netting_settings[0].exchange_index, 3);
}

TEST(OperationContracts, OrderCancellationAndBatchUseCurrentEventMarketRoutes) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	EXPECT_TRUE(client.cancel_order("order-1").has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::DEL);
	EXPECT_EQ(transport->path, "/portfolio/events/orders/order-1");

	kalshi::BatchOrderRequest batch_create;
	kalshi::CreateOrderParams batch_order;
	batch_order.ticker = "KXTEST";
	batch_order.book_side = kalshi::BookSide::Bid;
	batch_order.count_fp = "1.00";
	batch_order.price_dollars = "0.5000";
	batch_order.time_in_force = "good_till_canceled";
	batch_order.self_trade_prevention_type = "taker_at_cross";
	batch_create.orders.push_back(batch_order);
	EXPECT_TRUE(client.batch_create_orders(batch_create).has_value());
	EXPECT_EQ(transport->path, "/portfolio/events/orders/batched");

	kalshi::BatchCancelRequest batch_cancel;
	EXPECT_TRUE(client.batch_cancel_orders(batch_cancel).has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::DEL);
	EXPECT_EQ(transport->path, "/portfolio/events/orders/batched");
}

TEST(OperationContracts, OrderMutationsUseExactV2Bodies) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"order_id":"o1","fill_count":"0.25","remaining_count":"1.00","ts_ms":1770000000000})";
	kalshi::CreateOrderParams create;
	create.ticker = "KXTEST";
	create.book_side = kalshi::BookSide::Bid;
	create.count_fp = "1.25";
	create.price_dollars = "0.125000";
	create.time_in_force = "good_till_canceled";
	create.self_trade_prevention_type = "taker_at_cross";
	create.exchange_index = -1;
	const kalshi::Result<kalshi::Order> created = client.create_order(create);
	ASSERT_TRUE(created.has_value());
	EXPECT_EQ(
		transport->body,
		R"({"ticker":"KXTEST","side":"bid","count":"1.25","price":"0.125000","time_in_force":"good_till_canceled","self_trade_prevention_type":"taker_at_cross","exchange_index":-1})");
	EXPECT_EQ(created->order_id, "o1");
	EXPECT_EQ(created->fill_count_fp, "0.25");
	EXPECT_EQ(created->remaining_count_fp, "1.00");
	EXPECT_EQ(created->mutation_ts_ms, 1770000000000);

	transport->response_body =
		R"({"order_id":"o1","remaining_count":"0.75","fill_count":"0.50","ts_ms":1770000000001})";
	kalshi::AmendOrderParams amend;
	amend.order_id = "o1";
	amend.ticker = "KXTEST";
	amend.book_side = kalshi::BookSide::Ask;
	amend.price_dollars = "0.875000";
	amend.count_fp = "1.25";
	amend.exchange_index = 3;
	ASSERT_TRUE(client.amend_order(amend).has_value());
	EXPECT_EQ(
		transport->body,
		R"({"ticker":"KXTEST","side":"ask","price":"0.875000","count":"1.25","exchange_index":3})");

	transport->response_body =
		R"({"order_id":"o1","remaining_count":"0.50","ts_ms":1770000000002})";
	kalshi::DecreaseOrderParams decrease;
	decrease.order_id = "o1";
	decrease.reduce_by_fp = "0.25";
	decrease.market_ticker = "KXTEST";
	decrease.exchange_index = -1;
	ASSERT_TRUE(client.decrease_order(decrease).has_value());
	EXPECT_EQ(transport->body,
			  R"({"reduce_by":"0.25","exchange_index":-1,"market_ticker":"KXTEST"})");
}

TEST(OperationContracts, LegacyAmbiguousOrderBodyIsRejectedBeforeTransport) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);
	kalshi::CreateOrderParams legacy;
	legacy.ticker = "KXTEST";
	legacy.count = 1;
	legacy.yes_price = 50;

	const kalshi::Result<kalshi::Order> result = client.create_order(legacy);

	ASSERT_FALSE(result.has_value());
	EXPECT_EQ(result.error().code, kalshi::ErrorCode::InvalidRequest);
	EXPECT_TRUE(transport->path.empty());
}

TEST(OperationContracts, NamedResourcesUseCurrentPathsAndVerbs) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	EXPECT_TRUE(client.get_trades().has_value());
	EXPECT_EQ(transport->path, "/markets/trades");
	EXPECT_TRUE(client.get_order_groups().has_value());
	EXPECT_EQ(transport->path, "/portfolio/order_groups");
	EXPECT_TRUE(client.create_order_group({}).has_value());
	EXPECT_EQ(transport->path, "/portfolio/order_groups/create");
	EXPECT_TRUE(client.reset_order_group("group-1", {.subaccount = 0}).has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::PUT);
	EXPECT_EQ(transport->path, "/portfolio/order_groups/group-1/reset?subaccount=0");
	EXPECT_TRUE(client.get_order_queue_position("order-1").has_value());
	EXPECT_EQ(transport->path, "/portfolio/orders/order-1/queue_position");
	EXPECT_TRUE(client.get_structured_targets().has_value());
	EXPECT_EQ(transport->path, "/structured_targets");
	EXPECT_TRUE(client.get_multivariate_collections().has_value());
	EXPECT_EQ(transport->path, "/multivariate_event_collections");
	EXPECT_TRUE(client.get_incentive_programs().has_value());
	EXPECT_EQ(transport->path, "/incentive_programs");
	EXPECT_TRUE(client.get_total_resting_order_value().has_value());
	EXPECT_EQ(transport->path, "/portfolio/summary/total_resting_order_value");
}

TEST(OperationContracts, CommunicationsAndApiKeysUseCurrentPathsAndVerbs) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	EXPECT_TRUE(client.get_rfqs().has_value());
	EXPECT_EQ(transport->path, "/communications/rfqs");
	EXPECT_TRUE(client.get_rfq("rfq-1").has_value());
	EXPECT_EQ(transport->path, "/communications/rfqs/rfq-1");
	EXPECT_TRUE(client.get_quotes().has_value());
	EXPECT_EQ(transport->path, "/communications/quotes");
	transport->response_body = R"({"quote":{"id":"quote-1","rfq_id":"rfq-1"}})";
	EXPECT_TRUE(client.get_quote("rfq-1", "quote-1").has_value());
	EXPECT_EQ(transport->path, "/communications/rfqs/rfq-1/quotes/quote-1");
	EXPECT_TRUE(client.accept_quote("rfq-1", "quote-1", kalshi::Side::Yes).has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::PUT);
	EXPECT_EQ(transport->path, "/communications/rfqs/rfq-1/quotes/quote-1/accept");
	EXPECT_TRUE(client.confirm_quote("rfq-1", "quote-1").has_value());
	EXPECT_EQ(transport->path, "/communications/rfqs/rfq-1/quotes/quote-1/confirm");
	EXPECT_TRUE(client.delete_quote("rfq-1", "quote-1").has_value());
	EXPECT_EQ(transport->method, kalshi::HttpMethod::DEL);
	EXPECT_EQ(transport->path, "/communications/rfqs/rfq-1/quotes/quote-1");
	EXPECT_TRUE(client.get_api_keys().has_value());
	EXPECT_EQ(transport->path, "/api_keys");
	EXPECT_TRUE(client.generate_api_key({.name = "test"}).has_value());
	EXPECT_EQ(transport->path, "/api_keys/generate");
}

TEST(OperationContracts, ApiKeyListPreservesScopesBindingsAndRegionExpiration) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body =
		R"({"api_keys":[{"api_key_id":"key-1","name":"trader","scopes":["read","write::trade"],"subaccount":7,"fcm_subtrader_id":""},{"api_key_id":"key-2","name":"unbound","scopes":["read"],"subaccount": null}],"api_key_region_expiration_ts": 1770000000})";
	kalshi::KalshiClient client(transport);

	const kalshi::Result<kalshi::ApiKeysResponse> response = client.get_api_keys_response();

	ASSERT_TRUE(response.has_value());
	ASSERT_EQ(response->api_keys.size(), 2U);
	EXPECT_EQ(response->api_keys[0].id, "key-1");
	EXPECT_EQ(response->api_keys[0].scopes, (std::vector<std::string>{"read", "write::trade"}));
	ASSERT_TRUE(response->api_keys[0].subaccount.has_value());
	EXPECT_EQ(*response->api_keys[0].subaccount, 7);
	EXPECT_FALSE(response->api_keys[1].subaccount.has_value());
	ASSERT_TRUE(response->api_key_region_expiration_ts.has_value());
	EXPECT_EQ(*response->api_key_region_expiration_ts, 1770000000);
}

TEST(OperationContracts, GeneratedPrivateKeyIsDecodedFromJson) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	transport->response_body =
		R"({"api_key_id":"key-1","private_key":"-----BEGIN PRIVATE KEY-----\nABC\\DEF\n-----END PRIVATE KEY-----\n","warning":null})";
	kalshi::KalshiClient client(transport);

	const kalshi::Result<kalshi::ApiKey> result = client.generate_api_key({.name = "generated"});

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->private_key,
			  "-----BEGIN PRIVATE KEY-----\nABC\\DEF\n-----END PRIVATE KEY-----\n");
}

TEST(OperationContracts, CurrentStringArrayFieldsAreParsedWithoutJsonFragments) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"series":{"ticker":"SERIES","frequency":"weekly","title":"Title","category":"Sports","tags":["football","women's sports"],"settlement_sources":[],"contract_url":"https://example.com/contract","contract_terms_url":"https://example.com/terms","fee_type":"quadratic","fee_multiplier":0.75,"additional_prohibitions":["employees","escaped \"quote\""]}})";
	const kalshi::Result<kalshi::Series> series = client.get_series("SERIES");
	ASSERT_TRUE(series.has_value());
	EXPECT_EQ(series->tags, (std::vector<std::string>{"football", "women's sports"}));
	EXPECT_EQ(series->additional_prohibitions,
			  (std::vector<std::string>{"employees", "escaped \"quote\""}));
	EXPECT_DOUBLE_EQ(series->fee_multiplier, 0.75);

	transport->response_body =
		R"({"is_auto_cancel_enabled":true,"contracts_limit_fp":"10.00","orders":["order-1","order-2"],"exchange_index":3})";
	const kalshi::Result<kalshi::OrderGroup> group =
		client.get_order_group("group-1", {.subaccount = 0});
	ASSERT_TRUE(group.has_value());
	EXPECT_EQ(group->order_ids, (std::vector<std::string>{"order-1", "order-2"}));

	transport->response_body =
		R"({"milestone":{"id":"m1","category":"Sports","type":"football_game","start_date":"2026-09-03T00:00:00Z","related_event_tickers":["E1","E2"],"title":"Game","notification_message":"Starts soon","details":{},"primary_event_tickers":["E1"],"last_updated_ts":"2026-09-03T01:00:00Z"}})";
	const kalshi::Result<kalshi::Milestone> milestone = client.get_milestone("m1");
	ASSERT_TRUE(milestone.has_value());
	EXPECT_EQ(milestone->related_event_tickers, (std::vector<std::string>{"E1", "E2"}));
	EXPECT_EQ(milestone->primary_event_tickers, (std::vector<std::string>{"E1"}));
}

TEST(OperationContracts, RfqQuoteAndKeyBodiesUseCurrentExactSchemas) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body = R"({"id":"rfq-1"})";
	kalshi::CreateRfqParams rfq;
	rfq.market_ticker = "KXTEST";
	rfq.contracts_fp = "1.25";
	rfq.target_cost_dollars = "0.625000";
	rfq.rest_remainder = true;
	rfq.subaccount = 7;
	ASSERT_TRUE(client.create_rfq(rfq).has_value());
	EXPECT_EQ(
		transport->body,
		R"({"market_ticker":"KXTEST","contracts_fp":"1.25","target_cost_dollars":"0.625000","rest_remainder":true,"subaccount":7})");

	transport->response_body = R"({"id":"quote-1"})";
	kalshi::CreateQuoteParams quote;
	quote.rfq_id = "rfq-1";
	quote.yes_bid_dollars = "0.400000";
	quote.no_bid_dollars = "0.600000";
	quote.rest_remainder = false;
	quote.post_only = true;
	quote.subaccount = 7;
	ASSERT_TRUE(client.create_quote(quote).has_value());
	EXPECT_EQ(
		transport->body,
		R"({"rfq_id":"rfq-1","yes_bid":"0.400000","no_bid":"0.600000","rest_remainder":false,"post_only":true,"subaccount":7})");

	ASSERT_TRUE(client.accept_quote("rfq-1", "quote-1", kalshi::Side::Yes).has_value());
	EXPECT_EQ(transport->body, R"({"accepted_side":"yes"})");

	transport->response_body = R"({"api_key_id":"key-1"})";
	kalshi::CreateApiKeyParams key;
	key.name = "test";
	key.public_key = "-----BEGIN PUBLIC KEY-----";
	key.scopes = {"read"};
	key.subaccount = 7;
	const kalshi::Result<kalshi::ApiKey> created_key = client.create_api_key(key);
	ASSERT_TRUE(created_key.has_value());
	EXPECT_EQ(created_key->id, "key-1");
	EXPECT_EQ(
		transport->body,
		R"({"name":"test","public_key":"-----BEGIN PUBLIC KEY-----","scopes":["read"],"subaccount":7})");
}

TEST(OperationContracts, CurrentRfqAndQuoteResponsesPreserveExactFields) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"id":"rfq-1","creator_id":"u1","market_ticker":"KXTEST","contracts_fp":"1.25","target_cost_dollars":"0.625000","status":"open","created_ts":"2026-09-03T00:00:00Z","rest_remainder":true,"creator_subaccount":7})";
	const kalshi::Result<kalshi::Rfq> rfq = client.get_rfq("rfq-1");
	ASSERT_TRUE(rfq.has_value());
	EXPECT_EQ(rfq->contracts_fp, "1.25");
	EXPECT_EQ(rfq->target_cost_dollars, "0.625000");
	EXPECT_EQ(rfq->created_ts, "2026-09-03T00:00:00Z");
	EXPECT_EQ(rfq->creator_subaccount, 7);

	transport->response_body =
		R"({"id":"quote-1","rfq_id":"rfq-1","creator_id":"u2","rfq_creator_id":"u1","market_ticker":"KXTEST","contracts_fp":"1.25","yes_bid_dollars":"0.400000","no_bid_dollars":"0.600000","created_ts":"2026-09-03T00:00:00Z","updated_ts":"2026-09-03T00:01:00Z","status":"open","post_only":true})";
	const kalshi::Result<kalshi::Quote> quote = client.get_quote("rfq-1", "quote-1");
	ASSERT_TRUE(quote.has_value());
	EXPECT_EQ(quote->contracts_fp, "1.25");
	EXPECT_EQ(quote->yes_bid_dollars, "0.400000");
	EXPECT_EQ(quote->no_bid_dollars, "0.600000");
	EXPECT_TRUE(quote->post_only);
}

TEST(OperationContracts, RemovedGenericOperationsFailBeforeTransport) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	EXPECT_FALSE(client.get_communication("legacy").has_value());
	EXPECT_FALSE(client.lookup_multivariate_bundle("legacy", {}).has_value());
	EXPECT_FALSE(client.get_live_datas({"legacy"}).has_value());
#pragma clang diagnostic pop
	EXPECT_TRUE(transport->path.empty());
}

TEST(OperationContracts, DiscoveryResponsesUseCurrentCollectionKeysAndFields) {
	const std::shared_ptr<RecordingTransport> transport = std::make_shared<RecordingTransport>();
	kalshi::KalshiClient client(transport);

	transport->response_body =
		R"({"structured_targets":[{"id":"t1","name":"Team","type":"team","source_id":"s1","last_updated_ts":"2026-09-03T00:00:00Z"}],"cursor":""})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::StructuredTarget>> targets =
		client.get_structured_targets();
	ASSERT_TRUE(targets.has_value());
	ASSERT_EQ(targets->items.size(), 1U);
	EXPECT_EQ(targets->items[0].name, "Team");
	EXPECT_EQ(targets->items[0].type, "team");

	transport->response_body =
		R"({"multivariate_contracts":[{"collection_ticker":"MVE1","series_ticker":"SERIES","exchange_index":3,"title":"Collection","description":"desc","open_date":"2026-01-01T00:00:00Z","close_date":"2026-12-31T00:00:00Z","is_ordered":true,"is_single_market_per_event":false,"is_all_yes":true,"size_min":2,"size_max":5,"functional_description":"all legs"}],"cursor":""})";
	const kalshi::Result<kalshi::PaginatedResponse<kalshi::MultivariateCollection>> collections =
		client.get_multivariate_collections();
	ASSERT_TRUE(collections.has_value());
	ASSERT_EQ(collections->items.size(), 1U);
	EXPECT_EQ(collections->items[0].collection_ticker, "MVE1");
	EXPECT_EQ(collections->items[0].exchange_index, 3);
	EXPECT_TRUE(collections->items[0].is_ordered);

	transport->response_body =
		R"({"incentive_programs":[{"id":"i1","market_id":"m1","market_ticker":"KXTEST","incentive_type":"liquidity","incentive_description":"quote","start_date":"2026-09-01T00:00:00Z","end_date":"2026-09-30T00:00:00Z","period_reward":1000,"paid_out":false,"target_size_fp":"10.00"}]})";
	const kalshi::Result<std::vector<kalshi::IncentiveProgram>> programs =
		client.get_incentive_programs();
	ASSERT_TRUE(programs.has_value());
	ASSERT_EQ(programs->size(), 1U);
	EXPECT_EQ((*programs)[0].market_ticker, "KXTEST");
	EXPECT_EQ((*programs)[0].target_size_fp, "10.00");
}

} // namespace
