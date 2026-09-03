#pragma once

/// @file api.hpp
/// @brief Typed client for supported Kalshi Predictions REST operations

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
struct SettlementSource {
	std::string name;
	std::string url;
};

struct Event {
	std::string event_ticker;
	std::string series_ticker;
	std::string title;
	std::string category;
	std::string sub_title;
	std::int64_t mutually_exclusive{0};
	std::vector<std::string> market_tickers;
	std::string collateral_return_type;
	/// Removed upstream. Retained as a false compatibility field because a
	/// deprecated attribute would also warn from Event's implicit copy/move.
	bool available_on_brokers{false};
	std::vector<SettlementSource> settlement_source_details;
	std::string last_updated_ts;
	std::string fee_type_override;
	std::int32_t exchange_index{0};
	std::vector<Market> markets;
};

/// Series containing multiple events
struct Series {
	std::string ticker;
	std::string title;
	std::string category;
	std::string frequency;
	std::vector<std::string> tags;
	std::vector<std::string> settlement_sources;
	std::vector<SettlementSource> settlement_source_details;
	std::string contract_url;
	std::string contract_terms_url;
	std::string fee_type;
	double fee_multiplier{0.0};
	std::vector<std::string> additional_prohibitions;
	std::string last_updated_ts;
	std::string volume_fp;
	std::int32_t exchange_index{0};
	/// Exact JSON object returned when product metadata is requested; empty for null or omitted.
	std::string product_metadata_json;
};

/// Exchange status
struct ExchangeStatus {
	bool trading_active{false};
	bool exchange_active{false};
};

/// Token-bucket budget for one Kalshi API rate-limit bucket.
struct AccountRateLimitBucket {
	std::int64_t refill_rate{0};
	std::int64_t bucket_capacity{0};
};

/// Authenticated account API usage tier and read/write token budgets.
struct AccountApiLimits {
	std::string usage_tier;
	AccountRateLimitBucket read;
	AccountRateLimitBucket write;
};

/// Non-default token cost for one API endpoint.
struct EndpointCost {
	std::string method;
	std::string path;
	std::int64_t cost{0};
};

/// Account-level endpoint cost metadata.
struct EndpointCosts {
	std::int64_t default_cost{0};
	std::vector<EndpointCost> endpoint_costs;
};

struct IndexedBalance {
	std::int32_t exchange_index{0};
	std::string balance_dollars;
};

struct GetBalanceParams {
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
};

/// Account balance
struct Balance {
	std::int64_t balance{0};		   // cents
	std::int64_t available_balance{0}; // cents
	std::string balance_dollars;
	std::int64_t portfolio_value{0};
	std::int64_t updated_ts{0};
	std::vector<IndexedBalance> balance_breakdown;
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
	std::int32_t exchange_index{0};
	std::string fill_id;
	std::string count_fp;
	std::string yes_price_dollars;
	std::string no_price_dollars;
	std::string fee_cost;
	OutcomeSide outcome_side{OutcomeSide::Yes};
	BookSide book_side{BookSide::Bid};
	std::string created_time_iso;
	std::optional<std::int64_t> subaccount_number;
	std::int64_t timestamp{0};
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
	std::int32_t exchange_index{0};
	std::string event_ticker;
	std::string yes_count_fp;
	std::string no_count_fp;
	std::string yes_total_cost_dollars;
	std::string no_total_cost_dollars;
	std::string fee_cost;
	std::string settled_time_iso;
	std::optional<std::int32_t> value;
};

/// One row in ``GET /portfolio/deposits`` (Kalshi V2, shipped 2026-05-05).
/// `finalized_ts` is nullopt when the deposit is still pending.
struct Deposit {
	std::int64_t amount_cents{0};
	std::int64_t fee_cents{0};
	std::int64_t created_ts{0};
	std::optional<std::int64_t> finalized_ts;
	std::string id;
	std::string status; // pending | applied | failed | returned
	std::string type;	// ach | wire | crypto | debit | apm
};

/// One row in ``GET /portfolio/withdrawals`` (Kalshi V2, shipped 2026-05-05).
/// Schema mirrors `Deposit`; kept as a distinct type for call-site clarity.
struct Withdrawal {
	std::int64_t amount_cents{0};
	std::int64_t fee_cents{0};
	std::int64_t created_ts{0};
	std::optional<std::int64_t> finalized_ts;
	std::string id;
	std::string status; // pending | applied | failed | returned
	std::string type;	// ach | wire | crypto | debit | apm
};

