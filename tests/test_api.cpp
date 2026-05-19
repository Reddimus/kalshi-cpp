#include "kalshi/api.hpp"

#include <gtest/gtest.h>
#include <string>

// --- Parse side ---

TEST(Api, ParseSideYes) {
	ASSERT_EQ(kalshi::parse_side("yes"), kalshi::Side::Yes);
	ASSERT_EQ(kalshi::parse_side("Yes"), kalshi::Side::Yes);
}

TEST(Api, ParseSideNo) {
	ASSERT_EQ(kalshi::parse_side("no"), kalshi::Side::No);
	ASSERT_EQ(kalshi::parse_side("No"), kalshi::Side::No);
}

// --- Parse action ---

TEST(Api, ParseActionBuy) {
	ASSERT_EQ(kalshi::parse_action("buy"), kalshi::Action::Buy);
	ASSERT_EQ(kalshi::parse_action("Buy"), kalshi::Action::Buy);
}

TEST(Api, ParseActionSell) {
	ASSERT_EQ(kalshi::parse_action("sell"), kalshi::Action::Sell);
	ASSERT_EQ(kalshi::parse_action("Sell"), kalshi::Action::Sell);
}

// --- Parse market status ---

TEST(Api, ParseMarketStatusOpen) {
	ASSERT_EQ(kalshi::parse_market_status("active"), kalshi::MarketStatus::Open);
	ASSERT_EQ(kalshi::parse_market_status("open"), kalshi::MarketStatus::Open);
	ASSERT_EQ(kalshi::parse_market_status("initialized"), kalshi::MarketStatus::Open);
}

TEST(Api, ParseMarketStatusUnopened) {
	ASSERT_EQ(kalshi::parse_market_status("unopened"), kalshi::MarketStatus::Unopened);
}

TEST(Api, ParseMarketStatusPaused) {
	ASSERT_EQ(kalshi::parse_market_status("paused"), kalshi::MarketStatus::Paused);
}

TEST(Api, ParseMarketStatusSettled) {
	ASSERT_EQ(kalshi::parse_market_status("settled"), kalshi::MarketStatus::Settled);
	ASSERT_EQ(kalshi::parse_market_status("determined"), kalshi::MarketStatus::Settled);
}

TEST(Api, ParseMarketStatusClosed) {
	ASSERT_EQ(kalshi::parse_market_status("closed"), kalshi::MarketStatus::Closed);
}

// --- Parse order status ---

TEST(Api, ParseOrderStatusOpen) {
	ASSERT_EQ(kalshi::parse_order_status("open"), kalshi::OrderStatus::Open);
	ASSERT_EQ(kalshi::parse_order_status("resting"), kalshi::OrderStatus::Open);
}

TEST(Api, ParseOrderStatusPending) {
	ASSERT_EQ(kalshi::parse_order_status("pending"), kalshi::OrderStatus::Pending);
}

TEST(Api, ParseOrderStatusFilled) {
	ASSERT_EQ(kalshi::parse_order_status("filled"), kalshi::OrderStatus::Filled);
	ASSERT_EQ(kalshi::parse_order_status("executed"), kalshi::OrderStatus::Filled);
}

TEST(Api, ParseOrderStatusCancelled) {
	ASSERT_EQ(kalshi::parse_order_status("cancelled"), kalshi::OrderStatus::Cancelled);
	ASSERT_EQ(kalshi::parse_order_status("canceled"), kalshi::OrderStatus::Cancelled);
}

// --- Enum to JSON string ---

TEST(Api, ToJsonStringSide) {
	ASSERT_EQ(kalshi::to_json_string(kalshi::Side::Yes), std::string_view("yes"));
	ASSERT_EQ(kalshi::to_json_string(kalshi::Side::No), std::string_view("no"));
}

TEST(Api, ToJsonStringAction) {
	ASSERT_EQ(kalshi::to_json_string(kalshi::Action::Buy), std::string_view("buy"));
	ASSERT_EQ(kalshi::to_json_string(kalshi::Action::Sell), std::string_view("sell"));
}

// --- Request parameter structures ---

