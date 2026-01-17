#pragma once

/// @file api.hpp
/// @brief Complete REST API client for Kalshi

#include "kalshi/error.hpp"
#include "kalshi/http_client.hpp"
#include "kalshi/models/market.hpp"
#include "kalshi/models/order.hpp"
#include "kalshi/pagination.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kalshi {

// Forward declarations for API response types

/// Event containing multiple markets
struct Event {
	std::string event_ticker;
	std::string series_ticker;
	std::string title;
	std::string category;
	std::string sub_title;
	std::int64_t mutually_exclusive{0};
	std::vector<std::string> market_tickers;
};

/// Series containing multiple events
struct Series {
	std::string ticker;
	std::string title;
	std::string category;
	std::string frequency;
};

/// Exchange status
struct ExchangeStatus {
	bool trading_active{false};
	bool exchange_active{false};
};

/// Account balance
struct Balance {
	std::int64_t balance{0};		   // cents
	std::int64_t available_balance{0}; // cents
};

/// Fill (trade execution for user)
/// Memory layout optimized: 8-byte fields first, then 4-byte, then 1-byte, strings last
struct Fill {
	// 8-byte aligned field
	std::int64_t created_time{0};

	// 4-byte fields grouped together
	std::int32_t count{0};
	std::int32_t yes_price{0};
	std::int32_t no_price{0};

	// 1-byte fields packed together
	Side side{Side::Yes};
	Action action{Action::Buy};
	bool is_taker{false};

	// Strings last (have internal pointers, variable size)
	std::string trade_id;
	std::string order_id;
	std::string market_ticker;
};

/// Settlement record
/// Memory layout optimized
struct Settlement {
	// 8-byte aligned fields
	std::int64_t revenue{0};
	std::int64_t settled_time{0};

	// 4-byte fields
	std::int32_t yes_count{0};
	std::int32_t no_count{0};

	// Strings last
	std::string market_ticker;
	std::string result;
};

/// Candlestick data for market history
struct Candlestick {
	std::int64_t timestamp{0};
	std::int32_t open_price{0};
	std::int32_t close_price{0};
	std::int32_t high_price{0};
	std::int32_t low_price{0};
	std::int32_t volume{0};
};

/// Public trade record
struct PublicTrade {
	std::string trade_id;
	std::string market_ticker;
	std::int32_t yes_price{0};
	std::int32_t no_price{0};
	std::int32_t count{0};
	Side taker_side{Side::Yes};
	std::int64_t created_time{0};
};

// ===== Phase 1: Exchange API Models =====

/// Weekly schedule entry for exchange hours
struct WeeklySchedule {
	std::string day;
	std::string open;
	std::string close;
};

/// Maintenance window
struct MaintenanceWindow {
	std::int64_t start{0};
	std::int64_t end{0};
	std::string description;
};

/// Exchange schedule
struct Schedule {
	std::vector<WeeklySchedule> standard_hours;
	std::vector<MaintenanceWindow> maintenance_windows;
};

/// Exchange announcement
struct Announcement {
	std::string id;
	std::string title;
	std::string body;
	std::int64_t created_time{0};
	std::string type;
};

// ===== Phase 2: Events/Series API Models =====

/// Event metadata
struct EventMetadata {
	std::string event_ticker;
	std::string description;
	std::string rules;
	std::string resolution_source;
};

// ===== Phase 3: Order Groups Models =====

/// Order group
struct OrderGroup {
	std::string id;
	std::vector<std::string> order_ids;
	std::string status;
	std::string type;
	std::int64_t created_time{0};
};

// ===== Phase 4: Order Queue Position Models =====

/// Order queue position
struct OrderQueuePosition {
	std::string order_id;
	std::int32_t position{0};
	std::int32_t total_at_price{0};
};

// ===== Phase 5: RFQ/Quotes Models =====

/// Request for quote
struct Rfq {
	std::string id;
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	std::int32_t count{0};
	std::string status;
	std::int64_t expires_at{0};
	std::int64_t created_time{0};
};