/// Query params for ``GET /portfolio/deposits`` and
/// ``GET /portfolio/withdrawals``. Both endpoints accept the same shape;
/// limit is clamped server-side to [1, 500] with default 100.
struct GetPortfolioMovementParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
};

/// Candlestick data for market history
struct Candlestick {
	std::int64_t timestamp{0};
	std::int32_t open_price{0};
	std::int32_t close_price{0};
	std::int32_t high_price{0};
	std::int32_t low_price{0};
	std::int32_t volume{0};
	std::string open_price_dollars;
	std::string close_price_dollars;
	std::string high_price_dollars;
	std::string low_price_dollars;
	std::string volume_fp;
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
	/// True when the trade was executed as a negotiated block trade.
	bool is_block_trade{false};
	std::string count_fp;
	std::string yes_price_dollars;
	std::string no_price_dollars;
	OutcomeSide taker_outcome_side{OutcomeSide::Yes};
	BookSide taker_book_side{BookSide::Bid};
	std::string created_time_iso;
};

// ===== Phase 1: Exchange API Models =====

/// Weekly schedule entry for exchange hours
struct DailySchedule {
	std::string open_time;
	std::string close_time;
};

struct WeeklySchedule {
	std::string day;
	std::string open;
	std::string close;
	std::string start_time;
	std::string end_time;
	std::vector<DailySchedule> monday;
	std::vector<DailySchedule> tuesday;
	std::vector<DailySchedule> wednesday;
	std::vector<DailySchedule> thursday;
	std::vector<DailySchedule> friday;
	std::vector<DailySchedule> saturday;
	std::vector<DailySchedule> sunday;
};

/// Maintenance window
struct MaintenanceWindow {
	std::int64_t start{0};
	std::int64_t end{0};
	std::string description;
	std::string start_datetime;
	std::string end_datetime;
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
	struct MarketDetail {
		std::string market_ticker;
		std::string image_url;
		std::string color_code;
	};
	std::string image_url;
	std::string featured_image_url;
	std::vector<MarketDetail> market_details;
	std::vector<SettlementSource> settlement_sources;
	std::string competition;
	std::string competition_scope;
};

// ===== Phase 3: Order Groups Models =====

/// Order group
struct OrderGroup {
	std::string id;
	std::vector<std::string> order_ids;
	std::string status;
	std::string type;
	std::int64_t created_time{0};
	/// Subaccount that owns the group. Added to the v2 response surface on
	/// 2026-05-07; empty when the server omits the field (back-compat).
	std::string subaccount_number;
	std::string contracts_limit_fp;
	bool is_auto_cancel_enabled{false};
	std::int32_t exchange_index{0};
	std::int64_t subaccount{0};
};

struct OrderGroupSelector {
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
};

// ===== Phase 4: Order Queue Position Models =====

/// Order queue position
struct OrderQueuePosition {
	std::string order_id;
	std::int32_t position{0};
	std::int32_t total_at_price{0};
	std::string market_ticker;
	std::string queue_position_fp;
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
	std::string creator_id;
	std::string contracts_fp;
	std::string target_cost_dollars;
	bool rest_remainder{false};
	std::string cancellation_reason;
	std::string creator_user_id;
	std::int64_t creator_subaccount{0};
	std::string created_ts;
	std::string cancelled_ts;
	std::string updated_ts;
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
	std::string creator_id;
	std::string rfq_creator_id;
	std::string market_ticker;
	std::string contracts_fp;
	std::string yes_bid_dollars;
	std::string no_bid_dollars;
	std::string created_ts;
	std::string updated_ts;
	std::string accepted_side;
	std::string accepted_ts;
	std::string confirmed_ts;
	std::string executed_ts;
	std::string cancelled_ts;
	bool rest_remainder{false};
	bool post_only{false};
	std::string cancellation_reason;
	std::int64_t creator_subaccount{0};
	std::int64_t rfq_creator_subaccount{0};
	std::string yes_contracts_fp;
	std::string no_contracts_fp;
};

// ===== Phase 6: Administrative Models =====

/// API key
struct ApiKey {
	std::string id;
	std::string name;
	std::vector<std::string> scopes;
	std::int64_t created_time{0};
	std::optional<std::int64_t> expires_at;
	std::optional<std::int64_t> subaccount;
	std::string fcm_subtrader_id;
	std::string private_key;
	std::string warning;
};

