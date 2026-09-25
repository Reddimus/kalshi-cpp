# Changelog

All notable changes to **kalshi-cpp** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- An API reference built from the headers with `make docs` and published to
  <https://reddimus.github.io/kalshi-cpp/> for each release.
- `WebSocketClient` covers every channel and command in Kalshi's AsyncAPI
  document: all 13 channels, `update_subscription` for markets, snapshots, CF
  Benchmarks indices, and Pyth underlyings, and `list_subscriptions`. Message
  types in `kalshi/ws_models.hpp` are generated from `spec/asyncapi.yaml`, and
  [docs/channels.md](docs/channels.md) maps channels to them.
- The WebSocket client reconnects with backoff after a dropped connection,
  signs each attempt, and resubscribes with each subscription's current
  markets. `ws::Subscription` handles survive reconnects, and every
  `ws::Update` names its subscription.
- Commands on a subscription the server has not confirmed yet wait for the
  confirmation, so they never name a stale `sid`.
- Skipped sequence numbers are reported to `on_error`, and order book gaps
  request fresh snapshots (`WsConfig::resync_on_gap`).
- `WsConfig::connect_timeout`, `ping_interval`, `idle_timeout`, and
  `max_reconnect_delay`. A silent connection is pinged and then dropped, which
  triggers a reconnect.
- `WsError` carries the failed command's `id`, `sid`, `seq`, and subscription,
  and `WsState` reports connecting, connected, reconnecting, and disconnected.
- `KalshiClient` covers all 117 operations in Kalshi Predictions OpenAPI 3.31.0,
  up from 70. New areas include historical data, live data, fee changes, event
  candlesticks and forecasts, order-group triggers and limits, intra-exchange
  transfers, target balance allocation, block trades, API usage levels, search
  filters, and FCM. [docs/operations.md](docs/operations.md) lists every one.
- `tools/codegen/generate.py` generates the client, models, route tests, and
  operation list from the vendored `spec/openapi.yaml`. CI fails if they drift.
- Requests are validated before sending: required fields must be set and
  fixed-point strings must parse.
- `ErrorCode::NotFound` and `Error::api_code` (Kalshi's machine-readable code).
  HTTP errors map to specific codes and keep Kalshi's error message.
- Response parsing tolerates `null` where Kalshi's spec says a field cannot be
  null, and unknown enum values read as `Unknown` instead of failing.
- `collect_pages()` follows cursors for any list operation. `to_cents()`,
  `to_contracts()`, and `parse_timestamp()` convert exact wire values.
- Ed25519 API keys, which Kalshi recommends. `Signer` picks RSA-PSS or Ed25519
  from the loaded key and reports it through `key_type()`.
- `HttpClient(ClientConfig)` sends unauthenticated requests for public market
  data, so no API key is needed to read markets.
- `Environment::{Production, Demo}` with `rest_base_url()`, `websocket_url()`,
  `ClientConfig::for_environment()`, and `WsConfig::for_environment()`.
- `RetryingTransport`, a transport decorator that retries network errors, 429s,
  and 5xx responses with backoff. Writes (POST, PUT, DELETE) retry only on 429
  unless `RetryPolicy::retry_writes` is set, so an order is never placed twice
  and a canceled order never reports a spurious failure. `Retry-After` is
  honored up to `max_delay`.
- `TokenBucket` and `RateLimitedTransport`, which pace requests to Kalshi's
  Read and Write token budgets. Waiters reserve tokens, so large batches are
  served in order instead of being starved. Defaults match the Basic tier, and
  `rate_limit_config()` builds the account's real budgets from
  `get_account_api_limits()` and `get_account_endpoint_costs()`.
- `HttpResponse::header()` for case-insensitive header lookup.

### Changed

- CMake 3.31 or newer is required when Glaze is fetched (Glaze's own
  minimum), or 3.21 with `KALSHI_USE_SYSTEM_GLAZE=ON`. Glaze 8.3 or newer is
  enforced at compile time.
- Release builds no longer force `-march=x86-64-v3` or LTO. Both are opt-in
  through `KALSHI_NATIVE_ARCH` and `KALSHI_ENABLE_LTO` and apply only to
  kalshi-cpp's own targets.
- Tests and examples build by default only when kalshi-cpp is the top-level
  project, so FetchContent consumers no longer need to turn them off.
- The installed package version now uses `SameMinorVersion` compatibility,
  because 0.x minor releases may break the API.
- Glaze moved to v9.0.0. Set `KALSHI_USE_SYSTEM_GLAZE=ON` to build against an
  installed Glaze 8.3 or newer.
- `make bench` runs a Google Benchmark suite (`KALSHI_BUILD_BENCHMARKS`) in
  place of `tools/bench.sh`, which timed examples that exit immediately
  without credentials.
- The examples are now `market_data`, `portfolio`, `place_and_cancel_order`
  (demo only), and `stream_orderbook`, built as `example_NAME`. `make run-NAME`
  loads `.env` but no longer decodes `KALSHI_API_PRIVATE_KEY`; point
  `KALSHI_API_KEY_FILE` at a PEM file instead.
- `make configure-debug` and `make bench-compare` are gone. Use `make debug`,
  and compare `make bench` runs with Google Benchmark's `compare.py`.
- Tags with a pre-release suffix no longer trigger the release workflow. They
  never matched the CMake version check and could not publish.

### Removed

- The hand-written WebSocket frame scanners (`kalshi/detail/ws_json.hpp`),
  `classify_lifecycle_event()`, and the integer cent and contract fields on
  WebSocket messages; use `to_cents()` and `to_contracts()`.
- The hand-written REST parsers, the 0.5 model headers (`kalshi/models/`), and
  the deprecated methods for routes Kalshi removed (announcements, search, live
  data by ticker, bundle lookup, generic communications).
- `RateLimiter` and `ScopedRateLimit`. They counted one token per
  millisecond-resolution interval and could not express Kalshi's budgets; use
  `TokenBucket`.
- `RetryingClient`, `with_retry()`, `calculate_retry_delay()`, and
  `RetryResult`. Use `RetryingTransport` and `retry_delay()`.
  `should_retry()` now takes the HTTP method.
- `build_paginated_query()`, which did not percent-encode the cursor.

### Migrating from 0.5

- WebSocket: `subscribe(ws::Channel, ws::SubscribeParams)` replaces
  `subscribe_orderbook()` and the other per-channel methods and returns a
  `ws::Subscription`. Messages arrive as `ws::Update<T>`, such as
  `ws::Update<ws::OrderbookDelta>`, with the payload in `msg` and field names
  from the spec. `on_state_change` receives a `WsState`, and
  `max_reconnect_attempts` now defaults to 0 (keep trying).
- Subscriptions can be made before `connect()`; they are sent once connected.
  `disconnect()` forgets them.
- Models, parameters, and responses now use the spec's names and types from
  `kalshi/models.hpp`. For example, `get_markets()` returns
  `GetMarketsResponse{markets, cursor}`, orders use `CreateOrderV2Request`, and
  `Order::ticker` replaces `Order::market_ticker`. Responses with one field
  return that field directly (`get_market()` returns `Market`).
- Timestamps stay RFC 3339 strings; `parse_timestamp()` converts them. The
  integer cent and contract views are gone; `to_cents()` and `to_contracts()`
  convert exactly or fail.
- Methods drop Kalshi's "V2" suffix (`create_order`, `cancel_order`,
  `batch_create_orders`). Methods for routes Kalshi removed are gone.
