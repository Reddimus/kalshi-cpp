# Documentation

SDK documentation and research notes.

## Contents

| File          | Description                                                       |
| ------------- | ----------------------------------------------------------------- |
| `research.md` | Analysis of official Kalshi SDKs, API behavior, and parity matrix |

## API Reference

The SDK provides the following main components:

### Authentication (`kalshi/signer.hpp`)

```cpp
// Create from PEM string
auto signer = kalshi::Signer::from_pem("key-id", pem_string);

// Create from PEM file
auto signer = kalshi::Signer::from_pem_file("key-id", "/path/to/key.pem");

// Sign a request
auto headers = signer->sign("GET", "/markets");
// Returns: KALSHI-ACCESS-KEY, KALSHI-ACCESS-SIGNATURE, KALSHI-ACCESS-TIMESTAMP
```

### HTTP Client (`kalshi/http_client.hpp`)

```cpp
kalshi::HttpClient client(std::move(signer));

// Make requests
auto response = client.get("/markets");
auto response = client.post("/portfolio/orders", json_body);
auto response = client.del("/portfolio/orders/order-id");

// Check response
if (response && response->status_code == 200) {
    std::cout << response->body << "\n";
}
```

### WebSocket Streaming (`kalshi/websocket.hpp`)

```cpp
kalshi::WebSocketClient ws(signer);
ws.connect();

// Subscribe to orderbook updates
auto sub = ws.subscribe_orderbook({"TICKER-1", "TICKER-2"});

// Handle messages
ws.on_message([](const kalshi::WsMessage& msg) {
    std::visit([](auto&& m) { /* handle message */ }, msg);
});
```

### Pagination (`kalshi/pagination.hpp`)

```cpp
// Build paginated query
kalshi::PaginationParams params{.limit = 100};
auto query = kalshi::build_paginated_query("/markets", params);

// Use paginated iterator
kalshi::PaginatedIterator<Market> iter(fetch_fn, 100);
while (iter.has_more()) {
    auto page = iter.next_page();
}
```

### Rate Limiting (`kalshi/rate_limit.hpp`)

```cpp
kalshi::RateLimiter::Config config{.max_tokens = 10, .initial_tokens = 10};
kalshi::RateLimiter limiter(config);

if (limiter.try_acquire()) {
    // Make request
}
```

### Retry Logic (`kalshi/retry.hpp`)

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

## External Resources

- [Kalshi API Documentation](https://docs.kalshi.com)
- [Kalshi Trading Platform](https://kalshi.com)
- [Python SDK (sync)](https://pypi.org/project/kalshi-python/) - Official, v2.1.4
- [Python SDK (async)](https://pypi.org/project/kalshi-python-async/) - Official, v3.2.0+
- [TypeScript SDK](https://www.npmjs.com/package/kalshi) - Community, WebSocket-only, v0.0.5