/// API keys plus the account's API-key location-attestation expiry.
struct ApiKeysResponse {
	std::vector<ApiKey> api_keys;
	std::optional<std::int64_t> api_key_region_expiration_ts;
};

/// Milestone
struct Milestone {
	std::string id;
	std::string event_ticker;
	std::string title;
	std::string description;
	std::int64_t deadline{0};
	std::string status;
	std::string category;
	std::string type;
	std::string start_date;
	std::string end_date;
	std::string notification_message;
	std::string source_id;
	std::string last_updated_ts;
	std::vector<std::string> related_event_tickers;
	std::vector<std::string> primary_event_tickers;
	/// Exact flexible JSON objects from the published milestone contract.
	std::string source_ids_json;
	std::string details_json;
};

/// Expanded event-list response, including milestones when requested.
struct EventsResponse {
	std::vector<Event> events;
	std::vector<Milestone> milestones;
	std::optional<Cursor> next_cursor;
};

/// Multivariate collection
struct MultivariateCollection {
	std::string id;
	std::string title;
	std::string description;
	std::vector<std::string> event_tickers;
	std::string collection_ticker;
	std::string series_ticker;
	std::int32_t exchange_index{0};
	std::string open_date;
	std::string close_date;
	bool is_ordered{false};
	bool is_single_market_per_event{false};
	bool is_all_yes{false};
	std::int32_t size_min{0};
	std::int32_t size_max{0};
	std::string functional_description;
};

/// Structured target
struct StructuredTarget {
	std::string id;
	std::string title;
	std::string description;
	std::string target_type;
	std::string name;
	std::string type;
	std::string source_id;
	std::string last_updated_ts;
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
	std::string market_id;
	std::string market_ticker;
	std::string incentive_type;
	std::string incentive_description;
	std::string start_date;
	std::string end_date;
	std::int64_t period_reward{0};
	bool paid_out{false};
	std::optional<std::int32_t> discount_factor_bps;
	std::string target_size_fp;
	std::optional<std::int64_t> max_reward_per_account;
};

// ===== Additional Models for Full SDK Parity =====

/// Subaccount on the primary account holder. Created via
/// ``create_subaccount`` and identified for the rest of the API by
/// the integer ``subaccount_number``.
struct Subaccount {
	std::int64_t subaccount_number{0};
	std::int64_t balance{0}; // cents
	std::string balance_dollars;
	std::int32_t exchange_index{0};
	std::int64_t updated_ts{0};
};

/// Cross-subaccount transfer record (also the request body shape).
struct SubaccountTransfer {
	std::int64_t from_subaccount{0};
	std::int64_t to_subaccount{0};
	std::int64_t amount{0}; // cents
	std::string client_transfer_id;
	std::string transfer_id;
	std::int64_t amount_cents{0};
	std::int64_t created_ts{0};
	std::int32_t exchange_index{0};
};

/// Response for ``GET /portfolio/subaccounts/balances``.
struct SubaccountBalances {
	std::vector<Subaccount> balances;
};

/// Response for ``GET /portfolio/subaccounts/transfers``.
struct SubaccountTransfers {
	std::vector<SubaccountTransfer> transfers;
	std::string cursor; // empty when no further pages
};

/// One row in the netting-settings list.
struct SubaccountNetting {
	std::int64_t subaccount{0};
	bool netting_enabled{false};
	std::int32_t exchange_index{0};
};

/// Response for ``GET /portfolio/subaccounts/netting``.
struct SubaccountNettingList {
	std::vector<SubaccountNetting> netting_settings;
};

/// Query params for ``GET /portfolio/subaccounts/transfers``.
struct GetSubaccountTransfersParams {
	std::optional<std::int32_t> limit; // 1..200, default 100
	std::optional<std::string> cursor;
};

/// Total resting order value response
struct TotalRestingOrderValue {
	std::int64_t total_value{0}; // in cents
	std::vector<IndexedBalance> resting_order_value_breakdown;
};

/// User data timestamp response
struct UserDataTimestamp {
	std::int64_t timestamp{0};
	std::string as_of_time;
};