- Path parameters are percent-encoded, and an empty one fails with
  `InvalidRequest` instead of silently calling a different route.
- `ErrorCode::Ok`, `Error::ok()`, and `Error::is_ok()` are gone; a `Result`
  holds either a value or an error. `ErrorCode::NotFound` is new, so the
  enumerators' numeric values changed. Any other 4xx is `InvalidRequest`, and
  408 is `NetworkError`.
- `KalshiClient` takes a `std::shared_ptr<const HttpTransport>`, and
  `http_client()` and the non-const `transport()` are gone.
- `collect_pages()` replaces `PaginatedIterator`, `Cursor`,
  `PaginationParams`, and `PaginatedResponse`.
- `derive_outcome_side()` and `derive_book_side()` are now `outcome_side()`
  and `book_side()`, in `kalshi/helpers.hpp`. They return `Unknown` when the
  side or action is `Unknown`.
- `Signer` is copyable, and `WebSocketClient` stores its own copy, so the
  signer no longer has to outlive the client.
- `HttpResponse::status_code` is an `int`. `ClientConfig::timeout` is in
  milliseconds (assigning `std::chrono::seconds` still works), and
  `ClientConfig::connect_timeout` is new.
- `HttpClient`'s constructors are `explicit`.
- OpenSSL 3.0 or newer is required.
- Key-loading failures (unreadable file, malformed or encrypted PEM,
  unsupported algorithm) return `ErrorCode::InvalidKey`.

### Fixed

- WebSocket error frames keep the server's message; the client read a
  `message` key Kalshi does not send and always fell back to a generic name.
- `update_subscription` no longer sends an undocumented `channel` parameter,
  and fill `action` values such as `sell_to_close` no longer read as `buy`.
- `connect()` returns the actual failure at once instead of waiting 10
  seconds and reporting a timeout.
- Sends from other threads no longer call libwebsockets directly, which it
  does not allow; they wake the network thread instead.
- Destroying a `WebSocketClient` inside one of its own callbacks is safe.
- With libwebsockets 4.4 or newer, ending a WebSocket session no longer shuts
  down OpenSSL for the whole process. Every later TLS connection failed after
  that, REST calls included.
- A POST with an empty body (for example `create_subaccount()`) no longer makes
  libcurl read the request body from standard input.
