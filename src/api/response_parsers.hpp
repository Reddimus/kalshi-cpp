#pragma once

#include "kalshi/api.hpp"

#include <string_view>
#include <vector>

namespace kalshi::api_detail {

/// Parses a single market response. Accepts both ``{"market": {...}}`` and a
/// bare market object.
[[nodiscard]] Market parse_market_response(std::string_view body);

/// Parses the ``markets`` array returned by ``GET /markets``.
[[nodiscard]] std::vector<Market> parse_markets_response(std::string_view body);

[[nodiscard]] std::vector<Candlestick> parse_candlesticks_response(std::string_view body);

/// Parses a single orderbook response. Accepts both the legacy integer-cent
/// ``orderbook.yes`` / ``orderbook.no`` arrays and Kalshi's current
/// fixed-point-dollar ``orderbook_fp.yes_dollars`` / ``no_dollars`` arrays.
[[nodiscard]] OrderBook parse_orderbook_response(std::string_view body);

/// Parses the ``orderbooks`` array returned by ``GET /markets/orderbooks``.
[[nodiscard]] std::vector<OrderBook> parse_orderbooks_response(std::string_view body);

/// Parses the ``deposits`` array from ``GET /portfolio/deposits``. Returns
/// an empty vector when the array is missing or empty. The cursor field
/// is read separately by the client method.
[[nodiscard]] std::vector<Deposit> parse_deposits_response(std::string_view body);

/// Parses the ``withdrawals`` array from ``GET /portfolio/withdrawals``.
/// Symmetric to ``parse_deposits_response`` with a different array key.
[[nodiscard]] std::vector<Withdrawal> parse_withdrawals_response(std::string_view body);

/// Parses the direct response from ``DELETE /portfolio/events/orders/{id}``.
[[nodiscard]] OrderCancelResult parse_order_cancel_result_response(std::string_view body);

/// Parses the ``orders`` array from ``DELETE /portfolio/events/orders/batched``.
[[nodiscard]] std::vector<OrderCancelResult>
parse_batch_order_cancel_result_response(std::string_view body);

/// Parses ``GET /account/limits``.
[[nodiscard]] AccountApiLimits parse_account_api_limits_response(std::string_view body);

/// Parses ``GET /account/endpoint_costs``.
[[nodiscard]] EndpointCosts parse_endpoint_costs_response(std::string_view body);

} // namespace kalshi::api_detail