/// Parameters for generating an API key
struct GenerateApiKeyParams {
	std::string name;
	std::vector<std::string> scopes;
	std::optional<std::int64_t> expires_at; // legacy; rejected by v0.5
	std::optional<std::int64_t> subaccount;
	std::optional<std::string> fcm_subtrader_id;
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
	std::optional<std::int64_t> min_created_ts;
	std::optional<std::int64_t> max_created_ts;
	std::optional<std::int64_t> min_updated_ts;
	std::optional<std::int64_t> max_close_ts;
	std::optional<std::int64_t> min_close_ts;
	std::optional<std::int64_t> min_settled_ts;
	std::optional<std::int64_t> max_settled_ts;
	std::optional<std::string> mve_filter;
};

/// Parameters for listing events
struct GetEventsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> series_ticker;
	std::optional<std::string> status;
	std::optional<bool> with_nested_markets;
	std::optional<bool> with_milestones;
	std::optional<std::string> event_tickers;
	std::optional<std::int64_t> min_close_ts;
	std::optional<std::int64_t> min_updated_ts;
};

/// Parameters for listing series
struct GetSeriesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> category;
	std::optional<std::string> tags;
	std::optional<bool> include_product_metadata;
	std::optional<bool> include_volume;
	std::optional<std::int64_t> min_updated_ts;
};

struct GetQueuePositionsParams {
	std::optional<std::string> market_tickers;
	std::optional<std::string> event_ticker;
	std::optional<std::int64_t> subaccount;
};

/// Parameters for listing order groups
struct GetOrderGroupsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> status;
	std::optional<std::int64_t> subaccount;
};

/// Parameters for creating an order group
struct CreateOrderGroupParams {
	std::vector<std::string> order_ids;
	std::string type; // "oco", "otoco", etc.
	std::optional<std::int64_t> subaccount;
	std::optional<std::int64_t> contracts_limit;
	std::optional<std::string> contracts_limit_fp;
	std::optional<std::int32_t> exchange_index;
};

/// Parameters for listing RFQs
struct GetRfqsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::string> status;
	std::optional<std::string> event_ticker;
	std::optional<std::int64_t> subaccount;
	std::optional<std::string> user_filter;
};

/// Parameters for creating an RFQ
struct CreateRfqParams {
	std::string market_ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	std::int32_t count{0};
	std::optional<std::int64_t> expires_at;
	std::optional<std::string> contracts_fp;
	std::optional<std::string> target_cost_dollars;
	bool rest_remainder{false};
	std::optional<bool> replace_existing;
	std::optional<std::string> subtrader_id;
	std::optional<std::int64_t> subaccount;
	/// Required to acknowledge that the current RFQ contract has no direction fields.
	bool discard_legacy_direction{false};
};

/// Parameters for listing quotes
struct GetQuotesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> rfq_id;
	std::optional<std::string> status;
	std::optional<std::string> rfq_user_filter;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
	std::optional<std::string> user_filter;
	std::optional<std::string> rfq_creator_subtrader_id;
	/// Filter for quotes that responded to RFQs created by the authenticated
	/// user. Added to GET /communications/quotes on 2026-05-07. When set,
	/// only quotes whose parent RFQ was created by the calling user are
	/// returned. Values follow the Kalshi convention (typically `"true"` to
	/// enable; absent ≡ all quotes the user can see).
};

/// Parameters for creating a quote
struct CreateQuoteParams {
	std::string rfq_id;
	std::int32_t price{0};
	std::int32_t count{0};
	std::optional<std::int64_t> expires_at;
	/// When true, the quote will never take resting orders or pay taker fees;
	/// it is auto-cancelled at execution if it would have matched. Added to
	/// the Kalshi v2 API on 2026-05-05.
	std::optional<bool> post_only;
	std::string yes_bid_dollars;
	std::string no_bid_dollars;
	bool rest_remainder{false};
	std::optional<std::int64_t> subaccount;
};

/// Parameters for creating an API key
struct CreateApiKeyParams {
	std::string name;
	std::vector<std::string> scopes;
	std::optional<std::int64_t> expires_at; // legacy; rejected by v0.5
	std::string public_key;
	std::optional<std::int64_t> subaccount;
	std::optional<std::string> fcm_subtrader_id;
};

/// Parameters for listing milestones
struct GetMilestonesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> event_ticker; // legacy alias for related_event_ticker
	std::optional<std::string> minimum_start_date;
	std::optional<std::string> category;
	std::optional<std::string> competition;
	std::optional<std::string> source_id;
	std::optional<std::string> type;
	std::optional<std::string> related_event_ticker;
	std::optional<std::int64_t> min_updated_ts;
};

