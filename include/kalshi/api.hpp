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
    std::int64_t balance{0};          // cents
    std::int64_t available_balance{0}; // cents
};

/// Fill (trade execution for user)
struct Fill {
    std::string trade_id;
    std::string order_id;
    std::string market_ticker;
    Side side{Side::Yes};
    Action action{Action::Buy};
    std::int32_t count{0};
    std::int32_t yes_price{0};
    std::int32_t no_price{0};
    std::int64_t created_time{0};
    bool is_taker{false};
};

/// Settlement record
struct Settlement {
    std::string market_ticker;
    std::string result;
    std::int32_t yes_count{0};
    std::int32_t no_count{0};
    std::int64_t revenue{0};
    std::int64_t settled_time{0};
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

// Request parameter structures

/// Parameters for listing markets
struct GetMarketsParams {
    std::optional<std::int32_t> limit;
    std::optional<std::string> cursor;
    std::optional<std::string> event_ticker;
    std::optional<std::string> series_ticker;
    std::optional<std::string> status;  // "open", "closed", "settled"
    std::optional<std::string> tickers; // comma-separated
};

/// Parameters for listing events
struct GetEventsParams {
    std::optional<std::int32_t> limit;
    std::optional<std::string> cursor;
    std::optional<std::string> series_ticker;
    std::optional<std::string> status;
};

/// Parameters for listing orders
struct GetOrdersParams {
    std::optional<std::int32_t> limit;
    std::optional<std::string> cursor;
    std::optional<std::string> market_ticker;
    std::optional<std::string> status;  // "open", "pending", etc.
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
    std::string period;              // "1m", "5m", "1h", "1d"
    std::optional<std::int64_t> start_ts;
    std::optional<std::int64_t> end_ts;
};

/// Parameters for creating an order
struct CreateOrderParams {
    std::string ticker;
    Side side{Side::Yes};
    Action action{Action::Buy};
    std::string type{"limit"};       // "limit" or "market"
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
template<typename T>
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

    // ===== Markets API =====

    /// Get a single market by ticker
    [[nodiscard]] Result<Market> get_market(const std::string& ticker);

    /// List markets with optional filters
    [[nodiscard]] Result<PaginatedResponse<Market>> get_markets(const GetMarketsParams& params = {});

    /// Get market orderbook
    [[nodiscard]] Result<OrderBook> get_market_orderbook(const std::string& ticker,
                                                         std::optional<std::int32_t> depth = std::nullopt);

    /// Get market candlesticks (price history)
    [[nodiscard]] Result<std::vector<Candlestick>> get_market_candlesticks(const GetCandlesticksParams& params);

    /// Get public trades for a market
    [[nodiscard]] Result<PaginatedResponse<PublicTrade>> get_trades(const GetTradesParams& params = {});

    // ===== Events API =====

    /// Get a single event by ticker
    [[nodiscard]] Result<Event> get_event(const std::string& event_ticker);

    /// List events with optional filters
    [[nodiscard]] Result<PaginatedResponse<Event>> get_events(const GetEventsParams& params = {});

    // ===== Series API =====

    /// Get a single series by ticker
    [[nodiscard]] Result<Series> get_series(const std::string& series_ticker);

    // ===== Portfolio API (Authenticated) =====

    /// Get account balance
    [[nodiscard]] Result<Balance> get_balance();

    /// Get user positions
    [[nodiscard]] Result<PaginatedResponse<Position>> get_positions(const GetPositionsParams& params = {});

    /// Get user orders
    [[nodiscard]] Result<PaginatedResponse<Order>> get_orders(const GetOrdersParams& params = {});

    /// Get a single order by ID
    [[nodiscard]] Result<Order> get_order(const std::string& order_id);

    /// Get user fills (trade executions)
    [[nodiscard]] Result<PaginatedResponse<Fill>> get_fills(const GetFillsParams& params = {});

    /// Get user settlements
    [[nodiscard]] Result<PaginatedResponse<Settlement>> get_settlements(const GetPositionsParams& params = {});

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
    [[nodiscard]] Result<BatchResponse<Order>> batch_create_orders(const BatchOrderRequest& request);

    /// Cancel multiple orders in a batch
    [[nodiscard]] Result<BatchResponse<std::string>> batch_cancel_orders(const BatchCancelRequest& request);

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

    // JSON serialization helpers
    [[nodiscard]] std::string serialize_create_order(const CreateOrderParams& params);
    [[nodiscard]] std::string serialize_amend_order(const AmendOrderParams& params);
    [[nodiscard]] std::string serialize_decrease_order(const DecreaseOrderParams& params);
    [[nodiscard]] std::string serialize_batch_create(const BatchOrderRequest& request);
    [[nodiscard]] std::string serialize_batch_cancel(const BatchCancelRequest& request);
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
    if (s == "active" || s == "open" || s == "initialized") return MarketStatus::Open;
    if (s == "settled" || s == "determined") return MarketStatus::Settled;
    return MarketStatus::Closed;
}

/// Parse OrderStatus from JSON string
[[nodiscard]] inline OrderStatus parse_order_status(std::string_view s) {
    if (s == "open" || s == "resting") return OrderStatus::Open;
    if (s == "pending") return OrderStatus::Pending;
    if (s == "filled" || s == "executed") return OrderStatus::Filled;
    if (s == "cancelled" || s == "canceled") return OrderStatus::Cancelled;
    if (s == "partial") return OrderStatus::PartiallyFilled;
    return OrderStatus::Pending;
}

} // namespace kalshi
