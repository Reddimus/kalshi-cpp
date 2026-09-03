# Documentation

SDK documentation and research notes.

## Contents

| File          | Description                                                       |
| ------------- | ----------------------------------------------------------------- |
| `research.md` | Analysis of official Kalshi SDKs, API behavior, and parity matrix |
| `api-coverage.md` | Typed REST and WebSocket coverage against the official specs  |

## API reference

The SDK provides the following main components:

### Authentication (`kalshi/signer.hpp`)

```cpp
// Create from PEM string
auto signer = kalshi::Signer::from_pem("key-id", pem_string);

// Or create from a PEM file:
// auto signer = kalshi::Signer::from_pem_file("key-id", "/path/to/key.pem");

// Sign a request
auto headers = signer->sign("GET", "/trade-api/v2/markets");
// Returns: KALSHI-ACCESS-KEY, KALSHI-ACCESS-SIGNATURE, KALSHI-ACCESS-TIMESTAMP
```

### HTTP client (`kalshi/http_client.hpp`)

```cpp
kalshi::HttpClient client(std::move(*signer));

// Make requests
auto get_response = client.get("/markets");
auto post_response = client.post("/portfolio/orders", json_body);
auto delete_response = client.del("/portfolio/orders/order-id");

// Check response
if (get_response && get_response->status_code == 200) {
    std::cout << get_response->body << "\n";
}
```

### WebSocket streaming (`kalshi/websocket.hpp`)

```cpp
// WsConfig uses std::uint16_t for max_reconnect_attempts (max 65535)
kalshi::WsConfig config;
config.max_reconnect_attempts = 10;  // 0-65535

auto ws_signer = kalshi::Signer::from_pem_file("key-id", "/path/to/key.pem");
if (!ws_signer) return;
kalshi::WebSocketClient ws(*ws_signer, config); // ws_signer must outlive ws
if (auto connected = ws.connect(); !connected) {
    std::cerr << connected.error().message << "\n";
}

// Subscribe to orderbook updates
kalshi::Result<kalshi::SubscriptionId> sub = ws.subscribe_orderbook({"TICKER-1", "TICKER-2"});

// Handle messages
ws.on_message([](const kalshi::WsMessage& msg) {
    std::visit([](const auto& m) { /* handle message */ }, msg);
});
```

### Pagination (`kalshi/pagination.hpp`)

```cpp
// Build paginated query
kalshi::PaginationParams params{.limit = 100};
auto query = kalshi::build_paginated_query("/markets", params);

// Use paginated iterator
kalshi::PaginatedIterator<kalshi::Market> iter(fetch_fn, 100);
while (iter.has_more()) {
    auto page = iter.next_page();
}
```

### Rate limiting (`kalshi/rate_limit.hpp`)

```cpp
// Token counts use std::uint16_t for memory efficiency (max 65535 tokens)
kalshi::RateLimiter::Config config{.max_tokens = 10, .initial_tokens = 10};
kalshi::RateLimiter limiter(config);

if (limiter.try_acquire()) {
    // Make request
}
```

### Retry logic (`kalshi/retry.hpp`)

```cpp
kalshi::RetryPolicy policy{
    .max_attempts = 3,
    .initial_delay = std::chrono::milliseconds(100),
    .backoff_multiplier = 2.0
};

auto result = kalshi::with_retry([&]() { return client.get("/markets"); }, policy);
```

### Models (`kalshi/models/`)

- `Market` - Market information
- `OrderBook` - Order book with yes/no bids
- `Order` - Order details
- `OrderRequest` - New order parameters
- `Trade` - Trade execution
- `Position` - User position
- `Candlestick` - Historical OHLC price data

### Historical market data

The SDK supports fetching historical candlestick data via:

```cpp
kalshi::GetCandlesticksParams params;
params.series_ticker = "KXHIGHLAX";           // Series ticker
params.ticker = "KXHIGHLAX-26JAN18-T50";      // Market ticker
params.period_interval = 60;                   // 1 hour candles (1, 60, 1440 minutes)
params.start_ts = start_timestamp;             // Unix seconds
params.end_ts = end_timestamp;                 // Unix seconds

auto candles = client.get_market_candlesticks(params);
```

Notes:

- Endpoint: `GET /series/{series_ticker}/markets/{ticker}/candlesticks`
- Period intervals in minutes: 1 (1 min), 60 (1 hr), 1440 (1 day)
- Returns OHLC data with volume for each period

## External resources

- [Kalshi API Documentation](https://docs.kalshi.com)
- [Predictions OpenAPI](https://docs.kalshi.com/openapi.yaml)
- [Predictions AsyncAPI](https://docs.kalshi.com/asyncapi.yaml)