/// Parameters for listing multivariate collections
struct GetMultivariateCollectionsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> status;
	std::optional<std::string> associated_event_ticker;
	std::optional<std::string> series_ticker;
};

/// Parameters for listing structured targets
struct GetStructuredTargetsParams {
	std::optional<std::int32_t> limit; // legacy alias for page_size
	std::optional<std::string> cursor;
	std::optional<std::int32_t> page_size;
	std::vector<std::string> ids;
	std::optional<std::string> type;
	std::optional<std::string> competition;
};

/// Parameters for listing incentive programs.
struct GetIncentiveProgramsParams {
	std::optional<std::string> status;
	std::optional<std::string> type;
	std::optional<std::string> incentive_description;
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
	std::optional<std::string> event_ticker;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
};

/// Parameters for listing fills
struct GetFillsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::string> order_id;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
};

/// Parameters for listing positions
struct GetPositionsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> event_ticker;
	std::optional<std::string> market_ticker;
	std::optional<std::string> settlement_status;
	std::optional<std::string> count_filter;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
};

struct GetSettlementsParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::string> event_ticker;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
	std::optional<std::int64_t> subaccount;
};

/// Parameters for listing trades
struct GetTradesParams {
	std::optional<std::int32_t> limit;
	std::optional<std::string> cursor;
	std::optional<std::string> market_ticker;
	std::optional<std::int64_t> min_ts;
	std::optional<std::int64_t> max_ts;
	std::optional<bool> is_block_trade;
};

/// Parameters for market candlesticks
/// Requires a series ticker, market ticker, and time range.
struct GetCandlesticksParams {
	std::string event_ticker;			  ///< Legacy alias for series_ticker.
	std::string ticker;					  ///< Market ticker (e.g., "KXHIGHLAX-26JAN18-T50")
	std::int32_t period_interval{1};	  ///< Period in MINUTES: 1 (1m), 60 (1h), 1440 (1d)
	std::optional<std::int64_t> start_ts; ///< Start timestamp (unix seconds)
	std::optional<std::int64_t> end_ts;	  ///< End timestamp (unix seconds)
	std::optional<bool> include_latest_before_start;
	std::string series_ticker; ///< Series ticker used by the current endpoint.
};

/// Parameters for creating an order
struct CreateOrderParams {
	std::string ticker;
	Side side{Side::Yes};
	Action action{Action::Buy};
	std::string type{"limit"}; // "limit" or "market"
	std::int32_t count{0};
	std::optional<std::string> count_fp;
	std::optional<std::int32_t> yes_price;
	std::optional<std::int32_t> no_price;
	std::optional<std::string> yes_price_dollars;
	std::optional<std::string> no_price_dollars;
	std::optional<std::string> client_order_id;
	std::optional<std::int64_t> expiration_ts;
	std::optional<std::string> time_in_force;
	std::optional<std::int32_t> sell_position_floor;
	std::optional<std::int32_t> buy_max_cost;
	std::optional<bool> post_only;
	std::optional<bool> reduce_only;
	std::optional<std::string> self_trade_prevention_type;
	std::optional<std::string> order_group_id;
	std::optional<bool> cancel_order_on_pause;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
	/// Canonical V2 single-book side. Required by create_order().
	std::optional<BookSide> book_side;
	/// Canonical V2 fixed-point price. Required by create_order().
	std::optional<std::string> price_dollars;
	std::optional<std::int64_t> expiration_time;
};

/// Parameters for amending an order
struct AmendOrderParams {
	std::string order_id;
	std::optional<std::int32_t> count;
	std::optional<std::int32_t> yes_price;
	std::optional<std::int32_t> no_price;
	std::string ticker;
	std::optional<BookSide> book_side;
	std::optional<std::string> price_dollars;
	std::optional<std::string> count_fp;
	std::optional<std::string> client_order_id;
	std::optional<std::string> updated_client_order_id;
	std::optional<std::int32_t> exchange_index;
	std::optional<std::int64_t> subaccount;
};

/// Parameters for decreasing an order
struct DecreaseOrderParams {
	std::string order_id;
	std::int32_t reduce_by{0};
	std::optional<std::string> reduce_by_fp;
	std::optional<std::string> reduce_to_fp;
	std::optional<std::int32_t> exchange_index;
	std::optional<std::string> market_ticker;
	std::optional<std::int64_t> subaccount;
};

/// Batch order request
struct BatchOrderRequest {
	std::vector<CreateOrderParams> orders;
};

