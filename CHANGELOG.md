# Changelog

All notable changes to **kalshi-cpp** are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
uses [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.8...HEAD
[0.0.8]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.7...v0.0.8
[0.0.7]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.6...v0.0.7
[0.0.6]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.5...v0.0.6
[0.0.5]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.4...v0.0.5
[0.0.4]: https://github.com/Reddimus/kalshi-cpp/compare/v0.0.2...v0.0.4
[0.0.2]: https://github.com/Reddimus/kalshi-cpp/releases/tag/v0.0.2
