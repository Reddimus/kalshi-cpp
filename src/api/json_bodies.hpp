// Copyright (c) 2026 PredictionMarketsAI
// SPDX-License-Identifier: MIT
#pragma once

/// @file json_bodies.hpp
/// @brief Internal Glaze shim structs for outgoing REST request bodies
///
/// NOT a public API. Lives under `src/` (NOT under `include/`) so it is
/// never installed — downstream consumers (kalshi-trader) link against
/// the static libs and never see these symbols. They exist so that
/// `tests/test_json_serialize.cpp` can pin byte-exact equivalence to
/// the pre-migration `nlohmann::ordered_json::dump()` output without
/// needing friend-access into `KalshiClient`.
///
/// Each struct corresponds to one outgoing body shape that the Kalshi
/// API requires in a stable key order. The `glz::meta` specializations
/// below enumerate fields in the order Kalshi expects (matching the
/// pre-migration `body["x"] = ...` sequence).
///
/// Optional fields use `std::optional<T>` and rely on Glaze's default
/// `skip_null_members = true` — the field is omitted from the JSON
/// when nullopt, matching the pre-migration `if (params.foo) body["foo"] = *params.foo;`
/// pattern.

#include <cstdint>
#include <glaze/glaze.hpp>
#include <optional>
#include <string>
#include <vector>

namespace kalshi::ser {

/// POST /portfolio/orders (and embedded inside batched orders)
struct CreateOrderBody {
	std::string ticker;
	std::string side;
	std::string count;
	std::string price;
	std::string time_in_force;
	std::string self_trade_prevention_type;
	std::optional<std::string> client_order_id;
	std::optional<std::int64_t> expiration_time;
	std::optional<bool> post_only;
	std::optional<bool> reduce_only;
	std::optional<std::string> order_group_id;
	std::optional<bool> cancel_order_on_pause;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
};

/// POST /portfolio/orders/{id}/amend
struct AmendOrderBody {
	std::string ticker;
	std::string side;
	std::string price;
	std::string count;
	std::optional<std::string> client_order_id;
	std::optional<std::string> updated_client_order_id;
	std::optional<std::int32_t> exchange_index;
};

/// POST /portfolio/orders/{id}/decrease
struct DecreaseOrderBody {
	std::optional<std::string> reduce_by;
	std::optional<std::string> reduce_to;
	std::optional<std::int32_t> exchange_index;
	std::optional<std::string> market_ticker;
};

/// POST /portfolio/orders/batched
struct BatchOrdersBody {
	std::vector<CreateOrderBody> orders;
};

/// DELETE /portfolio/orders/batched
struct BatchCancelOrderBody {
	std::string order_id;
	std::optional<std::int64_t> subaccount;
	std::optional<std::int32_t> exchange_index;
	std::optional<std::string> market_ticker;
};

struct BatchCancelBody {
	std::vector<BatchCancelOrderBody> orders;
};

/// POST /portfolio/order-groups
struct OrderGroupBody {
	std::optional<std::int64_t> subaccount;
	std::optional<std::int64_t> contracts_limit;
	std::optional<std::string> contracts_limit_fp;
	std::optional<std::int32_t> exchange_index;
};

/// POST /rfqs
struct RfqBody {
	std::string market_ticker;
	std::optional<std::string> contracts_fp;
	std::optional<std::string> target_cost_dollars;
	bool rest_remainder{false};
	std::optional<bool> replace_existing;
	std::optional<std::string> subtrader_id;
	std::optional<std::int64_t> subaccount;
};

/// POST /quotes
struct QuoteBody {
	std::string rfq_id;
	std::string yes_bid;
	std::string no_bid;
	bool rest_remainder{false};
	std::optional<bool> post_only;
	std::optional<std::int64_t> subaccount;
};

struct AcceptQuoteBody {
	std::string accepted_side;
};

/// POST /api-keys
struct ApiKeyBody {
	std::string name;
	std::string public_key;
	std::optional<std::vector<std::string>> scopes;
	std::optional<std::int64_t> subaccount;
	std::optional<std::string> fcm_subtrader_id;
};

/// POST /api-keys/generate
///
/// `scopes` is `std::optional<vector>` rather than a plain vector
/// because the pre-migration impl OMITTED the key (rather than
/// emitting an empty array) when the caller passed no scopes:
///
///     if (!params.scopes.empty()) { body["scopes"] = params.scopes; }
struct GenerateApiKeyBody {
	std::string name;
	std::optional<std::vector<std::string>> scopes;
	std::optional<std::int64_t> subaccount;
	std::optional<std::string> fcm_subtrader_id;
};

/// POST /portfolio/orders/queue-positions
struct OrderIdsBody {
	std::vector<std::string> order_ids;
};

/// GET /live-data (POST body for batch)
struct TickersBody {
	std::vector<std::string> tickers;
};

/// POST /multivariate-event-collections/{ticker}/lookup
struct MarketTickersBody {
	std::vector<std::string> market_tickers;
};

/// POST /portfolio/subaccounts/transfer
struct SubaccountTransferBody {
	std::string client_transfer_id;
	std::int64_t from_subaccount{0};
	std::int64_t to_subaccount{0};
	std::int64_t amount_cents{0};
	std::int32_t exchange_index{0};
};

/// PUT /portfolio/subaccounts/netting
struct SubaccountNettingBody {
	std::int64_t subaccount_number{0};
	bool enabled{false};
};

// ===== Render helpers =====
//
// Glaze options for all REST body serializers — same `prettify=false` +
// `skip_null_members=true` as the WS command path. The Kalshi API
// rejects unordered payloads on the order-management routes; we rely
// on the explicit field order in each `glz::meta` below.
inline constexpr glz::opts kBodyOpts{.prettify = false};

template <class T>
[[nodiscard]] inline std::string render_body(const T& body) {
	std::string out;
	// `glz::write` returns error_ctx; a statically-typed struct with no
	// inf/NaN possibilities cannot produce a write error in practice.
	// Discard the result to keep the call-site signature as `std::string`.
	(void)glz::write<kBodyOpts>(body, out);
	return out;
}

} // namespace kalshi::ser