- Timeouts pass `long` values to libcurl; the `int64` passed before was
  undefined behavior on Windows.
- Requests set `CURLOPT_NOSIGNAL`, a connect timeout, TCP keepalive, gzip, and
  a `kalshi-cpp/<version>` User-Agent.
- Encrypted private keys fail with an error instead of prompting for a
  passphrase on the terminal. Unsupported key types fail when loaded rather
  than on the first request, and file errors name the path.
- OpenSSL, libcurl, and libwebsockets are private link dependencies. The
  unused build-tree `export()` file is gone; use `cmake --install` or
  FetchContent.
- `make` uses every CPU on macOS, and `make install-hooks` works in worktrees.

## [0.5.2] - 2026-09-04

### Fixed

- Validate WebSocket schemes, hosts, ports, IPv6 literals, and fragments before
  opening a connection. Invalid configuration now returns `InvalidRequest`
  instead of throwing from `std::stoi` or reaching the network.
- Guard the libwebsockets connection handle with the send-queue mutex, and
  retire it before the context that owns it is destroyed. Calling `connect()`
  or `disconnect()` while another thread queued a subscription was a data race,
  and could hand libwebsockets a handle from a destroyed context.
- Sign WebSocket paths without query parameters, matching Kalshi's current
  authentication contract while preserving the query in the upgrade request.
- Keep moved-from `Signer` and `HttpClient` objects safe to inspect and reject
  work through them with typed errors.
- Stop allocating inside the `noexcept` `HttpClient::config()` and
  `WebSocketClient::config()` accessors. Their moved-from sentinels were
  function-local statics whose construction could throw, which would call
  `std::terminate` rather than return.
- Give public error, HTTP response, order-book entry, and retry-result
  aggregates deterministic scalar defaults.
- Preserve library-size totals in `tools/bench.sh`; piping the prior loop into
  `sort` discarded the totals in a subshell.
- Correct the candlestick example to use the current `series_ticker` route.
- Replace stale directory guides that described a removed test framework,
  missing Makefiles, and an incomplete source layout.

### Performance

- Parse WebSocket frames from `std::string_view` without copying the full frame
  or allocating a quoted search string for each field. The receive benchmark
  now guards this path alongside request serialization.

### Security

- Generate test-only RSA keys at runtime instead of storing a private-key-shaped
  fixture in two test files. A clean tracked-tree secret scan now reports no
  findings.

## [0.5.1] - 2026-09-03

### Fixed

- Made the documented `kalshi::kalshi` target available to FetchContent
  consumers, matching the installed package interface.

## [0.5.0] - 2026-09-03

### Changed

- Corrected request signing to include the full API path and exclude the query
  string, matching Kalshi's current authentication contract.
- Preserved existing aggregate member prefixes while appending v0.5 fields.
- Added current RFQ-scoped quote routes alongside the generic quote routes.
- Updated supported WebSocket frames with current exact fields and canonical
  direction models. User callbacks now run outside internal locks, and a
  callback-triggered disconnect never joins its own service thread.
- Migrated event-market create, amend, decrease, cancel, batch, and cancel-all
  operations to the current single-book V2 contracts. Ambiguous legacy order
  bodies now fail before transport.
- Preserved REST and WebSocket fixed-point values as exact strings. Legacy
  integer views are populated only when conversion is lossless and in range.
- Added exchange-shard fields and filters across markets, orders, fills,
  positions, settlements, and lifecycle events.
- Updated Predictions routes and filters for trades, series, events,
  orderbooks, queue positions, communications, API keys, and user timestamps.
- Added current filters for nested event markets, Series volume, structured
  targets, incentives, milestones, and block trades. Array query parameters
  now use the repeated-key encoding required by the published contract.
- Added `get_events_response()` for event-list milestone expansions and
  preserved arbitrary Series product metadata as exact JSON.
- Preserved API-key scopes and location-attestation expiry, Series tags and
  prohibitions, milestone ticker lists, and order-group order IDs.
- Preserved compact order-mutation fields, including average fill price and
  average fee paid. Create, amend, and batch-create results recover canonical
  direction from their requests; decrease results report when direction was
  absent instead of presenting a guessed direction as authoritative.
- Matched exact success statuses for order batches and subaccount creation,
  and separated successful batch items from per-item errors.
- Rejected removed filters and conflicting legacy request fields before
  transport so migrations cannot silently broaden queries or change orders.
- Canonical ask orders now require `discard_legacy_direction=true` when they
  intentionally replace the default legacy Yes/Buy direction.
- RFQ creation now requires `discard_legacy_direction=true` to acknowledge
  that the current RFQ contract has no direction fields.
- Kept the legacy subaccount-transfer amount as an exact cents alias and
  rejected conflicting dual values.
- Removed network calls to deleted announcement and generic search operations.
  Legacy generic communications, collection lookup, and ticker-based live-data
  methods now return `InvalidRequest` instead of calling stale routes.
