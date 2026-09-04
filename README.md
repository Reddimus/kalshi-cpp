# kalshi-cpp

[![CI](https://github.com/Reddimus/kalshi-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Reddimus/kalshi-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Reddimus/kalshi-cpp)](https://github.com/Reddimus/kalshi-cpp/releases)

A C++23 client for Kalshi's Predictions REST API and WebSocket feeds. It uses
RSA-PSS request signing, lossless fixed-point strings, and
`std::expected<T, Error>` for failures.

This package targets the [Predictions API](https://docs.kalshi.com/openapi.yaml).
Kalshi's Margin and Perpetuals API has a separate host, authentication surface,
and [OpenAPI document](https://docs.kalshi.com/perps_openapi.yaml); it is not
part of this client.

## Install and test

Install OpenSSL, libcurl, libwebsockets, CMake 3.20+, and a C++23 compiler.

```bash
brew install cmake openssl curl libwebsockets pkg-config
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel && ctest --test-dir build --output-on-failure
```

Linux package names are `libssl-dev`, `libcurl4-openssl-dev`,
`libwebsockets-dev`, and `pkg-config`.

## Add to a CMake project

```cmake
include(FetchContent)
FetchContent_Declare(
  kalshi
  GIT_REPOSITORY https://github.com/Reddimus/kalshi-cpp.git
  GIT_TAG v0.5.2
)
set(KALSHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KALSHI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(kalshi)
target_link_libraries(myapp PRIVATE kalshi::kalshi)
```

The same target is exported by `cmake --install` and `find_package(kalshi)`.
Run `tools/test_consumers.sh` to verify both paths.

## Read market data

```cpp
#include <kalshi/kalshi.hpp>
#include <iostream>

int main() {
    auto signer = kalshi::Signer::from_pem_file("key-id", "private-key.pem");
    if (!signer) return 1;
    kalshi::KalshiClient client{kalshi::HttpClient{std::move(*signer)}};
    auto markets = client.get_markets();
    if (!markets) return 2;
    for (const auto& market : markets->items) {
        std::cout << market.ticker << ' ' << market.yes_bid_dollars << '\n';
    }
}
```

## Place a V2 order

The current order API uses one `bid`/`ask` book and fixed-point strings. The SDK
rejects ambiguous legacy integer bodies before sending a request.

```cpp
kalshi::CreateOrderParams order;
order.ticker = "MARKET-TICKER";
order.book_side = kalshi::BookSide::Bid;
order.count_fp = "10.00";
order.price_dollars = "0.500000";
order.time_in_force = "good_till_canceled";
order.self_trade_prevention_type = "taker_at_cross";
order.exchange_index = -1;

// For an ask that intentionally replaces the default legacy Yes/Buy direction:
// order.book_side = kalshi::BookSide::Ask;
// order.discard_legacy_direction = true;

auto result = client.create_order(order);
if (!result) std::cerr << result.error().message << '\n';
```

## Precision and exchange shards

- Dollar and fractional-count values retain their exact wire strings.
- `FixedPoint::scaled_integer(scale)` rejects lossy or overflowing conversion.
- Legacy integer fields are `0` when exact conversion is impossible.
- Portfolio filters and returned models expose `exchange_index`. Pass `-1`
  where the API supports ticker-based auto-routing.

## Predictions API coverage

The typed surface covers the common market-data, portfolio, order, subaccount,
RFQ, quote, and discovery workflows. See the exact [REST and WebSocket coverage
table](docs/api-coverage.md) before depending on a less common operation.

Unsupported legacy methods fail before transport instead of calling a stale
route. Margin endpoints belong in a separate client and remain out of scope.

## Where things live

| Path | Purpose |
| --- | --- |
| `include/kalshi/` | Public API and models |
| `src/api/` | REST contracts, parsing, and serialization |
| `src/http/` | Signed libcurl transport |
| `src/ws/` | WebSocket client and frame parsing |
| `tests/` | Unit, parser, and operation-contract tests |
| `docs/api-coverage.md` | Exact supported and deferred API surface |
| `docs/research.md` | Upstream contract provenance |

## Development gates

```bash
make format && make lint
cmake -S . -B build-sanitized -DKALSHI_ENABLE_SANITIZERS=ON -DKALSHI_ENABLE_LTO=OFF
cmake --build build-sanitized --parallel
ctest --test-dir build-sanitized --output-on-failure
./tools/test_consumers.sh
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution steps and
[CHANGELOG.md](CHANGELOG.md) for migration notes.