/// Order selector for batch cancellation.
struct BatchCancelOrder {
	std::string order_id;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
	std::optional<std::string> market_ticker;
};

/// Batch cancel request.
///
/// `order_ids` is kept for source compatibility. When `orders` is empty,
/// those IDs are emitted as the current `/portfolio/orders/batched`
/// `orders: [{order_id: ...}]` body shape.
struct BatchCancelRequest {
	std::vector<std::string> order_ids;
	std::vector<BatchCancelOrder> orders;
};

/// Parameters for Kalshi's event-market cancel-order V2 endpoint.
struct CancelOrderV2Params {
	std::string order_id;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
	std::optional<std::string> market_ticker;
};

/// Per-order error payload returned by event-market batch cancel V2.
struct OrderCancelError {
	std::string code;
	std::string message;
	std::string details;
	std::string service;
};

/// Cancel result returned by Kalshi's event-market order-cancel V2 endpoints.
struct OrderCancelResult {
	std::string order_id;
	std::string reduced_by;
	std::int64_t ts_ms{0};
	std::string client_order_id;
	std::optional<OrderCancelError> error;
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

/// Typed client for the supported Kalshi Predictions REST API operations.
///
/// The Margin API is a separate product and is intentionally not exposed by
/// this client. See the compatibility table in the README for operation gaps.
class KalshiClient {
public:
	/// Create a client with the given HTTP client
	explicit KalshiClient(HttpClient client);
	/// Create a client with an injected transport.
	explicit KalshiClient(std::shared_ptr<HttpTransport> transport);
	~KalshiClient();

	KalshiClient(KalshiClient&&) noexcept;
	KalshiClient& operator=(KalshiClient&&) noexcept;

	// Non-copyable
	KalshiClient(const KalshiClient&) = delete;
	KalshiClient& operator=(const KalshiClient&) = delete;

	/// Underlying request transport, including injected test/application transports.
	[[nodiscard]] HttpTransport& transport() noexcept;
	[[nodiscard]] const HttpTransport& transport() const noexcept;

	// ===== Exchange API =====

	/// Get exchange status
	[[nodiscard]] Result<ExchangeStatus> get_exchange_status();

	/// Get exchange schedule
	[[nodiscard]] Result<Schedule> get_exchange_schedule();

	/// Get exchange announcements
	[[deprecated(
		"removed from the Predictions API")]] [[nodiscard]] Result<std::vector<Announcement>>
	get_exchange_announcements();

	/// Get user data timestamp
	[[nodiscard]] Result<UserDataTimestamp> get_user_data_timestamp();

	// ===== Account API (Authenticated) =====

	/// Get account API tier and token-bucket limits
	[[nodiscard]] Result<AccountApiLimits> get_account_api_limits();

	/// List API endpoints whose token cost differs from the default
	[[nodiscard]] Result<EndpointCosts> get_endpoint_costs();

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

	/// Get orderbooks for up to 100 markets in one request
	[[nodiscard]] Result<std::vector<OrderBook>>
	get_market_orderbooks(const std::vector<std::string>& tickers);

	/// Get market candlesticks (price history)
	[[nodiscard]] Result<std::vector<Candlestick>>
	get_market_candlesticks(const GetCandlesticksParams& params);

	/// Get public trades for a market
	[[nodiscard]] Result<PaginatedResponse<PublicTrade>>
	get_trades(const GetTradesParams& params = {});

	// ===== Events API =====

	/// Get a single event by ticker
	[[nodiscard]] Result<Event> get_event(const std::string& event_ticker);
	[[nodiscard]] Result<Event> get_event(const std::string& event_ticker,
										  bool with_nested_markets);

	/// List events with optional filters
	[[nodiscard]] Result<PaginatedResponse<Event>> get_events(const GetEventsParams& params = {});
	/// List events and preserve optional top-level milestone expansions.
	[[nodiscard]] Result<EventsResponse> get_events_response(const GetEventsParams& params = {});

	/// Get event metadata
	[[nodiscard]] Result<EventMetadata> get_event_metadata(const std::string& event_ticker);

	// ===== Series API =====

	/// Get a single series by ticker
	[[nodiscard]] Result<Series> get_series(const std::string& series_ticker);
	[[nodiscard]] Result<Series> get_series(const std::string& series_ticker, bool include_volume);

	/// List all series
	[[nodiscard]] Result<PaginatedResponse<Series>>
	get_series_list(const GetSeriesParams& params = {});