TEST(Api, GetMarketsParamsDefault) {
	kalshi::GetMarketsParams params;
	ASSERT_FALSE(params.limit.has_value());
	ASSERT_FALSE(params.cursor.has_value());
	ASSERT_FALSE(params.event_ticker.has_value());
}

TEST(Api, CreateOrderParams) {
	kalshi::CreateOrderParams params;
	params.ticker = "TEST-MARKET";
	params.side = kalshi::Side::Yes;
	params.action = kalshi::Action::Buy;
	params.type = "limit";
	params.count = 10;
	params.yes_price = 50;

	ASSERT_EQ(params.ticker, std::string("TEST-MARKET"));
	ASSERT_EQ(params.side, kalshi::Side::Yes);
	ASSERT_EQ(params.action, kalshi::Action::Buy);
	ASSERT_EQ(params.type, std::string("limit"));
	ASSERT_EQ(params.count, 10);
	ASSERT_TRUE(params.yes_price.has_value());
	ASSERT_EQ(*params.yes_price, 50);
}

TEST(Api, AmendOrderParams) {
	kalshi::AmendOrderParams params;
	params.order_id = "order-123";
	params.count = 5;
	params.yes_price = 55;

	ASSERT_EQ(params.order_id, std::string("order-123"));
	ASSERT_TRUE(params.count.has_value());
	ASSERT_EQ(*params.count, 5);
	ASSERT_TRUE(params.yes_price.has_value());
	ASSERT_EQ(*params.yes_price, 55);
}

TEST(Api, BatchCancelRequestSupportsCurrentOrderSelectors) {
	kalshi::BatchCancelRequest request;
	request.order_ids = {"legacy-id"};
	request.orders.push_back(kalshi::BatchCancelOrder{
		.order_id = "order-123",
		.subaccount = 42,
		.exchange_index = 1,
	});

	ASSERT_EQ(request.order_ids.front(), std::string("legacy-id"));
	ASSERT_EQ(request.orders.front().order_id, std::string("order-123"));
	ASSERT_EQ(request.orders.front().subaccount, 42);
	ASSERT_EQ(request.orders.front().exchange_index, 1);
}

TEST(Api, CancelOrderV2ParamsSupportSubaccountSelectors) {
	kalshi::CancelOrderV2Params params;
	params.order_id = "order-123";
	params.subaccount = 7;
	params.exchange_index = 0;

	ASSERT_EQ(params.order_id, std::string("order-123"));
	ASSERT_EQ(params.subaccount, 7);
	ASSERT_EQ(params.exchange_index, 0);
}

TEST(Api, OrderCancelResultCapturesPerOrderError) {
	kalshi::OrderCancelResult result;
	result.order_id = "order-123";
	result.reduced_by = "10.00";
	result.ts_ms = 1779148800123;
	result.error = kalshi::OrderCancelError{.code = "not_found", .message = "missing"};

	ASSERT_EQ(result.order_id, std::string("order-123"));
	ASSERT_EQ(result.reduced_by, std::string("10.00"));
	ASSERT_EQ(result.ts_ms, 1779148800123);
	ASSERT_TRUE(result.error.has_value());
	ASSERT_EQ(result.error->code, std::string("not_found"));
	ASSERT_EQ(result.error->message, std::string("missing"));
}

// --- Response structures ---

TEST(Api, EventDefaultConstruction) {
	kalshi::Event event;
	ASSERT_TRUE(event.event_ticker.empty());
	ASSERT_TRUE(event.title.empty());
}

TEST(Api, BalanceDefaultConstruction) {
	kalshi::Balance balance;
	ASSERT_EQ(balance.balance, 0);
	ASSERT_EQ(balance.available_balance, 0);
}

TEST(Api, FillDefaultConstruction) {
	kalshi::Fill fill;
	ASSERT_TRUE(fill.trade_id.empty());
	ASSERT_EQ(fill.count, 0);
	ASSERT_FALSE(fill.is_taker);
}

TEST(Api, SettlementDefaultConstruction) {
	kalshi::Settlement settlement;
	ASSERT_TRUE(settlement.market_ticker.empty());
	ASSERT_EQ(settlement.yes_count, 0);
	ASSERT_EQ(settlement.no_count, 0);
}