- Added injectable HTTP transport support. libcurl initialization is
  process-safe, requests on one client are serialized, and TLS peer and host
  verification are controlled together.
- Updated Glaze to 8.3.0 and GoogleTest to 1.18.0. CI now checks project
  warnings, sanitizers, clang-tidy, installed-package consumption, and
  FetchContent consumption.

### Added

- `FixedPoint` for checked, exact decimal conversion.
- Operation-contract tests that assert routes, verbs, encoded filters, exact
  V2 bodies, and representative current response schemas without credentials.

### Migration

- Set `CreateOrderParams::book_side`, `count_fp`, `price_dollars`,
  `time_in_force`, and `self_trade_prevention_type` for V2 order creation.
- Use `series_ticker`, `start_ts`, and `end_ts` for candlesticks.
- Read exact `*_dollars` and `*_fp` fields instead of assuming whole cents or
  contracts.
- Margin and Perpetuals remain out of scope because Kalshi publishes them as a
  separate API contract.

## [0.4.9] - 2026-09-02

### Added

- **REST**: `PublicTrade::is_block_trade` — parsed from `GET /markets/trades`
  (Kalshi changelog 2026-05-29: public trade responses now flag block
  trades and support filtering by block status). Block trades are large
  negotiated prints routed off the central order book; exposing the flag
  lets trade-flow / microstructure consumers exclude them. The field is
  absent on older payloads and defaults to `false` (forward/backward
  compatible — no behavioural change for existing consumers).

### Changed

- **Internal**: extracted the inline `GET /markets/trades` body parser out
  of `KalshiClient::get_trades` into a testable
  `api_detail::parse_trades_response`, matching the existing
  `parse_deposits_response` / `parse_withdrawals_response` convention. Adds
  unit coverage for trade parsing (incl. the new `is_block_trade` flag).

### Fixed

- **WebSocket**: scalar integer fields now clamp below `INT32_MIN` or above
  `INT32_MAX`, and oversized non-negative order-book price and quantity entries
  clamp at `INT32_MAX`. Both scanners previously accumulated directly into
  `int32_t`, so malformed or hostile values could trigger signed-overflow
  undefined behaviour; all in-range values continue to parse unchanged.

## [0.4.8] - 2026-05-19

### Fixed

- **Market**: `Market::status` now preserves unknown / future Kalshi
  status strings in the parsed model instead of silently mapping them
  to a default enum. Consumers that introspect `status_raw` can keep
  trading against new states the SDK hasn't yet enumerated; the
  bracket vs tail routing on the trader side is unaffected.

## [0.4.7] - 2026-05-19

### Added

- **REST**: Added `get_account_api_limits()` and `get_endpoint_costs()`
  for Kalshi's authenticated account API metadata endpoints. These expose
  account usage tier, read/write token-bucket budgets, default endpoint
  token cost, and non-default endpoint cost overrides.
- **Market**: Parsed the new `current` lifecycle fields on `Market`
  (`current_open_ts`, `current_close_ts`, `current_settle_ts`,
  `current_expire_ts`) so consumers can read the in-flight contract's
  effective lifecycle timestamps without re-deriving them.

## [0.4.6] - 2026-05-19

### Fixed

- **WebSocket**: `unsubscribe`, `add_markets`, and `remove_markets` now
  translate the SDK's client command id to Kalshi's server-assigned
  subscription id after a `subscribed` acknowledgement. This prevents
  hourly resubscribe flows from sending stale client ids and receiving
  `Unknown subscription ID` errors from the live WebSocket API.

## [0.4.5] - 2026-05-19

### Added

- **REST**: Added `get_market_orderbooks(tickers)` for Kalshi's documented
  `GET /markets/orderbooks` endpoint. The SDK sends repeated `tickers`
  query params, matching the live API shape, and enforces the documented
  1-100 ticker request size.

### Fixed

- **REST**: Orderbook parsing now accepts the current `orderbook_fp`
  fixed-point-dollar response shape (`yes_dollars` / `no_dollars`) while
  preserving the legacy integer-cent `yes` / `no` arrays. This also fixes
  a single-orderbook parser bug that skipped the first nested `[price, qty]`
  pair in legacy payloads.

## [0.4.4] - 2026-05-18

### Added

- **REST**: Added typed event-market order-cancel V2 support:
  `CancelOrderV2Params`, `OrderCancelResult`, `cancel_order_v2(params)`,
  and `batch_cancel_orders_v2(request)`. These call Kalshi's
  `/portfolio/events/orders/{order_id}` and
  `/portfolio/events/orders/batched` endpoints and preserve V2 response
  fields including `reduced_by`, `ts_ms`, `client_order_id`, and
  per-order error payloads.

## [0.4.3] - 2026-05-18

### Fixed

