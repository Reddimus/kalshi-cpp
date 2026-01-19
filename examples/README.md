# Examples Directory

Example programs demonstrating Kalshi C++ SDK usage.

## Examples

| File                     | Description                                    |
| ------------------------ | ---------------------------------------------- |
| `basic_usage.cpp`        | Basic API setup and request                    |
| `get_markets.cpp`        | Fetch markets from the API                     |
| `get_daily_high_temp.cpp`| Temperature markets with optional live WebSocket streaming |
| `live_market_view.hpp`   | Helper library for tracking live market state  |

## Building Examples

From the repository root:

```bash
make build
```

## Running Examples

Examples require API credentials. Set environment variables:

```bash
export KALSHI_API_KEY_ID="your-api-key-id"
export KALSHI_API_KEY_FILE="/path/to/your/private-key.pem"
```

Then run:

```bash
# Using make targets
make run-basic_usage
make run-get_markets
make run-get_daily_high_temp

# Or directly
./build/examples/example_basic
./build/examples/example_markets
./build/examples/example_daily_high_temp
```

### WebSocket Streaming Mode

The `get_daily_high_temp` example supports live WebSocket streaming of market prices.

**Enable streaming:**

```bash
# Via command line flag
./build/examples/example_daily_high_temp --stream

# Via environment variable
KALSHI_STREAM=1 make run-get_daily_high_temp
```

**Rate limiting:**

The example uses exponential backoff to handle API rate limits (HTTP 429). On success, requests are made with a 150ms base delay. On rate limit errors, the delay doubles (up to 5 seconds) until requests succeed again.

**What it does:**
1. Discovers active temperature markets via REST API
2. Connects to WebSocket and subscribes to `orderbook_delta` and `trade` channels
3. Displays live best bid/ask and last trade for each market
4. Press Ctrl+C to stop streaming and exit cleanly

**Data shown:**
- **Bid/Ask**: Best bid and ask prices (cents) with sizes
- **Last Trade**: Price, size, and taker side of most recent trade

## Getting API Keys

1. Go to [Kalshi API Settings](https://kalshi.com/settings/api)
2. Generate an API key pair
3. Download the private key PEM file
4. Note the key ID shown on the page

## Helper Libraries

### `live_market_view.hpp`

A reusable helper for maintaining live market state from WebSocket streams:

```cpp
#include "live_market_view.hpp"

kalshi::LiveMarketView view;

// Register tickers to track
view.register_ticker("MARKET-TICKER-1");

// Process incoming WebSocket messages
ws.on_message([&view](const kalshi::WsMessage& msg) {
    view.process_message(msg);
});

// Get current state
auto state = view.get_state("MARKET-TICKER-1");
if (state) {
    std::cout << "Best bid: " << *state->best_bid_price << "c\n";
}

// Print all markets
view.print_all();
```