// ===== glz::meta specializations =====
//
// MUST be at namespace scope. Field order here defines the JSON key
// order on the wire — DO NOT reorder without verifying against the
// pre-migration `nlohmann::ordered_json::dump()` output via the test
// in `tests/test_json_serialize.cpp`.

template <>
struct glz::meta<kalshi::ser::CreateOrderBody> {
	using T = kalshi::ser::CreateOrderBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("ticker", &T::ticker, "side", &T::side, "count", &T::count, "price", &T::price,
			   "time_in_force", &T::time_in_force, "self_trade_prevention_type",
			   &T::self_trade_prevention_type, "client_order_id", &T::client_order_id,
			   "expiration_time", &T::expiration_time, "post_only", &T::post_only, "reduce_only",
			   &T::reduce_only, "order_group_id", &T::order_group_id, "cancel_order_on_pause",
			   &T::cancel_order_on_pause, "subaccount", &T::subaccount, "exchange_index",
			   &T::exchange_index);
};

template <>
struct glz::meta<kalshi::ser::AmendOrderBody> {
	using T = kalshi::ser::AmendOrderBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("ticker", &T::ticker, "side", &T::side, "price", &T::price, "count", &T::count,
			   "client_order_id", &T::client_order_id, "updated_client_order_id",
			   &T::updated_client_order_id, "exchange_index", &T::exchange_index);
};

template <>
struct glz::meta<kalshi::ser::DecreaseOrderBody> {
	using T = kalshi::ser::DecreaseOrderBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("reduce_by", &T::reduce_by, "reduce_to", &T::reduce_to, "exchange_index",
			   &T::exchange_index, "market_ticker", &T::market_ticker);
};

template <>
struct glz::meta<kalshi::ser::BatchOrdersBody> {
	using T = kalshi::ser::BatchOrdersBody;
	static constexpr auto value = object("orders", &T::orders); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::BatchCancelOrderBody> {
	using T = kalshi::ser::BatchCancelOrderBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("order_id", &T::order_id, "subaccount", &T::subaccount, "exchange_index",
			   &T::exchange_index, "market_ticker", &T::market_ticker);
};

template <>
struct glz::meta<kalshi::ser::BatchCancelBody> {
	using T = kalshi::ser::BatchCancelBody;
	static constexpr auto value = object("orders", &T::orders); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::OrderGroupBody> {
	using T = kalshi::ser::OrderGroupBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("subaccount", &T::subaccount, "contracts_limit", &T::contracts_limit,
			   "contracts_limit_fp", &T::contracts_limit_fp, "exchange_index", &T::exchange_index);
};

template <>
struct glz::meta<kalshi::ser::RfqBody> {
	using T = kalshi::ser::RfqBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("market_ticker", &T::market_ticker, "contracts_fp", &T::contracts_fp,
			   "target_cost_dollars", &T::target_cost_dollars, "rest_remainder", &T::rest_remainder,
			   "replace_existing", &T::replace_existing, "subtrader_id", &T::subtrader_id,
			   "subaccount", &T::subaccount);
};

template <>
struct glz::meta<kalshi::ser::QuoteBody> {
	using T = kalshi::ser::QuoteBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("rfq_id", &T::rfq_id, "yes_bid", &T::yes_bid, "no_bid", &T::no_bid, "rest_remainder",
			   &T::rest_remainder, "post_only", &T::post_only, "subaccount", &T::subaccount);
};

template <>
struct glz::meta<kalshi::ser::AcceptQuoteBody> {
	using T = kalshi::ser::AcceptQuoteBody;
	static constexpr auto value = object("accepted_side", &T::accepted_side); // auto-ok
};

template <>
struct glz::meta<kalshi::ser::ApiKeyBody> {
	using T = kalshi::ser::ApiKeyBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("name", &T::name, "public_key", &T::public_key, "scopes", &T::scopes, "subaccount",
			   &T::subaccount, "fcm_subtrader_id", &T::fcm_subtrader_id);
};

template <>
struct glz::meta<kalshi::ser::GenerateApiKeyBody> {
	using T = kalshi::ser::GenerateApiKeyBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("name", &T::name, "scopes", &T::scopes, "subaccount", &T::subaccount,
			   "fcm_subtrader_id", &T::fcm_subtrader_id);
};

template <>
struct glz::meta<kalshi::ser::OrderIdsBody> {
	using T = kalshi::ser::OrderIdsBody;
	static constexpr auto value = object("order_ids", &T::order_ids); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::TickersBody> {
	using T = kalshi::ser::TickersBody;
	static constexpr auto value = object("tickers", &T::tickers); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::MarketTickersBody> {
	using T = kalshi::ser::MarketTickersBody;
	static constexpr auto value = object("market_tickers", &T::market_tickers); // auto-ok
};

template <>
struct glz::meta<kalshi::ser::SubaccountTransferBody> {
	using T = kalshi::ser::SubaccountTransferBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("client_transfer_id", &T::client_transfer_id, "from_subaccount", &T::from_subaccount,
			   "to_subaccount", &T::to_subaccount, "amount_cents", &T::amount_cents,
			   "exchange_index", &T::exchange_index);
};

template <>
struct glz::meta<kalshi::ser::SubaccountNettingBody> {
	using T = kalshi::ser::SubaccountNettingBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("subaccount_number", &T::subaccount_number, "enabled", &T::enabled);
};