	// ===== Portfolio API (Authenticated) =====

	/// Get account balance
	[[nodiscard]] Result<Balance> get_balance(const GetBalanceParams& params = {});

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
	get_settlements(const GetSettlementsParams& params = {});
	[[deprecated("use GetSettlementsParams")]] [[nodiscard]] Result<PaginatedResponse<Settlement>>
	get_settlements(const GetPositionsParams& params);

	/// List deposits (Kalshi V2 ``GET /portfolio/deposits``, shipped
	/// 2026-05-05). Cursor-paginated; ``params.limit`` is clamped
	/// server-side to [1, 500] with default 100.
	[[nodiscard]] Result<PaginatedResponse<Deposit>>
	get_deposits(const GetPortfolioMovementParams& params = {});

	/// List withdrawals (Kalshi V2 ``GET /portfolio/withdrawals``,
	/// shipped 2026-05-05). Cursor-paginated; ``params.limit`` is
	/// clamped server-side to [1, 500] with default 100.
	[[nodiscard]] Result<PaginatedResponse<Withdrawal>>
	get_withdrawals(const GetPortfolioMovementParams& params = {});

	/// Get total resting order value
	[[nodiscard]] Result<TotalRestingOrderValue> get_total_resting_order_value();

	// ===== Subaccounts (Authenticated) =====

	/// Create a new subaccount under the primary account holder.
	/// Pass an exchange shard when the subaccount should be created there.
	[[nodiscard]] Result<Subaccount> create_subaccount();
	[[nodiscard]] Result<Subaccount> create_subaccount(std::int32_t exchange_index);

	/// Transfer ``amount`` cents from one subaccount to another.
	[[nodiscard]] Result<SubaccountTransfer> transfer_subaccount(const SubaccountTransfer& request);

	/// List balances across every subaccount on this account holder.
	[[nodiscard]] Result<SubaccountBalances> get_subaccount_balances();

	/// Paginated list of past cross-subaccount transfers.
	[[nodiscard]] Result<SubaccountTransfers>
	get_subaccount_transfers(const GetSubaccountTransfersParams& params = {});

	/// Toggle position-netting on a single subaccount.
	[[nodiscard]] Result<void> update_subaccount_netting(std::int64_t subaccount,
														 bool netting_enabled);

	/// Read netting settings across every subaccount.
	[[nodiscard]] Result<SubaccountNettingList> get_subaccount_netting();

	// ===== Order Management (Authenticated) =====

	/// Create a new order
	[[nodiscard]] Result<Order> create_order(const CreateOrderParams& params);

	/// Cancel an order
	[[nodiscard]] Result<void> cancel_order(const std::string& order_id);
	/// Cancel all resting event-market orders, optionally for one subaccount.
	[[nodiscard]] Result<void>
	cancel_all_orders(std::optional<std::int64_t> subaccount = std::nullopt);

	/// Cancel an event-market order using Kalshi's V2 response shape.
	[[nodiscard]] Result<OrderCancelResult> cancel_order_v2(const CancelOrderV2Params& params);

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

	/// Cancel multiple event-market orders using Kalshi's V2 response shape.
	[[nodiscard]] Result<BatchResponse<OrderCancelResult>>
	batch_cancel_orders_v2(const BatchCancelRequest& request);

	// ===== Order Groups (Authenticated) =====

	/// Create an order group
	[[nodiscard]] Result<OrderGroup> create_order_group(const CreateOrderGroupParams& params);

	/// List order groups
	[[nodiscard]] Result<PaginatedResponse<OrderGroup>>
	get_order_groups(const GetOrderGroupsParams& params = {});

	/// Get a single order group by ID
	[[deprecated("use the selector overload")]] [[nodiscard]] Result<OrderGroup>
	get_order_group(const std::string& group_id);
	[[nodiscard]] Result<OrderGroup> get_order_group(const std::string& group_id,
													 const OrderGroupSelector& selector);

	/// Delete an order group
	[[deprecated("use the selector overload")]] [[nodiscard]] Result<void>
	delete_order_group(const std::string& group_id);
	[[nodiscard]] Result<void> delete_order_group(const std::string& group_id,
												  const OrderGroupSelector& selector);

	/// Reset an order group
	[[deprecated("use the selector overload")]] [[nodiscard]] Result<OrderGroup>
	reset_order_group(const std::string& group_id);
	[[nodiscard]] Result<OrderGroup> reset_order_group(const std::string& group_id,
													   const OrderGroupSelector& selector);