TEST(Api, CandlestickDefaultConstruction) {
	kalshi::Candlestick candle;
	ASSERT_EQ(candle.timestamp, 0);
	ASSERT_EQ(candle.open_price, 0);
	ASSERT_EQ(candle.volume, 0);
}

TEST(Api, SubaccountDefaultConstruction) {
	kalshi::Subaccount sub;
	ASSERT_EQ(sub.subaccount_number, 0);
	ASSERT_EQ(sub.balance, 0);
}

TEST(Api, SubaccountTransferDefaultConstruction) {
	kalshi::SubaccountTransfer transfer;
	ASSERT_EQ(transfer.from_subaccount, 0);
	ASSERT_EQ(transfer.to_subaccount, 0);
	ASSERT_EQ(transfer.amount, 0);
}

TEST(Api, SubaccountBalancesDefaultConstruction) {
	kalshi::SubaccountBalances balances;
	ASSERT_TRUE(balances.balances.empty());
}

TEST(Api, SubaccountTransfersDefaultConstruction) {
	kalshi::SubaccountTransfers transfers;
	ASSERT_TRUE(transfers.transfers.empty());
	ASSERT_TRUE(transfers.cursor.empty());
}

TEST(Api, SubaccountNettingDefaultConstruction) {
	kalshi::SubaccountNetting netting;
	ASSERT_EQ(netting.subaccount, 0);
	ASSERT_FALSE(netting.netting_enabled);
}

TEST(Api, SubaccountNettingListDefaultConstruction) {
	kalshi::SubaccountNettingList list;
	ASSERT_TRUE(list.netting_settings.empty());
}

TEST(Api, GetSubaccountTransfersParamsDefaultConstruction) {
	kalshi::GetSubaccountTransfersParams params;
	ASSERT_FALSE(params.limit.has_value());
	ASSERT_FALSE(params.cursor.has_value());
}

TEST(Api, PublicTradeDefaultConstruction) {
	kalshi::PublicTrade trade;
	ASSERT_TRUE(trade.trade_id.empty());
	ASSERT_EQ(trade.count, 0);
}

TEST(Api, ExchangeStatusDefaultConstruction) {
	kalshi::ExchangeStatus status;
	ASSERT_FALSE(status.trading_active);
	ASSERT_FALSE(status.exchange_active);
}

TEST(Api, AccountApiLimitsDefaultConstruction) {
	kalshi::AccountApiLimits limits;
	ASSERT_TRUE(limits.usage_tier.empty());
	ASSERT_EQ(limits.read.refill_rate, 0);
	ASSERT_EQ(limits.read.bucket_capacity, 0);
	ASSERT_EQ(limits.write.refill_rate, 0);
	ASSERT_EQ(limits.write.bucket_capacity, 0);
}

TEST(Api, EndpointCostsDefaultConstruction) {
	kalshi::EndpointCosts costs;
	ASSERT_EQ(costs.default_cost, 0);
	ASSERT_TRUE(costs.endpoint_costs.empty());

	kalshi::EndpointCost row;
	ASSERT_TRUE(row.method.empty());
	ASSERT_TRUE(row.path.empty());
	ASSERT_EQ(row.cost, 0);
}

TEST(Api, SeriesDefaultConstruction) {
	kalshi::Series series;
	ASSERT_TRUE(series.ticker.empty());
	ASSERT_TRUE(series.title.empty());
}

TEST(Api, MarketDefaultTimestamps) {
	kalshi::Market market;
	ASSERT_EQ(market.open_time, 0);
	ASSERT_EQ(market.close_time, 0);
	ASSERT_FALSE(market.expected_expiration_time.has_value());
	ASSERT_FALSE(market.expiration_time.has_value());
	ASSERT_FALSE(market.latest_expiration_time.has_value());
	ASSERT_FALSE(market.settlement_ts.has_value());
	ASSERT_FALSE(market.settlement_timer_seconds.has_value());
	ASSERT_FALSE(market.settlement_value_cents.has_value());
}