- **REST**: `batch_cancel_orders` now sends the JSON body required by
  Kalshi's `DELETE /portfolio/orders/batched` endpoint instead of
  dropping the serialized body. Existing `BatchCancelRequest::order_ids`
  callers are preserved and emitted as the current `orders` selector
  shape; callers that need subaccount-scoped cancellation can populate
  `BatchCancelRequest::orders` directly.

## [0.4.2] - 2026-05-18

### Added

- **API**: `CreateOrderParams` now exposes the current optional
  create-order request fields documented by Kalshi:
  `count_fp`, fixed-point dollar prices, `time_in_force`, `post_only`,
  `reduce_only`, `self_trade_prevention_type`, `order_group_id`,
  `cancel_order_on_pause`, `subaccount`, and `exchange_index`. Existing
  integer-count / cent-price callers are unchanged.

### Changed

- Default REST and WebSocket hosts now use Kalshi's dedicated external
  Trade API endpoints:
  `https://external-api.kalshi.com/trade-api/v2` and
  `wss://external-api-ws.kalshi.com/trade-api/ws/v2`. The older
  `api.elections.kalshi.com` hosts remain supported by Kalshi and can
  still be supplied explicitly in `ClientConfig` / `WsConfig`.

- `get_balance()` now parses `balance` / `available_balance` via
  `extract_cents_or_dollars` instead of `extract_int`. Kalshi's
  2026-05-21 changelog adds `balance_dollars` (fixed-point dollar
  string) to `GET /portfolio/balance` alongside the integer-cent
  field; this picks up the `_dollars` shape when present and falls
  back to raw cents otherwise — same v2 wire-format handling already
  used across the other REST response parsers (CHANGELOG v0.0.8). No
  public API change; `Balance` struct layout is unchanged (still
  int64 cents).

## [0.4.1] - 2026-05-15

### Added