/// Quote response to RFQ
struct Quote {
	std::string id;
	std::string rfq_id;
	std::int32_t price{0};
	std::int32_t count{0};
	std::string status;
	std::int64_t created_time{0};
	std::int64_t expires_at{0};
};

// ===== Phase 6: Administrative Models =====

/// API key
struct ApiKey {
	std::string id;
	std::string name;
	std::vector<std::string> scopes;
	std::int64_t created_time{0};
	std::optional<std::int64_t> expires_at;
};

/// Milestone
struct Milestone {
	std::string id;
	std::string event_ticker;
	std::string title;
	std::string description;
	std::int64_t deadline{0};
	std::string status;
};

/// Multivariate collection
struct MultivariateCollection {
	std::string id;
	std::string title;
	std::string description;
	std::vector<std::string> event_tickers;
};

/// Structured target
struct StructuredTarget {
	std::string id;
	std::string title;
	std::string description;
	std::string target_type;
};

/// Communication
struct Communication {
	std::string id;
	std::string title;
	std::string body;
	std::string type;
	std::int64_t created_time{0};
};

// ===== Phase 7: Search/Live Data Models =====

/// Live market data
struct LiveData {
	std::string ticker;
	std::int32_t yes_bid{0};
	std::int32_t yes_ask{0};
	std::int32_t no_bid{0};
	std::int32_t no_ask{0};
	std::int32_t last_price{0};
	std::int64_t volume{0};
};

/// Incentive program
struct IncentiveProgram {
	std::string id;
	std::string title;
	std::string description;
	std::int64_t start_time{0};
	std::int64_t end_time{0};
};

// ===== Additional Models for Full SDK Parity =====

/// Total resting order value response
struct TotalRestingOrderValue {
	std::int64_t total_value{0}; // in cents
};

/// User data timestamp response
struct UserDataTimestamp {
	std::int64_t timestamp{0};
};

/// Parameters for generating an API key
struct GenerateApiKeyParams {
	std::string name;
	std::vector<std::string> scopes;
	std::optional<std::int64_t> expires_at;
};

/// Parameters for looking up a multivariate collection bundle
struct LookupBundleParams {
	std::vector<std::string> market_tickers;
};

/// Multivariate bundle lookup response
struct LookupBundleResponse {
	std::string collection_ticker;
	std::int32_t bundle_price{0};
	std::vector<std::string> market_tickers;
};

// Request parameter structures

/// Parameters for listing markets
struct GetMarketsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> event_ticker;
	std::optional<std::string> series_ticker;
	std::optional<std::string> status;	// "open", "closed", "settled"
	std::optional<std::string> tickers; // comma-separated
};

/// Parameters for listing events
struct GetEventsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> series_ticker;
	std::optional<std::string> status;
};

/// Parameters for listing series
struct GetSeriesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> category;
};

/// Parameters for listing order groups
struct GetOrderGroupsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> status;
};

/// Parameters for creating an order group
struct CreateOrderGroupParams {
	std::vector<std::string> order_ids;
	std::string type; // "oco", "otoco", etc.
};

/// Parameters for listing RFQs
struct GetRfqsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::string> status;
};

/// Parameters for creating an RFQ
struct CreateRfqParams {
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	std::int32_t count{0};
	std::optional<std::int64_t> expires_at;
};

/// Parameters for listing quotes
struct GetQuotesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> rfq_id;
	std::optional<std::string> status;
};

/// Parameters for creating a quote
struct CreateQuoteParams {
	std::string rfq_id;
	std::int32_t price{0};
	std::int32_t count{0};
	std::optional<std::int64_t> expires_at;
};

/// Parameters for creating an API key
struct CreateApiKeyParams {
	std::string name;
	std::vector<std::string> scopes;
	std::optional<std::int64_t> expires_at;
};

/// Parameters for listing milestones
struct GetMilestonesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> event_ticker;
};

/// Parameters for listing multivariate collections
struct GetMultivariateCollectionsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
};

