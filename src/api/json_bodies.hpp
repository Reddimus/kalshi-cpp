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
	std::string action;
	std::string type;
	std::int32_t count{0};
	std::optional<std::int32_t> yes_price;
	std::optional<std::int32_t> no_price;
	std::optional<std::string> client_order_id;
	std::optional<std::int64_t> expiration_ts;
	std::optional<std::int32_t> sell_position_floor;
	std::optional<std::int32_t> buy_max_cost;
};

/// POST /portfolio/orders/{id}/amend
struct AmendOrderBody {
	std::optional<std::int32_t> count;
	std::optional<std::int32_t> yes_price;
	std::optional<std::int32_t> no_price;
};

/// POST /portfolio/orders/{id}/decrease
struct DecreaseOrderBody {
	std::int32_t reduce_by{0};
};

/// POST /portfolio/orders/batched
struct BatchOrdersBody {
	std::vector<CreateOrderBody> orders;
};

/// DELETE /portfolio/orders/batched
struct BatchCancelBody {
	std::vector<std::string> order_ids;
};

/// POST /portfolio/order-groups
struct OrderGroupBody {
	std::string type;
	std::vector<std::string> order_ids;
};

/// POST /rfqs
struct RfqBody {
	std::string market_ticker;
	std::string side;
	std::string action;
	std::int32_t count{0};
	std::optional<std::int64_t> expires_at;
};

/// POST /quotes
struct QuoteBody {
	std::string rfq_id;
	std::int32_t price{0};
	std::int32_t count{0};
	std::optional<std::int64_t> expires_at;
	std::optional<bool> post_only;
};

/// POST /api-keys
struct ApiKeyBody {
	std::string name;
	std::vector<std::string> scopes;
	std::optional<std::int64_t> expires_at;
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
	std::optional<std::int64_t> expires_at;
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
	std::int64_t from_subaccount{0};
	std::int64_t to_subaccount{0};
	std::int64_t amount{0};
};

/// PUT /portfolio/subaccounts/netting
struct SubaccountNettingBody {
	std::int64_t subaccount{0};
	bool netting_enabled{false};
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
		object("ticker", &T::ticker, "side", &T::side, "action", &T::action, "type", &T::type,
			   "count", &T::count, "yes_price", &T::yes_price, "no_price", &T::no_price,
			   "client_order_id", &T::client_order_id, "expiration_ts", &T::expiration_ts,
			   "sell_position_floor", &T::sell_position_floor, "buy_max_cost", &T::buy_max_cost);
};

template <>
struct glz::meta<kalshi::ser::AmendOrderBody> {
	using T = kalshi::ser::AmendOrderBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("count", &T::count, "yes_price", &T::yes_price, "no_price", &T::no_price);
};

template <>
struct glz::meta<kalshi::ser::DecreaseOrderBody> {
	using T = kalshi::ser::DecreaseOrderBody;
	static constexpr auto value = object("reduce_by", &T::reduce_by); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::BatchOrdersBody> {
	using T = kalshi::ser::BatchOrdersBody;
	static constexpr auto value = object("orders", &T::orders); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::BatchCancelBody> {
	using T = kalshi::ser::BatchCancelBody;
	static constexpr auto value = object("order_ids", &T::order_ids); // auto-ok: glz::object
};

template <>
struct glz::meta<kalshi::ser::OrderGroupBody> {
	using T = kalshi::ser::OrderGroupBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("type", &T::type, "order_ids", &T::order_ids);
};

template <>
struct glz::meta<kalshi::ser::RfqBody> {
	using T = kalshi::ser::RfqBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("market_ticker", &T::market_ticker, "side", &T::side, "action", &T::action, "count",
			   &T::count, "expires_at", &T::expires_at);
};

template <>
struct glz::meta<kalshi::ser::QuoteBody> {
	using T = kalshi::ser::QuoteBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("rfq_id", &T::rfq_id, "price", &T::price, "count", &T::count, "expires_at",
			   &T::expires_at, "post_only", &T::post_only);
};

template <>
struct glz::meta<kalshi::ser::ApiKeyBody> {
	using T = kalshi::ser::ApiKeyBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("name", &T::name, "scopes", &T::scopes, "expires_at", &T::expires_at);
};

template <>
struct glz::meta<kalshi::ser::GenerateApiKeyBody> {
	using T = kalshi::ser::GenerateApiKeyBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("name", &T::name, "scopes", &T::scopes, "expires_at", &T::expires_at);
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
		object("from_subaccount", &T::from_subaccount, "to_subaccount", &T::to_subaccount, "amount",
			   &T::amount);
};

template <>
struct glz::meta<kalshi::ser::SubaccountNettingBody> {
	using T = kalshi::ser::SubaccountNettingBody;
	static constexpr auto value = // auto-ok: glz::object returns unspellable tuple
		object("subaccount", &T::subaccount, "netting_enabled", &T::netting_enabled);
};