	// ===== Order Queue Position (Authenticated) =====

	/// Get queue position for a single order
	[[nodiscard]] Result<OrderQueuePosition> get_order_queue_position(const std::string& order_id);

	/// Get queue positions for multiple orders
	[[nodiscard]] Result<std::vector<OrderQueuePosition>>
	get_queue_positions(const std::vector<std::string>& order_ids);
	[[nodiscard]] Result<std::vector<OrderQueuePosition>>
	get_queue_positions(const GetQueuePositionsParams& params);

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
	[[nodiscard]] Result<Quote> get_quote(const std::string& rfq_id, const std::string& quote_id);

	/// Accept a quote
	[[deprecated("accepted_side is required")]] [[nodiscard]] Result<void>
	accept_quote(const std::string& quote_id);
	[[nodiscard]] Result<void> accept_quote(const std::string& quote_id, Side accepted_side);
	[[nodiscard]] Result<void> accept_quote(const std::string& rfq_id, const std::string& quote_id,
											Side accepted_side);

	/// Confirm a quote
	[[nodiscard]] Result<void> confirm_quote(const std::string& quote_id);
	[[nodiscard]] Result<void> confirm_quote(const std::string& rfq_id,
											 const std::string& quote_id);

	/// Delete a quote
	[[nodiscard]] Result<void> delete_quote(const std::string& quote_id);
	[[nodiscard]] Result<void> delete_quote(const std::string& rfq_id, const std::string& quote_id);

	// ===== API Keys Management (Authenticated) =====

	/// List API keys
	[[nodiscard]] Result<ApiKeysResponse> get_api_keys_response();

	/// List API keys without the account-level location-attestation metadata.
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
	[[deprecated("removed from the Predictions API")]] [[nodiscard]] Result<LookupBundleResponse>
	lookup_multivariate_bundle(const std::string& collection_ticker,
							   const LookupBundleParams& params);

	// ===== Structured Targets =====

	/// List structured targets
	[[nodiscard]] Result<PaginatedResponse<StructuredTarget>>
	get_structured_targets(const GetStructuredTargetsParams& params = {});

	/// Get a single structured target by ID
	[[nodiscard]] Result<StructuredTarget> get_structured_target(const std::string& target_id);

	// ===== Communications =====

	/// Get a communication by ID
	[[deprecated("removed; use the RFQ, quote, and block-trade operations")]] [[nodiscard]] Result<
		Communication>
	get_communication(const std::string& comm_id);

	// ===== Search API =====

	/// Search events
	[[deprecated(
		"removed from the Predictions API")]] [[nodiscard]] Result<PaginatedResponse<Event>>
	search_events(const SearchParams& params);

	/// Search markets
	[[deprecated(
		"removed from the Predictions API")]] [[nodiscard]] Result<PaginatedResponse<Market>>
	search_markets(const SearchParams& params);

	// ===== Live Data API =====

	/// Get live data for a single ticker
	[[deprecated("use the typed live_data event, milestone, or weather "
				 "operations")]] [[nodiscard]] Result<LiveData>
	get_live_data(const std::string& ticker);

	/// Legacy market-ticker batch model. The current GET /live_data/batch
	/// operation accepts milestone_ids and returns sport-specific payloads.
	[[deprecated("use the current milestone-based live_data operations")]] [[nodiscard]] Result<
		std::vector<LiveData>>
	get_live_datas(const std::vector<std::string>& tickers);

	// ===== Incentive Programs =====

	/// List incentive programs
	[[nodiscard]] Result<std::vector<IncentiveProgram>> get_incentive_programs();
	[[nodiscard]] Result<PaginatedResponse<IncentiveProgram>>
	get_incentive_programs(const GetIncentiveProgramsParams& params);

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
	[[nodiscard]] Result<std::vector<OrderBook>> parse_orderbooks(const std::string& json);

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
	[[nodiscard]] std::string
	build_multivariate_query(const GetMultivariateCollectionsParams& params);
	[[nodiscard]] std::string
	build_structured_targets_query(const GetStructuredTargetsParams& params);
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

[[nodiscard]] constexpr std::string_view to_json_string(BookSide side) noexcept {
	return side == BookSide::Bid ? "bid" : "ask";
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
	if (s == "unopened")
		return MarketStatus::Unopened;
	if (s == "paused")
		return MarketStatus::Paused;
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