/// Parameters for listing structured targets
struct GetStructuredTargetsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
};

/// Parameters for searching events/markets
struct SearchParams {
	std::string query;
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
};

/// Parameters for listing orders
struct GetOrdersParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::string> status; // "open", "pending", etc.
};

/// Parameters for listing fills
struct GetFillsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::string> order_id;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
};

/// Parameters for listing positions
struct GetPositionsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> event_ticker;
	std::optional<std::string> market_ticker;
	std::optional<std::string> settlement_status;
};

/// Parameters for listing trades
struct GetTradesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
};

/// Parameters for market candlesticks
struct GetCandlesticksParams {
	std::string ticker;
	std::string period; // "1m", "5m", "1h", "1d"
	std::optional<std::int64_t> start_ts;
	std::optional<std::int64_t> end_ts;
};

/// Parameters for creating an order
struct CreateOrderParams {
	std::string ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	std::string type{"limit"}; // "limit" or "market"
	std::int32_t count{0};
	std::optional<std::int32_t> yes_price;
	std::optional<std::int32_t> no_price;
	std::optional<std::string> client_order_id;
	std::optional<std::int64_t> expiration_ts;
	std::optional<std::int32_t> sell_position_floor;
	std::optional<std::int32_t> buy_max_cost;
};

/// Parameters for amending an order
struct AmendOrderParams {
	std::string order_id;
	std::optional<std::int32_t> count;
	std::optional<std::int32_t> yes_price;
	std::optional<std::int32_t> no_price;
};

/// Parameters for decreasing an order
struct DecreaseOrderParams {
	std::string order_id;
	std::int32_t reduce_by{0};
};

/// Batch order request
struct BatchOrderRequest {
	std::vector<CreateOrderParams> orders;
};

/// Batch cancel request
struct BatchCancelRequest {
	std::vector<std::string> order_ids;
};

// Response structures

/// Response for creating an order
struct CreateOrderResponse {
	Order order;
};

/// Response for batch operations
template <typename T>
struct BatchResponse {
	std::vector<T> results;
	std::vector<std::string> errors;
};

/// Complete Kalshi REST API client
///
/// Provides typed methods for all Kalshi v2 API endpoints.
/// Uses the HttpClient for actual HTTP communication.
class KalshiClient {
public:
	/// Create a client with the given HTTP client
	explicit KalshiClient(HttpClient client);
	~KalshiClient();

	KalshiClient(KalshiClient&&) noexcept;
	KalshiClient& operator=(KalshiClient&&) noexcept;

	// Non-copyable
	KalshiClient(const KalshiClient&) = delete;
	KalshiClient& operator=(const KalshiClient&) = delete;

	// ===== Exchange API =====

	/// Get exchange status
	[[nodiscard]] Result<ExchangeStatus> get_exchange_status();

	/// Get exchange schedule
	[[nodiscard]] Result<Schedule> get_exchange_schedule();

	/// Get exchange announcements
	[[nodiscard]] Result<std::vector<Announcement>> get_exchange_announcements();

	/// Get user data timestamp
	[[nodiscard]] Result<UserDataTimestamp> get_user_data_timestamp();

	// ===== Markets API =====

	/// Get a single market by ticker
	[[nodiscard]] Result<Market> get_market(const std::string& ticker);

	/// List markets with optional filters
	[[nodiscard]] Result<PaginatedResponse<Market>>
	get_markets(const GetMarketsParams& params = {});

	/// Get market orderbook
	[[nodiscard]] Result<OrderBook>
	get_market_orderbook(const std::string& ticker,
						 std::optional<std::int32_t> depth = std::nullopt);

	/// Get market candlesticks (price history)
	[[nodiscard]] Result<std::vector<Candlestick>>
	get_market_candlesticks(const GetCandlesticksParams& params);

	/// Get public trades for a market
	[[nodiscard]] Result<PaginatedResponse<PublicTrade>>
	get_trades(const GetTradesParams& params = {});

	// ===== Events API =====

