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
kalshi::HttpClient public_client;                      // no key: public market data
kalshi::HttpClient client(std::move(*signer));         // signs every request
kalshi::Result<kalshi::HttpResponse> response = client.get("/markets?limit=5");
```

### WebSocket streaming (`kalshi/websocket.hpp`)

```cpp
kalshi::WebSocketClient ws(*signer); // keeps its own copy of the signer
ws.on_message([](const kalshi::WsMessage& message) { /* std::visit over the variant */ });
if (kalshi::Result<void> connected = ws.connect(); !connected) {
    std::cerr << connected.error().message << "\n";
}
kalshi::Result<kalshi::SubscriptionId> sub = ws.subscribe_orderbook({"TICKER-1", "TICKER-2"});
```

### Retries and rate limits (`kalshi/retry.hpp`, `kalshi/rate_limit.hpp`)

Both are transport decorators, so they compose with `KalshiClient`:

```cpp
std::shared_ptr<kalshi::HttpClient> http = std::make_shared<kalshi::HttpClient>(*signer);
std::shared_ptr<kalshi::RateLimitedTransport> paced =
    std::make_shared<kalshi::RateLimitedTransport>(http, kalshi::RateLimitConfig{});
kalshi::KalshiClient client{std::make_shared<kalshi::RetryingTransport>(paced)};
```

`RetryingTransport` repeats writes only after a 429, so an order is never sent
twice. `RateLimitConfig` defaults to Kalshi's Basic tier; build the real one
with `rate_limit_config(get_account_api_limits(), get_endpoint_costs())`.

### Pagination (`kalshi/pagination.hpp`)

`PaginatedIterator` follows cursors for any list call:

```cpp
kalshi::PaginatedIterator<kalshi::Market> pages(
    [&](const kalshi::PaginationParams& page) {
        kalshi::GetMarketsParams params;
        params.limit = page.limit;
        if (page.cursor) params.cursor = page.cursor->value;
        return client.get_markets(params);
    });
kalshi::Result<std::vector<kalshi::Market>> all = pages.fetch_all();
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
