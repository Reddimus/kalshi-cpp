# kalshi-cpp

[![CI](https://github.com/Reddimus/kalshi-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Reddimus/kalshi-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Reddimus/kalshi-cpp)](https://github.com/Reddimus/kalshi-cpp/releases)

A C++23 client for Kalshi's Predictions API. It covers every REST operation in
Kalshi's OpenAPI document and streams market data over WebSockets. Requests are
signed with Ed25519 or RSA-PSS keys, prices stay exact fixed-point strings, and
every call returns `std::expected<T, kalshi::Error>` instead of throwing.

The client is generated from Kalshi's OpenAPI and AsyncAPI documents in
[`spec/`](https://github.com/Reddimus/kalshi-cpp/tree/main/spec), so type and field names match
[Kalshi's API reference](https://docs.kalshi.com). The
[API reference for this library](https://reddimus.github.io/kalshi-cpp/) is
built from its headers. Kalshi's separate Margin and Perpetuals API is out of
scope.

## Build

You need a C++23 compiler, CMake 3.31+, OpenSSL 3, libcurl, and libwebsockets.

```bash
# macOS
brew install cmake openssl curl libwebsockets pkg-config

# Ubuntu 24.04, whose apt CMake is older than 3.31
sudo apt install build-essential pkg-config pipx libssl-dev libcurl4-openssl-dev libwebsockets-dev
pipx install cmake && pipx ensurepath   # then open a new shell

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Use it from CMake

```cmake
include(FetchContent)
FetchContent_Declare(kalshi
  GIT_REPOSITORY https://github.com/Reddimus/kalshi-cpp.git
  GIT_TAG v0.6.1)
FetchContent_MakeAvailable(kalshi)
target_link_libraries(myapp PRIVATE kalshi::kalshi)
```

After `cmake --install`, `find_package(kalshi CONFIG REQUIRED)` provides the
same `kalshi::kalshi` target.

## Read market data

Public endpoints need no key.

```cpp
#include <kalshi/kalshi.hpp>
#include <iostream>

int main() {
    kalshi::KalshiClient client{kalshi::HttpClient{}};

    kalshi::GetMarketsParams params;
    params.series_ticker = "KXHIGHNY";
    params.status = kalshi::GetMarketsStatus::Open;

    kalshi::Result<kalshi::GetMarketsResponse> page = client.get_markets(params);
    if (!page) {
        std::cerr << page.error().message << '\n';
        return 1;
    }
    for (const kalshi::Market& market : page->markets) {
        std::cout << market.ticker << ' ' << market.yes_bid_dollars << '\n';
    }
}
```

`kalshi::collect_pages` follows cursors when you want every page.

## Authenticate

Create a key under API keys at <https://kalshi.com/account/profile> and save
the private key file Kalshi gives you. Kalshi recommends Ed25519 keys. If the
page offers to use your own public key, you can generate the pair locally so
the private key never leaves your machine:

```bash
openssl genpkey -algorithm ed25519 -out kalshi.key
openssl pkey -in kalshi.key -pubout    # paste this public key into Kalshi
```

```cpp
kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file(key_id, "kalshi.key");
if (!signer) {
    std::cerr << signer.error().message << '\n';
    return 1;
}
kalshi::KalshiClient client{kalshi::HttpClient{*signer}};   // Signer is cheap to copy
kalshi::Result<kalshi::GetBalanceResponse> balance = client.get_balance();
```

`ClientConfig::for_environment(kalshi::Environment::Demo)` points the client at
Kalshi's demo exchange, which uses separate keys and play money.

## Place an order

```cpp
kalshi::CreateOrderV2Request order;
order.ticker = "KXHIGHNY-26SEP25-T70";
order.side = kalshi::BookSide::Bid;
order.count = "10.00";
order.price = "0.5600";
order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;

kalshi::Result<kalshi::CreateOrderV2Response> placed = client.create_order(order);
```

The client checks required fields and fixed-point strings before sending, so
`order.price = "56c"` fails locally with `InvalidRequest` instead of reaching the
exchange.

## Errors

`Error::code` says what went wrong: `InvalidRequest`, `AuthenticationError`,
`NotFound`, `RateLimited`, `ServerError`, `NetworkError`, `ParseError`,
`SigningError`, or `InvalidKey`. `Error::http_status` and `Error::api_code` carry
Kalshi's status and error code, and `Error::message` includes its explanation.

## Retries and rate limits

Both are transports you stack under the client:

```cpp
std::shared_ptr<kalshi::HttpClient> http = std::make_shared<kalshi::HttpClient>(*signer);
std::shared_ptr<kalshi::RateLimitedTransport> paced =
    std::make_shared<kalshi::RateLimitedTransport>(http, kalshi::RateLimitConfig{});
kalshi::KalshiClient client{std::make_shared<kalshi::RetryingTransport>(paced)};
```

`RetryingTransport` repeats a write only after a 429, so an order is never sent
twice. `RateLimitConfig` defaults to Kalshi's Basic tier. For your account's
budgets, pass the results of `get_account_api_limits()` and
`get_account_endpoint_costs()` to `rate_limit_config()`.

## Stream updates

```cpp
kalshi::WebSocketClient ws(*signer);
ws.on_message([](const kalshi::WsMessage& message) {
    using Delta = kalshi::ws::Update<kalshi::ws::OrderbookDelta>;
    if (const Delta* delta = std::get_if<Delta>(&message)) {
        std::cout << delta->msg.market_ticker << ' ' << delta->msg.delta_fp << '\n';
    }
});
kalshi::Result<kalshi::ws::Subscription> book = ws.subscribe(
    kalshi::ws::Channel::OrderbookDelta, {.market_tickers = {"KXHIGHNY-26SEP25-T70"}});
kalshi::Result<void> connected = ws.connect();
```

The client reconnects after a dropped connection and resubscribes with each
subscription's current markets. A `ws::Subscription` handle stays the same
throughout, and every `ws::Update` names the subscription it belongs to. When
an order book sequence number is skipped, the client reports the gap to
`on_error` and requests fresh snapshots. [docs/channels.md](docs/channels.md)
lists each channel's message types.

## Examples

| Program | What it does |
| --- | --- |
| [`market_data`](https://github.com/Reddimus/kalshi-cpp/blob/main/examples/market_data.cpp) | Markets, an order book, and candlesticks, without a key |
| [`portfolio`](https://github.com/Reddimus/kalshi-cpp/blob/main/examples/portfolio.cpp) | Balance, positions, and resting orders |
| [`place_and_cancel_order`](https://github.com/Reddimus/kalshi-cpp/blob/main/examples/place_and_cancel_order.cpp) | A resting order and its cancel, on the demo exchange only |
| [`stream_orderbook`](https://github.com/Reddimus/kalshi-cpp/blob/main/examples/stream_orderbook.cpp) | A live local order book that recovers from gaps and reconnects |

Put `KALSHI_API_KEY_ID`, `KALSHI_API_KEY_FILE`, and optionally `KALSHI_ENV=demo`
in `.env`, then run `make run-portfolio`.

## Develop

```bash
make format lint test          # before every commit
make sanitize tsan tidy        # ASan/UBSan, ThreadSanitizer, clang-tidy
make consumers bench           # packaging check, benchmarks
make codegen                   # after updating a spec in spec/
make docs                      # API reference in build-docs/html
```

[CONTRIBUTING.md](CONTRIBUTING.md) covers the workflow and release steps.
[CHANGELOG.md](CHANGELOG.md) lists changes and migration notes.
Report security issues as described in [SECURITY.md](SECURITY.md).
