# Changelog

All notable changes to **kalshi-cpp** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/Reddimus/kalshi-cpp/compare/v0.2.0...HEAD
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