	/// Get a single event by ticker
	[[nodiscard]] Result<Event> get_event(const std::string& event_ticker);

	/// List events with optional filters
	[[nodiscard]] Result<PaginatedResponse<Event>> get_events(const GetEventsParams& params = {});

	/// Get event metadata
	[[nodiscard]] Result<EventMetadata> get_event_metadata(const std::string& event_ticker);

	// ===== Series API =====

	/// Get a single series by ticker
	[[nodiscard]] Result<Series> get_series(const std::string& series_ticker);

	/// List all series
	[[nodiscard]] Result<PaginatedResponse<Series>>
	get_series_list(const GetSeriesParams& params = {});

	// ===== Portfolio API (Authenticated) =====

	/// Get account balance
	[[nodiscard]] Result<Balance> get_balance();

	/// Get user positions
	[[nodiscard]] Result<PaginatedResponse<Position>>
	get_positions(const GetPositionsParams& params = {});

	/// Get user orders
	[[nodiscard]] Result<PaginatedResponse<Order>> get_orders(const GetOrdersParams& params = {});

	/// Get a single order by ID
	[[nodiscard]] Result<Order> get_order(const std::string& order_id);

	/// Get user fills (trade executions)
	[[nodiscard]] Result<PaginatedResponse<Fill>> get_fills(const GetFillsParams& params = {});

	/// Get user settlements
	[[nodiscard]] Result<PaginatedResponse<Settlement>>
	get_settlements(const GetPositionsParams& params = {});

	/// Get total resting order value
	[[nodiscard]] Result<TotalRestingOrderValue> get_total_resting_order_value();

	// ===== Order Management (Authenticated) =====

	/// Create a new order
	[[nodiscard]] Result<Order> create_order(const CreateOrderParams& params);

	/// Cancel an order
	[[nodiscard]] Result<void> cancel_order(const std::string& order_id);

	/// Amend an existing order (change price/count)
	[[nodiscard]] Result<Order> amend_order(const AmendOrderParams& params);

	/// Decrease order count
	[[nodiscard]] Result<Order> decrease_order(const DecreaseOrderParams& params);

	/// Create multiple orders in a batch
	[[nodiscard]] Result<BatchResponse<Order>>
	batch_create_orders(const BatchOrderRequest& request);

	/// Cancel multiple orders in a batch
	[[nodiscard]] Result<BatchResponse<std::string>>
	batch_cancel_orders(const BatchCancelRequest& request);

	// ===== Order Groups (Authenticated) =====

	/// Create an order group
	[[nodiscard]] Result<OrderGroup> create_order_group(const CreateOrderGroupParams& params);

	/// List order groups
	[[nodiscard]] Result<PaginatedResponse<OrderGroup>>
	get_order_groups(const GetOrderGroupsParams& params = {});

	/// Get a single order group by ID
	[[nodiscard]] Result<OrderGroup> get_order_group(const std::string& group_id);

	/// Delete an order group
	[[nodiscard]] Result<void> delete_order_group(const std::string& group_id);

	/// Reset an order group
	[[nodiscard]] Result<OrderGroup> reset_order_group(const std::string& group_id);

	// ===== Order Queue Position (Authenticated) =====

	/// Get queue position for a single order
	[[nodiscard]] Result<OrderQueuePosition>
	get_order_queue_position(const std::string& order_id);

	/// Get queue positions for multiple orders
	[[nodiscard]] Result<std::vector<OrderQueuePosition>>
	get_queue_positions(const std::vector<std::string>& order_ids);

	// ===== RFQ/Quotes (Authenticated) =====

	/// Create a request for quote
	[[nodiscard]] Result<Rfq> create_rfq(const CreateRfqParams& params);

	/// List RFQs
	[[nodiscard]] Result<PaginatedResponse<Rfq>> get_rfqs(const GetRfqsParams& params = {});

	/// Get a single RFQ by ID
	[[nodiscard]] Result<Rfq> get_rfq(const std::string& rfq_id);

