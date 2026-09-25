# kalshi-cpp

[![CI](https://github.com/Reddimus/kalshi-cpp/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Reddimus/kalshi-cpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Reddimus/kalshi-cpp)](https://github.com/Reddimus/kalshi-cpp/releases)

An unofficial C++23 client for Kalshi's Predictions API. It covers every
[REST operation](docs/operations.md) and [WebSocket channel](docs/channels.md).
A generator builds it from Kalshi's OpenAPI and AsyncAPI documents in
[`spec/`](https://github.com/Reddimus/kalshi-cpp/tree/main/spec), so type and
field names match [Kalshi's API docs](https://docs.kalshi.com). Kalshi's
separate Margin and Perpetuals API is out of scope. The
[API reference](https://reddimus.github.io/kalshi-cpp/) lists every type and method.

## Install

You need a C++23 compiler, CMake 3.31+, OpenSSL 3, libcurl, and libwebsockets.
CI tests GCC 13 on Ubuntu 24.04, Apple Clang on Apple silicon, and MSVC on
Windows, where [`vcpkg.json`](https://github.com/Reddimus/kalshi-cpp/blob/main/vcpkg.json)
supplies the dependencies. On macOS, the deployment target must be 13.3 or later.

```bash
# macOS
brew install cmake openssl libwebsockets pkg-config

# Ubuntu 24.04, whose apt CMake is older than 3.31
sudo apt install build-essential pkg-config pipx libssl-dev libcurl4-openssl-dev libwebsockets-dev
pipx install cmake && pipx ensurepath   # then open a new shell
```

Add the library to your CMake project:

```cmake
include(FetchContent)
FetchContent_Declare(kalshi
  GIT_REPOSITORY https://github.com/Reddimus/kalshi-cpp.git
  GIT_TAG v0.6.2)
FetchContent_MakeAvailable(kalshi)
target_link_libraries(myapp PRIVATE kalshi::kalshi)
```

After `cmake --install`, `find_package(kalshi CONFIG REQUIRED)` provides the
same target. Until 1.0, a minor release may break the API.
[CHANGELOG.md](CHANGELOG.md) has migration notes.

## Read market data

Public endpoints need no key.

```cpp
#include <kalshi/kalshi.hpp>
#include <iostream>

int main() {
    kalshi::KalshiClient client{kalshi::HttpClient{}};

    kalshi::GetMarketsParams params;
    params.series_ticker = "KXHIGHNY";   // New York City's daily high temperature
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

## Errors

Calls that can fail return `kalshi::Result<T>`, which is
`std::expected<T, kalshi::Error>`, instead of throwing. `Error::code` classifies
the failure, such as `RateLimited` or `NetworkError`. When Kalshi rejects a
request, `Error::http_status` and `Error::api_code` hold its HTTP status and
error code.

## Authenticate

Create an API key at <https://kalshi.com/account/profile>. Save the private key
file and note the key ID. `Signer` loads unencrypted RSA and Ed25519 PEM keys.

```cpp
kalshi::Result<kalshi::Signer> signer = kalshi::Signer::from_pem_file("your-key-id", "kalshi.key");
if (!signer) {
    std::cerr << signer.error().message << '\n';
    return 1;
}
kalshi::KalshiClient client{kalshi::HttpClient{*signer}};   // Signer is cheap to copy
kalshi::Result<kalshi::GetBalanceResponse> balance = client.get_balance();
```

Kalshi's demo exchange uses play money and its own keys. To use it, pass
`kalshi::ClientConfig::for_environment(kalshi::Environment::Demo)` to
`HttpClient` after the signer. `kalshi::WsConfig::for_environment` does the same
for `WebSocketClient`.

## Place an order

This places a real order unless `client` points at the demo exchange.

```cpp
kalshi::CreateOrderV2Request order;
order.ticker = "KXHIGHNY-26SEP25-T70";
order.side = kalshi::BookSide::Bid;   // Bid buys Yes, Ask sells Yes
order.count = "10.00";                // contracts
order.price = "0.5600";               // dollars
order.time_in_force = kalshi::TimeInForce::GoodTillCanceled;
order.self_trade_prevention_type = kalshi::SelfTradePreventionType::TakerAtCross;

kalshi::Result<kalshi::CreateOrderV2Response> placed = client.create_order(order);
```

Counts and prices are fixed-point strings, not doubles. The client checks them
and the required fields before sending, so `order.price = "56c"` fails locally
with `InvalidRequest`.

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

`connect()` waits until the connection opens or fails. Callbacks then run on the
client's network thread until you call `disconnect()` or destroy `ws`, so keep
your program running. After a dropped connection, the client reconnects,
resubscribes, and keeps your `ws::Subscription` handles valid. When it sees a
skipped order book sequence number, it reports the gap to `on_error` and
requests fresh snapshots. [docs/channels.md](docs/channels.md) lists each
channel's message types.

## Retries and rate limits

Both are transports you stack under the client:

```cpp
std::shared_ptr<kalshi::HttpClient> http = std::make_shared<kalshi::HttpClient>(*signer);
std::shared_ptr<kalshi::RateLimitedTransport> paced =
    std::make_shared<kalshi::RateLimitedTransport>(http, kalshi::RateLimitConfig{});
kalshi::KalshiClient client{std::make_shared<kalshi::RetryingTransport>(paced)};
```

By default, `RetryingTransport` repeats a POST, PUT, or DELETE only after a 429,
so it never places an order twice. `RateLimitConfig{}` matches Kalshi's Basic
tier. To match your account's limits, pass the results of
`get_account_api_limits()` and `get_account_endpoint_costs()` to
`kalshi::rate_limit_config()`.

## Examples

[`examples/`](https://github.com/Reddimus/kalshi-cpp/tree/main/examples) has
four programs: public market data, your portfolio, an order placed and canceled
on the demo exchange, and a live order book. From a clone,
`make run-market_data` runs the first one without a key. For the others,
`make run-<name>` loads your key from `.env`, as the
[examples README](https://github.com/Reddimus/kalshi-cpp/blob/main/examples/README.md)
shows.

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) covers building from source, tests, code
generation, and releases. Report security issues as
[SECURITY.md](SECURITY.md) describes.