- `.editorconfig` (fleet-standard: tabs, 4-width, LF, UTF-8, 100-col
  max for C++; spaces, 2-width for YAML/JSON). Sibling to
  `.clang-format`. Covers editors that don't read `.clang-format`
  (Vim, VS Code without the extension). Matches the file already
  shipped in `infra-cpp` (#41).

### Fixed

- **WS error frame parser**: `WsError.message` was returning the
  literal string `"code"` for error frames that lacked an explicit
  `message` field (e.g. code 7 = "Unknown subscription ID"). The
  previous fallback `extract_string("msg")` returned the first quoted
  token inside the `msg` object — the `"code"` key name — because the
  hand-rolled scanner doesn't distinguish object-value vs nested-key
  occurrences. Now falls back to the documented `ws_error_code_name(err.code)`
  when no `message` field is present, so consumers see
  `WS error: code=7 message="Unknown subscription ID"` instead of
  `message="code"`. Caught in 2026-05-15 production logs against
  kalshi-websocket (#45).

## [0.4.0] - 2026-05-14

### Added

- **API**: `kalshi::Deposit` + `kalshi::Withdrawal` structs (same wire shape,
  distinct types for call-site clarity), `kalshi::GetPortfolioMovementParams`
  (limit + cursor), and two cursor-paginated client methods:
  `KalshiClient::get_deposits(...)` and `KalshiClient::get_withdrawals(...)`.
  Kalshi added the `GET /portfolio/deposits` and `GET /portfolio/withdrawals`
  endpoints on 2026-05-05. Schema per
  <https://docs.kalshi.com/api-reference/portfolio>: id, status (pending |
  applied | failed | returned), type (ach | wire | crypto | debit | apm),
  amount_cents, fee_cents, created_ts, finalized_ts (nullable). Parsers
  live in `src/api/response_parsers.hpp` as
  `parse_deposits_response` / `parse_withdrawals_response`; a shared
  `extract_nullable_int` helper handles the nullable `finalized_ts`
  field. Four new tests in `tests/test_response_parsers.cpp` cover the
  finalized + pending shapes for both endpoints.

## [0.3.0] - 2026-05-14

### Added

- **Models**: `kalshi::OutcomeSide` (Yes/No) and `kalshi::BookSide` (Bid/Ask)
  enums, plus `derive_outcome_side(side, action)` and
  `derive_book_side(side, action)` constexpr helpers. Kalshi added
  normalized `outcome_side` / `book_side` directional fields to Order /
  Fill / Trade response objects on 2026-05-06; the upstream spec calls
  these out as the eventual replacement for `(side, action)`. The
  derivation helpers let consumers normalize today without waiting for
  a response-field parse path. See `tests/test_models.cpp` for the truth
  table.
- **API**: `CreateQuoteParams::post_only` (`std::optional<bool>`). Kalshi
  added the `post_only` quote flag on 2026-05-05 — when true the quote
  never takes resting orders or pays taker fees; it auto-cancels at
  execution if it would have matched. Serialized after `expires_at` per
  the `glz::meta<ser::QuoteBody>` field order; omitted from the body
  when nullopt (preserves byte-equivalence with pre-flag calls).
- **API**: `OrderGroup::subaccount_number` field. Kalshi added this to
  the order-group response surface on 2026-05-07 (the subaccount that
  owns the group). Parsers in `create_order_group`, `get_order_group`,
  `get_order_groups`, and `reset_order_group` now populate it. Empty
  string when the server omits the field (back-compat).
- **API**: `GetQuotesParams::rfq_user_filter` (`std::optional<std::string>`).
  Kalshi added the parameter on 2026-05-07 — restricts listed quotes to
  those that responded to RFQs the authenticated user created. Appended
  to the query string by `build_quotes_query` when set; omitted when
  nullopt (preserves byte-equivalence with pre-filter calls).
- **WebSocket**: `MarketLifecycle::yes_sub_title` (`std::optional<std::string>`).
  Kalshi added this field to the v2 `metadata_updated` lifecycle sub-event
  on 2026-05-11 — emitted only when the yes-side subtitle changes. The
  hand-rolled dispatcher in `src/ws/websocket.cpp` extracts it when
  non-empty; nullopt for the open/close/determination/settlement
  sub-events.
- **WebSocket**: `kalshi::ws_error_code_name(code)` constexpr helper —
  maps the Kalshi WS error code (`WsError::code`) to its canonical name
  per the AsyncAPI spec, including code 25 (`Subscription buffer
  overflow`) added 2026-05-12. Falls back to `"Unknown error code"`
  for codes outside the documented 1-22 + 25 range so consumers can
  log unknown codes without branching themselves.
- **API**: `Order::mutation_ts_ms` (`std::optional<std::int64_t>`).
  Kalshi added a top-level `ts_ms` field to V2 order-mutating endpoint
  responses on 2026-05-05 — the matching-engine wall-clock timestamp
  (Unix epoch ms) at which the request was processed. `parse_order`
  extracts it from the response top-level (sibling to the wrapped
  `order` object) and surfaces it on the returned Order. Populated by
  `create_order`, `amend_order`, `decrease_order`, `batch_create_orders`,
  `batch_cancel_orders`; nullopt for `get_order` / `get_orders` reads
  and pre-2026-05-05 servers.
- **WebSocket**: `kalshi::LifecycleEventType` enum and
  `classify_lifecycle_event(const MarketLifecycle&)` constexpr helper.
  Kalshi's `market_lifecycle_v2` channel flat-encodes 9+ sub-events
  (created/activated/deactivated/determined/settled/metadata_updated/
  fractional_trading_updated/price_level_structure_updated/
  close_date_updated) onto one frame shape with no explicit `event_type`
  field. The helper inspects which fields are populated and returns the
  best-fit `LifecycleEventType` (Settled > Determined > Deactivated >
  MetadataUpdated > OpenOrCreated > Unknown). Consumers needing the
  finer 9-way distinction can branch on the underlying fields directly.

## [0.2.1] - 2026-05-13

### Fixed

- **WebSocket**: subscribe + dispatch paths now use the `market_lifecycle_v2`
  channel (Kalshi deprecated `market_lifecycle`). The dispatcher still
  accepts v1 frames for the transition window so older subscriptions don't
  break.
- **WebSocket**: handshake headers (`KALSHI-ACCESS-KEY`,
  `KALSHI-ACCESS-SIGNATURE`, `KALSHI-ACCESS-TIMESTAMP`) now include the
  trailing `:` required by `lws_add_http_header_by_name`. Previously the
  authenticated upgrade silently sent malformed header lines.
- **WebSocket**: omit the `Origin` header on Kalshi upgrades. Kalshi
  rejects upgrades that include one with HTTP 403; the documented Python
  client sends no Origin and succeeds.
- **REST**: query parameters are percent-encoded via the new
  `percent_encode_query_value` helper. Cursor + category values containing
  `,`/spaces/`=` now survive intact through `append_query_param`.
- **REST**: the `GET /markets/.../candlesticks` response parser now
  handles Kalshi's current `price.*_dollars` / `volume_fp` string-decimal
  schema alongside the legacy raw-cent ints. Adds a small
  `extract_fixed_point_int` helper.

### Tests

- Direct candlestick response parser coverage for Kalshi's current
  `price.*_dollars` / `volume_fp` schema, the legacy raw-cent schema, and
  the alternate `candlesticks` array key. Pins the parser behavior that
  downstream market-data ingestion depends on for nonzero OHLC backtest rows.
- Query-builder coverage for the `GET /series` percent-encoding through a
  new pure free function (`kalshi::api_detail::build_series_query_string`).
  The test routes through the free function instead of poking the private
  member method via `#define private public`; MSVC encodes access modifiers
  in mangled symbol names, so the hack broke `build-windows` on PR #20
  until this refactor.

### Build

- `Makefile`: new `pre-commit` and `install-hooks` targets. `make
  pre-commit` runs `format` then `lint` in one shot. `make install-hooks`
  drops a `.git/hooks/pre-commit` shim that fires `make pre-commit` on
  every `git commit` (idempotent). Mirrors the pattern adopted across the
  C++ SDK family.

## [0.2.0] - 2026-05-12

### Build

- Migrate outgoing-JSON serialization from `nlohmann/json` to
  [Glaze](https://github.com/stephenberry/glaze) v7.6.0. Affects the
  WS subscribe / unsubscribe / update_subscription frame builders and
  the dozen `KalshiClient::serialize_*` REST request body methods.
  Public API and ABI unchanged; downstream consumers (kalshi-trader)
  link without modification. Glaze is FetchContent-only and never
  appears in the install / export sets — it stays a TU-private
  implementation detail.

  Recorded benchmark (x86_64-v3, GCC 13.3, -O3 -DNDEBUG, 50-order
  batch-create payload, 1000 iters):

      nlohmann::ordered_json v3.11.3 : ~162 us/op  (pre-migration)
      glaze v7.6.0                   :   ~3 us/op  (post-migration)
      speedup                        :  ~55-60x

  See `tests/parse_benchmark.cpp` for the regression guard
  (cap = 500 us/op, ctest --timeout = 30s).

- WS *receive* hot path (`handle_message` + the `kalshi/detail/ws_json.hpp`
  scanners) is **deliberately untouched** — it was stripped of
  nlohmann in v0.0.7 / v0.0.8 for perf and v2-schema correctness. See
  `feedback_find_first_json_scanner` memory note for rationale. The
  REST response parsers (still hand-rolled `extract_*` in
  `src/api/client.cpp`) are likewise unchanged.

### Tests

- `tests/test_json_serialize.cpp` (25 cases) — byte-equivalence
  regression gate for every migrated `glz::meta`-driven serializer
  against the pre-migration `nlohmann::ordered_json::dump()` output.
  Kalshi's API rejects unordered payloads on the order-management
  routes and the WS server rejects unordered subscribe frames; any
  key-reorder or whitespace change in the emitted bytes is a
  production breakage.
- `tests/parse_benchmark.cpp` — serialize-throughput regression
  guard wired via `add_test` + `set_tests_properties(... TIMEOUT 30)`.

## [0.1.1] - 2026-05-10

### Fixed

- FetchContent consumers fail to find `kalshi/version.hpp` (introduced in
  v0.1.0). The configure-time generated header lives at
  `${PROJECT_BINARY_DIR}/include/kalshi/version.hpp` but the
  `target_include_directories(kalshi_core PUBLIC ...)` was pointing at
  `${CMAKE_BINARY_DIR}/include` — under FetchContent that resolves to the
  consumer's top-level build dir (where the header is NOT), not the
  kalshi-cpp subproject dir (where it IS). Using `PROJECT_BINARY_DIR`
  pins to the kalshi-cpp project's own build dir under either standalone
  or FetchContent layouts.

## [0.1.0] - 2026-05-10

### Added

- **`pm::api::Subaccount`** — Kalshi subaccount endpoint suite (create,
  list, get, update, disable). Returns `std::expected<Subaccount, Error>`
  on all operations; mirrors the `Account` surface for primary-account
  callers. Unblocks the multi-account separation Kalshi recommends for
  capital partitioning across strategies. (PR #12)

### Fixed

- `WebSocketClient` move-from null-guard — null-check all pimpl
  accessors (`is_connected()`, `last_error()`, etc.) so a moved-from
  instance is safe to destroy without UB. Caught by the same crash
  mode (`terminate called without an active exception`) v0.0.9 fixed
  in the reconnect path; this closes the move-destruction sibling
  case. (PR #13)
- Test signer key — `Signer.SignProducesHeaders` previously embedded
  an invalid `TEST_RSA_KEY` placeholder string; the test silently
  skipped its assertions. Replaced with a real PKCS#8-encoded private
  key so the test actually runs and pins the signer's header output
  shape. (PR #14)

### CI

- `build-windows` job added via vcpkg — parity with the rest of the
  SDK family (alpaca-markets-cpp, ncei-cpp, nws-cpp, open-meteo-cpp,
  polymarket-cpp all now have Linux + macOS + Windows). Zero source
  changes needed; the codebase was already POSIX-isms-free. (PR #11)
- `actions/checkout@v6` for Node 24 runtime parity with sibling SDKs.
- `cpp_auto_audit.py` walks `--cached` + `--others` so new test
  files in a feature branch trigger the audit during local lint
  (previously only `--cached` was checked).

### Docs

- `SECURITY.md` — canonical contact path for reporting vulnerabilities
  in the auth / signer / WebSocket paths. (PR #15)

## [0.0.9] — 2026-05-04

### Fixed

- `WebSocketClient::connect()` reaps the previous service thread + libwebsockets
  context before starting a new connection. After a
  `LWS_CALLBACK_CLIENT_CONNECTION_ERROR` the thread stays joinable
  (the callback only sets `connected = false`), and the caller's typical
  reconnect-on-error loop then move-assigned a new `std::thread` onto the
  still-joinable handle, hitting `std::terminate` with
  `terminate called without an active exception` and exiting 139 (SIGSEGV).
  Production observed ~5 crashes/day on `kalshi-websocket`; supervisor
  auto-restart masked it but each crash dropped ~10s of WS data.

### Build

- Drive `kalshi::VERSION` from CMake `PROJECT_VERSION` via `configure_file()`
  so the C++ constexpr can no longer drift from the FetchContent tag.
  Previously `kalshi.hpp` hardcoded `"0.1.0"` while `CMakeLists.txt` said
  `0.0.8`. The new `include/kalshi/version.hpp` is generated; do not edit
  by hand.

### CI

- Add `.markdownlint-cli2.yaml` mirroring the open-meteo-cpp config —
  disables `MD013` (line-length) and other style-noise rules but keeps
  `MD022`/`MD031`/`MD032` formatting hygiene enforced.
- Drop the macOS lint step. brew tracks the latest LLVM, so brew's
  clang-format output drifts from Ubuntu apt's pinned version. macOS
  still runs Build + Test for portability; lint runs once on Linux as
  format-of-record.

### Tests

- New `tests/test_version.cpp` pinning `kalshi::VERSION` to semver and
  round-tripping through `VERSION_MAJOR`/`MINOR`/`PATCH`.

## [0.0.8] — 2026-04-20

### Fixed

- REST parser accepts Kalshi v2 wire format: `_dollars` and quoted-int
  schema fields. ([`48eaff1`](https://github.com/Reddimus/kalshi-cpp/commit/48eaff1))

## [0.0.7] — 2026-04-19

### Fixed

- WebSocket parser handles the v2 schema: `yes_price_dollars` /
  `no_price_dollars` (string decimal dollars), `count_fp` / `delta_fp`
  (string floats), and ISO-8601 `orderbook_delta.ts` instead of unix
  ints. ([`2a3a8e0`](https://github.com/Reddimus/kalshi-cpp/commit/2a3a8e0))

## [0.0.6]

### Fixed

- WebSocket trade and orderbook frames parse quoted numeric fields
  correctly. ([`09c5dc1`](https://github.com/Reddimus/kalshi-cpp/commit/09c5dc1))

### Refactor

- API client builds JSON bodies via `nlohmann::json` instead of
  hand-rolled string concatenation. ([`e9c6f03`](https://github.com/Reddimus/kalshi-cpp/commit/e9c6f03))

## [0.0.5]

### Added

- Migrate test suite to GoogleTest; add sanitizer and coverage CMake
  options (`KALSHI_ENABLE_SANITIZERS`, `KALSHI_ENABLE_COVERAGE`).
  ([`452b3d4`](https://github.com/Reddimus/kalshi-cpp/commit/452b3d4))

### Performance

- Default `-march=x86-64-v3` (2017+ baseline: AVX2, BMI2, FMA) when
  `KALSHI_NATIVE_ARCH=OFF`. Matches the service Dockerfiles so
  FetchContent-consumed builds produce identical codegen.
  ([`7956824`](https://github.com/Reddimus/kalshi-cpp/commit/7956824))

## [0.0.4] — 2026-04-15

### Fixed

- Parse ISO-8601 datetime strings for market timestamps.
  ([`2bd34d6`](https://github.com/Reddimus/kalshi-cpp/commit/2bd34d6))

## [0.0.2] — initial public release

[Unreleased]: https://github.com/Reddimus/kalshi-cpp/compare/v0.5.2...HEAD
[0.5.2]: https://github.com/Reddimus/kalshi-cpp/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/Reddimus/kalshi-cpp/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.9...v0.5.0
[0.4.9]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.8...v0.4.9
[0.4.8]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.7...v0.4.8
[0.4.7]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.6...v0.4.7
[0.4.6]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.5...v0.4.6
[0.4.5]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.4...v0.4.5
[0.4.4]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.3...v0.4.4
[0.4.3]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.2...v0.4.3
[0.4.2]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.1...v0.4.2
[0.4.1]: https://github.com/Reddimus/kalshi-cpp/compare/v0.4.0...v0.4.1
[0.4.0]: https://github.com/Reddimus/kalshi-cpp/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/Reddimus/kalshi-cpp/compare/v0.2.1...v0.3.0
[0.2.1]: https://github.com/Reddimus/kalshi-cpp/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/Reddimus/kalshi-cpp/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/Reddimus/kalshi-cpp/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.9...v0.1.0
[0.0.9]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.8...v0.0.9
[0.0.8]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.7...v0.0.8
[0.0.7]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.6...v0.0.7
[0.0.6]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.2...v0.0.4
[0.0.2]: https://github.com/Reddimus/kalshi-cpp/releases/tag/v0.0.2