	/// Delete an RFQ
	[[nodiscard]] Result<void> delete_rfq(const std::string& rfq_id);

	/// Create a quote for an RFQ
	[[nodiscard]] Result<Quote> create_quote(const CreateQuoteParams& params);

	/// List quotes
	[[nodiscard]] Result<PaginatedResponse<Quote>> get_quotes(const GetQuotesParams& params = {});

	/// Get a single quote by ID
	[[nodiscard]] Result<Quote> get_quote(const std::string& quote_id);

	/// Accept a quote
	[[nodiscard]] Result<void> accept_quote(const std::string& quote_id);

	/// Confirm a quote
	[[nodiscard]] Result<void> confirm_quote(const std::string& quote_id);

	/// Delete a quote
	[[nodiscard]] Result<void> delete_quote(const std::string& quote_id);

	// ===== API Keys Management (Authenticated) =====

	/// List API keys
	[[nodiscard]] Result<std::vector<ApiKey>> get_api_keys();

	/// Create an API key
	[[nodiscard]] Result<ApiKey> create_api_key(const CreateApiKeyParams& params);

	/// Delete an API key
	[[nodiscard]] Result<void> delete_api_key(const std::string& key_id);

	/// Generate an API key with specific scopes
	[[nodiscard]] Result<ApiKey> generate_api_key(const GenerateApiKeyParams& params);

	// ===== Milestones =====

	/// List milestones
	[[nodiscard]] Result<PaginatedResponse<Milestone>>
	get_milestones(const GetMilestonesParams& params = {});

	/// Get a single milestone by ID
	[[nodiscard]] Result<Milestone> get_milestone(const std::string& milestone_id);

	// ===== Multivariate Collections =====

	/// List multivariate collections
	[[nodiscard]] Result<PaginatedResponse<MultivariateCollection>>
	get_multivariate_collections(const GetMultivariateCollectionsParams& params = {});

	/// Get a single multivariate collection by ID
	[[nodiscard]] Result<MultivariateCollection>
	get_multivariate_collection(const std::string& collection_id);

	/// Lookup bundle pricing for a multivariate collection
	[[nodiscard]] Result<LookupBundleResponse>
	lookup_multivariate_bundle(const std::string& collection_ticker,
							   const LookupBundleParams& params);

	// ===== Structured Targets =====

	/// List structured targets
	[[nodiscard]] Result<PaginatedResponse<StructuredTarget>>
	get_structured_targets(const GetStructuredTargetsParams& params = {});

	/// Get a single structured target by ID
	[[nodiscard]] Result<StructuredTarget>
	get_structured_target(const std::string& target_id);

	// ===== Communications =====

	/// Get a communication by ID
	[[nodiscard]] Result<Communication> get_communication(const std::string& comm_id);

	// ===== Search API =====

	/// Search events
	[[nodiscard]] Result<PaginatedResponse<Event>> search_events(const SearchParams& params);

	/// Search markets
	[[nodiscard]] Result<PaginatedResponse<Market>> search_markets(const SearchParams& params);

	// ===== Live Data API =====

	/// Get live data for a single ticker
	[[nodiscard]] Result<LiveData> get_live_data(const std::string& ticker);

	/// Get live data for multiple tickers
	[[nodiscard]] Result<std::vector<LiveData>>
	get_live_datas(const std::vector<std::string>& tickers);

	// ===== Incentive Programs =====

	/// List incentive programs
	[[nodiscard]] Result<std::vector<IncentiveProgram>> get_incentive_programs();

	/// Access the underlying HTTP client
	[[nodiscard]] HttpClient& http_client();
	[[nodiscard]] const HttpClient& http_client() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;

	// JSON parsing helpers
	[[nodiscard]] Result<Market> parse_market(const std::string& json);
	[[nodiscard]] Result<std::vector<Market>> parse_markets(const std::string& json);
	[[nodiscard]] Result<Order> parse_order(const std::string& json);
	[[nodiscard]] Result<std::vector<Order>> parse_orders(const std::string& json);
	[[nodiscard]] Result<OrderBook> parse_orderbook(const std::string& json);

	// Query string builders
	[[nodiscard]] std::string build_markets_query(const GetMarketsParams& params);
	[[nodiscard]] std::string build_events_query(const GetEventsParams& params);
	[[nodiscard]] std::string build_orders_query(const GetOrdersParams& params);
	[[nodiscard]] std::string build_fills_query(const GetFillsParams& params);
	[[nodiscard]] std::string build_positions_query(const GetPositionsParams& params);
	[[nodiscard]] std::string build_trades_query(const GetTradesParams& params);
	[[nodiscard]] std::string build_series_query(const GetSeriesParams& params);
	[[nodiscard]] std::string build_order_groups_query(const GetOrderGroupsParams& params);
	[[nodiscard]] std::string build_rfqs_query(const GetRfqsParams& params);
	[[nodiscard]] std::string build_quotes_query(const GetQuotesParams& params);
	[[nodiscard]] std::string build_milestones_query(const GetMilestonesParams& params);
	[[nodiscard]] std::string build_multivariate_query(const GetMultivariateCollectionsParams& params);
	[[nodiscard]] std::string build_structured_targets_query(const GetStructuredTargetsParams& params);
	[[nodiscard]] std::string build_search_query(const SearchParams& params);

	// JSON serialization helpers
	[[nodiscard]] std::string serialize_create_order(const CreateOrderParams& params);
	[[nodiscard]] std::string serialize_amend_order(const AmendOrderParams& params);
	[[nodiscard]] std::string serialize_decrease_order(const DecreaseOrderParams& params);
	[[nodiscard]] std::string serialize_batch_create(const BatchOrderRequest& request);
	[[nodiscard]] std::string serialize_batch_cancel(const BatchCancelRequest& request);
	[[nodiscard]] std::string serialize_order_group(const CreateOrderGroupParams& params);
	[[nodiscard]] std::string serialize_rfq(const CreateRfqParams& params);
	[[nodiscard]] std::string serialize_quote(const CreateQuoteParams& params);
	[[nodiscard]] std::string serialize_api_key(const CreateApiKeyParams& params);
	[[nodiscard]] std::string serialize_order_ids(const std::vector<std::string>& order_ids);
	[[nodiscard]] std::string serialize_tickers(const std::vector<std::string>& tickers);
};

// Helper functions for enum conversion

/// Convert Side to JSON string
[[nodiscard]] constexpr std::string_view to_json_string(Side side) noexcept {
	return side == Side::Yes ? "yes" : "no";
}

/// Convert Action to JSON string
[[nodiscard]] constexpr std::string_view to_json_string(Action action) noexcept {
	return action == Action::Buy ? "buy" : "sell";
}

/// Parse Side from JSON string
[[nodiscard]] inline Side parse_side(std::string_view s) {
	return (s == "yes" || s == "Yes") ? Side::Yes : Side::No;
}

/// Parse Action from JSON string
[[nodiscard]] inline Action parse_action(std::string_view s) {
	return (s == "buy" || s == "Buy") ? Action::Buy : Action::Sell;
}

/// Parse MarketStatus from JSON string
[[nodiscard]] inline MarketStatus parse_market_status(std::string_view s) {
	if (s == "active" || s == "open" || s == "initialized")
		return MarketStatus::Open;
	if (s == "settled" || s == "determined")
		return MarketStatus::Settled;
	return MarketStatus::Closed;
}

/// Parse OrderStatus from JSON string
[[nodiscard]] inline OrderStatus parse_order_status(std::string_view s) {
	if (s == "open" || s == "resting")
		return OrderStatus::Open;
	if (s == "pending")
		return OrderStatus::Pending;
	if (s == "filled" || s == "executed")
		return OrderStatus::Filled;
	if (s == "cancelled" || s == "canceled")
		return OrderStatus::Cancelled;
	if (s == "partial")
		return OrderStatus::PartiallyFilled;
	return OrderStatus::Pending;
}

} // namespace kalshi
